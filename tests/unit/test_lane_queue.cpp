// 两车道队列：交互优先 / 背景丢弃计数 / 批量排空上限 / 容量边界
#include <oly/exec/lane_queue.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>

using Q = oly::exec::LaneQueue<std::string>;

TEST_CASE("交互道满则拒收（不丢）", "[exec][lane]") {
    Q q(2, 8);
    REQUIRE(q.push(Q::Lane::Interactive, "a") == Q::Push::Ok);
    REQUIRE(q.push(Q::Lane::Interactive, "b") == Q::Push::Ok);
    REQUIRE(q.push(Q::Lane::Interactive, "c") == Q::Push::InteractiveFull);
    REQUIRE(q.interactive_size() == 2);
    REQUIRE(q.dropped_background() == 0);
}

TEST_CASE("背景道满则丢新件并计数（优雅降级）", "[exec][lane]") {
    Q q(8, 2);
    REQUIRE(q.push(Q::Lane::Background, "x1") == Q::Push::Ok);
    REQUIRE(q.push(Q::Lane::Background, "x2") == Q::Push::Ok);
    REQUIRE(q.push(Q::Lane::Background, "x3") == Q::Push::BackgroundDropped);
    REQUIRE(q.background_size() == 2);
    REQUIRE(q.dropped_background() == 1);
}

TEST_CASE("drain 交互道绝对优先", "[exec][lane]") {
    Q q(8, 8);
    q.push(Q::Lane::Background, "b1");
    q.push(Q::Lane::Interactive, "i1");
    q.push(Q::Lane::Background, "b2");
    q.push(Q::Lane::Interactive, "i2");

    const auto out = q.drain(10);
    REQUIRE(out.size() == 4);
    REQUIRE(out[0] == "i1");
    REQUIRE(out[1] == "i2");
    REQUIRE(out[2] == "b1");
    REQUIRE(out[3] == "b2");
    REQUIRE(q.empty());
}

TEST_CASE("drain 尊重批量上限（唤醒即批，01 §5）", "[exec][lane]") {
    Q q(8, 8);
    for (int i = 0; i < 6; ++i) q.push(Q::Lane::Interactive, "i" + std::to_string(i));
    q.push(Q::Lane::Background, "bg");

    const auto first = q.drain(3);
    REQUIRE(first.size() == 3);
    REQUIRE(first[2] == "i2");
    REQUIRE_FALSE(q.empty());

    const auto second = q.drain(3);
    REQUIRE(second.size() == 3);
    REQUIRE(second[2] == "i5");   // 交互道优先：i3..i5 排完

    const auto third = q.drain(3);
    REQUIRE(third.size() == 1);
    REQUIRE(third[0] == "bg");
}
