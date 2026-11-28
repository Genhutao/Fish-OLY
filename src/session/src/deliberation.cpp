#include "oly/session/deliberation.hpp"

#include <algorithm>
#include <cstdint>
#include <ranges>
#include <unordered_set>

namespace oly::session {
namespace {

// ── 感知特征派生（06 §2.2 的 M0 实现）────────────────────────────
// 字节级相邻对集合（UTF-8 透明；06 §2.2：词汇相关度只做粗筛，borderline 才升级向量）
using Bigrams = std::unordered_set<uint16_t>;

Bigrams bigrams_of(const std::string_view s) {
    Bigrams out;
    if (s.size() < 2) return out;
    for (std::size_t i = 0; i + 1 < s.size(); ++i) {
        out.insert(static_cast<uint16_t>(static_cast<uint8_t>(s[i]) << 8 | static_cast<uint8_t>(s[i + 1])));
    }
    return out;
}

double jaccard(const Bigrams& a, const Bigrams& b) {
    if (a.empty() || b.empty()) return 0.0;
    std::size_t inter = 0;
    for (const auto g : a) {
        if (b.contains(g)) ++inter;
    }
    return static_cast<double>(inter) / static_cast<double>(a.size() + b.size() - inter);
}

double overlap_ratio(const Bigrams& a, const Bigrams& b) {
    if (b.empty()) return 0.0;
    std::size_t inter = 0;
    for (const auto g : a) {
        if (b.contains(g)) ++inter;
    }
    return static_cast<double>(inter) / static_cast<double>(b.size());
}

}   // namespace

DeliberationDriver::DeliberationDriver(const domain::PrincipalId persona, gateway::Gateway& gw,
                                       std::vector<std::string> reserve, const DeliberationConfig cfg)
    : persona_(persona), gw_(gw), reserve_(std::move(reserve)), cfg_(cfg) {}

TickOutcome DeliberationDriver::degrade() {
    TickOutcome oc;
    // 第一级：语气储备（06 §3.2）——角色依然"在说话"，fallback 标记入事件
    if (reserve_idx_ < reserve_.size()) {
        oc.speak = true;
        oc.fallback = true;
        oc.model = "reserve";
        oc.text = reserve_[reserve_idx_++];
        energy_ = std::max(0.0f, energy_ - 0.2f);
        streak_ += 1.0f;
        cooldown_ = cfg_.score.hard_cooldown;
        return oc;
    }
    // 第二级：沉默事件（分数已有；06 §2.4）
    oc.silence_noted = true;
    return oc;
}

TickOutcome DeliberationDriver::on_tick(const TickContext& ctx) {
    TickOutcome oc;

    // ── 每拍状态推进（疲惫恢复 / 冷却衰减）────────────────────────
    energy_ = std::min(1.0f, energy_ + 0.05f);
    if (cooldown_ > 0) --cooldown_;
    cfg_.score.activity_ema = activity_ema_;
    oc.tau = speak_threshold(cfg_.score);

    // ── 感知特征（M0 派生；换型点：M1 反思管线的 presence/affinity）──
    float mention = 0.0f, urgency = 0.1f;
    const auto* p = ctx.registry != nullptr ? ctx.registry->persona(persona_) : nullptr;
    const auto name = p != nullptr ? std::string_view(p->head().name) : std::string_view{};
    for (const auto& msg : ctx.new_messages) {
        if (!name.empty() && msg.text.find(name) != std::string::npos) mention = 1.0f;
        const bool from_user =
            ctx.session != nullptr && msg.actor == ctx.session->owner;   // 用户消息 > 角色消息
        urgency = std::max(urgency, from_user ? 1.0f : 0.4f);
    }

    // 关系/互惠：M0 常量（M1 由 affinity[X][author] 与互转窗口替代）
    constexpr float kRelation = 0.5f;
    constexpr float kReciprocity = 0.5f;

    // topic：窗口文本与 canon 的字汇重叠（BM25-lite 粗筛）
    double topic = 0.0;
    // novelty：1 − 与近期窗口相似度（抑制复读刷屏）
    double novelty = 1.0;
    {
        Bigrams canon_bigrams;
        if (ctx.session != nullptr) canon_bigrams = bigrams_of(ctx.session->canon);
        Bigrams window_bigrams;
        for (const auto& msg : ctx.window) {
            const auto bg = bigrams_of(msg.text);
            window_bigrams.insert(bg.begin(), bg.end());
            topic = std::max(topic, overlap_ratio(canon_bigrams, bg));
        }
        if (!ctx.new_messages.empty()) {
            Bigrams news;
            for (const auto& msg : ctx.new_messages) {
                const auto bg = bigrams_of(msg.text);
                news.insert(bg.begin(), bg.end());
            }
            novelty = 1.0 - jaccard(news, window_bigrams);
        }
    }

    // ── 估分（单候选自评，index=0；M1 会话层 O(M) 批打分见 scoring.hpp）──
    const float att = attention_of(cfg_.weights, cfg_.score, ctx.stream_key, ctx.tick, 0, mention,
                                   static_cast<float>(topic), kRelation, kReciprocity,
                                   static_cast<float>(novelty), urgency, energy_, streak_);
    oc.attention = att;

    if (att >= oc.tau && cooldown_ == 0) {
        // ── 决策（06 §3）：装配提示板 → 网关 → speak ────────────────
        SlotInput in;
        if (p != nullptr) {
            const auto& head = p->head();
            in.identity = "名字：" + head.name + "\n锚点：" + head.core_anchor +
                          (head.description.empty() ? "" : "\n描述：" + head.description);
        }
        if (ctx.session != nullptr) in.canon = ctx.session->canon;
        in.state = "精力：" + std::to_string(static_cast<int>(energy_ * 100.0f)) + "%";
        for (const auto& msg :
             ctx.window | std::views::drop(std::max<std::ptrdiff_t>(
                              0, static_cast<std::ptrdiff_t>(ctx.window.size()) -
                                     static_cast<std::ptrdiff_t>(cfg_.window_size)))) {
            in.recent.push_back(msg.text);
        }
        in.output = "以「" + std::string{name.empty() ? "角色" : name} +
                    "」的口吻，输出一句符合场景的中文短句；只输出台词本身。";

        const auto board = assemble_board(cfg_.context_tokens, in);
        gateway::CompletionRequest req;
        req.priority = gateway::Priority::P2_Ambient;   // 自治发言：环境档（07 §2）
        req.prompt = render_prompt(board);
        req.max_tokens = 128;
        req.deadline_ms = 8000;   // 8s 截止（06 §3.2）；M0 同步 pump 以 0 现在时驱动
        req.trace = ctx.tick;
        if (const auto ref = gw_.submit(std::move(req)); ref != 0) {
            auto rep = gw_.pump(0);
            if (rep && rep->ok) {
                oc.speak = true;
                oc.text = rep->text;
                oc.model = rep->provider;
                oc.tokens_in = rep->tokens_in;
                oc.tokens_out = rep->tokens_out;
            }
        }
        if (!oc.speak) return degrade();   // 超时/失败/拒绝 → 阶梯

        energy_ = std::max(0.0f, energy_ - 0.2f);
        streak_ += 1.0f;
        cooldown_ = cfg_.score.hard_cooldown;
    } else {
        // ── 沉默估分（06 §2.4）：被@沉默必记；borderline 采样 ───────
        const float scores[1] = {att};
        const float mentioned[1] = {mention};
        oc.silence_noted = !collect_silences(cfg_.score, ctx.stream_key, ctx.tick, scores, mentioned)
                                .empty();
    }

    // 近期发言率 EMA（06 §2.3 校准输入；热闹房间自动 ↑τ）
    activity_ema_ = 0.9f * activity_ema_ + 0.1f * (oc.speak ? 1.0f : 0.0f);
    return oc;
}

}   // namespace oly::session
