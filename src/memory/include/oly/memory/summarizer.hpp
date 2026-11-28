// OLY · memory —— 确定性归纳器（06 §4 反思管线的规则形态）：
//   每完整 window 条消息折叠一条 Summary 候选；尾段不足窗不产册——追加式流下
//   早前窗口永不重排 → 指纹稳定 → 覆盖重建可挂（02 §5.4 重建协议的前提）。
//   LLM 批化归纳（P3 车道、token 大头，06 §4）= M1-2 网关完整批的换型点；
//   本形态是重建协议与召回规则的地基：同输入 → 逐字节同候选。
#pragma once

#include "oly/core/id.hpp"
#include "oly/memory/memory.hpp"

#include <span>
#include <string_view>
#include <vector>

namespace oly::memory {

struct SummarizerConfig {
    std::size_t window = 10;         // 折叠窗（06 §4：事实抽取每 10 轮批化）
    std::size_t max_content = 120;   // 候选文本上限（UTF-8 码点边界截断，不劈字）
    double base_importance = 0.3;
    std::size_t max_sources = 16;    // 溯源 seq 上限（防超长引用链）
};

struct FoldedMessage {
    uint64_t seq = 0;
    PrincipalId actor;
    std::string_view text;
};

// 折叠：同输入流 → 同候选册。owner = 归属主体（02 A5：记忆归属与该角色互动的主体），
// persona = 分册角色；id 由 Store 内容寻址回填（指纹前缀）。
std::vector<LongTermEntry> fold_messages(const SessionId session, const PrincipalId owner,
                                         const PrincipalId persona, const int card_version,
                                         const int generation, const SummarizerConfig& cfg,
                                         std::span<const FoldedMessage> msgs);

}   // namespace oly::memory
