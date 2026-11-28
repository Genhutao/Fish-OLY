// OLY · session —— 决策驱动（06 §3）：
//   感知（窗口特征派生）→ 数值估分（scoring 内核）→ 过阈值则装配提示板（06 §3.0，
//   Memory 槽 = 长期册召回，02 §5.3 召回规则；无召回源时槽缺席 = M0 形态）
//   → 网关通路 submit/pump → speak；失败走降级阶梯（06 §3.2）：
//   第一级语气储备（内置预生成台词）→ 第二级沉默事件化。
// M1-1 形态（注释即换型点）：单候选自评、无工具循环（≤3 轮属 M1）、关系/互惠特征
// 取常量（反思管线接管）；状态（精力/连续发言/EMA）不进快照，由拍序与事件流
// 确定性重导出（05 §5）。last_manifest 供测试与可观测（board_manifest，06 §3.0）。
#pragma once

#include "oly/config/config.hpp"
#include "oly/domain/domain.hpp"
#include "oly/gateway/gateway.hpp"
#include "oly/memory/memory.hpp"
#include "oly/session/autonomy.hpp"
#include "oly/session/prompt_board.hpp"
#include "oly/session/scoring.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
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
    const config::Effective* effective = nullptr;   // 生效配置（08 §1：每拍同步，热生效）
    // ── 饱和度反馈闭环（07 §4：自治频率是控制变量，不是静态参数）────
    quota::Ledger* quota_ledger = nullptr;   // 配额册（空 = 旁路；硬墙在网关）
    ContentId world_ref{};                   // 世界内容键（世界预算第二维度，09 §2）
    float sat_gain = 0.5f;                   // τ_speak 随饱和度上调增益：预算将尽 → 群体"今日话少"
};

// 行动解释（11 §5 的内核形态）：一次决策（发言或被记沉默）的全部归因原料。
// API 端点（10 §4.8）后续批经 runtime 组装下发；本形态供测试/审计/可观测。
struct DecisionTrace {
    uint64_t tick = 0;
    float attention = 0.0f, tau = 0.0f;
    float mention = 0.0f, topic = 0.0f, novelty = 0.0f, urgency = 0.0f;   // 特征表（06 §2.2）
    bool speak = false, fallback = false, silence_noted = false;
    std::string model;
    uint32_t tokens_in = 0, tokens_out = 0;
    BoardManifest manifest{};                  // board_manifest（06 §3.0 可解释性）
    std::vector<uint64_t> cited_seqs;          // 引用记忆的溯源 seq（02 §5.3 provenance）
    ContentId config_id{};                     // 生效配置（08 §1）
};

class DeliberationDriver final : public AutonomyDriver {
public:
    // reserve：语气储备库（06 §3.2：会话空闲拍预生成；M0 由构造方注入固定组）
    DeliberationDriver(domain::PrincipalId persona, gateway::Gateway& gw,
                       std::vector<std::string> reserve, DeliberationConfig cfg = {});

    TickOutcome on_tick(const TickContext& ctx) override;

    // 最近一次装配的提示板清单（06 §3.0 可解释性：槽位 token 实耗；测试/观测用）
    const BoardManifest& last_manifest() const noexcept { return last_manifest_; }
    // 最近一次决策的行动解释（11 §5：发言/沉默均可展开；审计·评测·客服共用）
    const std::optional<DecisionTrace>& last_trace() const noexcept { return last_trace_; }

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
    std::optional<DecisionTrace> last_trace_;
};

}   // namespace oly::session
