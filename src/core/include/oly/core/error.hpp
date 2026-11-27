// OLY · core —— 错误值类型：code 机器可读（错误包 10 §2 的服务端前身），message 人类可读。
#pragma once

#include <string>
#include <utility>

namespace oly {

struct Error {
    std::string code;
    std::string message;
};

inline Error err(std::string code, std::string message = {}) {
    return Error{std::move(code), std::move(message)};
}

}   // namespace oly
