// OLY · config —— 配置面（08）M1-3 确定性内核形态：
//   配置即内容：Values 规范编码 → ContentId（行为归因粒度：同 id 同行为）；
//   分层合并：platform → world → session → persona，下层覆盖上层；
//   包络：平台上下限是承诺不是默认值——写入时拒绝（denial），合并后防御性钳制
//   （历史 blob / 导入 blob 可能越界）；
//   三级自调（08 §3）：人格类仅创建者、操作类创建者或被授予 tune_self 的角色本人、
//   世界锁定（canon_lock=true → 下层 session/persona 覆盖整体失效）；
//   变更即命令事件：patch 落所属流（会话层 → 会话流，runtime 折叠重放还原）——
//   本模块只管值语义与合并，流事件由调用方（session runtime）落地。
// 换型点（注释即契约）：world/platform 层进程流下发、per-principal 灰度、
//   persona 层身份流事件化（随身份流落库批）、调性格演化走 card.propose（03 §5）。
#pragma once

#include "oly/core/id.hpp"
#include "oly/core/result.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace oly::config {

// 提示板槽位序（06 §3.0 表序；与 session/prompt_board 的 Slot 枚举一一对应）
constexpr std::size_t kSlotCount = 7;
enum SlotIndex : std::size_t { kIdentity = 0, kCanon, kState, kMemory, kRecent, kSkills, kOutput };

// 特征权重（session::AttentionWeights 的配置面镜像——依赖方向 session → config）
struct Weights {
    float mention = 1.6f;
    float topic = 0.8f;
    float relation = 0.6f;
    float reciprocity = 0.5f;
    float novelty = 0.4f;
    float urgency = 1.2f;
    float base_cost = 0.5f;
    float streak_k = 0.35f;
    friend bool operator==(const Weights&, const Weights&) = default;
};

// 生效值集（M1-3 子集：自治 + 提示板预算 + 记忆召回；08 §2 总账逐域扩展）
struct Values {
    float tau_base = 1.0f;
    uint32_t max_decisions = 2;
    uint32_t hard_cooldown = 2;
    float sat_gain = 0.5f;                 // 饱和度→τ 增益（07 §4）
    std::size_t recall_topk = 4;           // Memory 槽召回上限
    Weights weights{};
    std::array<float, kSlotCount> budgets{0.12f, 0.13f, 0.08f, 0.25f, 0.22f, 0.08f, 0.12f};

    friend bool operator==(const Values&, const Values&) = default;
};

// 增量补丁（变更即命令事件的载荷形态；每项独立可选）
struct Patch {
    std::optional<float> tau_base;
    std::optional<uint32_t> max_decisions;
    std::optional<uint32_t> hard_cooldown;
    std::optional<float> sat_gain;
    std::optional<std::size_t> recall_topk;
    std::optional<Weights> weights;
    std::optional<std::array<float, kSlotCount>> budgets;

    [[nodiscard]] bool empty() const noexcept;
    friend bool operator==(const Patch&, const Patch&) = default;
};

// 平台包络（08 §2：承诺而非默认值；M1-3 进程内常量，运营经进程流下发属后续批）
struct Envelope {
    float tau_floor = 0.05f;        // τ_base 下限（防刷屏——τ 过低世界变话痨）
    uint32_t k_max = 8;             // 每拍决策数上限
    uint32_t cooldown_max = 20;     // 硬冷却上限（拍）
    float sat_gain_max = 2.0f;
    std::size_t recall_max = 16;
    float budget_floor_identity = 0.12f;   // Identity/Output 地板锁死（平台承诺）
    float budget_floor_output = 0.12f;
    float budget_slot_max = 0.60f;         // 单槽上限（防挤压全部）
};

// 字段组（08 §3 三级自调的裁决单位）
enum class FieldGroup : uint8_t { Persona, Ops };   // 人格类 / 操作类

// patch 所触字段组集合（ personalities vs 运行方式）
[[nodiscard]] bool touches(const Patch& p, FieldGroup g) noexcept;

// 三级自调裁决（08 §3，persona 层）：人格类仅创建者；操作类创建者或
// 被授予 tune_self 的角色本人。返回 denial 原因（审计原料）。
struct Verdict {
    bool allow = false;
    std::string reason;
};
[[nodiscard]] Verdict authorize_persona(const Patch& p, PrincipalId who, PrincipalId creator,
                                        bool tune_self_granted);

// 应用补丁（persona/session 层通用；调用方先过包络/授权裁决）
[[nodiscard]] Values apply(const Values& base, const Patch& p);

// 防御性包络钳制（合并后强制；地板 = 平台承诺）
[[nodiscard]] Values clamp(const Values v, const Envelope& env);

// 合并后的生效配置：values（已钳制）+ 内容寻址 id（行为归因：decision.recorded 附着）
struct Effective {
    Values values{};
    ContentId content_id{};   // = sha256(规范字节(values))；同 id 同行为
};

struct Layer {
    std::optional<Patch> patch;
    bool canon_lock = false;   // 世界锁定类（08 §3）：下层 session/persona 覆盖整体失效
};

// 分层合并（08 §1）：自底向上 platform → world → session → persona；
// world 层 canon_lock=true 时跳过 session/persona 层（多人世界行为一致性）。
[[nodiscard]] Effective merge(std::span<const Layer> bottom_up);

// 规范编码（同输入逐字节同输出）：values → ContentId（行为归因）；patch → 事件载荷
[[nodiscard]] std::vector<std::byte> encode_values(const Values& v);
[[nodiscard]] std::vector<std::byte> encode_patch(const Patch& p);
[[nodiscard]] Patch decode_patch(std::span<const std::byte> bytes);   // 失败 → 空 patch

// 默认层（platform 基线 = Values 默认值；不产 patch）
[[nodiscard]] Effective platform_defaults();

// 由已解析值构造生效配置（钳制 + 内容寻址）——runtime 会话层生效配置的构造通路
[[nodiscard]] Effective finalize(Values v);

}   // namespace oly::config
