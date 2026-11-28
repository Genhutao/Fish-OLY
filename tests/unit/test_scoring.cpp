// 估分内核（06 §2）：确定性、阈值校准、成本递增、K 上限、冷却、沉默事件化采样
#include <oly/session/scoring.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>

using namespace oly::session;

namespace {

// 全零权重 + 指定单项：单变量精确断言用
AttentionWeights only(const float v, float AttentionWeights::*field) {
    AttentionWeights w{};
    w.*field = v;
    return w;
}

}   // namespace

TEST_CASE("估分：确定性（同输入逐位同输出）", "[session][scoring]") {
    const AttentionWeights w;
    const ScoreConfig cfg;
    const auto a = attention_of(w, cfg, 42, 7, 0, 1.0f, 0.5f, 0.5f, 0.5f, 0.9f, 1.0f, 1.0f, 0.0f);
    const auto b = attention_of(w, cfg, 42, 7, 0, 1.0f, 0.5f, 0.5f, 0.5f, 0.9f, 1.0f, 1.0f, 0.0f);
    REQUIRE(a == b);   // 位级相等（噪声由 seed 唯一决定）
    const auto c = attention_of(w, cfg, 42, 8, 0, 1.0f, 0.5f, 0.5f, 0.5f, 0.9f, 1.0f, 1.0f, 0.0f);
    REQUIRE(c != a);   // 拍不同 → 噪声不同（重放时拍号是确定的）
}

TEST_CASE("估分：特征线性项与成本项精确", "[session][scoring]") {
    const ScoreConfig cfg;
    // 同种子下噪声相同 → 差值 = 权重 × 特征差（逐位）
    const AttentionWeights w1 = only(2.0f, &AttentionWeights::mention);
    const auto no = attention_of(w1, cfg, 1, 1, 0, 0.0f, 0, 0, 0, 0, 0, 1.0f, 0.0f);
    const auto yes = attention_of(w1, cfg, 1, 1, 0, 1.0f, 0, 0, 0, 0, 0, 1.0f, 0.0f);
    REQUIRE(yes - no == Catch::Approx(2.0f).margin(1e-6));

    // 连续发言抬升成本：att(streak=1) = att(streak=0) − base×k/(0.2+energy)
    const AttentionWeights w0{};
    const auto s0 = attention_of(w0, cfg, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1.0f, 0.0f);
    const auto s1 = attention_of(w0, cfg, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1.0f, 1.0f);
    REQUIRE(s0 - s1 == Catch::Approx(0.5f * 0.35f / 1.2f).margin(1e-6));

    // 疲惫抬升成本：能量减半 → 成本升
    const auto e0 = attention_of(w0, cfg, 1, 1, 0, 0, 0, 0, 0, 0, 0, 1.0f, 0.0f);
    const auto e1 = attention_of(w0, cfg, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0.5f, 0.0f);
    REQUIRE(e1 < e0);
}

TEST_CASE("估分：阈值随活跃度上调（热闹房间防刷屏）", "[session][scoring]") {
    ScoreConfig cfg;
    cfg.tau_base = 1.0f;
    cfg.activity_ema = 0.0f;
    const float cold = speak_threshold(cfg);
    cfg.activity_ema = 0.5f;
    const float warm = speak_threshold(cfg);
    REQUIRE(warm == Catch::Approx(1.0f * (0.6f + 1.8f * 0.5f)).margin(1e-6));
    REQUIRE(warm > cold);
}

TEST_CASE("估分：批打分 —— 降序 / K 上限 / 硬冷却", "[session][scoring]") {
    AttentionWeights w{};
    w.mention = 10.0f;   // 唯一信号源
    ScoreConfig cfg;
    cfg.max_decisions = 2;
    cfg.noise = 0.0f;   // 消噪：纯信号断言
    cfg.hard_cooldown = 2;

    std::array<float, 4> mention{1.0f, 0.9f, 0.5f, 0.2f};
    std::array<float, 4> zeros{0, 0, 0, 0};
    std::array<uint32_t, 4> cooldown{0, 0, 0, 0};
    CandidateFeatures f;
    f.count = 4;
    f.mention = mention;
    f.topic = f.relation = f.reciprocity = f.novelty = f.urgency = zeros;
    f.energy = zeros;
    f.streak = zeros;
    f.cooldown = cooldown;

    const auto top = score(w, cfg, 0, 0, f);
    REQUIRE(top.size() == 2);                        // K 上限
    REQUIRE(top[0].index == 0);                      // 降序
    REQUIRE(top[0].attention > top[1].attention);
    REQUIRE(top[1].index == 1);

    // 最高分者硬冷却 → 出局，次高补位
    cooldown[0] = 2;
    const auto top2 = score(w, cfg, 0, 0, f);
    REQUIRE(top2.size() == 2);
    REQUIRE(top2[0].index == 1);

    // 全员冷却 → 空
    cooldown = {1, 1, 1, 1};
    REQUIRE(score(w, cfg, 0, 0, f).empty());
}

TEST_CASE("估分：沉默事件化 —— 被@必记，borderline 采样确定", "[session][scoring]") {
    ScoreConfig cfg;
    cfg.noise = 0.0f;
    const float tau = speak_threshold(cfg);

    // 被@但低于阈值 → 必记
    const std::array<float, 2> scores{tau - 0.01f, tau * 0.5f};
    const std::array<float, 2> mentioned{1.0f, 0.0f};
    const auto noted = collect_silences(0, 0, scores, mentioned, speak_threshold(cfg));
    REQUIRE(noted.size() == 1);
    REQUIRE(noted[0] == 0);

    // 未被@且远低于带 → 不记
    const std::array<float, 1> low{tau * 0.3f};
    const std::array<float, 1> none{0.0f};
    REQUIRE(collect_silences(0, 0, low, none, speak_threshold(cfg)).empty());

    // borderline 带内采样：确定性（两次一致），且带外不采
    const float band = 0.6f * tau;
    std::array<float, 2> two{band + 0.01f, band - 0.01f};
    const auto s1 = collect_silences(9, 3, two, none, speak_threshold(cfg));
    const auto s2 = collect_silences(9, 3, two, none, speak_threshold(cfg));
    REQUIRE(s1 == s2);
}
