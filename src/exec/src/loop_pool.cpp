#include "oly/exec/loop_pool.hpp"

#include <cstdio>

namespace oly::exec {

Result<LoopPool> LoopPool::create(const std::size_t thread_count) {
    if (thread_count == 0) return unexpected(err("exec:invalid", "thread_count must be >= 1"));

    LoopPool pool;
    pool.ctxs_.reserve(thread_count);
    pool.guards_.reserve(thread_count);
    for (std::size_t i = 0; i < thread_count; ++i) {
        pool.ctxs_.push_back(std::make_unique<boost::asio::io_context>());
        pool.guards_.push_back(std::make_unique<
            boost::asio::executor_work_guard<boost::asio::io_context::executor_type>>(
            boost::asio::make_work_guard(*pool.ctxs_.back())));
    }
    return std::move(pool);   // move-only 类型：显式 move 进 Result 构造参数
}

LoopPool::LoopPool(LoopPool&& other) noexcept
    : ctxs_(std::move(other.ctxs_)),
      guards_(std::move(other.guards_)),
      threads_(std::move(other.threads_)),
      rr_(other.rr_.load(std::memory_order_relaxed)),
      running_(other.running_.load(std::memory_order_relaxed)) {
    // 防御：源池线程若未 join，交由其自身析构处理；此处只保证句柄转移完整
    other.threads_.clear();
}

LoopPool::~LoopPool() { stop(); }

boost::asio::io_context& LoopPool::pick() noexcept {
    const std::size_t i = rr_.fetch_add(1, std::memory_order_relaxed) % ctxs_.size();
    return *ctxs_[i];
}

boost::asio::io_context& LoopPool::at(const std::size_t index) { return *ctxs_.at(index); }

void LoopPool::start() {
    if (running_.exchange(true)) return;
    threads_.reserve(ctxs_.size());
    for (auto& ctx : ctxs_) {
        threads_.emplace_back([ctx = ctx.get()] {
            // 监管者纪律（01 §6）：worker 逃逸异常不终结进程，记 stderr 后让线程退出
            try {
                ctx->run();
            } catch (const std::exception& e) {
                std::fprintf(stderr, "exec: loop worker exception: %s\n", e.what());
            } catch (...) {
                std::fprintf(stderr, "exec: loop worker unknown exception\n");
            }
        });
    }
}

void LoopPool::stop() noexcept {
    if (!running_.exchange(false)) return;
    for (auto& g : guards_) g->reset();
    for (auto& ctx : ctxs_) ctx->stop();
    for (auto& t : threads_) {
        if (t.joinable()) t.join();
    }
    threads_.clear();
}

}   // namespace oly::exec
