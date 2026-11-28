// OLY · identity —— 身份分片执行器（01 §1 / 02）：主体与身份流的可变状态
// 只属于本分片的 loop（单写者）；与会话域之间只有两种交互形态：
//   ① 有界通道受理变更命令（move-only 语义、满即拒 = 背压）
//   ② 只读版本化快照（atomic shared_ptr 无锁发布/读取，01 §1 I3 例外）
// M0 单片（按 principal_id hash 的多片路由属 M1，随身份流事件落地）。
#pragma once

#include "oly/core/result.hpp"
#include "oly/domain/domain.hpp"

#include <boost/asio/awaitable.hpp>
#include <boost/asio/experimental/channel.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/system/system_error.hpp>

#include <atomic>
#include <cstddef>
#include <memory>
#include <string>
#include <variant>

namespace oly::identity {

// 变更命令（04 §2 身份域动作子集；M0 形态 = 结构化命令，M1 换身份流事实事件驱动）
struct AddPrincipalCmd {
    domain::Principal principal;
};
struct AddPersonaCmd {
    domain::PrincipalId id;
    domain::PrincipalId owner;
    domain::CardBody body;
};
struct PublishCardCmd {
    domain::PrincipalId persona;
    domain::CardBody body;
};
struct CreateSessionCmd {
    domain::Session session;
};
struct AddMemberCmd {
    domain::SessionId session;
    domain::PrincipalId who;
    domain::SessionRole role;
};
struct SetRoleCmd {
    domain::SessionId session;
    domain::PrincipalId who;
    domain::SessionRole role;
};

using Mutation = std::variant<AddPrincipalCmd, AddPersonaCmd, PublishCardCmd, CreateSessionCmd,
                              AddMemberCmd, SetRoleCmd>;

class Shard {
public:
    struct Deps {
        boost::asio::io_context& io;
        std::size_t channel_capacity = 4096;   // 有界：满即拒（01 §4 背压点）
    };

    // 在 deps.io 上 co_spawn 泵协程（须在 io 运行前入队或由其他线程触发均可）。
    // 堆式所有：协程帧捕获 this，禁止移动。
    static Result<std::unique_ptr<Shard>> create(Deps deps);
    ~Shard();
    Shard(const Shard&) = delete;
    Shard& operator=(const Shard&) = delete;

    // 跨线程受理变更：true = 已入队（最终一致地反映到快照）；false = 满（调用方限流重试）
    bool post(Mutation m);

    // 无锁读当前快照（C++20 atomic<shared_ptr>；快照不可变，读取方可长期持有）
    std::shared_ptr<const domain::IdentitySnapshot> snapshot() const noexcept {
        return snapshot_.load(std::memory_order_acquire);
    }
    uint64_t snapshot_version() const noexcept {
        return version_.load(std::memory_order_acquire);
    }
    uint64_t applied() const noexcept { return applied_.load(std::memory_order_relaxed); }
    uint64_t rejected() const noexcept { return rejected_.load(std::memory_order_relaxed); }

    // 泵（供测试直驱；生产由 co_spawn 驱动）：清空通道内全部待处理变更并发布
    void drain_once();

    boost::asio::io_context& io() const noexcept { return io_; }

private:
    using Channel = boost::asio::experimental::channel<void(boost::system::error_code, Mutation)>;

    Shard(Deps deps, Channel&& ch);

    boost::asio::awaitable<void> run();

    Result<void> apply(const Mutation& m);
    void publish();

    boost::asio::io_context& io_;
    Channel inbox_;
    domain::DomainRegistry registry_;
    std::atomic<std::shared_ptr<const domain::IdentitySnapshot>> snapshot_{};   // 空初版
    std::atomic<uint64_t> version_{0};
    std::atomic<uint64_t> applied_{0};
    std::atomic<uint64_t> rejected_{0};
};

}   // namespace oly::identity
