// OLY · session —— 会话分片与宿主（01 §1 / 13 §4 步骤 3）：多 loop 同进程托管。
//   Shard  = 一组会话流 + 一个绑定 loop（单写者域）：轮式调度、两车道队列、
//            感知缓冲、段存储全部归属本 loop；跨 loop 只经有界通道（受理）
//            与只读身份快照（权限）。
//   Host   = 分片表 + 每分片拍泵协程（exec::beat_loop）+ 会话→分片路由。
// M0 路由 = 会话键 % 分片数（确定性零锁）；迁移协议（01 §1 会话迁移）随 M1。
#pragma once

#include "oly/domain/domain.hpp"
#include "oly/exec/loop_pool.hpp"
#include "oly/exec/tick_wheel.hpp"
#include "oly/logdb/store.hpp"
#include "oly/session/session_runtime.hpp"

#include <boost/asio/experimental/channel.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/system/system_error.hpp>

#include <chrono>
#include <filesystem>
#include <memory>
#include <unordered_map>
#include <vector>

namespace oly::session {

class Shard {
public:
    struct Deps {
        boost::asio::io_context& io;
        domain::DomainRegistry& registry;                            // 引导装配 + 会话投影宿主
        std::shared_ptr<const domain::IdentitySnapshot> identity;    // 身份快照（无锁权限源）
        uint64_t tick_period_ms = 1000;
        std::filesystem::path store_root;                            // 段目录 = root/<sid>
        std::filesystem::path snapshot_root;                         // 快照目录 = root/<sid>
        std::size_t inbox_capacity = 4096;                           // 跨 loop 通道界（01 §4）
        // 提交外送（10 §5）：每分片一份，透传至会话运行时 on_commit
        std::function<void(const events::EventEnvelope&)> sink;
    };

    // 堆式构造（拍泵协程捕获 this）；泵协程在此入队，随池 start 生效。
    static Result<std::unique_ptr<Shard>> create(Deps deps);
    ~Shard();
    Shard(const Shard&) = delete;
    Shard& operator=(const Shard&) = delete;

    // 引导期（loop 未运行时）装配：段存储 + 运行时（created 事实）+ 轮式登记
    Result<SessionRuntime*> create_session(domain::SessionId id, uint64_t now_ms);

    // 引导期恢复（05 §6）：快照 + 补重放；会话 canon/成员须已在注册表
    Result<SessionRuntime*> recover_session(domain::SessionId id, uint64_t now_ms);

    // 跨 loop 受理（线程安全，满即拒 = 背压）：会话必须归属本分片
    bool post(domain::SessionId to, Command cmd, bool background = false);

    // 拍（beat_loop 驱动，loop 线程）：入站转投 → 轮式到期 advance+drain → 滞留排空
    void on_beat(uint64_t now_ms);

    // 观测：session() 仅限 loop 内或池停止后使用
    SessionRuntime* session(domain::SessionId id) noexcept;
    std::size_t load() const noexcept { return sessions_.size(); }
    const std::vector<double>& beat_ms() const noexcept { return beat_ms_; }   // 每拍耗时(ms)
    std::size_t wheel_size() const noexcept { return wheel_.size(); }
    uint64_t due_seen() const noexcept { return due_seen_.load(std::memory_order_relaxed); }

private:
    struct Delivery {
        domain::SessionId sid;
        Command cmd;
        bool background = false;
    };
    using Channel = boost::asio::experimental::channel<void(boost::system::error_code, Delivery)>;

    Shard(Deps deps, Channel&& ch);

    void pump_inbox();

    boost::asio::io_context& io_;
    exec::TickWheel wheel_;
    domain::DomainRegistry* registry_;
    std::shared_ptr<const domain::IdentitySnapshot> identity_;
    uint64_t period_ms_ = 1000;
    std::filesystem::path store_root_, snapshot_root_;
    std::unordered_map<uint64_t, std::unique_ptr<logdb::SegmentStore>> stores_;
    std::unordered_map<uint64_t, std::unique_ptr<SessionRuntime>> sessions_;
    Channel inbox_;
    std::vector<double> beat_ms_;
    std::atomic<uint64_t> due_seen_{0};   // 诊断：累计摘到的到期键数
    std::function<void(const events::EventEnvelope&)> sink_;
};

// ── 宿主：N loop × N 分片 ─────────────────────────────────────────────
class Host {
public:
    struct Deps {
        exec::LoopPool& pool;
        domain::DomainRegistry& registry;
        std::shared_ptr<const domain::IdentitySnapshot> identity;
        uint64_t tick_period_ms = 1000;
        std::filesystem::path store_root;
        std::filesystem::path snapshot_root;
        std::function<void(const events::EventEnvelope&)> sink;   // 提交外送（10 §5）
    };

    static Result<Host> create(Deps deps);   // 分片数 = 池 loop 数

    // 引导期（start 前）：注册表须已含会话领域态
    Result<SessionRuntime*> create_session(domain::SessionId id, uint64_t now_ms);
    Result<SessionRuntime*> recover_session(domain::SessionId id, uint64_t now_ms);

    bool post(domain::SessionId to, Command cmd, bool background = false);

    void start();          // 每分片挂拍泵协程 → pool.start()
    void stop() noexcept;  // pool.stop()（分片析构须在其后）

    std::size_t shard_count() const noexcept { return shards_.size(); }
    SessionRuntime* session(domain::SessionId id) noexcept;
    const Shard& shard(std::size_t i) const noexcept { return *shards_[i]; }
    Shard& shard_mut(std::size_t i) noexcept { return *shards_[i]; }   // 测试手动拍驱动

private:
    Shard& shard_for(domain::SessionId id) noexcept {
        return *shards_[SessionRuntime::key(id) % shards_.size()];
    }

    exec::LoopPool* pool_ = nullptr;
    uint64_t period_ms_ = 1000;
    std::vector<std::unique_ptr<Shard>> shards_;
};

}   // namespace oly::session
