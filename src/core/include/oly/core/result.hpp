// OLY · core —— Result<T>：编译器具备 C++23 expected 时直通 std::expected，
// 否则落到本文件的最小兜底实现。调用方 API 一致（04/05 的库路径零异常纪律）。
#pragma once

#include "oly/core/error.hpp"

#include <cassert>
#include <optional>
#include <type_traits>
#include <utility>

#if defined(__cpp_lib_expected) && __cpp_lib_expected >= 202202L
#include <expected>

namespace oly {

template <typename T, typename E = Error>
using Result = std::expected<T, E>;

template <typename E>
auto unexpected(E&& e) {
    return std::unexpected(std::forward<E>(e));
}

}   // namespace oly
#else

namespace oly {

// 兜底 unexpected 包装：Result<T, E> 可从其隐式构造（对齐 std::expected 语义）。
template <typename E>
class Unexpected {
public:
    explicit Unexpected(E e) : e_(std::move(e)) {}
    const E& value() const noexcept { return e_; }

private:
    E e_;
};

template <typename E>
auto unexpected(E e) {
    return Unexpected<std::decay_t<E>>(std::move(e));
}

template <typename T, typename E = Error>
class [[nodiscard]] Result {
public:
    Result(T value) : value_(std::move(value)) {}
    Result(Unexpected<E> u) : error_(std::move(u.value())) {}

    // 转换构造（对齐 std::expected）：允许 T 的可转换源（如 vector → optional）一跳完成。
    // 必须排除 nullopt_t：optional<optional<T>>(nullopt_t) 的重载决议会命中外层的空构造，
    // 产生既非值也非错误的退化对象（result.hpp 语义缺陷，2026-11-28 修复）。
    template <typename U,
              typename = std::enable_if_t<std::is_convertible_v<U &&, T> &&
                                          !std::is_same_v<std::decay_t<U>, Result> &&
                                          !std::is_same_v<std::decay_t<U>, Unexpected<E>> &&
                                          !std::is_same_v<std::decay_t<U>, std::nullopt_t>>>
    Result(U &&value) : value_(std::forward<U>(value)) {}

    explicit operator bool() const noexcept { return value_.has_value(); }
    bool has_value() const noexcept { return value_.has_value(); }

    T* operator->() {
        assert(value_.has_value());
        return &*value_;
    }
    const T* operator->() const {
        assert(value_.has_value());
        return &*value_;
    }
    T& operator*() & {
        assert(value_.has_value());
        return *value_;
    }
    const T& operator*() const & {
        assert(value_.has_value());
        return *value_;
    }
    T&& operator*() && {
        assert(value_.has_value());
        return std::move(*value_);
    }

    T& value() & {
        assert(value_.has_value());
        return *value_;
    }
    const T& value() const & {
        assert(value_.has_value());
        return *value_;
    }
    T&& take() && {
        assert(value_.has_value());
        return std::move(*value_);
    }

    E& error() & {
        assert(!value_.has_value());
        return *error_;
    }
    const E& error() const & {
        assert(!value_.has_value());
        return *error_;
    }

private:
    std::optional<T> value_;
    std::optional<E> error_;
};

template <typename E>
class [[nodiscard]] Result<void, E> {
public:
    Result() = default;
    Result(Unexpected<E> u) : error_(std::move(u.value())) {}

    explicit operator bool() const noexcept { return !error_.has_value(); }
    bool has_value() const noexcept { return !error_.has_value(); }
    void value() const noexcept {}

    E& error() & {
        assert(error_.has_value());
        return *error_;
    }
    const E& error() const & {
        assert(error_.has_value());
        return *error_;
    }

private:
    std::optional<E> error_;
};

}   // namespace oly
#endif
