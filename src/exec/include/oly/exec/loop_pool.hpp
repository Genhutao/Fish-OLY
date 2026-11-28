// OLY · exec —— loop 池（01 §1）：N 线程 × 每线程一个 io_context，thread-per-core。
// 领域状态按 shard 绑定 loop；池只负责"线程 + 事件循环"的生灭，不知道领域语义。
#pragma once

#include "oly/core/result.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/executor_work_guard.hpp>

#include <atomic>
#include <memory>
#include <stddef.h>
#include <thread>
#include <vector>

namespace oly::exec {

class LoopPool {
public:
    // 创建池（线程数 ≥ 1；不自动启动——显式 start()，便于装配后再起线程）
    static Result<LoopPool> create(std::size_t thread_count);

    LoopPool(LoopPool&& other) noexcept;   // atomic 成员不可移动 → 手写转移语义
    LoopPool& operator=(LoopPool&&) = delete;
    LoopPool(const LoopPool&) = delete;
    LoopPool& operator=(const LoopPool&) = delete;
    ~LoopPool();

    // 轮询取一个 loop（创建会话/shard 时由路由表决定归属后用 at()；pick() 用于无归属杂务）
    boost::asio::io_context& pick() noexcept;
    boost::asio::io_context& at(std::size_t index);
    std::size_t size() const noexcept { return ctxs_.size(); }

    void start();            // 启动 worker 线程（幂等；重复调用无副作用）
    void stop() noexcept;    // 停止全部事件循环并 join（幂等）

private:
    LoopPool() = default;

    std::vector<std::unique_ptr<boost::asio::io_context>> ctxs_;
    std::vector<std::unique_ptr<boost::asio::executor_work_guard<
        boost::asio::io_context::executor_type>>>
        guards_;
    std::vector<std::jthread> threads_;
    std::atomic<std::size_t> rr_{0};
    std::atomic<bool> running_{false};
};

}   // namespace oly::exec
