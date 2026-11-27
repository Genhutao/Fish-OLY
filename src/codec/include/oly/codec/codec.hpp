// OLY · codec —— 规范二进制编码（05 §2）：定宽整数一律小端，变长一律 u32 前缀。
// 事件哈希链只对本编码的规范字节计算；跨版本稳定性由段头的 format 版本号守护。
#pragma once

#include "oly/core/error.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace oly::codec {

class Encoder {
public:
    void u8(uint8_t v);
    void u16(uint16_t v);
    void u32(uint32_t v);
    void u64(uint64_t v);
    void i64(int64_t v);                             // 补码直写（规范定义）
    void raw(std::span<const std::byte> data);       // 不带长度前缀
    void bytes(std::span<const std::byte> data);     // u32 长度 + 内容
    void text(std::string_view s);                   // u32 字节数 + UTF-8
    void ulid(const std::array<std::byte, 16>& v);   // 16 字节定宽
    void content(const std::array<std::byte, 32>& v);// 32 字节定宽

    std::span<const std::byte> span() const noexcept { return buf_; }
    std::vector<std::byte> take() && { return std::move(buf_); }
    std::size_t size() const noexcept { return buf_.size(); }

private:
    std::vector<std::byte> buf_;
};

// 状态机式解码：任何一次越界/损坏置错一次，后续读取短路返回默认值；调用方以 ok() 收口。
class Decoder {
public:
    explicit Decoder(std::span<const std::byte> in) : in_(in) {}

    bool ok() const noexcept { return ok_; }
    const Error& error() const noexcept { return err_; }

    uint8_t u8();
    uint16_t u16();
    uint32_t u32();
    uint64_t u64();
    int64_t i64();
    std::vector<std::byte> bytes();
    std::string text();
    std::array<std::byte, 16> ulid();
    std::array<std::byte, 32> content();
    std::span<const std::byte> raw(std::size_t n);   // 定长原始字节（无前缀）

    std::size_t remaining() const noexcept { return in_.size() - pos_; }

private:
    std::span<const std::byte> take(std::size_t n);   // 成功返回子串并推进；失败置错返回空
    void fail(std::string message);

    std::span<const std::byte> in_;
    std::size_t pos_ = 0;
    bool ok_ = true;
    Error err_{};
};

}   // namespace oly::codec
