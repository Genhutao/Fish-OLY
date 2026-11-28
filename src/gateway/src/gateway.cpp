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

Gateway::Gateway(LLMProvider& provider, const Caps caps) : provider_(provider), caps_(caps) {}

uint64_t Gateway::submit(CompletionRequest req) {
    const auto lane = static_cast<std::size_t>(req.priority);
    if (lane <= 1) {
        if (lanes_[lane].size() >= caps_.lane_depth[lane]) {
            ++c_.rejected_hi;   // P0 永不丢：满即拒绝（调用方限流/退避），07 §2
            return 0;
        }
    } else {
        if (lanes_[lane].size() >= caps_.lane_depth[lane]) {
            (lane == 2 ? c_.dropped_p2 : c_.dropped_p3) += 1;   // 挤兑自底向上削减
            return 0;
        }
    }
    const auto ref = next_ref_++;
    lanes_[lane].emplace_back(ref, std::move(req));
    ++c_.submitted;
    return ref;
}

std::optional<Completion> Gateway::pump(const uint64_t now_ms) {
    for (auto& lane : lanes_) {
        if (lane.empty()) continue;
        auto [ref, req] = std::move(lane.front());
        lane.pop_front();

        Completion c;
        c.request_ref = ref;
        c.provider = provider_.name();
        if (req.deadline_ms != 0 && now_ms >= req.deadline_ms) {
            // 到期回收：失败应答，预留随之释放（无悬挂占用，07 §3）
            ++c_.expired;
            c.ok = false;
            c.error = "gateway:deadline";
            return c;
        }
        c = provider_.complete(req);
        c.request_ref = ref;
        c.provider = provider_.name();
        if (c.ok) {
            ++c_.completed;
        } else {
            ++c_.failed;
        }
        return c;
    }
    return std::nullopt;
}

float Gateway::saturation() const noexcept {
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
