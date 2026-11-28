// 身份分片（01 §1）：单写者执行器、有界通道背压、原子快照发布（无锁读）
#include <oly/exec/loop_pool.hpp>
#include <oly/identity/shard.hpp>

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <oly/core/ulid.hpp>

#include <thread>

using namespace oly;
using namespace oly::identity;

namespace {

domain::PrincipalId pid_of(std::string_view s) {
    return domain::PrincipalId::from_string(s).value();
}
SessionId sid_of(std::string_view s) {
    return SessionId::from_string(s).value();
}

}   // namespace

TEST_CASE("身份分片：变更受理 → 快照发布（同步直驱，不依赖 io 运行）", "[identity]") {
    exec::LoopPool pool = exec::LoopPool::create(1).take();   // 不 start：用 drain_once 直驱
    auto shard = Shard::create({pool.at(0), 64}).take();

    REQUIRE(shard->snapshot_version() == 0);

    domain::Principal r;
    r.id = r.owner = pid_of("01ARZ3NDEKTSV4RRFFQ69G5FAV");
    domain::Principal a;
    a.id = pid_of("01ARZ3NDEKTSV4RRFFQ69G5FBW");
    a.kind = domain::PrincipalKind::Persona;
    a.owner = r.id;

    REQUIRE(shard->post(AddPrincipalCmd{r}));
    REQUIRE(shard->post(AddPersonaCmd{a.id, r.id, domain::CardBody{"苍月千鹤", "锚", ""}}));
    REQUIRE(shard->post(AddPrincipalCmd{a}));   // 角色同时是主体（A1 同壳）

    shard->drain_once();   // 泵一批（run() 协程未运行，测试直驱等价）

    REQUIRE(shard->applied() == 3);
    REQUIRE(shard->snapshot_version() == 3);
    const auto snap = shard->snapshot();
    REQUIRE(snap->principals.size() == 2);
    REQUIRE(snap->personas.size() == 1);   // Persona 与 Principal 分表
    REQUIRE(snap->persona(a.id) != nullptr);
    REQUIRE(snap->persona(a.id)->head().name == "苍月千鹤");
    REQUIRE(snap->principal(a.id)->owner == r.id);
}

TEST_CASE("身份分片：通道满即拒（背压计数）+ 会话命令受理", "[identity]") {
    exec::LoopPool pool = exec::LoopPool::create(1).take();
    auto shard = Shard::create({pool.at(0), 2}).take();   // 容量 2

    domain::Principal r;
    r.id = r.owner = pid_of("01ARZ3NDEKTSV4RRFFQ69G5FAV");
    REQUIRE(shard->post(AddPrincipalCmd{r}));
    REQUIRE(shard->post(AddPrincipalCmd{r}));   // 重复主体：受理入队（应用时拒绝）
    REQUIRE_FALSE(shard->post(AddPrincipalCmd{r}));   // 第 3 条：满即拒
    REQUIRE(shard->rejected() == 1);

    domain::Session sess;
    sess.id = sid_of("01ARZ3NDEKTSV4RRFFQ69G5FDY");
    sess.owner = r.id;
    sess.members.push_back(domain::Member{r.id, domain::SessionRole::Owner, 0});
    sess.members.push_back(
        domain::Member{pid_of("01ARZ3NDEKTSV4RRFFQ69G5FBW"), domain::SessionRole::Member, 0});
    REQUIRE_FALSE(shard->post(CreateSessionCmd{sess}));   // 容量 2 已满 → 拒（背压）
    REQUIRE(shard->rejected() == 2);

    shard->drain_once();   // 第 2 条（重复主体）应用失败：applied 不计，但快照不发布无效版本
    shard->drain_once();
    REQUIRE(shard->applied() == 1);
    REQUIRE(shard->snapshot_version() == 1);
    REQUIRE(shard->snapshot()->principals.size() == 1);
}

TEST_CASE("身份分片：真线程驱动（io 运行 → run() 协程消费）", "[identity]") {
    exec::LoopPool pool = exec::LoopPool::create(1).take();
    auto shard = Shard::create({pool.at(0), 256}).take();
    pool.start();

    domain::Principal r;
    r.id = r.owner = pid_of("01ARZ3NDEKTSV4RRFFQ69G5FAV");
    domain::Principal a;
    a.id = pid_of("01ARZ3NDEKTSV4RRFFQ69G5FBW");
    a.kind = domain::PrincipalKind::Persona;
    a.owner = r.id;
    REQUIRE(shard->post(AddPrincipalCmd{r}));
    REQUIRE(shard->post(AddPersonaCmd{a.id, r.id, domain::CardBody{"白昼·艾尔", "锚", ""}}));

    // 等 run() 协程消费（异步；超时 2s）
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (shard->applied() < 2 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    pool.stop();

    REQUIRE(shard->applied() == 2);
    REQUIRE(shard->snapshot()->personas.size() == 1);
    REQUIRE(shard->snapshot()->persona(a.id) != nullptr);
}
