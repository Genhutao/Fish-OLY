#include "oly/logdb/store.hpp"

#include "oly/core/hash.hpp"

#include <algorithm>
#include <charconv>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace oly::logdb {
namespace {

constexpr char kMagic[8] = {'O', 'L', 'Y', 'S', 'E', 'G', '1', '\0'};
constexpr uint16_t kFormatVer = 1;
constexpr std::size_t kHeaderSize = 8 + 2 + 1 + 16 + 8 + 4;   // magic+ver+kind+id+start+crc

void sync_file(std::FILE* f) {
    std::fflush(f);
#ifdef _WIN32
    (void)_commit(_fileno(f));
#else
    (void)::fsync(::fileno(f));
#endif
}

std::filesystem::path seg_path(const std::filesystem::path& dir, const uint32_t index) {
    char name[32];   // "seg-" + 8 位序号 + ".log" + NUL；缓冲必须完整容纳（曾因 16 字节截断成 .lo）
    std::snprintf(name, sizeof(name), "seg-%08u.log", index);
    return dir / name;
}

uint32_t read_file_index(const std::filesystem::path& p) {
    const auto stem = p.stem().string();   // "seg-00000042"
    uint32_t v = 0;
    if (stem.size() == 12 && stem.starts_with("seg-")) {
        std::from_chars(stem.data() + 4, stem.data() + 12, v);
    }
    return v;
}

struct SegCloser {
    void operator()(std::FILE* f) const noexcept {
        if (f != nullptr) std::fclose(f);
    }
};

struct CheckedSeg {
    std::FILE* f = nullptr;
    uint64_t start_seq = 0;
};

// 打开段文件并校验段头（magic / 版本 / 流清单 / CRC），回填段起始 seq。
// 成功时 f 非空、所有权移交调用方；open() 与 read_range() 共用（05 §2 段头契约唯一）。
Result<CheckedSeg> open_segment_checked(const std::filesystem::path& path,
                                        const events::StreamRef& stream) {
    CheckedSeg out;
    std::unique_ptr<std::FILE, SegCloser> f(std::fopen(path.string().c_str(), "rb"));
    if (f == nullptr) return unexpected(err("logdb:io", "cannot open segment: " + path.string()));

    std::vector<std::byte> hbuf(kHeaderSize);
    if (std::fread(hbuf.data(), 1, hbuf.size(), f.get()) != hbuf.size()) {
        return unexpected(err("logdb:corrupt", "truncated segment header"));
    }
    codec::Decoder hd(hbuf);
    if (std::memcmp(hd.raw(8).data(), kMagic, 8) != 0) {
        return unexpected(err("logdb:corrupt", "bad segment magic: " + path.string()));
    }
    if (hd.u16() != kFormatVer) return unexpected(err("logdb:corrupt", "unsupported format version"));
    if (static_cast<events::StreamKind>(hd.u8()) != stream.kind) {
        return unexpected(err("logdb:corrupt", "segment kind mismatch"));
    }
    if (hd.ulid() != stream.id) return unexpected(err("logdb:corrupt", "segment stream mismatch"));
    out.start_seq = hd.u64();
    const uint32_t stored_crc = hd.u32();
    codec::Encoder hb;
    hb.raw(std::span<const std::byte>(hbuf).first(kHeaderSize - 4));
    if (crc32c(hb.span()) != stored_crc) return unexpected(err("logdb:corrupt", "header crc mismatch"));

    out.f = f.release();
    return out;
}

std::vector<std::filesystem::path> list_segments(const std::filesystem::path& dir) {
    std::vector<std::filesystem::path> segs;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".log") segs.push_back(entry.path());
    }
    if (ec) segs.clear();
    std::sort(segs.begin(), segs.end(), [](const auto& a, const auto& b) {
        return read_file_index(a) < read_file_index(b);
    });
    return segs;
}

struct ChainHead {
    uint64_t start_seq = 0;
    uint64_t last_seq = 0;
    ContentId prev_hash;
    uint32_t last_index = 0;
};

// 前置声明：批次读取定义在本文件后部（open() 的续链扫描先于此使用）
using BatchRecords = std::optional<std::vector<std::vector<std::byte>>>;
Result<BatchRecords> read_batch_records(std::FILE* f);

