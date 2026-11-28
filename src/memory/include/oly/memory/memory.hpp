// OLY · memory —— 长期记忆管线（02 §5、03 §6 的 M1-1 确定性内核形态）。
// 两层模型：短期 WorkingSet 驻会话 loop、不落库（重放可重建，session 模块持有）；
// 本模块只管长期层 = 派生库：归纳候选（可整批重算）× 审阅覆盖（人工劳动永不丢失）。
//   生效视图 = 候选 × 覆盖；重建协议 = 重跑归纳 → 按 内容指纹 重新套用覆盖（02 §5.4）；
//   默认候选即生效（无人值守世界自治可引用），覆盖仅作人工修正面。
// M1-1 进程内单写者形态（同身份分片纪律）：写端（归纳/审阅）单线程，
//   读端经 atomic shared_ptr<const View> 无锁召回；候选派生自确定性归纳器（summarizer.hpp）。
// 换型点（注释即契约）：usearch 向量召回 / PG 归档（05 §8）/ superseded 区间排除
//   （11 §4 回滚语义）/ 归纳 worker 池与身份流 memory.review 事件化（03 §6）。
#pragma once

#include "oly/core/id.hpp"
#include "oly/core/result.hpp"

#include <atomic>
#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace oly::memory {

enum class EntryKind : uint8_t {
    Fact = 1,     // SPO + 置信度 + valid 区间（M1-1 预留；归纳器仅产 Summary）
    Summary,      // 场景摘要（批化折叠）
    Episode,      // 情节（M1-2 网关批化归纳接管）
};

// 生效区间（流内 seq 域）：确定性裁决不碰墙钟，时效用 seq 表达；WallTime 仅信息性。
struct TimeRange {
    uint64_t from_seq = 0;
    uint64_t to_seq = ~0ull;   // 开放右端 = 至今有效

    constexpr bool covers(const uint64_t seq) const noexcept {
        return seq >= from_seq && seq <= to_seq;
    }
};

// 候选条目（02 §5.2）：不可变；审阅态不驻条目，驻覆盖层（生效视图 = 候选 × 覆盖）。
struct LongTermEntry {
    MemoryId id;                          // 内容寻址：指纹前 16 字节（02 §1 二元结构）
    PrincipalId owner;                    // 归属与该角色互动的主体（A5 裁决）
    PrincipalId persona;
    SessionId session{};                  // 源会话（M1-1 同流溯源；跨流 EventId 属 M2）
    std::vector<uint64_t> source_seqs;    // 溯源：折叠窗口内消息 seq（重建匹配键的一半）
    int card_version = 0;                 // 产生时的卡片"镜头"
    EntryKind kind = EntryKind::Summary;
    std::string content;
    double importance = 0.0;
    TimeRange valid{};                    // 冲突不覆盖，保留时效
    int generation = 0;                   // 归纳管线代数（重建按代整批重算）
};

// 审阅覆盖操作（02 §5.4 / 03 §6：accept|edit|merge|delete|pin_importance）
enum class ReviewOp : uint8_t { Accept, Edit, Merge, Delete, PinImportance };

// 一条覆盖记录（03 §6 事件形态 memory.reviewed 的 M1-1 内存驻留；身份流事件化随落库批）
struct Review {
    ReviewOp op{};
    PrincipalId by{};         // 必须 = 目标候选的 owner（A5：归属主体方可审阅）
    std::string content;      // Edit/Merge 的人工修订文本
    MemoryId merge_with{};    // Merge 的对端候选（合并后对端隐藏）
    double importance = 0.0;  // PinImportance 的钉定值
};

// 重建匹配键（03 §6）：sources + content 的规范哈希——归纳重跑产出逐字节同候选，
// 覆盖即可重新挂接（人工劳动永不丢失）。
ContentId fingerprint(const LongTermEntry& e);

// 生效条目（召回返回）：候选 × 覆盖 折叠后的投影；provenance（溯源 seq + 卡片版本）
// 随条目自带——前端"为什么记得"可展开（02 §5.3）。
struct Recalled {
    MemoryId id;
    PrincipalId owner, persona;
    SessionId session;
    std::vector<uint64_t> source_seqs;
    int card_version = 0;
    EntryKind kind = EntryKind::Summary;
    std::string content;       // 覆盖后生效文本
    double importance = 0.0;   // PinImportance 钉定后生效重要度
    bool reviewed = false;     // 是否存在人工覆盖
    int generation = 0;
};

// 只读快照：写端 publish 整册重制后原子换手（候选/覆盖均低频，M1-1 规模下最简一致形态）。
class View {
public:
    // 召回（02 §5.3）：scope=(persona, P) 且 P ∈ members 才出册——不在座主体的
    // 私密记忆不可被引用（隐私 + 相关性）；Deleted 不召回；valid 不覆盖 at_seq 不召回；
    // 打分 = 关键词重叠（bigram 粗筛，同 06 §2.2 词汇相关度）+ importance 加权，
    // 分数降序取 limit；零重叠且未钉定（pin）不入册——importance 只加权，不豁免
    // 相关性；pin 是唯一强制浮现通道（向量双召回 → MMR 去重 = usearch 接入后的换型点）
    void recall(const PrincipalId persona, std::span<const PrincipalId> members,
                std::string_view query, uint64_t at_seq, std::size_t limit,
                std::vector<Recalled>& out) const;

private:
    friend class Store;
    std::vector<LongTermEntry> entries_;                          // 候选全册（指纹序稳定）
    std::vector<std::pair<ContentId, Review>> overlays_;          // 覆盖册（含待挂：目标未生成）
};

// 长期记忆册（单写者）：归纳候选批写入 + 人工覆盖 + 原子快照发布。
class Store {
public:
    Store() = default;

    // 归纳候选批写入（06 §4：批化、滞后无害；幂等 = 同指纹候选去重）。
    // 返回实际新增数。
    std::size_t submit_candidates(std::span<const LongTermEntry> batch);

    // 人工覆盖（02 §5.4）：by 必须 = 目标候选 owner；目标不存在 → 记"待挂"
    // （重建协议：候选恢复后覆盖自动复效——人工劳动永不丢失）。
    Result<void> review(const ContentId& target, const Review& r);

    // 发布只读快照（整册重制 → 原子换手；写端单线程调用）。
    void publish();

    std::shared_ptr<const View> view() const noexcept { return view_.load(std::memory_order_acquire); }

    std::size_t candidate_count() const noexcept { return entries_.size(); }
    std::size_t overlay_count() const noexcept { return overlays_.size(); }

    // 重建协议（02 §5.4 / 03 §6）：归纳重跑的新候选 + 既有覆盖 → 新 Store。
    // 覆盖原册全量携带（匹配不上的保留待挂）。纯函数：不触碰 this。
    static std::unique_ptr<Store> rebuild(std::span<const LongTermEntry> candidates,
                                          std::span<const std::pair<ContentId, Review>> overlays);

private:
    std::vector<LongTermEntry> entries_;   // 指纹升序（稳定遍历 → 确定性召回）
    std::vector<std::pair<ContentId, Review>> overlays_;
    std::atomic<std::shared_ptr<const View>> view_;
};

}   // namespace oly::memory
