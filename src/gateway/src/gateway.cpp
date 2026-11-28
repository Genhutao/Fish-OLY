#include "oly/gateway/gateway.hpp"

#include <algorithm>
#include <limits>

namespace oly::gateway {
namespace {

// 与 session/prompt_board 的启发式同思路；网关侧独立一份避免反向依赖
uint32_t rough_tokens(const std::string_view s) noexcept {
    std::size_t ascii = 0, wide = 0, other = 0;
    std::size_t i = 0;
    while (i < s.size()) {
        const auto b = static_cast<unsigned char>(s[i]);
        if (b < 0x80) {
            ++ascii;
            i += 1;
        } else if ((b & 0xF0) == 0xE0) {
            ++wide;
            i += 3;
        } else {
            ++other;
            i += (b >= 0xF0) ? 4 : 2;
        }
    }
    return static_cast<uint32_t>((ascii + 3) / 4 + (wide * 3 + 1) / 2 + other);
}

constexpr std::string_view kAmbient[] = {
    "夜色把石阶浸得更深了。",
    "风从檐角掠过，带走半句未说完的话。",
    "灯芯轻轻爆了个花。",
    "远处传来更漏的声响。",
    "她把目光放回原处，像什么都没发生。",
    "庭院里只剩一片月光。",
    "呼吸放得很轻，怕惊动了什么。",
    "这一刻安静得可以听见尘埃。",
};

}   // namespace

FakeProvider::FakeProvider(std::vector<std::string> scripted, const uint64_t fail_from)
    : scripted_(std::move(scripted)), fail_from_(fail_from) {}

Completion FakeProvider::complete(const CompletionRequest& req) {
    Completion c;
    c.provider = name();
    c.request_ref = req.trace;   // M0：以 trace 回带，网关 pump 会覆写为真 ref
    c.tokens_in = rough_tokens(req.prompt);
    const auto n = calls_++;
    if (n >= fail_from_) {
        c.ok = false;
        c.error = "fake:failed#" + std::to_string(n);
        return c;
    }
    if (next_ < scripted_.size()) {
        c.text = scripted_[next_++];
    } else {
        // 表耗尽 → prompt 内容哈希选句（确定性；13 §2）
        auto bytes = req.prompt;
        const auto seed = sha256(std::span<const std::byte>(
            reinterpret_cast<const std::byte*>(bytes.data()), bytes.size()));
        const auto v = std::to_integer<uint64_t>(seed.bytes[0]) |
                       (std::to_integer<uint64_t>(seed.bytes[1]) << 8);
        c.text = std::string{kAmbient[v % std::size(kAmbient)]};
    }
    c.ok = true;
    c.tokens_out = rough_tokens(c.text);
    return c;
}

Gateway::Gateway(LLMProvider& provider, const Caps caps, quota::Ledger* ledger)
    : provider_(provider), ledger_(ledger), caps_(caps) {}

bool Gateway::admit_by_breaker(const uint64_t now_ms, bool& probe) {
    probe = false;
    if (breaker_ == State::Closed) return true;
    // 冷却期满 → 半开：放行单件探测（07 §5 半开探测恢复）
    if (breaker_ == State::Open &&
        now_ms - opened_at_ms_ >= caps_.breaker.cooldown_ms) {
        breaker_ = State::HalfOpen;
    }
    if (breaker_ == State::HalfOpen) {
        if (probe_in_flight_) return false;   // 探测件在途：其余请求仍拒
        probe = true;
        probe_in_flight_ = true;
        return true;
    }
    return false;
}

void Gateway::record_outcome(const bool ok, const uint64_t now_ms) {
    if (breaker_ == State::HalfOpen) {
        probe_in_flight_ = false;
        if (ok) {
            breaker_ = State::Closed;   // 探测成功 → 闭合，滑窗重置
            outcomes_.clear();
        } else {
            breaker_ = State::Open;   // 探测失败 → 重新熔断，新冷却
            opened_at_ms_ = now_ms;
        }
        return;
    }
    outcomes_.push_back(ok);
    while (outcomes_.size() > caps_.breaker.window) outcomes_.pop_front();
    if (breaker_ != State::Open && outcomes_.size() == caps_.breaker.window) {
        std::size_t fails = 0;
        for (const auto o : outcomes_) {
            if (!o) ++fails;
        }
        if (static_cast<double>(fails) / static_cast<double>(outcomes_.size()) >=
            caps_.breaker.fail_ratio) {
            breaker_ = State::Open;
            opened_at_ms_ = now_ms;
            ++c_.breaker_openings;
            outcomes_.clear();
        }
    }
}

uint64_t Gateway::submit(CompletionRequest req, const uint64_t now_ms) {
    const auto lane = static_cast<std::size_t>(req.priority);

    // ── 熔断闸（07 §5）：开期内 P2/P3 直接拒；P0/P1 仍过（会话侧降级兜底，
    //    P0 永不丢）；半开探测件单件放行 ─────────────────────────────
    bool probe = false;
    if (!admit_by_breaker(now_ms, probe)) {
        if (lane <= 1) return 0;   // P0/P1 过闸进供应商（失败走降级阶梯）
        (lane == 2 ? c_.dropped_p2 : c_.dropped_p3) += 1;
        ++c_.breaker_dropped;
        return 0;
    }

    // ── 车道容量（07 §2）─────────────────────────────────────────
    if (lane <= 1) {
        if (lanes_[lane].size() >= caps_.lane_depth[lane]) {
            ++c_.rejected_hi;   // P0 永不丢：满即拒绝（调用方限流/退避）
            return 0;
        }
    } else {
        if (lanes_[lane].size() >= caps_.lane_depth[lane]) {
            (lane == 2 ? c_.dropped_p2 : c_.dropped_p3) += 1;
            return 0;
        }
    }

    // ── 配额硬墙（09 §3.2 预留-结算；09 §5 差异化耗尽语义）─────────
    double estimate = 0.0;
    if (ledger_ != nullptr &&
        (req.on_behalf_of.key() != 0 || req.world_ref.key() != 0)) {
        estimate = static_cast<double>(rough_tokens(req.prompt)) + req.max_tokens;
        const auto v = ledger_->reserve(now_ms, req.on_behalf_of.key(), req.world_ref.key(),
                                        estimate);
        if (v.code != quota::Verdict::Code::Ok) {
            if (lane <= 1) {
                ++c_.quota_rejected;   // P0 硬失败（quota_exhausted 结构化错误原料）
            } else {
                ++c_.quota_dropped;   // P2/P3：不硬停，会话侧降级阶梯接管
            }
            return 0;
        }
    }

    const auto ref = next_ref_++;
    lanes_[lane].push_back(Entry{ref, std::move(req), estimate, probe});
    ++c_.submitted;
    return ref;
}

std::optional<Completion> Gateway::pump(const uint64_t now_ms) {
    for (auto& lane : lanes_) {
        if (lane.empty()) continue;
        auto entry = std::move(lane.front());
        lane.pop_front();

        Completion c;
        c.request_ref = entry.ref;
        c.provider = provider_.name();
        if (entry.req.deadline_ms != 0 && now_ms >= entry.req.deadline_ms) {
            // 到期回收：失败应答，预留随 deadline 释放（无悬挂占用，07 §3）
            ++c_.expired;
            if (ledger_ != nullptr && entry.estimate > 0.0) {
                ledger_->release(entry.req.on_behalf_of.key(), entry.req.world_ref.key(),
                                 entry.estimate);
            }
            if (entry.probe) record_outcome(false, now_ms);
            c.ok = false;
            c.error = "gateway:deadline";
            return c;
        }
        c = provider_.complete(entry.req);
        c.request_ref = entry.ref;
        c.provider = provider_.name();
        if (c.ok) {
            ++c_.completed;
            c_.tokens_in_total += c.tokens_in;
            c_.tokens_out_total += c.tokens_out;
        } else {
            ++c_.failed;
        }
        // 结算：真实用量多退少补（09 §3.2；少补容忍小幅透支）
        if (ledger_ != nullptr && entry.estimate > 0.0) {
            ledger_->settle(now_ms, entry.req.on_behalf_of.key(), entry.req.world_ref.key(),
                            entry.estimate,
                            static_cast<double>(c.tokens_in) + c.tokens_out);
        }
        // 熔断滑窗只记供应商结果（deadline 不算供应商失败）
        record_outcome(c.ok, now_ms);
        return c;
    }
    return std::nullopt;
}

float Gateway::saturation() const noexcept {
    if (breaker_ != State::Closed) return 1.0f;   // 熔断开期 = 全饱和（07 §4 反馈上调）
    std::size_t cap = 0;
    for (std::size_t i = 0; i < 4; ++i) cap += caps_.lane_depth[i];
    if (cap == 0) return 1.0f;
    return std::min(1.0f, static_cast<float>(queued()) / static_cast<float>(cap));
}

std::size_t Gateway::queued() const noexcept {
    std::size_t n = 0;
    for (const auto& lane : lanes_) n += lane.size();
    return n;
}

}   // namespace oly::gateway
