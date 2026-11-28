// 会话路由表：绑定/查询/改绑/解绑 + 并发读写冒烟
#include <oly/exec/session_router.hpp>

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <thread>
#include <vector>

TEST_CASE("路由表：bind / loop_of / rebind / unbind 语义", "[exec][router]") {
    oly::exec::SessionRouter r;
    REQUIRE(r.bind(100, 0));
    REQUIRE_FALSE(r.bind(100, 1));   // 重复绑定拒绝
    REQUIRE(r.loop_of(100) == std::optional<std::size_t>{0});
    REQUIRE(r.loop_of(999) == std::nullopt);

    REQUIRE(r.rebind(100, 2) == std::optional<std::size_t>{0});   // 迁移：返回旧 loop
    REQUIRE(r.loop_of(100) == std::optional<std::size_t>{2});
    REQUIRE(r.rebind(999, 1) == std::nullopt);

    REQUIRE(r.unbind(100) == std::optional<std::size_t>{2});
    REQUIRE(r.loop_of(100) == std::nullopt);
    REQUIRE(r.size() == 0);
}

TEST_CASE("路由表：并发读写冒烟（白名单 rwlock）", "[exec][router]") {
    oly::exec::SessionRouter r;
    for (uint64_t k = 0; k < 64; ++k) r.bind(k, 0);

    std::atomic<bool> stop{false};
    std::atomic<int> read_ok{0};
    std::vector<std::jthread> readers;
    for (int t = 0; t < 3; ++t) {
        readers.emplace_back([&] {
            while (!stop.load(std::memory_order_relaxed)) {
                if (r.loop_of(7).has_value()) ++read_ok;
            }
        });
    }
    for (int i = 0; i < 200; ++i) {
        r.rebind(i % 64, static_cast<std::size_t>(i % 4) + 1);
    }
    stop.store(true);
    for (auto& t : readers) t.join();
    REQUIRE(read_ok.load() > 0);
}
