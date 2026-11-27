// SHA-256 标准向量 / CRC-32C 标准向量 / 链式哈希
#include <oly/core/hash.hpp>

#include <catch2/catch_test_macros.hpp>

#include <oly/core/bytes.hpp>

#include <string_view>

namespace {

oly::ContentId sha256_of(std::string_view s) {
    const auto* p = reinterpret_cast<const std::byte*>(s.data());
    return oly::sha256({p, s.size()});
}

uint32_t crc32c_of(std::string_view s) {
    const auto* p = reinterpret_cast<const std::byte*>(s.data());
    return oly::crc32c({p, s.size()});
}

uint32_t crc32c_of(std::string_view s, uint32_t seed) {
    const auto* p = reinterpret_cast<const std::byte*>(s.data());
    return oly::crc32c({p, s.size()}, seed);
}

}   // namespace

TEST_CASE("SHA-256 已知向量", "[core][hash]") {
    // FIPS 180-4 向量
    REQUIRE(oly::to_hex(sha256_of("").bytes) ==
            "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    REQUIRE(oly::to_hex(sha256_of("abc").bytes) ==
            "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    // 多块（56 字节，跨两个处理块）向量——以系统 SHA256 独立计算的真值为准
    REQUIRE(oly::to_hex(sha256_of("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq").bytes) ==
            "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST_CASE("CRC-32C 已知向量", "[core][hash]") {
    REQUIRE(crc32c_of("123456789") == 0xE3069283u);
    REQUIRE(crc32c_of("") == 0x0u);
    // 续算一致性：分两段 == 整段
    const std::string_view s = "oly world engine";
    const uint32_t whole = crc32c_of(s);
    const uint32_t part = crc32c_of(s.substr(5), crc32c_of(s.substr(0, 5)));
    REQUIRE(part == whole);
}

TEST_CASE("链式哈希：prev 参与且确定性", "[core][hash]") {
    const auto body = oly::from_hex("00ff10").value();
    std::array<std::byte, 32> p1{};
    std::array<std::byte, 32> p2{};
    p2[0] = std::byte{0x01};
    const auto c1 = oly::ContentId::from_bytes(p1);
    const auto c2 = oly::ContentId::from_bytes(p2);

    const auto h1a = oly::chain_hash(c1, body);
    const auto h1b = oly::chain_hash(c1, body);
    const auto h2 = oly::chain_hash(c2, body);

    REQUIRE(h1a == h1b);
    REQUIRE(h1a != h2);
}
