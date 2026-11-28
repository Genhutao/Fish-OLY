#include "oly/memory/memory.hpp"

#include "oly/codec/codec.hpp"
#include "oly/core/error.hpp"
#include "oly/core/hash.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <unordered_set>

namespace oly::memory {
namespace {

// 词汇粗筛（06 §2.2 同型的码点级二元组；字节对方案会把多字节字的续字节
// 折叠出跨字伪命中——私(E7 A7 81) 与 见(E8 A7 81) 共享尾对 (A7,81)——
// 打分可容噪，召回闸不可。向量召回 = usearch 接入后的换型点。）
using Cgrams = std::unordered_set<uint32_t>;

Cgrams cgrams_of(const std::string_view s) {
    Cgrams out;
    char32_t prev = 0;
    bool has_prev = false;
    for (std::size_t i = 0; i < s.size();) {
        const auto lead = static_cast<uint8_t>(s[i]);
        std::size_t len = 1;
        char32_t cp = lead;
        if ((lead & 0xE0) == 0xC0 && i + 1 < s.size()) {
            len = 2;
            cp = (static_cast<char32_t>(lead & 0x1F) << 6) |
                 (static_cast<char32_t>(static_cast<uint8_t>(s[i + 1])) & 0x3F);
        } else if ((lead & 0xF0) == 0xE0 && i + 2 < s.size()) {
            len = 3;
            cp = (static_cast<char32_t>(lead & 0x0F) << 12) |
                 (static_cast<char32_t>(static_cast<uint8_t>(s[i + 1])) & 0x3F) << 6 |
                 (static_cast<char32_t>(static_cast<uint8_t>(s[i + 2])) & 0x3F);
        } else if ((lead & 0xF8) == 0xF0 && i + 3 < s.size()) {
            len = 4;
            cp = (static_cast<char32_t>(lead & 0x07) << 18) |
                 (static_cast<char32_t>(static_cast<uint8_t>(s[i + 1])) & 0x3F) << 12 |
                 (static_cast<char32_t>(static_cast<uint8_t>(s[i + 2])) & 0x3F) << 6 |
                 (static_cast<char32_t>(static_cast<uint8_t>(s[i + 3])) & 0x3F);
        }
        if (has_prev) {
            out.insert((static_cast<uint32_t>(prev & 0xFFFF) << 16) |
                       static_cast<uint32_t>(cp & 0xFFFF));
        }
        prev = cp;
        has_prev = true;
        i += len;
    }
    return out;
}

double overlap_ratio(const Cgrams& a, const Cgrams& b) {
    if (a.empty() || b.empty()) return 0.0;
    std::size_t inter = 0;
    for (const auto g : a) {
        if (b.contains(g)) ++inter;
    }
    return static_cast<double>(inter) / static_cast<double>(b.size());
}

// 指纹全序（ContentId 自带 <=>）：候选册与覆盖册均按指纹升序维护——
// 遍历确定 = 召回确定（重建协议的确定性前提）。
auto entry_before_fp() {
    return [](const LongTermEntry& e, const ContentId& k) { return fingerprint(e) < k; };
}
auto overlay_before_fp() {
    return [](const std::pair<ContentId, Review>& o, const ContentId& k) { return o.first < k; };
}

}   // namespace

ContentId fingerprint(const LongTermEntry& e) {
    // 规范字节（03 §6：sources + content）＝ 会话 ULID ‖ 溯源数 u32 ‖ 各溯源 seq u64 ‖ 内容
    codec::Encoder enc;
    enc.ulid(e.session.bytes());
    enc.u32(static_cast<uint32_t>(e.source_seqs.size()));
    for (const auto seq : e.source_seqs) enc.u64(seq);
    enc.text(e.content);
    return ContentId::from_bytes(sha256(enc.span()).bytes);
}

std::size_t Store::submit_candidates(const std::span<const LongTermEntry> batch) {
    std::size_t added = 0;
    for (const auto& e : batch) {
        const auto fp = fingerprint(e);
        // 幂等：同指纹候选只记一次（归纳重跑 / 批重投递不产生重复册目）
        const auto it = std::lower_bound(entries_.begin(), entries_.end(), fp, entry_before_fp());
        if (it != entries_.end() && fingerprint(*it) == fp) continue;
        LongTermEntry copy = e;
        // 内容寻址 id = 指纹前 16 字节（02 §1 二元结构：实体 id 即内容寻址）
        Ulid::Bytes idb{};
        std::copy_n(fp.bytes.begin(), 16, idb.begin());
        copy.id = MemoryId::from_bytes(idb);
        entries_.insert(it, std::move(copy));
        ++added;
    }
    return added;
}

Result<void> Store::review(const ContentId& target, const Review& r) {
    // 归属校验（A5）：目标在册时 by 必须 = 归属主体。待挂覆盖（目标尚未归纳出）
    // 无法即时校验——挂接生效走召回路径，册目存在后以 owner 过滤兜底（重建协议注）。
    const auto it = std::lower_bound(entries_.begin(), entries_.end(), target, entry_before_fp());
    if (it != entries_.end() && fingerprint(*it) == target) {
        if (!(r.by == it->owner)) {
            return unexpected(err("memory:forbidden", "review by non-owner"));
        }
    }
    if (r.op == ReviewOp::Edit && r.content.empty()) {
        return unexpected(err("memory:validation", "edit content required"));
    }
    if (r.op == ReviewOp::Merge && r.merge_with.is_nil()) {
        return unexpected(err("memory:validation", "merge target required"));
    }
    if (r.op == ReviewOp::PinImportance && (r.importance < 0.0 || r.importance > 1.0)) {
        return unexpected(err("memory:validation", "importance out of range"));
    }
    // 同目标重复覆盖 = 修正（latest-wins，键 = 指纹）
    const auto ov = std::lower_bound(overlays_.begin(), overlays_.end(), target, overlay_before_fp());
    if (ov != overlays_.end() && ov->first == target) {
        ov->second = r;
    } else {
        overlays_.insert(ov, {target, r});
    }
    return {};
}

void Store::publish() {
    // 整册重制 → 原子换手：写端低频（归纳批 / 人工审阅），读端无锁（召回每拍每角色）
    auto v = std::make_shared<View>();
    v->entries_ = entries_;
    v->overlays_ = overlays_;
    view_.store(std::move(v), std::memory_order_release);
}

std::unique_ptr<Store> Store::rebuild(
    const std::span<const LongTermEntry> candidates,
    const std::span<const std::pair<ContentId, Review>> overlays) {
    auto s = std::make_unique<Store>();
    (void)s->submit_candidates(candidates);
    // 覆盖全量携带：匹配不上的保留待挂——候选恢复后自动复效（02 §5.4 人工劳动永不丢失）
    s->overlays_.assign(overlays.begin(), overlays.end());
    std::sort(s->overlays_.begin(), s->overlays_.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    s->publish();
    return s;
}

void View::recall(const PrincipalId persona, const std::span<const PrincipalId> members,
                  const std::string_view query, const uint64_t at_seq, const std::size_t limit,
                  std::vector<Recalled>& out) const {
    out.clear();
    const auto qcg = cgrams_of(query);

    // 合并对端册：overlay(A)={Merge, merge_with=B} → B 隐藏、A 出合并文本（02 §5.4 merge）
    std::vector<MemoryId> absorbed;
    for (const auto& [fp, ov] : overlays_) {
        if (ov.op == ReviewOp::Merge) absorbed.push_back(ov.merge_with);
    }

    struct Ranked {
        Recalled r;
        double score = 0.0;
        ContentId fp{};
    };
    std::vector<Ranked> ranked;
    for (const auto& e : entries_) {
        // 时效（02 §5.2 valid）：不覆盖查询位点的候选不召回（冲突保留、以区间裁决）
        if (!e.valid.covers(at_seq)) continue;
        // scope 过滤（02 §5.3）：归属主体必须在座 + 本 persona 分册——
        // 不在座主体的私密记忆不可被引用（隐私 + 相关性）
        if (!(e.persona == persona)) continue;
        const bool present = std::any_of(members.begin(), members.end(),
                                         [&](const PrincipalId m) { return m == e.owner; });
        if (!present) continue;

        const auto fp = fingerprint(e);
        const auto ov = std::lower_bound(overlays_.begin(), overlays_.end(), fp, overlay_before_fp());
        const bool has = ov != overlays_.end() && ov->first == fp;
        if (has && ov->second.op == ReviewOp::Delete) continue;   // 删除不召回
        if (std::any_of(absorbed.begin(), absorbed.end(),
                        [&](const MemoryId m) { return m == e.id; })) {
            continue;   // 已并入他条
        }

        Recalled r;
        r.id = e.id;
        r.owner = e.owner;
        r.persona = e.persona;
        r.session = e.session;
        r.source_seqs = e.source_seqs;
        r.card_version = e.card_version;
        r.kind = e.kind;
        r.content = e.content;
        r.importance = e.importance;
        r.generation = e.generation;
        if (has) {
            const auto& o = ov->second;
            r.reviewed = true;
            if (o.op == ReviewOp::Edit || o.op == ReviewOp::Merge) r.content = o.content;
            if (o.op == ReviewOp::PinImportance) r.importance = o.importance;
        }

        Ranked rk;
        rk.fp = fp;
        rk.r = std::move(r);
        // 关键词召回 + importance 加权（等权折半；MMR 去重/向量重排 = 换型点）
        const double kw = overlap_ratio(cgrams_of(rk.r.content), qcg);
        rk.score = 0.5 * kw + 0.5 * std::clamp(rk.r.importance, 0.0, 1.0);
        // 相关性闸：零关键词命中且未钉定 → 不入册（importance 只加权序，不豁免
        // 相关性；pin 是唯一强制浮现通道——人工钉定即立场）
        const bool pinned = has && ov->second.op == ReviewOp::PinImportance;
        if (kw <= 0.0 && !pinned) continue;
        ranked.push_back(std::move(rk));
    }

    std::sort(ranked.begin(), ranked.end(), [](const Ranked& a, const Ranked& b) {
        if (a.score != b.score) return a.score > b.score;   // 分数降序
        return a.fp < b.fp;                                 // 平分按指纹序（确定性）
    });
    if (ranked.size() > limit) ranked.resize(limit);
    out.reserve(ranked.size());
    for (auto& rk : ranked) out.push_back(std::move(rk.r));
}

}   // namespace oly::memory
