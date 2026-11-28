// 轮式调度器：到期批 / 幂等重排 / 摘除 / 过期插入 / 溢出表回收 / 时钟不回拨
#include <oly/exec/tick_wheel.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <set>
#include <vector>

namespace {
bool contains(const std::vector<uint64_t>& v, uint64_t k) {
    return std::find(v.begin(), v.end(), k) != v.end();
}
}   // namespace

TEST_CASE("到期会话整批返回且一拍只出现一次", "[exec][wheel]") {
    oly::exec::TickWheel w;

    w.insert(1, 400);   // 4 拍后到期
    w.insert(2, 400);
    w.insert(3, 700);

    auto due = w.advance(400);
    REQUIRE(due.size() == 2);
    REQUIRE(contains(due, 1));
    REQUIRE(contains(due, 2));
    REQUIRE_FALSE(contains(due, 3));

    // 同拍再推进：无重复
    REQUIRE(w.advance(450).empty());

    auto due2 = w.advance(700);
    REQUIRE(due2.size() == 1);
    REQUIRE(contains(due2, 3));
    REQUIRE(w.size() == 0);   // 到期即出索引
}

TEST_CASE("幂等重排：旧截止作废", "[exec][wheel]") {
    oly::exec::TickWheel w;
    w.insert(9, 5000);
    w.insert(9, 1200);   // 重排提前

    REQUIRE(w.advance(1500).size() == 1);   // 在 1200 拍到期
    REQUIRE(w.advance(5500).empty());       // 5000 拍不再触发
}

TEST_CASE("remove 摘除（导演暂停 = 摘槽）", "[exec][wheel]") {
    oly::exec::TickWheel w;
    w.insert(7, 600);
    w.remove(7);
    REQUIRE_FALSE(w.scheduled(7));
    REQUIRE(w.advance(1000).empty());
}

TEST_CASE("过期插入立即返回", "[exec][wheel]") {
    oly::exec::TickWheel w;
    w.advance(1000);   // 推进至 1000ms
    w.insert(5, 200);  // deadline 已过 → 立即到期
    auto due = w.advance(1001);
    REQUIRE(due.size() == 1);
    REQUIRE(contains(due, 5));
}

TEST_CASE("溢出表：窗口外截止时间自动回收", "[exec][wheel]") {
    oly::exec::TickWheel w;   // 2048 槽 × 100ms = 204.8s 窗口
    w.insert(8, 500'000);     // ~500s，远超窗口
    REQUIRE(w.advance(400'000).empty());
    auto due = w.advance(500'100);
    REQUIRE(due.size() == 1);
    REQUIRE(contains(due, 8));
}

TEST_CASE("时钟回拨被忽略", "[exec][wheel]") {
    oly::exec::TickWheel w;
    w.insert(4, 1000);
    REQUIRE(w.advance(1100).size() == 1);
    REQUIRE(w.advance(500).empty());   // 回拨：无操作
}

TEST_CASE("规模冒烟：20 万会话插入与推进", "[exec][wheel][perf]") {
    oly::exec::TickWheel w;
    uint64_t now = 0;
    for (uint64_t k = 0; k < 200'000; ++k) w.insert(k, now + 4000 + (k % 5) * 100);
    REQUIRE(w.size() == 200'000);

    std::size_t total = 0;
    for (int beat = 0; beat < 5; ++beat) {
        now += 1000;
        total += w.advance(now).size();
    }
    REQUIRE(total == 200'000);
}
