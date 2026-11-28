// 单元：两层桶（09 §3.1）——突发桶/体量窗口、预留-结算-释放（09 §3.2）、
// 世界第二维度（09 §2 min 语义 + 原子回滚）、水位（07 §4 闭环输入）。
#include <oly/quota/quota.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace oly::quota;

namespace {
constexpr uint64_t kUser = 101;
constexpr uint64_t kWorld = 202;
}

TEST_CASE("配额：突发桶——满桶起步 / 耗尽限流 / 速率回复", "[unit][quota]") {
    Ledger l(Limits{.burst = {.rate_per_s = 100.0, .burst = 50.0},
                    .volume = {.cap = 1e12, .window_ms = 1000},
                    .world = {.cap = 1e12, .window_ms = 1000}});
    // 满桶起步：首用即有突发额度
    REQUIRE(l.reserve(0, kUser, 0, 50.0).code == Verdict::Code::Ok);
    // 桶空 → 限流（reset_ms = 恢复时刻）
    const auto v = l.reserve(0, kUser, 0, 10.0);
    REQUIRE(v.code == Verdict::Code::BurstLimited);
    REQUIRE(v.reset_ms == Catch::Approx(100.0).margin(1.0));   // 10 token ÷ 100/s
    // 时间流逝 → 速率回复（100/s × 0.5s = 50）
    REQUIRE(l.reserve(500, kUser, 0, 50.0).code == Verdict::Code::Ok);
}

TEST_CASE("配额：体量窗口——耗尽即停 / 窗口滚动重置 / reset_ms", "[unit][quota]") {
    Ledger l(Limits{.burst = {1e9, 1e9},
                    .volume = {.cap = 100.0, .window_ms = 1000},
                    .world = {.cap = 1e12, .window_ms = 1000}});
    REQUIRE(l.reserve(0, kUser, 0, 60.0).code == Verdict::Code::Ok);
    REQUIRE(l.reserve(0, kUser, 0, 40.0).code == Verdict::Code::Ok);   // 恰好到顶
    const auto v = l.reserve(500, kUser, 0, 1.0);   // 窗口内耗尽
    REQUIRE(v.code == Verdict::Code::VolumeExhausted);
    REQUIRE(v.reset_ms == 1000);   // 下一窗口起点（retry-after 原料）
    REQUIRE(l.reserve(999, kUser, 0, 1.0).code == Verdict::Code::VolumeExhausted);
    REQUIRE(l.reserve(1000, kUser, 0, 100.0).code == Verdict::Code::Ok);   // 滚动重置
}

TEST_CASE("配额：世界第二维度——min 裁决 / 原子回滚", "[unit][quota]") {
    Ledger l(Limits{.burst = {1e9, 1e9},
                    .volume = {.cap = 1000.0, .window_ms = 1000},
                    .world = {.cap = 100.0, .window_ms = 1000}});
    // 世界先触顶 → WorldExhausted（主体未动）
    REQUIRE(l.reserve(0, kUser, kWorld, 60.0).code == Verdict::Code::Ok);
    REQUIRE(l.reserve(0, kUser, kWorld, 50.0).code == Verdict::Code::WorldExhausted);
    REQUIRE(l.world_pressure(0, 0, 0) == Catch::Approx(0.0f).margin(0.01));
    REQUIRE(l.world_pressure(0, kUser, kWorld) > 0.5f);
    // 世界余 40 → 主体桶不受影响（独立窗口账）
    const auto v = l.reserve(0, kUser, kWorld, 40.0);
    REQUIRE(v.code == Verdict::Code::Ok);
    // min 裁决：主体余 900、世界余 0 → 世界先拒
    REQUIRE(l.reserve(0, kUser, kWorld, 1.0).code == Verdict::Code::WorldExhausted);
}

TEST_CASE("配额：预留-结算-释放——多退少补 / 少补追缴 / deadline 释放", "[unit][quota]") {
    Ledger l(Limits{.burst = {1e9, 1e9},
                    .volume = {.cap = 100.0, .window_ms = 1000},
                    .world = {.cap = 100.0, .window_ms = 1000}});
    // 预留 100、实耗 60 → 多退 40（净耗 60 → 水位 0.6）
    REQUIRE(l.reserve(0, kUser, kWorld, 100.0).code == Verdict::Code::Ok);
    l.settle(0, kUser, kWorld, 100.0, 60.0);
    REQUIRE(l.world_pressure(0, kUser, kWorld) == Catch::Approx(0.6).margin(0.01));
    // 退额可再预留
    REQUIRE(l.reserve(0, kUser, kWorld, 30.0).code == Verdict::Code::Ok);   // used 90
    // 少补：预留 30、实耗 45 → 追缴 15（used 105 → 余量 -5，任何正预留被拒；
    // 世界维先行裁决 → 报 WorldExhausted）
    l.settle(0, kUser, kWorld, 30.0, 45.0);
    REQUIRE(l.world_pressure(0, kUser, kWorld) == Catch::Approx(1.0).margin(0.01));
    REQUIRE(l.reserve(0, kUser, kWorld, 1.0).code == Verdict::Code::WorldExhausted);
    // deadline 释放 30 → used 75 → 水位 0.75（无悬挂占用，07 §3）
    l.release(kUser, kWorld, 30.0);
    REQUIRE(l.world_pressure(0, kUser, kWorld) == Catch::Approx(0.75).margin(0.01));
}

TEST_CASE("配额：主体触顶 → 世界预留原子回滚（不悬挂占用）", "[unit][quota]") {
    Ledger l(Limits{.burst = {1e9, 1e9},
                    .volume = {.cap = 50.0, .window_ms = 1000},
                    .world = {.cap = 100.0, .window_ms = 1000}});
    REQUIRE(l.reserve(0, kUser, kWorld, 40.0).code == Verdict::Code::Ok);
    // 世界余 60 ≥ 30 可预留，但主体余仅 10 → 主体拒 + 世界预留回滚
    REQUIRE(l.reserve(0, kUser, kWorld, 30.0).code == Verdict::Code::VolumeExhausted);
    // 世界回滚（40/100）+ 主体已耗（40/50）→ 水位取 max = 0.8
    REQUIRE(l.world_pressure(0, 0, kWorld) == Catch::Approx(0.4).margin(0.01));   // 世界回滚
    REQUIRE(l.world_pressure(0, kUser, 0) == Catch::Approx(0.8).margin(0.01));
    REQUIRE(l.reserve(0, kUser, kWorld, 10.0).code == Verdict::Code::Ok);   // 主体余恰够
}
