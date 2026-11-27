// OLY · core —— 哈希原语：SHA-256（事件哈希链 + 内容寻址）与 CRC-32C（段批次校验）。
// 当前为独立实现；sha256 的接口即换型点（libcrypto/EVP，05 §10），调用方无感。
#pragma once

#include "oly/core/id.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace oly {

ContentId sha256(std::span<const std::byte> data);

// 链式哈希：hash = SHA-256(prev ‖ canonical)。事件流哈希链（04 §3）。
ContentId chain_hash(const ContentId& prev, std::span<const std::byte> canonical);

// CRC-32C（Castagnoli）：段文件批次完整性（05 §2）。crc 为前段续算值。
uint32_t crc32c(std::span<const std::byte> data, uint32_t crc = 0);

}   // namespace oly
