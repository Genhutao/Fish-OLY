#include "oly/core/ulid.hpp"

#include <chrono>
#include <cstring>

namespace oly {
namespace {

constexpr char kCrockford[] = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

// 130-bit 视角下第 k 位（自最高位起，0 基）：ULID 字符串 = 26×5 = 130 bits，高 2 位恒 0。
uint32_t padded_bit(const std::array<std::byte, 16>& b, uint32_t k) {
    const int32_t p = static_cast<int32_t>(k) - 2;   // 130-bit 高位的 2 bit 填充
    if (p < 0) return 0;
    const auto& byte = b[static_cast<std::size_t>(15 - p / 8)];
    return (std::to_integer<uint32_t>(byte) >> (p % 8)) & 1u;
}

void set_padded_bit(std::array<std::byte, 16>& b, uint32_t k, uint32_t v) {
    const int32_t p = static_cast<int32_t>(k) - 2;
    if (p < 0) return;
    auto& byte = b[static_cast<std::size_t>(15 - p / 8)];
    const uint32_t mask = uint32_t{1} << (p % 8);
    const uint32_t cur = std::to_integer<uint32_t>(byte);
    byte = std::byte{static_cast<uint8_t>((cur & ~mask) | (v ? mask : 0u))};
}

void put_be_u48(std::array<std::byte, 16>& out, uint64_t ms) {
    for (int i = 0; i < 6; ++i) out[static_cast<std::size_t>(5 - i)] = std::byte{static_cast<uint8_t>((ms >> (8 * i)) & 0xFFu)};
}

}   // namespace

uint64_t Ulid::timestamp_ms() const noexcept {
    uint64_t ms = 0;
    for (int i = 0; i < 6; ++i) {
        ms = (ms << 8) | std::to_integer<uint64_t>(bytes_[static_cast<std::size_t>(i)]);
    }
    return ms;
}

std::string Ulid::str() const { return encode_ulid(bytes_); }

std::optional<Ulid> Ulid::parse(std::string_view s) {
    auto raw = decode_ulid(s);
    if (!raw) return std::nullopt;
    return from_bytes(*raw);
}

std::string encode_ulid(const std::array<std::byte, 16>& bytes) {
    std::string out(26, '0');
    for (uint32_t d = 0; d < 26; ++d) {
        const uint32_t base = 5 * d;
        const uint32_t v = (padded_bit(bytes, base) << 4) | (padded_bit(bytes, base + 1) << 3) |
                           (padded_bit(bytes, base + 2) << 2) | (padded_bit(bytes, base + 3) << 1) |
                           padded_bit(bytes, base + 4);
        out[d] = kCrockford[v];
    }
    return out;
}

std::optional<std::array<std::byte, 16>> decode_ulid(std::string_view text) {
    if (text.size() != 26) return std::nullopt;
    std::array<std::byte, 16> out{};
    for (std::size_t d = 0; d < 26; ++d) {
        char c = text[d];
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
        const char* pos = std::strchr(kCrockford, c);
        if (pos == nullptr || c == '\0') return std::nullopt;   // 含 I/L/O/U 及非法字符
        const uint32_t v = static_cast<uint32_t>(pos - kCrockford);
        const uint32_t base = static_cast<uint32_t>(5 * d);
        for (uint32_t bit = 0; bit < 5; ++bit) {
            set_padded_bit(out, base + bit, (v >> (4 - bit)) & 1u);
        }
    }
    return out;
}

UlidFactory::UlidFactory() : UlidFactory([] {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}) {}

UlidFactory::UlidFactory(Clock clock) : clock_(std::move(clock)) {
    std::random_device rd;
    std::seed_seq seed{rd(), rd(), rd(), rd()};
    rng_.seed(seed);
}

UlidFactory::UlidFactory(Clock clock, std::array<std::byte, 10> seed) : clock_(std::move(clock)) {
    random_ = seed;
    uint64_t s = 0;
    for (int i = 0; i < 8; ++i) s = (s << 8) | std::to_integer<uint64_t>(seed[static_cast<std::size_t>(i)]);
    rng_.seed(s);
    primed_ = true;   // 首拍直接用 seed 段并递增，保证与 seed 可辨识
    last_ms_ = clock_();
}

Ulid UlidFactory::next() {
    const uint64_t now = clock_();
    if (!primed_ || now != last_ms_) {
        last_ms_ = now;
        primed_ = true;
        const uint64_t a = rng_();
        const uint64_t b = rng_();
        for (int i = 0; i < 8; ++i) random_[static_cast<std::size_t>(i)] = std::byte{static_cast<uint8_t>((a >> (8 * (7 - i))) & 0xFFu)};
        for (int i = 0; i < 2; ++i) random_[static_cast<std::size_t>(8 + i)] = std::byte{static_cast<uint8_t>((b >> (8 * (1 - i))) & 0xFFu)};
    } else {
        for (int i = 9; i >= 0; --i) {   // 同毫秒：80-bit 大端递增（溢出概率工程上为零）
            auto& byte = random_[static_cast<std::size_t>(i)];
            const uint32_t v = std::to_integer<uint32_t>(byte) + 1u;
            byte = std::byte{static_cast<uint8_t>(v & 0xFFu)};
            if ((v & 0x100u) == 0) break;
        }
    }

    Ulid::Bytes out{};
    put_be_u48(out, last_ms_);
    for (std::size_t i = 0; i < 10; ++i) out[6 + i] = random_[i];
    return Ulid::from_bytes(out);
}

}   // namespace oly
