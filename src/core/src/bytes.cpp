#include "oly/core/bytes.hpp"

namespace oly {
namespace {

constexpr char kDigits[] = "0123456789abcdef";

uint32_t hex_value(char c) {
    if (c >= '0' && c <= '9') return static_cast<uint32_t>(c - '0');
    if (c >= 'a' && c <= 'f') return static_cast<uint32_t>(c - 'a' + 10);
    if (c >= 'A' && c <= 'F') return static_cast<uint32_t>(c - 'A' + 10);
    return 0xFFFF'FFFFu;
}

}   // namespace

std::string to_hex(std::span<const std::byte> data) {
    std::string out;
    out.reserve(data.size() * 2);
    for (auto byte : data) {
        const uint32_t v = std::to_integer<uint32_t>(byte);
        out.push_back(kDigits[v >> 4]);
        out.push_back(kDigits[v & 0xF]);
    }
    return out;
}

std::optional<std::vector<std::byte>> from_hex(std::string_view text) {
    if (text.size() % 2 != 0) return std::nullopt;
    std::vector<std::byte> out;
    out.reserve(text.size() / 2);
    for (std::size_t i = 0; i < text.size(); i += 2) {
        const uint32_t hi = hex_value(text[i]);
        const uint32_t lo = hex_value(text[i + 1]);
        if (hi == 0xFFFF'FFFFu || lo == 0xFFFF'FFFFu) return std::nullopt;
        out.push_back(std::byte{static_cast<uint8_t>((hi << 4) | lo)});
    }
    return out;
}

}   // namespace oly
