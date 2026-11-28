// 黄金场景端到端（13 §1 M0 验收）：单 loop、单会话、两角色脚本对话 30 轮。
// 断言链：① seq 连续与事实计数 ② 哈希链读端校验 ③ 幂等命令恰一次准入
// ④ 权限拒绝入审计 ⑤ 重放投影与内存投影一致（重启不丢世界的事实层雏形）。
#include <oly/core/ulid.hpp>
#include <oly/domain/domain.hpp>
#include <oly/events/envelope.hpp>
#include <oly/exec/loop_pool.hpp>
#include <oly/exec/tick_wheel.hpp>
#include <oly/logdb/store.hpp>
#include <oly/session/autonomy.hpp>
#include <oly/session/session_runtime.hpp>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>

using namespace oly;

namespace {

domain::PrincipalId pid_of(std::string_view s) {
    return domain::PrincipalId::from_string(s).value();
}
SessionId sid_of(std::string_view s) {
    return SessionId::from_string(s).value();
}

}   // namespace

TEST_CASE("黄金场景：两角色 30 轮脚本对话端到端", "[golden][e2e]") {
    // ── 世界装配 ────────────────────────────────────────────────
    domain::DomainRegistry reg;
    const auto root = pid_of("01ARZ3NDEKTSV4RRFFQ69G5FAV");
    const auto alice = pid_of("01ARZ3NDEKTSV4RRFFQ69G5FBW");
    const auto bob = pid_of("01ARZ3NDEKTSV4RRFFQ69G5FCX");
    const auto intruder = pid_of("01ARZ3NDEKTSV4RRFFQ69G5FCY");   // 非成员

    {
        domain::Principal r;
        r.id = r.owner = root;
        REQUIRE(reg.add_principal(r).has_value());
        for (const auto [id, name] : {std::pair{alice, "苍月千鹤"}, std::pair{bob, "白昼·艾尔"}}) {
            domain::Principal p;
            p.id = id;
            p.kind = domain::PrincipalKind::Persona;
            p.owner = root;
            REQUIRE(reg.add_principal(p).has_value());
            REQUIRE(reg.add_persona(id, root, domain::CardBody{name, "锚点", ""}).has_value());
        }
        domain::Principal x;
        x.id = intruder;
        REQUIRE(reg.add_principal(x).has_value());

        domain::Session sess;
        sess.id = sid_of("01ARZ3NDEKTSV4RRFFQ69G5FDY");
        sess.owner = root;
        sess.canon = "月下庭院：青石铺地，桂影婆娑。";
        sess.members.push_back(domain::Member{root, domain::SessionRole::Owner, 0});
        sess.members.push_back(domain::Member{alice, domain::SessionRole::Member, 0});
        sess.members.push_back(domain::Member{bob, domain::SessionRole::Member, 0});
        REQUIRE(reg.create_session(sess).has_value());
    }

    // ── 执行环境：单 loop + 段存储 + 轮式调度 ────────────────────
    auto pool = exec::LoopPool::create(1);
    REQUIRE(pool.has_value());
    pool->start();

    const auto dir = std::filesystem::temp_directory_path() /
                     ("oly-golden-e2e-" + UlidFactory{}.next().str());
    auto store = logdb::SegmentStore::open(dir, events::StreamRef{events::StreamKind::Session,
                                                                  sid_of("01ARZ3NDEKTSV4RRFFQ69G5FDY").bytes()},
                                           0);
    REQUIRE(store.has_value());

    exec::TickWheel wheel;
    constexpr uint64_t kPeriod = 1000;

    auto rt = session::SessionRuntime::create(sid_of("01ARZ3NDEKTSV4RRFFQ69G5FDY"),
                                              {reg, *store, wheel, kPeriod}, 0);
    REQUIRE(rt.has_value());

    // 两角色：A 偶数拍、B 奇数拍，各 15 句 → 30 轮
    std::vector<std::string> lines_a, lines_b;
    for (int i = 0; i < 15; ++i) {
        lines_a.push_back("【千鹤】第" + std::to_string(i + 1) + "夜：月色未变。");
        lines_b.push_back("【艾尔】第" + std::to_string(i + 1) + "夜：光在燃烧！");
    }
    rt->attach_autonomy(alice, std::make_unique<session::ScriptedDriver>(lines_a, 2, 0));
    rt->attach_autonomy(bob, std::make_unique<session::ScriptedDriver>(lines_b, 2, 1));

    // 用户中途发言（交互道）+ 幂等重放同 ID
    const auto dup_id = CommandId::from_string("01ARZ3NDEKTSV4RRFFQ69G5G2Z").value_or(CommandId{});
    session::Command user_cmd;
    user_cmd.id = dup_id;
    user_cmd.actor = root;
    user_cmd.action = session::CmdAction::SendMessage;
    user_cmd.text = "（用户插话）两位继续。";
    REQUIRE(rt->enqueue(user_cmd) == session::SessionRuntime::Enqueue::Ok);
    REQUIRE(rt->enqueue(user_cmd) == session::SessionRuntime::Enqueue::Duplicate);   // 恰一次

    // 非成员发言：准入拒绝 → denial 审计事实
    session::Command intruder_cmd;
    intruder_cmd.id = CommandId::from_string("01ARZ3NDEKTSV4RRFFQ69G5G3A").value_or(CommandId{});
    intruder_cmd.actor = intruder;
    intruder_cmd.action = session::CmdAction::SendMessage;
    intruder_cmd.text = "闯入者的低语。";
    REQUIRE(rt->enqueue(intruder_cmd) == session::SessionRuntime::Enqueue::Ok);

    // ── 推进 30 拍（先排空队列再走时钟，模拟 loop 主循环）────────
    uint64_t now = 0;
    for (int tick = 0; tick < 30; ++tick) {
        now += kPeriod;
        (void)rt->advance_clock(now);
        (void)rt->drain(64);
    }
    // 排空交互道残留
    (void)rt->drain(64);

    // ── 断言链 ─────────────────────────────────────────────────
    const auto& proj = reg.session(sid_of("01ARZ3NDEKTSV4RRFFQ69G5FDY"))->messages;
    REQUIRE(proj.size() == 31);   // 1 用户插话 + 30 角色轮次
    // 用户消息最先入队且交互道绝对优先（01 §3）→ 投影头部
    REQUIRE(proj[0].second == "（用户插话）两位继续。");
    REQUIRE(proj[1].second == "【艾尔】第1夜：光在燃烧！");    // 拍1：奇数拍 → B
    REQUIRE(proj[2].second == "【千鹤】第1夜：月色未变。");    // 拍2：偶数拍 → A
    REQUIRE(proj[30].second == "【千鹤】第15夜：月色未变。");  // 拍30：偶数拍 → A 收尾

    // 全量读回：哈希链逐条校验 + 事实计数（created 1 + 31 消息 + 1 denied = 33）
    auto events = store->read_range(0, 10000);
    REQUIRE(events.has_value());
    REQUIRE(events->size() == 33);
    std::size_t msgs = 0, denied = 0, created = 0;
    for (const auto& e : *events) {
        if (e.type == "message.appended") ++msgs;
        if (e.type == "cmd.denied") ++denied;
        if (e.type == "session.created") ++created;
    }
    REQUIRE(msgs == 31);
    REQUIRE(denied == 1);
    REQUIRE(created == 1);

    // 重放投影：从事件流重建消息列表 == 内存投影（事实层"重启不丢"）
    std::vector<std::pair<uint64_t, std::string>> replayed;
    for (const auto& e : *events) {
        if (e.type != "message.appended") continue;
        codec::Decoder d(e.payload_bin);
        /* actor */ (void)d.ulid();
        replayed.emplace_back(e.seq, d.text());
    }
    REQUIRE(replayed == proj);

    // 首事件 = created @ seq 0；用户消息 seq 与投影一致
    REQUIRE((*events)[0].type == "session.created");
    REQUIRE((*events)[0].seq == 0);
    REQUIRE(replayed.back().first == proj[30].first);

    pool->stop();
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}
