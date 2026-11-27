#include "oly/codec/codec.hpp"

#include <algorithm>

namespace oly::codec {
namespace {

template <typename T>
void put_le(std::vector<std::byte>& buf, T v) {
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        buf.push_back(std::byte{static_cast<uint8_t>((v >> (8 * i)) & 0xFFu)});
    }
}

template <typename T>
T get_le(std::span<const std::byte> in, std::size_t pos) {
    T v{};
    for (std::size_t i = 0; i < sizeof(T); ++i) {
        v |= static_cast<T>(std::to_integer<uint64_t>(in[pos + i])) << (8 * i);
    }
    return v;
}

}   // namespace

void Encoder::u8(uint8_t v) { buf_.push_back(std::byte{v}); }

void Encoder::u16(uint16_t v) { put_le(buf_, v); }
void Encoder::u32(uint32_t v) { put_le(buf_, v); }
void Encoder::u64(uint64_t v) { put_le(buf_, v); }
void Encoder::i64(int64_t v) { put_le(buf_, v); }

void Encoder::raw(const std::span<const std::byte> data) {
    buf_.insert(buf_.end(), data.begin(), data.end());
}

void Encoder::bytes(const std::span<const std::byte> data) {
    u32(static_cast<uint32_t>(data.size()));
    raw(data);
}

void Encoder::text(const std::string_view s) {
    u32(static_cast<uint32_t>(s.size()));
    raw(std::as_bytes(std::span{s.data(), s.size()}));
}

void Encoder::ulid(const std::array<std::byte, 16>& v) { raw(v); }
void Encoder::content(const std::array<std::byte, 32>& v) { raw(v); }

std::span<const std::byte> Decoder::take(const std::size_t n) {
    if (!ok_) return {};
    if (n > in_.size() - pos_) {
        fail("codec: truncated");
        return {};
    }
    const auto sub = in_.subspan(pos_, n);
    pos_ += n;
    return sub;
}

void Decoder::fail(std::string message) {
    if (!ok_) return;
    ok_ = false;
    err_ = err("codec", std::move(message));
}

uint8_t Decoder::u8() {
    const auto s = take(1);
    return s.empty() ? 0u : std::to_integer<uint8_t>(s[0]);
}

uint16_t Decoder::u16() {
    const auto s = take(2);
    return s.empty() ? 0u : get_le<uint16_t>(s, 0);
}

uint32_t Decoder::u32() {
    const auto s = take(4);
    return s.empty() ? 0u : get_le<uint32_t>(s, 0);
}

uint64_t Decoder::u64() {
    const auto s = take(8);
    return s.empty() ? 0u : get_le<uint64_t>(s, 0);
}

int64_t Decoder::i64() { return static_cast<int64_t>(u64()); }

std::vector<std::byte> Decoder::bytes() {
    const uint32_t n = u32();
    const auto s = take(n);
    if (s.empty() && n != 0) return {};
    return {s.begin(), s.end()};
}

std::string Decoder::text() {
    const uint32_t n = u32();
    const auto s = take(n);
    if (s.empty() && n != 0) return {};
    return {reinterpret_cast<const char*>(s.data()), s.size()};
}

std::array<std::byte, 16> Decoder::ulid() {
    const auto s = take(16);
    std::array<std::byte, 16> out{};
    if (s.empty()) return out;
    std::copy(s.begin(), s.end(), out.begin());
    return out;
}

std::array<std::byte, 32> Decoder::content() {
    const auto s = take(32);
    std::array<std::byte, 32> out{};
    if (s.empty()) return out;
    std::copy(s.begin(), s.end(), out.begin());
    return out;
}

std::span<const std::byte> Decoder::raw(const std::size_t n) { return take(n); }

}   // namespace oly::codec
