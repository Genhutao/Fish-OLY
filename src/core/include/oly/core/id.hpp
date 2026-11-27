// OLY · core —— 强类型 ID（02 §1）：实体用 ULID（时间前缀有序），内容用 SHA-256。
// 标签类型防串用：PrincipalId 与 SessionId 不可能被编译器互换。
#pragma once

#include "oly/core/ulid.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace oly {

template <typename Tag>
class UlidId {
public:
    UlidId() = default;
    static UlidId from_bytes(Ulid::Bytes b) { return UlidId{b}; }
    static std::optional<UlidId> from_string(std::string_view s) {
        if (auto raw = decode_ulid(s)) return from_bytes(*raw);
        return std::nullopt;
    }

    const Ulid::Bytes& bytes() const noexcept { return bytes_; }
    std::string str() const { return encode_ulid(bytes_); }
    bool is_nil() const noexcept { return bytes_ == Ulid::Bytes{}; }

    friend bool operator==(const UlidId&, const UlidId&) = default;
    friend auto operator<=>(const UlidId&, const UlidId&) = default;

private:
    explicit UlidId(Ulid::Bytes b) : bytes_(b) {}
    Ulid::Bytes bytes_{};
};

struct TagPrincipal {};
struct TagSession {};
struct TagEvent {};
struct TagCommand {};
struct TagMemory {};

using PrincipalId = UlidId<TagPrincipal>;
using SessionId   = UlidId<TagSession>;
using EventId     = UlidId<TagEvent>;
using CommandId   = UlidId<TagCommand>;
using MemoryId    = UlidId<TagMemory>;

// 内容标识：不可变 blob 的 SHA-256（02 §1 内容寻址）。哈希链值同型（04 §3）。
struct ContentId {
    std::array<std::byte, 32> bytes{};

    static ContentId from_bytes(std::array<std::byte, 32> b) {
        ContentId c;
        c.bytes = b;
        return c;
    }

    bool is_nil() const noexcept { return bytes == std::array<std::byte, 32>{}; }
    std::string str() const;   // 64 位小写 hex

    friend bool operator==(const ContentId&, const ContentId&) = default;
    friend auto operator<=>(const ContentId&, const ContentId&) = default;
};

}   // namespace oly

template <typename Tag>
struct std::hash<oly::UlidId<Tag>> {
    std::size_t operator()(const oly::UlidId<Tag>& id) const noexcept {
        const auto& b = id.bytes();
        std::size_t h = 1469598103934665603ull;   // FNV-1a
        for (auto byte : b) {
            h ^= std::to_integer<std::size_t>(byte);
            h *= 1099511628211ull;
        }
        return h;
    }
};

template <>
struct std::hash<oly::ContentId> {
    std::size_t operator()(const oly::ContentId& id) const noexcept {
        std::size_t h = 1469598103934665603ull;
        for (auto byte : id.bytes) {
            h ^= std::to_integer<std::size_t>(byte);
            h *= 1099511628211ull;
        }
        return h;
    }
};
