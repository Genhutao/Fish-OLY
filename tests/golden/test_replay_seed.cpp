// 黄金回放框架种子（13 §2）：写 → 读 → 确定性折叠断言。
// 这是"重放 + 断言"评测闸门的雏形：同一种子时钟 + 确定性工厂 → 事件流逐字节可复现。
#include <oly/core/bytes.hpp>
#include <oly/core/id.hpp>
#include <oly/events/envelope.hpp>
#include <oly/logdb/store.hpp>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>

namespace {

struct FoldResult {
    uint64_t count = 0;
    uint64_t sum = 0;      // payload 前 8 字节按小端 u64 折叠
    std::string last_hash; // 事件链尾哈希（hex）
};

FoldResult fold(const std::vector<oly::events::EventEnvelope>& events) {
    FoldResult f;
    for (const auto& e : events) {
        ++f.count;
        if (e.payload_bin.size() >= 8) {
            uint64_t v = 0;
            for (std::size_t i = 0; i < 8; ++i) {
                v |= static_cast<uint64_t>(std::to_integer<uint8_t>(e.payload_bin[i])) << (8 * i);
            }
            f.sum += v;
        }
        f.last_hash = oly::to_hex(e.hash.bytes);
    }
    return f;
}

}   // namespace

TEST_CASE("黄金回放种子：写入 → 读回 → 折叠确定", "[golden]") {
    // 完全确定性：固定时钟 + 固定种子
    std::array<std::byte, 10> seed{};
    for (std::size_t i = 0; i < seed.size(); ++i) seed[i] = std::byte{static_cast<uint8_t>(0xA0 + i)};
    oly::UlidFactory ulids([] { return 77'000'000ull; }, seed);

    const auto dir = std::filesystem::temp_directory_path() /
                     ("oly-golden-" + ulids.next().str());
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);

    oly::events::StreamRef stream;
    stream.kind = oly::events::StreamKind::Session;
    stream.id = oly::SessionId::from_string("01ARZ3NDEKTSV4RRFFQ69G5FAV").value().bytes();

    auto store = oly::logdb::SegmentStore::open(dir, stream, 0);
    REQUIRE(store.has_value());

    // 事件 1..9，payload = i*13；分三批（3/3/3）
    uint64_t expect_sum = 0;
    for (int batchNo = 0; batchNo < 3; ++batchNo) {
        std::vector<oly::events::EventEnvelope> batch;
        for (int i = 0; i < 3; ++i) {
            const uint64_t n = static_cast<uint64_t>(batchNo * 3 + i + 1);
            oly::events::EventEnvelope e;
            e.type = "test.tick";
            e.tick = n;                        // 虚拟时钟位（会话流）
            e.command = oly::CommandId::from_bytes(ulids.next().bytes());
            oly::codec::Encoder p;
            p.u64(n * 13);
            e.payload_bin = std::move(p).take();
            expect_sum += n * 13;
            batch.push_back(std::move(e));
        }
        REQUIRE(store->append(batch).has_value());
    }

    // 重放一：读回并折叠
    auto replay = store->read_range(0, 1000);
    REQUIRE(replay.has_value());
    REQUIRE(replay->size() == 9);
    const FoldResult f1 = fold(*replay);
    REQUIRE(f1.count == 9);
    REQUIRE(f1.sum == expect_sum);

    // 重放二：再次读回折叠——逐字段一致（确定性）
    auto replay2 = store->read_range(0, 1000);
    const FoldResult f2 = fold(*replay2);
    REQUIRE(f1.count == f2.count);
    REQUIRE(f1.sum == f2.sum);
    REQUIRE(f1.last_hash == f2.last_hash);

    // 从中点开始的续读：仅事件 5..9，折叠和 = (5+6+7+8+9)×13 = 455
    auto half = store->read_range(5, 1000);
    REQUIRE(half.has_value());
    REQUIRE(half->size() == 4);
    REQUIRE(fold(*half).sum == 390);   // (6+7+8+9)×13
    std::filesystem::remove_all(dir, ec);
}
