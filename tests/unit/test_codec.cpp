// 规范编码：往返 / 小端序 / 截断错误 / 解码器状态机
#include <oly/codec/codec.hpp>

#include <catch2/catch_test_macros.hpp>

using oly::codec::Decoder;
using oly::codec::Encoder;

TEST_CASE("整数编码为小端", "[codec]") {
    Encoder e;
    e.u16(0x0102);
    const auto bytes = std::move(e).take();
    REQUIRE(bytes.size() == 2);
    REQUIRE(bytes[0] == std::byte{0x02});
    REQUIRE(bytes[1] == std::byte{0x01});
}

TEST_CASE("全类型往返", "[codec]") {
    Encoder e;
    e.u8(0xAB);
    e.u64(0x0102030405060708ull);
    e.i64(-7);
    e.text("oly 世界引擎");                 // UTF-8 多字节
    const std::vector<std::byte> two{std::byte{0x11}, std::byte{0x22}};
    e.bytes(two);
    std::array<std::byte, 16> uid{};
    for (std::size_t i = 0; i < 16; ++i) uid[i] = std::byte{static_cast<uint8_t>(i)};
    e.ulid(uid);
    std::array<std::byte, 32> cid{};
    cid[31] = std::byte{0x7F};
    e.content(cid);
    const auto wire = std::move(e).take();

    Decoder d(wire);
    REQUIRE(d.u8() == 0xAB);
    REQUIRE(d.u64() == 0x0102030405060708ull);
    REQUIRE(d.i64() == -7);
    REQUIRE(d.text() == "oly 世界引擎");
    const auto b = d.bytes();
    REQUIRE(b.size() == 2);
    REQUIRE(b[1] == std::byte{0x22});
    REQUIRE(d.ulid() == uid);
    REQUIRE(d.content() == cid);
    REQUIRE(d.ok());
    REQUIRE(d.remaining() == 0);
}

TEST_CASE("截断置错并短路", "[codec]") {
    Encoder e;
    e.u32(4);
    const std::vector<std::byte> two{std::byte{1}, std::byte{2}};
    e.raw(two);   // 声称 4 字节实给 2
    const auto wire = std::move(e).take();

    Decoder d(wire);
    (void)d.bytes();
    REQUIRE_FALSE(d.ok());
    REQUIRE(d.error().code == "codec");
}

TEST_CASE("空字段往返", "[codec]") {
    Encoder e;
    e.text("");
    e.bytes({});
    const auto wire = std::move(e).take();
    Decoder d(wire);
    REQUIRE(d.text().empty());
    REQUIRE(d.bytes().empty());
    REQUIRE(d.ok());
}
