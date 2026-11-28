// 黄金场景：配额/饱和度闭环（07 §4、09 §5 的 M1-2 形态）。
//   场景 1：世界预算硬顶零突破（13 §2 成本闸门）——预算见底后 P2 自降：
//           网关拒绝 → 会话侧降级阶梯（语气储备 → 沉默），全程不报错、不硬停；
//   场景 2：预算将尽 → τ_speak 被水位抬过点名注意力 → 角色"今日话少"；
//           预算恢复（窗口滚动）→ 自然重新开口。自治频率是控制变量，不是静态参数。
#include <oly/core/id.hpp>
#include <oly/core/ulid.hpp>
#include <oly/domain/domain.hpp>
#include <oly/events/envelope.hpp>
#include <oly/exec/tick_wheel.hpp>
#include <oly/gateway/gateway.hpp>
#include <oly/logdb/store.hpp>
#include <oly/quota/quota.hpp>
#include <oly/session/deliberation.hpp>
#include <oly/session/session_runtime.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <vector>

using namespace oly;

namespace {

domain::PrincipalId pid_of(std::string_view s) {
    return domain::PrincipalId::from_string(s).value();
}
SessionId sid_of(std::string_view s) {
    return SessionId::from_string(s).value();
}

constexpr const char* kRoot = "01ARZ3NDEKTSV4RRFFQ69G5FAV";
constexpr const char* kAlice = "01ARZ3NDEKTSV4RRFFQ69G5FBW";
constexpr const char* kSess = "01ARZ3NDEKTSV4RRFFQ69G5FDY";

void build_world(domain::DomainRegistry& reg) {
    domain::Principal r;
    r.id = r.owner = pid_of(kRoot);
    REQUIRE(reg.add_principal(r).has_value());
    domain::Principal p;
    p.id = pid_of(kAlice);
    p.kind = domain::PrincipalKind::Persona;
    p.owner = pid_of(kRoot);
    REQUIRE(reg.add_principal(p).has_value());
    REQUIRE(reg.add_persona(p.id, pid_of(kRoot), domain::CardBody{"苍月千鹤", "锚", ""}).has_value());
    domain::Session s;
    s.id = sid_of(kSess);
    s.owner = pid_of(kRoot);
    s.canon = "月下庭院。";
    s.members.push_back(domain::Member{s.owner, domain::SessionRole::Owner, 0});
    s.members.push_back(domain::Member{pid_of(kAlice), domain::SessionRole::Member, 0});
    REQUIRE(reg.create_session(s).has_value());
}

// 点名驱动（同决策 e2e 静音装配；NSDMI 默认值不是零——显式零化）
session::DeliberationConfig mention_only() {
    session::DeliberationConfig cfg;
    session::AttentionWeights w{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    w.mention = 2.0f;
    cfg.weights = w;
    cfg.score.tau_base = 0.5f;
    cfg.score.hard_cooldown = 0;
    return cfg;
}

struct TickInput {
    std::vector<session::PerceivedMessage> win;
    std::vector<session::PerceivedMessage> tail;
};

TickInput callout(const std::string& text) {
    TickInput in;
    in.win.push_back({1, pid_of(kRoot), text});
    in.tail.push_back({1, pid_of(kRoot), text});
    return in;
}

}   // namespace

TEST_CASE("配额闭环 e2e：世界预算见底 → 自降话少（不报错），预算恢复 → 重新开口", "[golden][quota]") {
    domain::DomainRegistry reg;
    build_world(reg);
    const auto* sess = reg.session(sid_of(kSess));

    gateway::FakeProvider prov({"【千鹤】庭前月色正好。"});
    gateway::Gateway gw_rich(prov);   // 驱动 A：充足预算（网关不挂账册 → 旁路）
    const auto world_id = ContentId::from_bytes(sha256(
        std::span<const std::byte>{reinterpret_cast<const std::byte*>("月下庭院"), 12}).bytes);

    // ── 驱动 A：预算充足 → 网关真实发言 ─────────────────────────
    quota::Ledger rich(quota::Limits{
        .burst = {1e9, 1e9},
        .volume = {.cap = 1e12, .window_ms = 1000},
        .world = {.cap = 1e12, .window_ms = 1000}});
    {
        auto cfg = mention_only();
        cfg.quota_ledger = &rich;
        cfg.world_ref = world_id;
        session::DeliberationDriver driver(pid_of(kAlice), gw_rich, {"储备。"}, cfg);
        const auto in = callout("苍月千鹤，给你讲讲今天的事。");
        session::TickContext ctx;
        ctx.tick = 1;
        ctx.now_ms = 0;
        ctx.registry = &reg;
        ctx.session = sess;
        ctx.window = in.win;
        ctx.new_messages = in.tail;
        const auto oc = driver.on_tick(ctx);
        REQUIRE(oc.speak);
        REQUIRE_FALSE(oc.fallback);
        REQUIRE(oc.model == "fake");
        REQUIRE(gw_rich.counters().quota_dropped == 0);
    }

    // ── 驱动 B：预算见底（余 0.5 token）→ 自降阶梯接管，零报错 ──
    //   （账册先建，网关挂账后预扣至见底）
    quota::Ledger poor(quota::Limits{
        .burst = {1e9, 1e9},
        .volume = {.cap = 1e12, .window_ms = 1000},
        .world = {.cap = 400.0, .window_ms = 1000}});
    gateway::Gateway gw_poor(prov, {}, &poor);
    REQUIRE(poor.reserve(0, 0, world_id.key(), 399.5).code == quota::Verdict::Code::Ok);
    {
        auto cfg = mention_only();
        cfg.quota_ledger = &poor;
        cfg.world_ref = world_id;
        session::DeliberationDriver driver(pid_of(kAlice), gw_poor, {"储备。"}, cfg);
        const auto in = callout("苍月千鹤，给你讲讲今天的事。");

        // 拍 1：网关拒绝（配额）→ 第一级语气储备（不走网关，零 token）
        session::TickContext ctx;
        ctx.tick = 1;
        ctx.now_ms = 0;
        ctx.registry = &reg;
        ctx.session = sess;
        ctx.window = in.win;
        ctx.new_messages = in.tail;
        const auto oc1 = driver.on_tick(ctx);
        REQUIRE(oc1.speak);
        REQUIRE(oc1.fallback);
        REQUIRE(oc1.model == "reserve");
        REQUIRE(gw_poor.counters().quota_dropped >= 1);

        // 拍 2：储备耗尽 → 沉默事件化（不硬停、不报错）
        ctx.tick = 2;
        const auto oc2 = driver.on_tick(ctx);
        REQUIRE_FALSE(oc2.speak);
        REQUIRE(oc2.silence_noted);

        // 窗口滚动（预算恢复）→ 自然重新开口（"今日话少"而非永久静音）
        ctx.tick = 3;
        ctx.now_ms = 2000;
        const auto oc3 = driver.on_tick(ctx);
        REQUIRE(oc3.speak);
        REQUIRE_FALSE(oc3.fallback);
        REQUIRE(gw_poor.counters().tokens_in_total > 0);
    }
    // 全程世界账本未破顶（13 §2 成本闸门：预留-结算纪律）
    REQUIRE(poor.world_pressure(2000, 0, world_id.key()) < 1.0f);
}

TEST_CASE("配额闭环 e2e：预算将尽 → τ_speak 被水位抬过点名注意力（群体话少）", "[golden][quota]") {
    domain::DomainRegistry reg;
    build_world(reg);
    const auto* sess = reg.session(sid_of(kSess));

    gateway::FakeProvider prov({"【千鹤】嗯。"});
    quota::Ledger ledger(quota::Limits{
        .burst = {1e9, 1e9},
        .volume = {.cap = 1e12, .window_ms = 1000},
        .world = {.cap = 1000.0, .window_ms = 1000}});
    gateway::Gateway gw(prov, {}, &ledger);   // τ 闸在先（预算将尽不进网关）；
                                              // 账册挂网关保证语义完整

    const auto world_key = sha256(std::span<const std::byte>{
        reinterpret_cast<const std::byte*>("月下庭院"), 12}).key();

    // 造水位：直接预扣世界预算至 95%（预算将尽，但未触底）
    REQUIRE(ledger.reserve(0, 0, world_key, 950.0).code == quota::Verdict::Code::Ok);

    session::DeliberationConfig cfg = mention_only();
    cfg.quota_ledger = &ledger;
    cfg.world_ref = ContentId::from_bytes(sha256(
        std::span<const std::byte>{reinterpret_cast<const std::byte*>("月下庭院"), 12}).bytes);
    cfg.sat_gain = 3.0f;   // 水位 0.95 → τ ≈ 0.3 + 2.85 ≈ 3.15 ≫ mention 注意力 ≈ 2±0.15

    session::DeliberationDriver driver(pid_of(kAlice), gw, {"储备。"}, cfg);
    const auto in = callout("苍月千鹤，说话呀。");

    session::TickContext ctx;
    ctx.tick = 1;
    ctx.now_ms = 0;
    ctx.registry = &reg;
    ctx.session = sess;
    ctx.window = in.win;
    ctx.new_messages = in.tail;
    const auto oc = driver.on_tick(ctx);
    // 被@仍沉默且必记（06 §2.4），τ 被水位抬高——"今日话少"的机制形态
    REQUIRE_FALSE(oc.speak);
    REQUIRE(oc.silence_noted);
    REQUIRE(oc.tau > 2.5f);
    REQUIRE(prov.calls() == 0);   // 阈值未过 → 根本不进网关（省 token）
}
