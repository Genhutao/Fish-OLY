// loop 池：协程调度到工作线程 / start-stop 生命周期
#include <oly/exec/loop_pool.hpp>

#include <catch2/catch_test_macros.hpp>

#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

namespace {
boost::asio::awaitable<void> record_thread(std::vector<std::thread::id>& sink,
                                           std::atomic<std::size_t>& done) {
    sink.push_back(std::this_thread::get_id());   // loop 内单写
    done.fetch_add(1, std::memory_order_relaxed);
    co_return;
}
}   // namespace

TEST_CASE("loop 池：协程在 worker 线程上执行，而非主线程", "[exec][pool]") {
    auto pool = oly::exec::LoopPool::create(2);
    REQUIRE(pool.has_value());
    pool->start();

    const auto main_id = std::this_thread::get_id();
    // 每 ctx 独立收集 + 原子完成计数（骨架期曾用共享 vector 轮询 size——并发
    // push_back/size 读取是 UB，偶发 3==4）
    std::array<std::vector<std::thread::id>, 2> ran_on;
    std::atomic<std::size_t> done{0};
    for (int i = 0; i < 4; ++i) {
        auto& ctx = pool->at(i % 2);
        boost::asio::co_spawn(ctx, record_thread(ran_on[i % 2], done), boost::asio::detached);
    }

    for (int spin = 0; spin < 500 && done.load(std::memory_order_relaxed) < 4; ++spin) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    REQUIRE(done.load() == 4);
    for (const auto& v : ran_on) {
        for (const auto id : v) REQUIRE(id != main_id);
    }

    pool->stop();
}

TEST_CASE("loop 池：stop 后可安全析构；线程数 0 被拒绝", "[exec][pool]") {
    auto bad = oly::exec::LoopPool::create(0);
    REQUIRE_FALSE(bad.has_value());
    REQUIRE(bad.error().code == "exec:invalid");

    auto pool = oly::exec::LoopPool::create(1);
    REQUIRE(pool.has_value());
    pool->start();
    pool->start();   // 幂等
    pool->stop();
    pool->stop();    // 幂等
}
