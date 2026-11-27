// 段存储：写读往返 / 完成游标 / 滚动 / CRC 与哈希链损坏检测
#include <oly/core/id.hpp>
#include <oly/events/envelope.hpp>
#include <oly/logdb/store.hpp>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

namespace {

oly::events::StreamRef make_stream() {
    oly::events::StreamRef s;
    s.kind = oly::events::StreamKind::Session;
    s.id = oly::SessionId::from_string("01ARZ3NDEKTSV4RRFFQ69G5FAV").value().bytes();
    return s;
}

oly::events::EventEnvelope make_event(oly::UlidFactory& f, const std::string& type, const uint64_t v) {
    oly::events::EventEnvelope e;
    e.type = type;
    e.command = oly::CommandId::from_bytes(f.next().bytes());
    oly::codec::Encoder p;
    p.u64(v);
    e.payload_bin = std::move(p).take();
    return e;
}

struct TempDir {
    std::filesystem::path path;
    explicit TempDir(const std::string& tag)
        : path(std::filesystem::temp_directory_path() /
               ("oly-logdb-" + tag + "-" + oly::UlidFactory{}.next().str())) {}
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

}   // namespace

TEST_CASE("写读往返 + 完成游标 + 哈希链全校验", "[logdb]") {
    const TempDir dir("basic");
    uint64_t clock = 1000;
    oly::UlidFactory f([&] { return clock++; });

    auto store = oly::logdb::SegmentStore::open(dir.path, make_stream(), 0);
    REQUIRE(store.has_value());

    oly::events::StreamRef st = make_stream();
    std::vector<oly::events::EventEnvelope> batch;
    for (uint64_t i = 1; i <= 12; ++i) {
        batch.push_back(make_event(f, "message.appended", i));
    }
    REQUIRE(store->append(batch).has_value());
    REQUIRE(store->committed_seq() == std::optional<uint64_t>{11});

    auto back = store->read_range(0, 100);
    REQUIRE(back.has_value());
    REQUIRE(back->size() == 12);
    for (std::size_t i = 0; i < back->size(); ++i) {
        REQUIRE((*back)[i].seq == i);   // seq 从 0 起（0 号留给 created 事件）
        REQUIRE((*back)[i].type == "message.appended");
        REQUIRE((*back)[i].payload_bin == batch[i].payload_bin);
        REQUIRE((*back)[i].command == batch[i].command);
        REQUIRE((*back)[i].hash == batch[i].hash);
    }

    // 部分区间
    auto tail = store->read_range(10, 2);
    REQUIRE(tail.has_value());
    REQUIRE(tail->size() == 2);
    REQUIRE((*tail)[0].seq == 10);
}

TEST_CASE("重复 seq 被拒绝（无空洞纪律）", "[logdb]") {
    const TempDir dir("seqgap");
    oly::UlidFactory f([] { return 7; });

    auto store = oly::logdb::SegmentStore::open(dir.path, make_stream(), 0);
    REQUIRE(store.has_value());

    std::vector<oly::events::EventEnvelope> first;
    first.push_back(make_event(f, "a", 1));
    REQUIRE(store->append(first).has_value());

    std::vector<oly::events::EventEnvelope> dup;
    dup.push_back(make_event(f, "b", 2));
    dup[0].seq = 5;   // 流头期望 1，显式错序 → 拒绝
    auto r = store->append(dup);
    REQUIRE_FALSE(r.has_value());
    REQUIRE(r.error().code == "logdb:seq-mismatch");
}

TEST_CASE("按大小滚动为多段，读取仍一致", "[logdb]") {
    const TempDir dir("roll");
    oly::UlidFactory f([] { return 55; });

    oly::logdb::StoreOptions opts;
    opts.max_segment_bytes = 256;   // 极小值强制滚动
    auto store = oly::logdb::SegmentStore::open(dir.path, make_stream(), 0, opts);
    REQUIRE(store.has_value());

    // 按真实节奏提交：每拍一批（5 条 × 4 拍），批次尺寸超过阈值触发段滚动
    std::vector<oly::events::EventEnvelope> all;
    for (int b = 0; b < 4; ++b) {
        std::vector<oly::events::EventEnvelope> batch;
        for (int i = 0; i < 5; ++i) batch.push_back(make_event(f, "tick", b * 5 + i + 1));
        REQUIRE(store->append(batch).has_value());   // 先提交（store 接链并 seal）
        all.insert(all.end(), batch.begin(), batch.end());   // 再留档（含 seal 后哈希）
    }

    std::size_t segs = 0;
    for (const auto& entry : std::filesystem::directory_iterator(dir.path)) {
        if (entry.path().extension() == ".log") ++segs;
    }
    REQUIRE(segs >= 2);

    auto back = store->read_range(0, 1000);
    REQUIRE(back.has_value());
    REQUIRE(back->size() == 20);
    REQUIRE((*back)[19].hash == all[19].hash);
}

TEST_CASE("字节损坏在读端被发现", "[logdb]") {
    const TempDir dir("corrupt");
    oly::UlidFactory f([] { return 9; });

    auto store = oly::logdb::SegmentStore::open(dir.path, make_stream(), 0);
    REQUIRE(store.has_value());
    std::vector<oly::events::EventEnvelope> batch;
    for (uint64_t i = 1; i <= 4; ++i) batch.push_back(make_event(f, "m", i));
    REQUIRE(store->append(batch).has_value());

    // 定位段文件并翻转记录区中的一个字节
    std::filesystem::path seg;
    for (const auto& entry : std::filesystem::directory_iterator(dir.path)) {
        if (entry.path().extension() == ".log") seg = entry.path();
    }
    {
        std::fstream io(seg, std::ios::in | std::ios::out | std::ios::binary);
        REQUIRE(io.is_open());
        const auto size = std::filesystem::file_size(seg);
        io.seekg(static_cast<std::streamoff>(size - 10));   // 打进记录区尾部
        char c = 0;
        io.read(&c, 1);
        io.seekg(static_cast<std::streamoff>(size - 10));
        io.put(static_cast<char>(static_cast<uint8_t>(c) ^ 0xFFu));
        io.flush();
    }

    auto back = store->read_range(0, 100);
    REQUIRE_FALSE(back.has_value());
    REQUIRE(back.error().code == "logdb:corrupt");
}
