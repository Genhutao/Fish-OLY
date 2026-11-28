// OLY · session —— 决策驱动（06 §3 的 M0 形态）：
//   感知（窗口特征派生）→ 数值估分（scoring 内核）→ 过阈值则装配提示板（06 §3.0）
//   → 网关基础通路 submit/pump → speak；失败走降级阶梯（06 §3.2）：
//   第一级语气储备（内置预生成台词）→ 第二级沉默事件化。
// M0 简化（注释即换型点）：单候选自评、无工具循环（≤3 轮属 M1）、无记忆召回、
// 关系/互惠特征取常量（反思管线 M1 接管）。状态（精力/连续发言/EMA）不进快照，
// 由拍序与事件流确定性重导出（05 §5）。
#pragma once

#include "oly/domain/domain.hpp"
#include "oly/gateway/gateway.hpp"
#include "oly/session/autonomy.hpp"
#include "oly/session/prompt_board.hpp"
#include "oly/session/scoring.hpp"

#include <array>
#include <cstdint>
#include <string>

namespace oly::session {

struct DeliberationConfig {
    AttentionWeights weights{};        // 特征权重（08 配置面覆盖点）
    ScoreConfig score{};               // 阈值/冷却/K
    uint32_t context_tokens = 4096;    // 提示板预算基准（模型上下文 × 预留系数在板内处理）
    std::size_t window_size = 8;       // Recent 槽取最近 N 条
};

class DeliberationDriver final : public AutonomyDriver {
public:
    // reserve：语气储备库（06 §3.2：会话空闲拍预生成；M0 由构造方注入固定组）
    DeliberationDriver(domain::PrincipalId persona, gateway::Gateway& gw,
                       std::vector<std::string> reserve, DeliberationConfig cfg = {});

    TickOutcome on_tick(const TickContext& ctx) override;

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
};

}   // namespace oly::session
