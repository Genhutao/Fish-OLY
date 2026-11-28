// OLY · api —— 鉴权（10 §1/§4.1）：Bearer token → 用户主体绑定。
// M0 形态 = 静态 token 表（进程内注册；服务端引导期签发）；OAuth/密码外接随 M1。
// token 表属基础设施读模型（互斥锁保护；命中路径非领域热路径）。
#pragma once

#include "oly/core/id.hpp"

#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace oly::api {

// "Authorization: Bearer <token>" 提取
inline std::optional<std::string_view> bearer_of(std::string_view header) {
    constexpr std::string_view kScheme = "Bearer ";
    if (header.size() <= kScheme.size() || header.starts_with(kScheme) == false) {
        return std::nullopt;
    }
    return header.substr(kScheme.size());
}

class Auth {
public:
    void register_token(std::string token, PrincipalId principal) {
        const std::scoped_lock lk(mu_);
        tokens_.emplace(std::move(token), principal);
    }

    std::optional<PrincipalId> resolve(std::string_view token) const {
        const std::scoped_lock lk(mu_);
        const auto it = tokens_.find(std::string{token});
        return it == tokens_.end() ? std::nullopt : std::optional{it->second};
    }

private:
    mutable std::mutex mu_;
    std::unordered_map<std::string, PrincipalId> tokens_;
};

}   // namespace oly::api
