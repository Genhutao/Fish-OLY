// OLY · exec —— 会话路由表（01 §1/§9）：session → loop 分片映射。
// 这是进程白名单中唯一允许的锁（shared_mutex）：仅世界创建/迁移时触碰，读多写少。
// 迁移协议本体（快照+重放）在会话运行时（05 §6）；此处只维护映射的原子切换。
#pragma once

#include <cstdint>
#include <optional>
#include <shared_mutex>
#include <stddef.h>
#include <unordered_map>

namespace oly::exec {

class SessionRouter {
public:
    // 绑定（会话创建时）；重复绑定返回 false
    bool bind(uint64_t session_key, std::size_t loop_index);

    // 查询（会话 loop 之外的路由场景；热路径走各 loop 本地缓存，此处为强读）
    std::optional<std::size_t> loop_of(uint64_t session_key) const;

    // 迁移：原子改绑，返回旧 loop（无绑定 → nullopt）
    std::optional<std::size_t> rebind(uint64_t session_key, std::size_t new_loop);

    // 解绑（会话归档/墓碑）；返回原 loop（无绑定 → nullopt）
    std::optional<std::size_t> unbind(uint64_t session_key);

    std::size_t size() const;

private:
    mutable std::shared_mutex mu_;
    std::unordered_map<uint64_t, std::size_t> table_;
};

}   // namespace oly::exec
