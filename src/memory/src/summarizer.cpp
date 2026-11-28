#include "oly/memory/summarizer.hpp"

#include <algorithm>
#include <string>

namespace oly::memory {
namespace {

// UTF-8 码点边界截断（不劈多字节字）：截到至多 max_bytes 字节，回退到首字节
// （0b0xxxxxxx / 0b11xxxxxx）为止。确定性：同输入同截点。
std::string truncate_utf8(const std::string_view s, const std::size_t max_bytes) {
    if (s.size() <= max_bytes) return std::string{s};
    std::size_t cut = max_bytes;
    while (cut > 0 && (static_cast<uint8_t>(s[cut]) & 0xC0) == 0x80) --cut;   // 续字节回退
    return std::string{s.substr(0, cut)};
}

}   // namespace

std::vector<LongTermEntry> fold_messages(const SessionId session, const PrincipalId owner,
                                         const PrincipalId persona, const int card_version,
                                         const int generation, const SummarizerConfig& cfg,
                                         const std::span<const FoldedMessage> msgs) {
    std::vector<LongTermEntry> out;
    if (cfg.window == 0) return out;
    if (msgs.size() < cfg.window) return out;   // 尾段不足窗不产册（窗口永不重排）

    const std::size_t windows = msgs.size() / cfg.window;
    out.reserve(windows);
    for (std::size_t w = 0; w < windows; ++w) {
        const auto begin = msgs.begin() + static_cast<std::ptrdiff_t>(w * cfg.window);
        const auto end = begin + static_cast<std::ptrdiff_t>(cfg.window);

        LongTermEntry e;
        e.owner = owner;
        e.persona = persona;
        e.session = session;
        e.card_version = card_version;
        e.generation = generation;
        e.kind = EntryKind::Summary;
        e.importance = cfg.base_importance;
        e.valid = TimeRange{begin->seq, ~0ull};   // 开放右端：候选有效至今（时效冲突裁决随事实抽取批）

        // 溯源 = 窗口内消息 seq（上限截前缀，防超长引用链；指纹随之稳定）
        e.source_seqs.reserve(std::min(cfg.window, cfg.max_sources));
        for (auto it = begin; it != end && e.source_seqs.size() < cfg.max_sources; ++it) {
            e.source_seqs.push_back(it->seq);
        }

        // 内容 = 窗口文本按序折叠（"；"衔接）+ 码点边界截断
        std::string joined;
        for (auto it = begin; it != end; ++it) {
            if (!joined.empty()) joined += "；";
            joined += it->text;
        }
        e.content = truncate_utf8(joined, cfg.max_content);
        out.push_back(std::move(e));
    }
    return out;
}

}   // namespace oly::memory
