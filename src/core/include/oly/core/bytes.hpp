// OLY · core —— 字节串/十六进制小工具（调试、ID 展示、测试断言）。
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace oly {

std::string to_hex(std::span<const std::byte> data);

std::optional<std::vector<std::byte>> from_hex(std::string_view text);

template <std::size_t N>
std::string to_hex(const std::array<std::byte, N>& data) {
    return to_hex(std::span<const std::byte>(data.data(), data.size()));
}

}   // namespace oly
