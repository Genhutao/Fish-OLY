#include "oly/session/prompt_board.hpp"

#include <algorithm>
#include <type_traits>

namespace oly::session {
namespace {

// 槽位预算比例（06 §3.0 表；比例属配置面 08，Identity/Output 地板为平台包络）
constexpr float kSlotRatio[kSlotCount] = {
    0.12f,   // Identity
    0.13f,   // Canon
    0.08f,   // State
    0.25f,   // Memory
    0.22f,   // Recent
    0.08f,   // Skills
    0.12f,   // Output
};

}   // namespace

std::size_t estimate_tokens_heuristic(const std::string_view s) noexcept {
    std::size_t ascii = 0, wide = 0, other = 0;
    std::size_t i = 0;
    while (i < s.size()) {
        const auto b = static_cast<unsigned char>(s[i]);
        if (b < 0x80) {
            ++ascii;
            i += 1;
        } else if ((b & 0xE0) == 0xC0) {
            ++other;   // 2 字节：拉丁扩展等
            i += 2;
        } else if ((b & 0xF0) == 0xE0) {
            ++wide;   // 3 字节：CJK
            i += 3;
        } else {
            other += 2;   // 4 字节：emoji 等
            i += 4;
        }
    }
    return (ascii + 3) / 4 + (wide * 3 + 1) / 2 + other;
}

namespace {

template <typename T>
const std::string& item_text(const T& item) {
    if constexpr (std::is_same_v<T, std::string>) {
        return item;
    } else {
        return item.second;
    }
}

// 槽内贪心装配：保留装得下的前缀（输入已按取舍优先级排序），返回保留条数
template <typename Vec>
std::size_t fit_prefix(Vec& items, const std::size_t cap, const TokenEstimator& count) {
    std::size_t used = 0, keep = 0;
    while (keep < items.size()) {
        const auto t = count(item_text(items[keep]));
        if (used + t > cap) break;
        used += t;
        ++keep;
    }
    return keep;
}

std::string join(const auto& items) {
    std::string acc;
    for (const auto& item : items) {
        acc += item_text(item);
        acc += '\n';
    }
    return acc;
}

}   // namespace

Assembled assemble_board(const std::size_t context_tokens, const SlotInput& in,
                         const TokenEstimator& est) {
    TokenEstimator count = est;
    if (!count) count = [](const std::string_view s) { return estimate_tokens_heuristic(s); };

    Assembled b;
    BoardManifest& m = b.manifest;
    m.budget = static_cast<std::size_t>(static_cast<float>(context_tokens) * 0.8f);
    const auto budget_of = [&](const Slot s) noexcept {
        return std::max<std::size_t>(
            1, static_cast<std::size_t>(static_cast<float>(m.budget) *
                                        kSlotRatio[static_cast<std::size_t>(s)]));
    };

    // ── 地板/高优槽：整块放入，永不截断（06 §3.0）──────────────────
    b.slots[static_cast<std::size_t>(Slot::Identity)] = in.identity;
    b.slots[static_cast<std::size_t>(Slot::Canon)] = in.canon;
    b.slots[static_cast<std::size_t>(Slot::State)] = in.state;
    b.slots[static_cast<std::size_t>(Slot::Output)] = in.output;

    // ── 可降级槽：按取舍优先级排序后预算内贪心 ─────────────────────
    auto memory = in.memory;
    std::stable_sort(memory.begin(), memory.end(),
                     [](const auto& a, const auto& b2) { return a.first > b2.first; });   // 分数降序
    memory.resize(fit_prefix(memory, budget_of(Slot::Memory), count));

    auto recent = in.recent;   // 时间正序（最旧在前）
    recent.resize(fit_prefix(recent, budget_of(Slot::Recent), count));

    auto skills = in.skills;
    std::stable_sort(skills.begin(), skills.end(),
                     [](const auto& a, const auto& b2) { return a.first < b2.first; });   // 优先级 1 最高在前
    skills.resize(fit_prefix(skills, budget_of(Slot::Skills), count));

    // ── 全局降级循环（06 §3.0 顺序）：Memory（丢分数最低）→ Recent（丢最旧）→ Skills（丢最低优先级）
    const auto total_of = [&] {
        std::size_t t = 0;
        for (const auto& s : b.slots) t += count(s);
        t += count(join(memory)) + count(join(recent)) + count(join(skills));
        return t;
    };
    std::size_t mem_dropped = 0, rec_dropped = 0, skl_dropped = 0;
    while (total_of() > m.budget) {
        if (!memory.empty()) {
            memory.pop_back();
            ++mem_dropped;
        } else if (!recent.empty()) {
            recent.erase(recent.begin());
            ++rec_dropped;
        } else if (!skills.empty()) {
            skills.pop_back();
            ++skl_dropped;
        } else {
            break;   // 只剩地板/高优槽：比例表下不出现；Canon/State 超支仅标记不截（M1 细化）
        }
    }

    b.slots[static_cast<std::size_t>(Slot::Memory)] = join(memory);
    b.slots[static_cast<std::size_t>(Slot::Recent)] = join(recent);
    b.slots[static_cast<std::size_t>(Slot::Skills)] = join(skills);

    // ── manifest（board_manifest，可解释性）───────────────────────
    const auto floor_slot = [&](const Slot s) noexcept {
        return s == Slot::Identity || s == Slot::Output;
    };
    for (std::size_t i = 0; i < kSlotCount; ++i) {
        const auto slot = static_cast<Slot>(i);
        m.used[i] = count(b.slots[i]);
        m.truncated[i] = !floor_slot(slot) &&
                         (m.used[i] > budget_of(slot) ||
                          (slot == Slot::Memory && mem_dropped > 0) ||
                          (slot == Slot::Recent && rec_dropped > 0) ||
                          (slot == Slot::Skills && skl_dropped > 0));
        m.total += m.used[i];
    }
    return b;
}

std::string render_prompt(const Assembled& b) {
    static constexpr const char* kHeader[kSlotCount] = {
        "[Identity]", "[Canon]", "[State]", "[Memory]", "[Recent]", "[Skills]", "[Output]",
    };
    std::string out;
    for (std::size_t i = 0; i < kSlotCount; ++i) {
        const auto& s = b.slots[i];
        if (s.empty()) continue;
        out += kHeader[i];
        out += '\n';
        out += s;
        out += '\n';
    }
    return out;
}

}   // namespace oly::session
