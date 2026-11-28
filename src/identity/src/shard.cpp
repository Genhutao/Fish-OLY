#include "oly/identity/shard.hpp"

#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <type_traits>
#include <utility>
#include <variant>

namespace oly::identity {

Result<std::unique_ptr<Shard>> Shard::create(const Deps deps) {
    // 堆式构造：协程帧捕获 this，对象地址必须稳定（禁止移动语义）
    std::unique_ptr<Shard> s(new Shard(deps, Channel(deps.io.get_executor(), deps.channel_capacity)));
    boost::asio::co_spawn(deps.io, s->run(), boost::asio::detached);
    return s;
}

Shard::Shard(const Deps deps, Channel&& ch)
    : io_(deps.io), inbox_(std::move(ch)) {}

Shard::~Shard() {
    // 生命周期契约：io 停止（pool.stop()）先于分片析构；泵协程捕获 this，
    // 通道随 io 一并消亡（asio channel 无同步 close——不做半关闭）。
}

bool Shard::post(Mutation m) {
    // 通道签名 = void(ec, Mutation)：消息显式携带 ec 槽位（本版 asio try_send 无计数参数）
    if (inbox_.try_send(boost::system::error_code{}, std::move(m))) return true;
    rejected_.fetch_add(1, std::memory_order_relaxed);
    return false;
}

void Shard::drain_once() {
    Mutation m;
    while (inbox_.try_receive([&](boost::system::error_code, Mutation v) {
        m = std::move(v);
        return true;
    })) {
        if (apply(m).has_value()) {
            applied_.fetch_add(1, std::memory_order_relaxed);
            publish();
        }
    }
}

boost::asio::awaitable<void> Shard::run() {
    for (;;) {
        Mutation m;
        try {
            m = co_await inbox_.async_receive(boost::asio::use_awaitable);
        } catch (const boost::system::system_error&) {
            co_return;   // 通道关闭 / io 停止
        }
        // 唤醒即批（01 §5）：收一件后非阻塞清空余量
        if (apply(m).has_value()) {
            applied_.fetch_add(1, std::memory_order_relaxed);
            publish();
        }
        Mutation next;
        while (inbox_.try_receive([&](boost::system::error_code, Mutation v) {
            next = std::move(v);
            return true;
        })) {
            if (apply(next).has_value()) {
                applied_.fetch_add(1, std::memory_order_relaxed);
                publish();
            }
        }
    }
}

Result<void> Shard::apply(const Mutation& m) {
    return std::visit(
        [this](const auto& cmd) -> Result<void> {
            using T = std::decay_t<decltype(cmd)>;
            if constexpr (std::is_same_v<T, AddPrincipalCmd>) {
                return registry_.add_principal(cmd.principal);
            } else if constexpr (std::is_same_v<T, AddPersonaCmd>) {
                return registry_.add_persona(cmd.id, cmd.owner, cmd.body);
            } else if constexpr (std::is_same_v<T, PublishCardCmd>) {
                const auto r = registry_.publish_card(cmd.persona, cmd.body);
                if (!r) return unexpected(r.error());
                return {};
            } else if constexpr (std::is_same_v<T, CreateSessionCmd>) {
                return registry_.create_session(cmd.session);
            } else if constexpr (std::is_same_v<T, AddMemberCmd>) {
                return registry_.add_member(cmd.session, cmd.who, cmd.role);
            } else if constexpr (std::is_same_v<T, SetRoleCmd>) {
                return registry_.set_role(cmd.session, cmd.who, cmd.role);
            }
        },
        m);
}

void Shard::publish() {
    const auto v = version_.load(std::memory_order_relaxed) + 1;
    snapshot_.store(std::make_shared<const domain::IdentitySnapshot>(
                        registry_.identity_snapshot(v)),
                    std::memory_order_release);
    version_.store(v, std::memory_order_release);
}

}   // namespace oly::identity
