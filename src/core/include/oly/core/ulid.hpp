// OLY · core —— ULID：48-bit 毫秒时间戳（大端）+ 80-bit 随机段，Crockford Base32（26 位）。
// 工厂按线程/loop 持有（01 §1 单写者纪律）；同毫秒内对随机段做 80-bit 递增，保证严格单调。
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <random>
#include <string>
#include <string_view>

namespace oly {

class Ulid {
public:
    using Bytes = std::array<std::byte, 16>;

    Ulid() = default;
    static Ulid from_bytes(Bytes b) {
        Ulid u;
        u.bytes_ = b;
        return u;
    }

    const Bytes& bytes() const noexcept { return bytes_; }
    uint64_t timestamp_ms() const noexcept;                 // 前 48-bit，大端
    std::string str() const;                                // 26 位 Crockford Base32
    static std::optional<Ulid> parse(std::string_view s);   // 接受小写，归一后解码

    friend bool operator==(const Ulid&, const Ulid&) = default;
    friend auto operator<=>(const Ulid&, const Ulid&) = default;

private:
    Bytes bytes_{};
};

// 单调 ULID 工厂。clock 可注入（黄金回放/单测用固定时钟）；seed 可注入（完全确定性）。
class UlidFactory {
public:
    using Clock = std::function<uint64_t()>;   // 毫秒时间戳

    UlidFactory();                             // 系统时钟 + 随机种子
    explicit UlidFactory(Clock clock);         // 注入时钟
    UlidFactory(Clock clock, std::array<std::byte, 10> seed);   // 完全确定性（测试）

    Ulid next();                               // 同毫秒内严格递增

private:
    std::mt19937_64 rng_;
    Clock clock_;
    uint64_t last_ms_ = 0;
    std::array<std::byte, 10> random_{};       // 80-bit 随机段（新毫秒重掷；同毫秒递增）
    bool primed_ = false;
};

std::string encode_ulid(const std::array<std::byte, 16>& bytes);
std::optional<std::array<std::byte, 16>> decode_ulid(std::string_view text);

}   // namespace oly