// 扫描既有段重建链头（05 §6 启动路径）：全链校验，任何损坏即拒——
// 写端不跨损坏续写（残批丢弃/截尾修复属恢复矩阵，交由运维显式决策）。
Result<std::optional<ChainHead>> scan_existing(const std::filesystem::path& dir,
                                               const events::StreamRef& stream) {
    const auto segs = list_segments(dir);
    if (segs.empty()) return std::optional<ChainHead>{};   // 全新流

    ChainHead h;
    ContentId prev = events::genesis_prev_hash;
    uint64_t expect = 0;
    bool first = true;
    for (const auto& path : segs) {
        auto seg = open_segment_checked(path, stream);
        if (!seg) return unexpected(seg.error());
        std::unique_ptr<std::FILE, SegCloser> f(seg.value().f);
        if (first) {
            h.start_seq = seg.value().start_seq;
            expect = h.start_seq;
            first = false;
        } else if (seg.value().start_seq != expect) {
            return unexpected(err("logdb:corrupt", "segment start discontinuity at " + path.string()));
        }
        while (true) {
            auto batch = read_batch_records(f.get());
            if (!batch) return unexpected(batch.error());
            if (!batch.value().has_value()) break;   // 干净 EOF，进下一段
            for (const auto& wire : batch.value().value()) {
                auto env = events::decode_envelope(wire);
                if (!env) return unexpected(env.error());
                if (env.value().seq != expect || env.value().prev_hash != prev) {
                    return unexpected(err("logdb:corrupt", "chain/seq discontinuity at seq " +
                                                                 std::to_string(expect)));
                }
                prev = env.value().hash;
                h.last_seq = expect++;
            }
        }
    }
    h.prev_hash = prev;
    h.last_index = read_file_index(segs.back());
    return h;
}

codec::Encoder header_bytes(const events::StreamRef& stream, const uint64_t seg_start_seq) {
    codec::Encoder e;
    e.raw({reinterpret_cast<const std::byte*>(kMagic), 8});
    e.u16(kFormatVer);
    e.u8(static_cast<uint8_t>(stream.kind));
    e.ulid(stream.id);
    e.u64(seg_start_seq);
    return e;
}

// 读一个批次的记录字节（含长度前缀的原始区域），返回按条拆分后的 wire 列表。
// 干净 EOF → 空 optional；任何截断/损坏 → 错误（05 §6：残批整体丢弃语义）。
using BatchRecords = std::optional<std::vector<std::vector<std::byte>>>;
Result<BatchRecords> read_batch_records(std::FILE* f) {
    uint8_t fixed[12];   // count(u32) + first_seq(u64)
    const std::size_t n = std::fread(fixed, 1, sizeof(fixed), f);
    if (n == 0) return BatchRecords{};
    if (n != sizeof(fixed)) return unexpected(err("logdb:corrupt", "truncated batch header"));

    codec::Decoder head({reinterpret_cast<const std::byte*>(fixed), sizeof(fixed)});
    const uint32_t count = head.u32();
    (void)head.u64();   // first_seq：读端以流级连续性校验为准

    std::vector<std::byte> region;   // 记录原始区域（长度前缀 + envelope），CRC 对象
    region.reserve(count * 96 + 16);
    for (uint32_t i = 0; i < count; ++i) {
        uint8_t lenbuf[4];
        if (std::fread(lenbuf, 1, 4, f) != 4) return unexpected(err("logdb:corrupt", "truncated record length"));
        codec::Decoder ld({reinterpret_cast<const std::byte*>(lenbuf), 4});
        const uint32_t len = ld.u32();
        const auto at = region.size();
        region.resize(at + 4 + len);
        std::memcpy(region.data() + at, lenbuf, 4);
        if (len != 0 && std::fread(region.data() + at + 4, 1, len, f) != len) {
            return unexpected(err("logdb:corrupt", "truncated record body"));
        }
    }

    uint8_t crcbuf[4];
    if (std::fread(crcbuf, 1, 4, f) != 4) return unexpected(err("logdb:corrupt", "truncated crc"));
    codec::Decoder cd({reinterpret_cast<const std::byte*>(crcbuf), 4});
    if (crc32c(region) != cd.u32()) return unexpected(err("logdb:corrupt", "batch crc mismatch"));

    codec::Decoder rd(region);
    std::vector<std::vector<std::byte>> records;
    records.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t len = rd.u32();
        const auto wire = rd.raw(len);
        if (!rd.ok()) return unexpected(err("logdb:corrupt", "record framing broken"));
        records.emplace_back(wire.begin(), wire.end());
    }
    return records;
}

}   // namespace

void SegmentStore::FileCloser::operator()(std::FILE* f) const noexcept {
    if (f != nullptr) std::fclose(f);
}

Result<SegmentStore> SegmentStore::open(const std::filesystem::path& dir,
                                        const events::StreamRef stream,
                                        const uint64_t start_seq,
                                        const StoreOptions options) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) return unexpected(err("logdb:io", ec.message()));

    SegmentStore s;
    s.dir_ = dir;
    s.stream_ = stream;
    s.opts_ = options;

    // 既有段 → 续链（05 §6：启动重放的前提；调用方传 start_seq 必须与流创世一致）
    auto scan = scan_existing(dir, stream);
    if (!scan) return unexpected(scan.error());
    if (scan.value().has_value()) {
        const auto& h = scan.value().value();
        if (start_seq != h.start_seq) {
            return unexpected(err("logdb:seq-mismatch",
                                  "stream exists with start_seq " + std::to_string(h.start_seq) +
                                      ", caller passed " + std::to_string(start_seq)));
        }
        s.start_seq_ = h.start_seq;
        s.head_seq_ = h.last_seq + 1;
        s.prev_hash_ = h.prev_hash;
        s.committed_ = h.last_seq;
        s.cur_index_ = h.last_index;   // 后续滚动从下一序号起
    } else {
        s.start_seq_ = start_seq;
        s.head_seq_ = start_seq;
    }
    return s;
}

