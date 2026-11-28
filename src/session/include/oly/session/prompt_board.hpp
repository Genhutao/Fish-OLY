// OLY · session —— 提示板（06 §3.0）：槽位与 token 预算制装配。
// 装配不是"把检索结果拼进去"，而是按槽位预算取舍：
//   Identity / Output 地板锁死（平台包络，任何配置层不可越——08 §2）；
//   超预算降级顺序 Memory → Recent → Skills；Canon/State 不到万不得已不动。
// 每槽位实际 token 数入 manifest（可解释性；M1 写入 decision.recorded.board_manifest）。
// 估算器可换型（M1 接真分词器 / 模型侧计数）。
#pragma once

#include <array>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace oly::session {

enum class Slot : uint8_t {
    Identity = 0,   // 名字、描述、锚点、禁忌 —— 地板锁死
    Canon,          // 世界规则、当前场景 —— 高优先
    State,          // 情绪、精力、关系、日程 —— 高优先
    Memory,         // 事实 + 检索片段 —— 超出按分数截
    Recent,         // 最近 N 轮原文 —— 超出从最旧截
    Skills,         // 已装配技能片段 —— 按装配优先级截
    Output,         // 输出格式与安全约束 —— 地板锁死
    Count,
};
constexpr std::size_t kSlotCount = static_cast<std::size_t>(Slot::Count);

// M0 估算：CJK ≈ 1.5 token/字、ASCII ≈ 4 字符/token、其他按 2 token/字。
// 换型点：真分词器接入后此函数仅作兜底（07 §7 前缀缓存同样透明）。
std::size_t estimate_tokens_heuristic(std::string_view s) noexcept;

using TokenEstimator = std::function<std::size_t(std::string_view)>;

// 装配输入（分数/优先级的排序契约见 assemble_board 内实现）
struct SlotInput {
    std::string identity, canon, state, output;                  // 地板/高优槽（整块放入）
    std::vector<std::pair<float, std::string>> memory;           // {score, text}，分数降序取舍
    std::vector<std::string> recent;                             // 时间正序，超预算从最旧截
    std::vector<std::pair<uint32_t, std::string>> skills;        // {priority(1 最高), text}
};

struct BoardManifest {
    std::array<std::size_t, kSlotCount> used{};     // 每槽位实际 token（board_manifest）
    std::array<bool, kSlotCount> truncated{};       // 是否被降级/标记超支
    std::size_t total = 0;
    std::size_t budget = 0;                         // context × 预留系数
};

struct Assembled {
    std::array<std::string, kSlotCount> slots;
    BoardManifest manifest;
};

// 槽位预算比例（06 §3.0 默认表；比例属配置面 08——限内滑动，Identity/Output
// 地板为平台包络，任何配置层不可越：装配端防御性强制地板）。
struct SlotBudgets {
    std::array<float, kSlotCount> share{0.12f, 0.13f, 0.08f, 0.25f, 0.22f, 0.08f, 0.12f};
};
constexpr float kFloorIdentity = 0.12f;
constexpr float kFloorOutput = 0.12f;
constexpr float kSlotMax = 0.60f;

// 总预算 = context_tokens × 0.8（06 §3.0 预留系数）；budgets 参数 = 配置面覆盖点。
// 确定性：同输入逐字节同输出。
Assembled assemble_board(std::size_t context_tokens, const SlotInput& in,
                         const TokenEstimator& est = {},
                         const SlotBudgets& budgets = {});

// 渲染为单一 prompt 文本（M0 形态；M1 升格为结构化消息序列，槽位次序不变）。
// 空槽位跳过；槽位次序固定 = Identity → Canon → State → Memory → Recent → Skills → Output。
std::string render_prompt(const Assembled& b);

}   // namespace oly::session
