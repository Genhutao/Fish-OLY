// OLY · gateway —— 模型网关（07）：全部模型流量的唯一入口。
// M0 形态 = "基础通路（单供应商）"：优先级车道 P0–P3、submit/pump、
// LLMProvider 适配器接口（插件扩展点之一，12）、确定性 FakeProvider。
// 异步化（真实分片域、流式聚合、预留-结算、饱和度广播）是 M1 事项；
// 本模块刻意不依赖 asio —— 会话侧驱动以同步 pump 走通全链。
#pragma once

#include "oly/core/hash.hpp"
#include "oly/core/result.hpp"

#include <array>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace oly::gateway {

// 优先级（07 §2）：挤兑自底向上削减，P0 永不丢（满即拒绝 = 背压，不静默丢弃）
enum class Priority : uint8_t { P0_Interactive = 0, P1_Drama, P2_Ambient, P3_Summary };

struct CompletionRequest {
    Priority priority = Priority::P2_Ambient;
    std::string prompt;
    uint32_t max_tokens = 256;
    uint64_t deadline_ms = 0;   // 网关时钟绝对值；0 = 不限（M0 同步通路多为 0）
    uint64_t trace = 0;         // 调用方追踪（tick 等）
};

struct Completion {
    uint64_t request_ref = 0;
    bool ok = false;
    std::string text;
    std::string error;
    std::string provider;
    uint32_t tokens_in = 0;
    uint32_t tokens_out = 0;
};

// 供应商适配器（07 §1）。M0：同步 complete，网关在其调用线程驱动；
// M1 异步化后此接口换 awaitable 形态，适配器是稳定的换型位点。
class LLMProvider {
public:
    virtual ~LLMProvider() = default;
    virtual std::string name() const = 0;
    virtual Completion complete(const CompletionRequest& req) = 0;
};

// 确定性假供应商（13 §2 确定性闸门的座驾）：
//   - scripted 表非空：按调用序消耗（黄金场景台词）；
//   - 表耗尽后：prompt 哈希种子从内置句库确定性选句；
//   - fail_from = 第 N 次调用起一律失败（降级阶梯/熔断演练，06 §3.2）。
class FakeProvider final : public LLMProvider {
public:
    explicit FakeProvider(std::vector<std::string> scripted = {}, uint64_t fail_from = ~uint64_t{0});

    std::string name() const override { return "fake"; }
    Completion complete(const CompletionRequest& req) override;

    uint64_t calls() const noexcept { return calls_; }

private:
    std::vector<std::string> scripted_;
    uint64_t fail_from_;
    uint64_t calls_ = 0;
    std::size_t next_ = 0;
};

// 网关分片（M0 单分片）：4 优先级车道 + 计数 + 饱和度估计。
// 车道语义：P0/P1 满即拒绝（背压可见）；P2/P3 满即丢（计数上浮，07 §2 削减顺序）。
class Gateway {
public:
    struct Caps {
        std::array<std::size_t, 4> lane_depth{64, 32, 128, 256};
    };

    struct Counters {
        uint64_t submitted = 0;
        uint64_t completed = 0;
        uint64_t failed = 0;
        uint64_t expired = 0;      // deadline 到期回收（预留随 deadline 释放，07 §3）
        uint64_t rejected_hi = 0;  // P0/P1 满即拒
        uint64_t dropped_p2 = 0;
        uint64_t dropped_p3 = 0;
    };

    explicit Gateway(LLMProvider& provider, Caps caps = {});

    // 受理：返回 request_ref；0 = 未受理（拒绝或丢弃，看 counters）
    uint64_t submit(CompletionRequest req);

    // 驱动一件：队头 deadline 已过 → 到期失败回收；否则交供应商完成。
    // 无事可做返回 nullopt。M0 同步逐件；M1 换协程批量泵。
    std::optional<Completion> pump(uint64_t now_ms);

    // 饱和度 ∈ [0,1]：队列深度水位（07 §4 广播信号的本地估计雏形）
    float saturation() const noexcept;

    const Counters& counters() const noexcept { return c_; }

private:
    std::size_t queued() const noexcept;

    LLMProvider& provider_;
    Caps caps_;
    Counters c_;
    uint64_t next_ref_ = 1;   // 0 保留为"未受理"
    std::array<std::deque<std::pair<uint64_t, CompletionRequest>>, 4> lanes_;
};

}   // namespace oly::gateway
