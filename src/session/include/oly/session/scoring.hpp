// OLY · session —— 感知估分数值内核（06 §2）：纯函数、零分配、确定性可重放。
// 注意力 = Σ wᵢ·fᵢ − cost + 噪声；噪声种子 = hash(stream_key, tick, index)
// —— 打分状态不进快照，任意时刻可由事件流重导出（05 §5）。
#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace oly::session {

// 特征权重与成本参数（06 §2.2 特征表；数值属配置面 08，M0 常量起步）
struct AttentionWeights {
    float mention = 1.6f;      // 被@/被引用/被回复
    float topic = 0.8f;        // 词汇重叠（BM25-lite 粗筛）
    float relation = 0.6f;     // affinity[X][author]（M1 反思管线更新）
    float reciprocity = 0.5f;  // 近期互转
    float novelty = 0.4f;      // 1 − 与近期窗口相似度（抑制复读刷屏）
    float urgency = 1.2f;      // 导演注入 > 用户消息 > 环境
    float base_cost = 0.5f;    // 发言成本基线
    float streak_k = 0.35f;    // 连续发言成本斜率
};

// 每拍配置（阈值校准 06 §2.3；每次调用完整给定，无隐藏状态）
struct ScoreConfig {
    float tau_base = 1.0f;
    float activity_ema = 0.0f;   // 近期发言率 EMA ∈ [0,1]：热闹房间 ↑τ 防刷屏、冷场 ↓τ 防死寂
    float mood_offset = 0.0f;    // 疲惫/低落 ↑τ（M1 presence 接入）
    float noise = 0.15f;         // 噪声幅度（拍内对称）
    uint32_t max_decisions = 2;  // 每拍 K 个决策上限（08 配置）
    uint32_t hard_cooldown = 2;  // 硬冷却 H 拍：期间不可发言
};

// SoA 候选特征（06 §2.1 零分配布局的 M0 投影：跨度 = count，调用方池化）
struct CandidateFeatures {
    uint32_t count = 0;
    std::span<const float> mention, topic, relation, reciprocity, novelty, urgency;
    std::span<const float> energy;     // ∈ [0,1]，低精力抬高成本
    std::span<const float> streak;     // 连续发言计数（成本递增）
    std::span<const uint32_t> cooldown;   // 剩余硬冷却拍数
};

struct Scored {
    uint32_t index = 0;
    float attention = 0.0f;
};

// 发言阈值 τ = τ_base × activity_factor + mood_offset
float speak_threshold(const ScoreConfig& cfg) noexcept;

// 单候选原始注意力（含噪声；score() 逐元素调用它，测试可精确断言）
float attention_of(const AttentionWeights& w, const ScoreConfig& cfg, uint64_t stream_key,
                   uint64_t tick, uint32_t index, float mention, float topic, float relation,
                   float reciprocity, float novelty, float urgency, float energy,
                   float streak) noexcept;

// 会话级批打分（M1 会话层 O(M) 主循环的入口；M0 决策驱动走单候选形态）：
// 返回 attention 降序（同分按 index 升序）、过 τ 且未硬冷却的前 K 个。
std::vector<Scored> score(const AttentionWeights& w, const ScoreConfig& cfg, uint64_t stream_key,
                          uint64_t tick, const CandidateFeatures& f);

// 沉默事件化采样（06 §2.4）：被@却沉默必记；borderline 带 [0.6τ, τ) 10% 采样。
// 返回应记沉默的候选下标（确定性：同输入同结果）。
std::vector<uint32_t> collect_silences(const ScoreConfig& cfg, uint64_t stream_key, uint64_t tick,
                                       std::span<const float> scores,
                                       std::span<const float> mentioned);

}   // namespace oly::session
