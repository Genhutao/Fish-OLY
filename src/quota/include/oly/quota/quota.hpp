// OLY · quota —— 配额与限流（09）M1-2 网关硬墙形态：
//   两层桶（突发 token-bucket + 体量窗口）× 键（主体维 + 世界维）；
//   预留-结算-释放协议（09 §3.2：token 事后才知道真实值——submit 预估预留，
//   响应结算多退少补，deadline 到期释放，无悬挂占用）。
//   世界预算 = 第二维度（09 §2）：一次花费实际可用 = min(主体余量, 世界余量)，
//   任一处触顶即受限；配额树（quota.granted 分层授予）与租约下发属后续批。
// M1-2 形态：每键同一组限制（全局 Limits）；per-node 配额树/租约/身份流事件
//   = 后续批（注释即契约）。确定性：时钟由调用方注入（now_ms），无墙钟依赖。
#pragma once

#include <cstdint>
#include <unordered_map>

namespace oly::quota {

// 突发桶（09 §3.1）：令牌桶（速率 + 容量，秒级）——防瞬时挤兑
struct BurstConfig {
    double rate_per_s = 0.0;   // 回复速率（token/s）
    double burst = 0.0;        // 桶容量（token）
};

// 体量配额（09 §3.1）：固定窗口（日/月窗口的 M1-2 雏形）——成本控制
struct VolumeConfig {
    double cap = 0.0;          // 窗口内总预算（token）
    uint64_t window_ms = 0;    // 窗口长度（ms）
};

// 一组限制（M1-2：全键同限；配额树 per-node 化属后续批）
struct Limits {
    BurstConfig burst;
    VolumeConfig volume;       // 主体维体量
    VolumeConfig world;        // 世界维体量（第二维度）
};

// 预留裁决（P0 结构化错误原料，09 §5：quota_exhausted + 重置时间）
struct Verdict {
    enum class Code : uint8_t { Ok, BurstLimited, VolumeExhausted, WorldExhausted };
    Code code = Code::Ok;
    double granted = 0.0;      // 实际预留量（等于 want 或 0）
    uint64_t reset_ms = 0;     // 受限时下一窗口起点（retry-after 原料）
};

// 键控两层桶册。键由调用方派生（主体 ulid 键 / 世界内容键）。
class Ledger {
public:
    explicit Ledger(Limits limits) : limits_(limits) {}

    // 预留：min(主体, 世界) 逐层裁决（09 §2 任一处触顶即受限）；
    // 主/世界键为 0 = 该维度旁路（无归属请求）。
    Verdict reserve(uint64_t now_ms, uint64_t principal_key, uint64_t world_key, double want);

    // 结算：真实用量多退少补（预留已扣 reserved；实际少扣回退、超扣追缴）
    void settle(uint64_t now_ms, uint64_t principal_key, uint64_t world_key, double reserved,
                double actual);

    // 释放（deadline 到期回收；无悬挂占用，07 §3）
    void release(uint64_t principal_key, uint64_t world_key, double want);

    // 水位 ∈ [0,1]：1 − min(主体余, 世界余)/cap（饱和度闭环输入，07 §4；
    // 世界预算将尽 → 角色群体性"今日话少"）
    float world_pressure(uint64_t now_ms, uint64_t principal_key, uint64_t world_key) const;

private:
    struct Bucket {
        double tokens = 0.0;       // 突发桶现有量（首用播种满桶）
        bool seeded = false;
        uint64_t last_ms = 0;      // 上次回复/消费位点
        double volume_used = 0.0;  // 窗口已耗
        uint64_t window_start = 0; // 窗口起点
    };
    Bucket& bucket(uint64_t key);
    Bucket* find(uint64_t key);
    const Bucket* find(uint64_t key) const;

    double burst_available(const Bucket& b, uint64_t now_ms) const;
    double volume_available(const Bucket& b, uint64_t now_ms, const VolumeConfig& vc) const;
    Verdict try_take(Bucket& b, const VolumeConfig& vc, uint64_t now_ms, double want,
                     Verdict::Code on_empty);

    Limits limits_;
    std::unordered_map<uint64_t, Bucket> buckets_;
};

}   // namespace oly::quota
