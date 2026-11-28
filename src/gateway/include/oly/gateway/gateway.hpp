// OLY · gateway —— 模型网关（07）：全部模型流量的唯一入口。
// M1-2 形态 = "网关完整"：优先级车道 P0–P3、submit/pump、配额硬墙
//   （预留-结算-释放，09 §3.2）、熔断器（07 §5 滑窗失败率 → 半开探测恢复）、
//   成本台账计数（07 §7 的 M1-2 计数雏形，cost.recorded 事件属进程流批）、
//   饱和度估计（07 §4 闭环的输入端）。
// 会话侧只做 submit/pump，永不阻塞；饱和度 → τ_speak 的反馈在 session 侧
//   （DeliberationDriver 读 saturation/配额水位）。本模块仍不依赖 asio。
#pragma once

#include "oly/core/hash.hpp"
#include "oly/core/id.hpp"
#include "oly/core/result.hpp"
#include "oly/quota/quota.hpp"

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
    uint64_t deadline_ms = 0;   // 网关时钟绝对值；0 = 不限
    uint64_t trace = 0;         // 调用方追踪（tick 等）
    // 记账三面（07 §7 cost.recorded 原料；09 §6 角色消费永远归账到人）：
    PrincipalId actor{};            // 消费主体（角色）
    PrincipalId on_behalf_of{};     // 归账主体（会话归属用户）；0 = 不记账
    ContentId world_ref{};          // 世界内容键（世界预算第二维度，09 §2）；nil = 旁路
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

// 供应商适配器（07 §1）。M0–M1：同步 complete，网关在其调用线程驱动；
// M1 协程化后此接口换 awaitable 形态，适配器是稳定的换型位点。
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

// 熔断器参数（07 §5）：滑窗失败率超阈 → 熔断；冷却期满 → 半开放一件探测。
struct BreakerConfig {
    std::size_t window = 16;         // 滑窗样本数
    double fail_ratio = 0.5;         // 失败率阈值
    uint64_t cooldown_ms = 30000;    // 熔断冷却（半开前静默）
};

// 网关分片（M1-2 单分片单供应商）：4 优先级车道 + 配额硬墙 + 熔断 + 台账计数。
// 车道语义：P0/P1 满即拒绝（背压可见）；P2/P3 满即丢（计数上浮，07 §2 削减顺序）。
class Gateway {
public:
    struct Caps {
        std::array<std::size_t, 4> lane_depth{64, 32, 128, 256};
        BreakerConfig breaker{};
    };

    struct Counters {
        uint64_t submitted = 0;
        uint64_t completed = 0;
        uint64_t failed = 0;
        uint64_t expired = 0;        // deadline 到期回收（预留随 deadline 释放，07 §3）
        uint64_t rejected_hi = 0;    // P0/P1 车道满即拒
        uint64_t dropped_p2 = 0;
        uint64_t dropped_p3 = 0;
        // 配额（09 §5 差异化耗尽语义）：
        uint64_t quota_rejected = 0;   // P0/P1 硬失败（结构化配额错误原料）
        uint64_t quota_dropped = 0;    // P2/P3 拒绝 → 会话侧降级阶梯接管
        // 熔断（07 §5）：
        uint64_t breaker_dropped = 0;  // 熔断开期内 P2/P3 直接拒
        uint64_t breaker_openings = 0;
        // 成本台账（07 §7 计数雏形；事件形态属进程流批）
        uint64_t tokens_in_total = 0;
        uint64_t tokens_out_total = 0;
    };

    explicit Gateway(LLMProvider& provider, Caps caps = {}, quota::Ledger* ledger = nullptr);

    // 受理：返回 request_ref；0 = 未受理（车道满 / 配额耗尽 / 熔断丢弃）。
    // now_ms：配额桶与熔断冷却的时钟（调用方注入，确定性）。
    uint64_t submit(CompletionRequest req, uint64_t now_ms = 0);

    // 驱动一件：队头 deadline 已过 → 到期失败回收（预留释放）；否则交供应商完成
    // 并结算真实用量（多退少补）+ 台账累计。无事可做返回 nullopt。
    std::optional<Completion> pump(uint64_t now_ms);

    // 饱和度 ∈ [0,1]：队列深度水位（07 §4 广播信号的本地估计；熔断开期 = 1）
    float saturation() const noexcept;

    // 熔断器状态（07 §5：状态事件进进程流属后续批；此处供测试/观测）
    bool breaker_open() const noexcept { return breaker_ != State::Closed; }

    const Counters& counters() const noexcept { return c_; }

private:
    enum class State : uint8_t { Closed, Open, HalfOpen };

    struct Entry {
        uint64_t ref = 0;
        CompletionRequest req;
        double estimate = 0.0;   // 预留量（结算/释放基准）
        bool probe = false;      // 半开探测件
    };

    std::size_t queued() const noexcept;
    void record_outcome(bool ok, uint64_t now_ms);
    bool admit_by_breaker(uint64_t now_ms, bool& probe);

    LLMProvider& provider_;
    quota::Ledger* ledger_;
    Caps caps_;
    Counters c_;
    uint64_t next_ref_ = 1;   // 0 保留为"未受理"
    std::array<std::deque<Entry>, 4> lanes_;

    // 熔断器状态机（07 §5：Closed →(失败率超阈) Open →(冷却期满+单件探测) Closed）
    State breaker_ = State::Closed;
    std::deque<bool> outcomes_;
    uint64_t opened_at_ms_ = 0;
    bool probe_in_flight_ = false;
};

}   // namespace oly::gateway
