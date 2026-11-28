// OLY · exec —— 拍泵协程（01 §2 / §6）：loop 上的周期心跳。
// 每拍到期回调宿主处理器（轮式调度摘槽 + 队列排空都在处理器内）。
// 监管者纪律（01 §6）：回调异常吞掉并继续（M1 记入进程流）；取消/停止即退出。
#pragma once

#include <boost/asio/awaitable.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/system/system_error.hpp>

#include <chrono>
#include <cstdint>
#include <functional>

namespace oly::exec {

inline boost::asio::awaitable<void> beat_loop(
    std::chrono::steady_clock::duration period, std::function<void(uint64_t now_ms)> on_beat) {
    const auto t0 = std::chrono::steady_clock::now();
    boost::asio::steady_timer timer(co_await boost::asio::this_coro::executor);
    for (;;) {
        timer.expires_after(period);
        try {
            co_await timer.async_wait(boost::asio::use_awaitable);
        } catch (const boost::system::system_error&) {
            co_return;   // 取消 / io 停止
        }
        const uint64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::steady_clock::now() - t0)
                                    .count();
        try {
            on_beat(now_ms);
        } catch (...) {
            // 监管者（01 §6）：领域异常不杀 loop；M1 记入进程流事件
        }
    }
}

}   // namespace oly::exec
