// OLY · logdb —— 段存储（05 §2–3）：每流一个目录、段内批次组帧、CRC 校验、按大小滚动。
// 纪律：先持久后可见——append() 返回成功即该批已持久，committed_seq() 为完成游标。
// 段头 = [magic "OLYSEG1"][u16 format][u8 kind][16B stream id][u64 start_seq][u32 header crc]
// 批次 = [u32 count][u64 first_seq][records…（u32 len + envelope）][u32 crc(records)]
// 哈希链跨记录、跨段、跨批次连续；本实现为链的唯一属主（写端 seal，读端全链校验）。
#pragma once

#include "oly/core/result.hpp"
#include "oly/events/envelope.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace oly::logdb {

struct StoreOptions {
    std::uintmax_t max_segment_bytes = 128ull << 20;   // 128MB/段（05 §2）
    bool fsync_on_commit = false;                      // 组提交窗口由上层 writer 协程控制
};

class SegmentStore {
public:
    // 打开（当前为全新打开；断线续写经清单重建是 M0-2 事项）
    static Result<SegmentStore> open(const std::filesystem::path& dir,
                                     events::StreamRef stream,
                                     uint64_t start_seq,
                                     StoreOptions options = {});
    SegmentStore(SegmentStore&&) noexcept = default;
    SegmentStore& operator=(SegmentStore&&) noexcept = default;
    ~SegmentStore() = default;
    SegmentStore(const SegmentStore&) = delete;
    SegmentStore& operator=(const SegmentStore&) = delete;

    // 追加一批：分配/校验 seq → 逐条接链 seal → 组帧 + CRC → 落盘（可选 fsync）。
    // envelope.seq == 0 时由存储分配；否则必须等于预期头 seq。
    Result<void> append(std::span<events::EventEnvelope> batch);

    // 完成游标（05 §3：由它驱动发布与应答）。空 = 尚无已提交记录。
    std::optional<uint64_t> committed_seq() const noexcept { return committed_; }

    // 从 from_seq（含）读取至多 limit 条。从段首全量走链校验（O(全流)，骨架期够用）。
    Result<std::vector<events::EventEnvelope>> read_range(uint64_t from_seq, std::size_t limit) const;

private:
    SegmentStore() = default;

    Result<void> roll_segment(uint64_t seg_start_seq);
    Result<std::vector<events::EventEnvelope>> scan_from_start(uint64_t from_seq, std::size_t limit) const;

    struct FileCloser {
        void operator()(std::FILE* f) const noexcept;
    };

    std::filesystem::path dir_;
    events::StreamRef stream_{};
    StoreOptions opts_{};
    uint64_t start_seq_ = 0;
    uint64_t head_seq_ = 0;                  // 下一条待写 seq
    std::optional<uint64_t> committed_;
    ContentId prev_hash_;                    // 链续接（初始 = genesis）
    std::unique_ptr<std::FILE, FileCloser> cur_;   // 当前写段
    std::uintmax_t cur_bytes_ = 0;
    uint32_t cur_index_ = 0;
};

}   // namespace oly::logdb