Result<void> SegmentStore::roll_segment(const uint64_t seg_start_seq) {
    cur_.reset();
    ++cur_index_;
    const auto path = seg_path(dir_, cur_index_);
    cur_.reset(std::fopen(path.string().c_str(), "wb"));
    if (cur_ == nullptr) return unexpected(err("logdb:io", "cannot create segment: " + path.string()));

    auto head = header_bytes(stream_, seg_start_seq);
    const auto body = std::move(head).take();
    codec::Encoder full;
    full.raw(body);
    full.u32(crc32c(body));
    const auto bytes = std::move(full).take();
    if (std::fwrite(bytes.data(), 1, bytes.size(), cur_.get()) != bytes.size()) {
        return unexpected(err("logdb:io", "short write on segment header"));
    }
    cur_bytes_ = bytes.size();
    return {};
}

Result<void> SegmentStore::append(const std::span<events::EventEnvelope> batch) {
    if (batch.empty()) return {};

    // 1) 只读校验：seq 连续（04 §3 无空洞；seq==0 表示由存储分配）
    uint64_t expected = head_seq_;
    for (const auto& env : batch) {
        const uint64_t s = (env.seq == 0) ? expected : env.seq;
        if (s != expected) {
            return unexpected(err("logdb:seq-mismatch",
                                  "expected seq " + std::to_string(expected) + ", got " +
                                      std::to_string(s)));
        }
        ++expected;
    }

    // 2) 批次尺寸预估 → 必要时滚动（滚动失败不触碰任何链状态，写路径保持原子）
    std::uintmax_t batch_bytes = 12;   // count(u32) + first_seq(u64)
    for (const auto& env : batch) batch_bytes += 4 + events::canonical_bytes(env).size() + 32;
    if (cur_ == nullptr) {
        if (auto r = roll_segment(head_seq_); !r) return r;
    } else if (cur_bytes_ + batch_bytes > opts_.max_segment_bytes) {
        if (auto r = roll_segment(head_seq_); !r) return r;
    }

    // 3) 编码 + 接链（链由本存储属主管理：prev 取内部游标，写前 seal——05 §6）
    codec::Encoder recs;
    uint64_t seq = head_seq_;
    for (auto& env : batch) {
        env.seq = seq++;
        env.prev_hash = prev_hash_;
        seal(env);
        const auto wire = encode_envelope(env);
        recs.u32(static_cast<uint32_t>(wire.size()));
        recs.raw(wire);
        prev_hash_ = env.hash;
    }
    const auto records = std::move(recs).take();

    codec::Encoder frame;
    frame.u32(static_cast<uint32_t>(batch.size()));
    frame.u64(head_seq_);
    frame.raw(records);
    frame.u32(crc32c(records));
    const auto out = std::move(frame).take();

    // 4) 落盘提交：成功返回即持久（先持久后可见，05 §3）
    if (std::fwrite(out.data(), 1, out.size(), cur_.get()) != out.size()) {
        return unexpected(err("logdb:io", "short write on batch"));
    }
    std::fflush(cur_.get());
    if (opts_.fsync_on_commit) sync_file(cur_.get());
    cur_bytes_ += out.size();

    head_seq_ = seq;
    committed_ = seq - 1;
    return {};
}

Result<std::vector<events::EventEnvelope>> SegmentStore::read_range(const uint64_t from_seq,
                                                                    const std::size_t limit) const {
    // 从段首走全链：创世 → 逐条 prev/seq 校验（读端完整性优先于读取速度）
    ContentId prev = events::genesis_prev_hash;
    uint64_t expect_seq = start_seq_;
    std::vector<events::EventEnvelope> out;
    out.reserve(std::min(limit, std::size_t{64}));

    for (const auto& path : list_segments(dir_)) {
        auto seg = open_segment_checked(path, stream_);
        if (!seg) return unexpected(seg.error());
        const std::unique_ptr<std::FILE, FileCloser> f(seg.value().f);

        // 批次循环
        while (out.size() < limit) {
            auto batch = read_batch_records(f.get());
            if (!batch) return unexpected(batch.error());
            if (!batch.value().has_value()) break;   // 干净 EOF，进下一段

            for (const auto& wire : batch.value().value()) {
                auto env = events::decode_envelope(wire);
                if (!env) return unexpected(env.error());
                if (env.value().seq != expect_seq || env.value().prev_hash != prev) {
                    return unexpected(err("logdb:corrupt", "chain/seq discontinuity at seq " +
                                                             std::to_string(expect_seq)));
                }
                prev = env.value().hash;
                ++expect_seq;
                if (env.value().seq >= from_seq) {
                    out.push_back(std::move(env.value()));
                    if (out.size() >= limit) return out;
                }
            }
        }
    }
    return out;
}

}   // namespace oly::logdb
