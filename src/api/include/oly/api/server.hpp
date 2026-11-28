// OLY · api —— 服务器装配（10）：REST（命令与查询）+ WS（事件流）的唯一入口。
// M0 最小面（13 §1"API 最小集"）：
//   REST: auth/tokens · me · personas 查询/创建 · sessions 查询 · messages 读写 ·
//         read 游标推进 · commands 状态（幂等键同源）
//   WS:   /v1/stream —— subscribe(since 重放→sync) / event 实时 / signal 瞬态信令 /
//         ping-pong。slow_consumer 断开 + last_seq（01 §4 背压）。
// 已知 M0 限制（M1 换型）：会话创建走引导装配（POST /v1/sessions 未开放——
// 需身份流驱动的结构化注册表变更）；WS 心跳定时器、订阅鉴权 ACL 静态登记。
#pragma once

#include "oly/api/auth.hpp"
#include "oly/domain/domain.hpp"
#include "oly/exec/loop_pool.hpp"
#include "oly/identity/shard.hpp"
#include "oly/session/shard.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace oly::api {

struct ServerConfig {
    std::string bind = "127.0.0.1";
    uint16_t port = 0;   // 0 = 临时端口（测试）
    std::filesystem::path store_root = "data/seg";
    std::filesystem::path snapshot_root = "data/snap";
    std::size_t session_loops = 2;
    uint64_t tick_period_ms = 100;
    std::size_t ws_outbound_capacity = 256;   // 每订阅者有界缓冲（01 §4 背压）
};

class Server {
public:
    // 堆式装配（io 线程/锁成员不可移动；协程与 sink 捕获要求地址稳定）
    static Result<std::unique_ptr<Server>> create(ServerConfig cfg = {});
    ~Server();

    // ── 引导期（start 前调用；单线程）─────────────────────────────
    domain::DomainRegistry& registry() noexcept { return registry_; }
    Auth& auth() noexcept { return *auth_; }
    identity::Shard& identity_shard() noexcept { return *identity_; }
    session::Host& bootstrap_host();   // 装配多 loop 会话宿主（幂等）
    void allow(domain::SessionId sid, domain::PrincipalId who);   // 订阅 ACL 登记

    // ── 运行期 ─────────────────────────────────────────────────────
    Result<uint16_t> start();   // 绑定监听，返回实际端口（临时端口场景）
    void stop() noexcept;

    const ServerConfig& config() const noexcept { return cfg_; }

    struct Impl;   // http/ws 状态（public 仅为自由函数具名；成员全私有）
    std::unique_ptr<Impl> impl_;

private:
    Server() = default;

    ServerConfig cfg_;
    std::unique_ptr<exec::LoopPool> session_pool_;
    std::unique_ptr<exec::LoopPool> identity_pool_;
    domain::DomainRegistry registry_;
    std::unique_ptr<identity::Shard> identity_;
    std::optional<session::Host> host_;
    std::unique_ptr<Auth> auth_;
};

}   // namespace oly::api
