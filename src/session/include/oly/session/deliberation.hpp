// OLY · session —— 决策驱动（06 §3）：
//   感知（窗口特征派生）→ 数值估分（scoring 内核）→ 过阈值则装配提示板（06 §3.0，
//   Memory 槽 = 长期册召回，02 §5.3 召回规则；无召回源时槽缺席 = M0 形态）
//   → 网关通路 submit/pump → speak；失败走降级阶梯（06 §3.2）：
//   第一级语气储备（内置预生成台词）→ 第二级沉默事件化。
// M1-1 形态（注释即换型点）：单候选自评、无工具循环（≤3 轮属 M1）、关系/互惠特征
// 取常量（反思管线接管）；状态（精力/连续发言/EMA）不进快照，由拍序与事件流
// 确定性重导出（05 §5）。last_manifest 供测试与可观测（board_manifest，06 §3.0）。
#pragma once

#include "oly/domain/domain.hpp"
#include "oly/gateway/gateway.hpp"
#include "oly/memory/memory.hpp"
#include "oly/session/autonomy.hpp"
#include "oly/session/prompt_board.hpp"
#include "oly/session/scoring.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace oly::session {

struct DeliberationConfig {
    AttentionWeights weights{};        // 特征权重（08 配置面覆盖点）
    ScoreConfig score{};               // 阈值/冷却/K
    uint32_t context_tokens = 4096;    // 提示板预算基准（模型上下文 × 预留系数在板内处理）
    std::size_t window_size = 8;       // Recent 槽取最近 N 条
    const memory::Store* memory = nullptr;   // 长期召回源（空 = Memory 槽缺席）
    std::size_t recall_limit = 4;      // Memory 槽召回上限（06 §3.0 超预算按分截）
    // ── 饱和度反馈闭环（07 §4：自治频率是控制变量，不是静态参数）────
    quota::Ledger* quota_ledger = nullptr;   // 配额册（空 = 旁路；硬墙在网关）
    ContentId world_ref{};                   // 世界内容键（世界预算第二维度，09 §2）
    float sat_gain = 0.5f;                   // τ_speak 随饱和度上调增益：预算将尽 → 群体"今日话少"
};

class DeliberationDriver final : public AutonomyDriver {
public:
    // reserve：语气储备库（06 §3.2：会话空闲拍预生成；M0 由构造方注入固定组）
    DeliberationDriver(domain::PrincipalId persona, gateway::Gateway& gw,
                       std::vector<std::string> reserve, DeliberationConfig cfg = {});

    TickOutcome on_tick(const TickContext& ctx) override;

    // 最近一次装配的提示板清单（06 §3.0 可解释性：槽位 token 实耗；测试/观测用）
    const BoardManifest& last_manifest() const noexcept { return last_manifest_; }

private:
    TickOutcome degrade();   // 网关失败/拒绝 → 储备 → 沉默

    domain::PrincipalId persona_;
    gateway::Gateway& gw_;
    DeliberationConfig cfg_;
    std::vector<std::string> reserve_;

    // 每主体评分状态（不进快照；重放重导出）
    float energy_ = 1.0f;
    float streak_ = 0.0f;
    uint32_t cooldown_ = 0;
    float activity_ema_ = 0.0f;
    std::size_t reserve_idx_ = 0;
    BoardManifest last_manifest_{};
};

}   // namespace oly::session
