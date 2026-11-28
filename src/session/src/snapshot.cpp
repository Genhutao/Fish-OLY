#include "oly/session/snapshot.hpp"

#include "oly/codec/codec.hpp"
#include "oly/core/hash.hpp"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

namespace oly::session {
namespace {

constexpr char kMagic[8] = {'O', 'L', 'Y', 'S', 'N', 'A', 'P', '1'};
constexpr uint16_t kFormatVer = 1;
// magic(8) + ver(2) + body_len(4) + hash(32)
constexpr std::size_t kFrameHeader = 8 + 2 + 4 + 32;

struct SnapCloser {
    void operator()(std::FILE* f) const noexcept {
        if (f != nullptr) std::fclose(f);
    }
};

std::filesystem::path snap_path(const std::filesystem::path& dir, const uint64_t seq) {
    char name[32];
    std::snprintf(name, sizeof(name), "snap-%016llu.bin", static_cast<unsigned long long>(seq));
    return dir / name;
}

uint64_t parse_snap_seq(const std::filesystem::path& p) {
    const auto stem = p.stem().string();   // "snap-0000000000000042"
    uint64_t v = 0;
    if (stem.size() == 21 && stem.starts_with("snap-")) {
        std::from_chars(stem.data() + 5, stem.data() + 21, v);
    }
    return v;
}

std::vector<std::byte> encode_body(const SnapshotData& s) {
    codec::Encoder e;
    e.u64(s.seq);
    e.u64(s.ticks);
    e.u32(static_cast<uint32_t>(s.messages.size()));
    for (const auto& [seq, text] : s.messages) {
        e.u64(seq);
        e.text(text);
    }
    return std::move(e).take();
}

}   // namespace

Result<ContentId> write_snapshot(const std::filesystem::path& dir, const SnapshotData& snap) {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) return unexpected(err("snapshot:io", ec.message()));

    const auto body = encode_body(snap);
    const auto hash = sha256(body);

    codec::Encoder head;
    head.raw({reinterpret_cast<const std::byte*>(kMagic), 8});
    head.u16(kFormatVer);
    head.u32(static_cast<uint32_t>(body.size()));
    head.content(hash.bytes);
    auto out = std::move(head).take();
    out.insert(out.end(), body.begin(), body.end());

    const auto path = snap_path(dir, snap.seq);
    const std::unique_ptr<std::FILE, SnapCloser> f(std::fopen(path.string().c_str(), "wb"));
    if (f == nullptr) return unexpected(err("snapshot:io", "cannot create " + path.string()));
    if (std::fwrite(out.data(), 1, out.size(), f.get()) != out.size()) {
        return unexpected(err("snapshot:io", "short write on snapshot"));
    }
    std::fflush(f.get());
    return hash;
}

Result<std::optional<SnapshotData>> load_latest_snapshot(const std::filesystem::path& dir) {
    std::vector<std::filesystem::path> files;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".bin" &&
            entry.path().stem().string().starts_with("snap-")) {
            files.push_back(entry.path());
        }
    }
    if (ec) return unexpected(err("snapshot:io", ec.message()));
    if (files.empty()) return std::optional<SnapshotData>{};   // 无快照：启动走全量重放

    // seq 降序取最新有效；损坏/半写跳过（异步拍允许，05 §5）
    std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) {
        return parse_snap_seq(a) > parse_snap_seq(b);
    });
    for (const auto& path : files) {
        const std::unique_ptr<std::FILE, SnapCloser> f(std::fopen(path.string().c_str(), "rb"));
        if (f == nullptr) continue;
        std::vector<std::byte> frame(kFrameHeader);
        if (std::fread(frame.data(), 1, frame.size(), f.get()) != frame.size()) continue;

        codec::Decoder d(frame);
        if (std::memcmp(d.raw(8).data(), kMagic, 8) != 0) continue;
        if (d.u16() != kFormatVer) continue;
        const uint32_t body_len = d.u32();
        const auto hash_bytes = d.content();
        std::vector<std::byte> body(body_len);
        if (std::fread(body.data(), 1, body.size(), f.get()) != body.size()) continue;
        if (sha256(body).bytes != hash_bytes) continue;   // 内容哈希锚不符 → 试更旧

        codec::Decoder bd(body);
        SnapshotData s;
        s.seq = bd.u64();
        s.ticks = bd.u64();
        const uint32_t count = bd.u32();
        s.messages.reserve(count);
        for (uint32_t i = 0; i < count; ++i) {
            const auto seq = bd.u64();
            auto text = bd.text();
            if (!bd.ok()) break;
            s.messages.emplace_back(seq, std::move(text));
        }
        if (!bd.ok() || s.messages.size() != count) continue;
        s.body_hash = ContentId::from_bytes(hash_bytes);
        return s;
    }
    return std::optional<SnapshotData>{};   // 全部损坏 → 视同无快照
}

}   // namespace oly::session
