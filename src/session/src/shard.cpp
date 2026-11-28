#include "oly/session/shard.hpp"

#include "oly/exec/beat_pump.hpp"
#include "oly/log/log.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>

#include <utility>

namespace oly::session {

Result<std::unique_ptr<Shard>> Shard::create(const Deps deps) {
    // 堆式构造：拍泵协程捕获 this，地址必须稳定
    std::unique_ptr<Shard> s(
        new Shard(deps, Channel(deps.io.get_executor(), deps.inbox_capacity)));
    boost::asio::co_spawn(deps.io,
                          exec::beat_loop(std::chrono::milliseconds(deps.tick_period_ms),
                                          [raw = s.get()](const uint64_t now) { raw->on_beat(now); }),
                          boost::asio::detached);
    return s;
}

Shard::Shard(const Deps deps, Channel&& ch)
    : io_(deps.io),
      wheel_(),
      registry_(&deps.registry),
      identity_(std::move(deps.identity)),
      period_ms_(deps.tick_period_ms),
      store_root_(deps.store_root),
      snapshot_root_(deps.snapshot_root),
      inbox_(std::move(ch)),
      sink_(std::move(deps.sink)) {}

Shard::~Shard() {
    // 生命周期契约：pool.stop() 先于分片析构；拍泵协程捕获 this，通道随 io 一并消亡
}

Result<SessionRuntime*> Shard::create_session(const domain::SessionId id, const uint64_t now_ms) {
    const uint64_t k = SessionRuntime::key(id);
    if (sessions_.contains(k)) return unexpected(err("session:exists", "already on this shard"));

    auto store = logdb::SegmentStore::open(
        store_root_ / id.str(), events::StreamRef{events::StreamKind::Session, id.bytes()}, 0);
    if (!store) return unexpected(store.error());
    auto owned = std::make_unique<logdb::SegmentStore>(std::move(store.value()));

    SessionRuntime::Deps d{*registry_, *owned, wheel_, period_ms_, snapshot_root_ / id.str(),
                           identity_};
    if (sink_) {   // 空 sink 不挂钩子：on_commit 在 append 与 clear 之间调用，
                   // 空包装器会抛 bad_function_call → 批滞留 → seq 永久失配
        d.on_commit = [sink = sink_](const std::vector<events::EventEnvelope>& facts) {
            for (const auto& e : facts) sink(e);
        };
    }
    auto rt = SessionRuntime::create(id, d, now_ms);
    if (!rt) return unexpected(rt.error());

    auto up = std::make_unique<SessionRuntime>(std::move(rt.value()));
    SessionRuntime* raw = up.get();
    sessions_.emplace(k, std::move(up));
    stores_.emplace(k, std::move(owned));
    return raw;
}

Result<SessionRuntime*> Shard::recover_session(const domain::SessionId id, const uint64_t now_ms) {
    const uint64_t k = SessionRuntime::key(id);
    if (sessions_.contains(k)) return unexpected(err("session:exists", "already on this shard"));

    auto store = logdb::SegmentStore::open(
        store_root_ / id.str(), events::StreamRef{events::StreamKind::Session, id.bytes()}, 0);
    if (!store) return unexpected(store.error());
    auto owned = std::make_unique<logdb::SegmentStore>(std::move(store.value()));

    SessionRuntime::Deps d{*registry_, *owned, wheel_, period_ms_, snapshot_root_ / id.str(),
                           identity_};
    if (sink_) {   // 空 sink 不挂钩子：on_commit 在 append 与 clear 之间调用，
                   // 空包装器会抛 bad_function_call → 批滞留 → seq 永久失配
        d.on_commit = [sink = sink_](const std::vector<events::EventEnvelope>& facts) {
            for (const auto& e : facts) sink(e);
        };
    }
    auto rt = SessionRuntime::recover(id, d, now_ms);
    if (!rt) return unexpected(rt.error());

    auto up = std::make_unique<SessionRuntime>(std::move(rt.value()));
    SessionRuntime* raw = up.get();
    sessions_.emplace(k, std::move(up));
    stores_.emplace(k, std::move(owned));
    return raw;
}

bool Shard::post(const domain::SessionId to, Command cmd, const bool background) {
    // 线程安全受理（有界，满即拒 = 背压 01 §4）；通道签名 void(ec, Delivery)：
    // 消息显式携带 ec 槽位（本版 asio try_send 无计数参数）
    return inbox_.try_send(boost::system::error_code{},
                           Delivery{to, std::move(cmd), background});
}

void Shard::on_beat(const uint64_t now_ms) {
    const auto t0 = std::chrono::steady_clock::now();

    pump_inbox();   // 先收交互输入（绝对优先于拍驱动，01 §3）

    for (const auto due : wheel_.advance(now_ms)) {
        due_seen_.fetch_add(1, std::memory_order_relaxed);
        const auto it = sessions_.find(due);
        if (it == sessions_.end()) continue;
        (void)it->second->on_tick(now_ms);   // 槽已由分片统一摘取（避免二次消费）
        auto dr = it->second->drain(64);     // 唤醒即批（01 §5）
        if (!dr) {
            OLY_LOG_ERROR("shard drain failed: {} / {} (reg_head={})", dr.error().code,
                          dr.error().message, it->second->session().head_seq);
        }
    }
    // 入站命令不必等轮式拍：有滞留即排空（交互延迟 ≤ 1 拍的兜底）
    for (auto& [key, rt] : sessions_) {
        if (rt->pending() > 0) (void)rt->drain(64);
    }

    beat_ms_.push_back(std::chrono::duration<double, std::milli>(
                           std::chrono::steady_clock::now() - t0)
                           .count());
}

SessionRuntime* Shard::session(const domain::SessionId id) noexcept {
    const auto it = sessions_.find(SessionRuntime::key(id));
    return it == sessions_.end() ? nullptr : it->second.get();
}

void Shard::pump_inbox() {
    Delivery d;
    while (inbox_.try_receive([&](boost::system::error_code, Delivery v) {
        d = std::move(v);
        return true;
    })) {
        const auto it = sessions_.find(SessionRuntime::key(d.sid));
        if (it == sessions_.end()) continue;   // 路由竞态（迁移中）：M1 迁移协议处理
        const auto r = d.background ? it->second->enqueue_background(std::move(d.cmd))
                                    : it->second->enqueue(std::move(d.cmd));
        (void)r;
    }
}

// ── Host ─────────────────────────────────────────────────────────────

Result<Host> Host::create(const Deps deps) {
    Host h;
    h.pool_ = &deps.pool;
    h.period_ms_ = deps.tick_period_ms;
    for (std::size_t i = 0; i < deps.pool.size(); ++i) {
        Shard::Deps d{deps.pool.at(i), deps.registry, deps.identity, deps.tick_period_ms,
                      deps.store_root, deps.snapshot_root, 4096, deps.sink};
        auto sh = Shard::create(d);
        if (!sh) return unexpected(sh.error());
        h.shards_.push_back(std::move(sh.value()));
    }
    return h;
}

Result<SessionRuntime*> Host::create_session(const domain::SessionId id, const uint64_t now_ms) {
    return shard_for(id).create_session(id, now_ms);
}

Result<SessionRuntime*> Host::recover_session(const domain::SessionId id, const uint64_t now_ms) {
    return shard_for(id).recover_session(id, now_ms);
}

bool Host::post(const domain::SessionId to, Command cmd, const bool background) {
    return shard_for(to).post(to, std::move(cmd), background);
}

void Host::start() { pool_->start(); }

void Host::stop() noexcept { pool_->stop(); }

SessionRuntime* Host::session(const domain::SessionId id) noexcept {
    return shard_for(id).session(id);
}

}   // namespace oly::session
