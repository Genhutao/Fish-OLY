// 黄金场景：配置面闭环（08 §1/§2/§3 M1-3 形态）。
//   config.set 热生效（下一拍可见）→ 包络越界拒绝写 denial → 权限裁决
//   （Owner 可调 / Member 拒）→ decision.recorded 附生效配置 ContentId
//   （行为归因）→ 重启 recover 重放还原配置（确定性叙事完整）。
#include <oly/codec/codec.hpp>
#include <oly/config/config.hpp>
#include <oly/core/id.hpp>
#include <oly/core/ulid.hpp>
#include <oly/domain/domain.hpp>
#include <oly/events/envelope.hpp>
#include <oly/exec/tick_wheel.hpp>
#include <oly/gateway/gateway.hpp>
#include <oly/logdb/store.hpp>
#include <oly/session/deliberation.hpp>
#include <oly/session/session_runtime.hpp>

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
constexpr const char* kBob = "01ARZ3NDEKTSV4RRFFQ69G5FCX";
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
    domain::Principal b;
    b.id = pid_of(kBob);
    REQUIRE(reg.add_principal(b).has_value());
    domain::Session s;
    s.id = sid_of(kSess);
    s.owner = pid_of(kRoot);
    s.canon = "月下庭院。";
    s.members.push_back(domain::Member{s.owner, domain::SessionRole::Owner, 0});
    s.members.push_back(domain::Member{pid_of(kAlice), domain::SessionRole::Member, 0});
    s.members.push_back(domain::Member{pid_of(kBob), domain::SessionRole::Member, 0});
    REQUIRE(reg.create_session(s).has_value());
}

session::Command config_cmd(const domain::PrincipalId actor, const config::Patch& patch) {
    session::Command c;
    c.id = CommandId::from_bytes(UlidFactory{}.next().bytes());
    c.actor = actor;
    c.action = session::CmdAction::ConfigSet;
    c.config_patch = config::encode_patch(patch);
    return c;
}

}   // namespace

TEST_CASE("配置闭环 e2e：热生效 / 包络拒绝 / 行为归因 / 重放还原", "[golden][config]") {
    const auto dir = std::filesystem::temp_directory_path() /
                     ("oly-golden-cfg-" + UlidFactory{}.next().str());
    domain::DomainRegistry reg;
    build_world(reg);
    auto store = logdb::SegmentStore::open(
        dir, events::StreamRef{events::StreamKind::Session, sid_of(kSess).bytes()}, 0);
    REQUIRE(store.has_value());
    exec::TickWheel wheel;
    session::SessionRuntime::Deps deps{reg, *store, wheel, 1000, {}};
    auto rt = session::SessionRuntime::create(sid_of(kSess), deps, 0);
    REQUIRE(rt.has_value());

    const auto platform_id = rt->effective().content_id;
    REQUIRE(rt->effective().values.tau_base == 1.0f);   // platform 基线

    // ── 决策驱动挂运行时（决策事实由 runtime 落地）+ 生效配置热通道 ──
    gateway::FakeProvider prov({"【千鹤】听你说。"});
    gateway::Gateway gw(prov);
    session::DeliberationConfig cfg;
    session::AttentionWeights w{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    w.mention = 2.0f;
    cfg.weights = w;
    cfg.score.hard_cooldown = 0;
    cfg.effective = &rt->effective();
    rt->attach_autonomy(pid_of(kAlice),
                        std::make_unique<session::DeliberationDriver>(pid_of(kAlice), gw, std::vector<std::string>{}, cfg));

    const auto say = [&](const std::string& text) {
        session::Command c;
        c.id = CommandId::from_bytes(UlidFactory{}.next().bytes());
        c.actor = pid_of(kRoot);
        c.action = session::CmdAction::SendMessage;
        c.text = text;
        REQUIRE(rt->enqueue(c) == session::SessionRuntime::Enqueue::Ok);
        (void)rt->drain(64);
    };

    // 拍 1：platform 基线（τ = 1.0×0.6 = 0.6 < att ≈ 1.5±0.15）→ 发言
    say("苍月千鹤，在吗？");
    (void)rt->on_tick(1000);
    // ── Owner config.set：τ_base 4.0 → 热生效（下一拍话少）─────────
    {
        config::Patch p;
        p.tau_base = 4.0f;
        REQUIRE(rt->enqueue(config_cmd(pid_of(kRoot), p)) ==
                session::SessionRuntime::Enqueue::Ok);
        const auto r = rt->drain(64);
        REQUIRE(r.has_value());
        REQUIRE(rt->effective().values.tau_base == 4.0f);
        REQUIRE(rt->effective().content_id != platform_id);   // 变更即换 ContentId
    }
    // 拍 2：τ = 4.0×0.6 = 2.4 > att → 沉默（被@沉默必记 → autonomy.silent）
    say("苍月千鹤，说话呀。");
    (void)rt->on_tick(2000);

    // ── Member 提 config.set → 权限 denial（会话级配置 Owner/Admin，08 §2）──
    {
        config::Patch p;
        p.tau_base = 2.0f;
        REQUIRE(rt->enqueue(config_cmd(pid_of(kBob), p)) ==
                session::SessionRuntime::Enqueue::Ok);
        (void)rt->drain(64);
        REQUIRE(rt->effective().values.tau_base == 4.0f);   // 拒绝即不变
    }
    // ── 越界 patch（τ 低于包络地板）→ 包络 denial（承诺而非默认值）──
    {
        config::Patch p;
        p.tau_base = 0.0001f;
        REQUIRE(rt->enqueue(config_cmd(pid_of(kRoot), p)) ==
                session::SessionRuntime::Enqueue::Ok);
        (void)rt->drain(64);
        REQUIRE(rt->effective().values.tau_base == 4.0f);
    }

    // ── 事件流审计：config.set 事实 / decision 归因 / 两类 denial ──
    {
        const auto all = store->read_range(0, 10000);
        REQUIRE(all.has_value());
        bool saw_cfg_set = false, saw_denied_envelope = false, saw_denied_perm = false;
        std::size_t decisions = 0;
        for (const auto& e : all.value()) {
            if (e.type == "config.set") {
                saw_cfg_set = true;
                REQUIRE(e.payload_bin == config::encode_patch([] {
                            config::Patch p;
                            p.tau_base = 4.0f;
                            return p;
                        }()));
            }
            if (e.type == "decision.recorded") {
                ++decisions;
                // 尾部归因：…att/tau 后 32 字节 = 生效配置 ContentId（platform 基线）
                codec::Decoder d(e.payload_bin);
                (void)d.ulid();
                (void)d.text();
                (void)d.u8();
                (void)d.u8();
                (void)d.u32();
                (void)d.u32();
                (void)d.u32();
                (void)d.u32();
                const auto cid = d.content();
                REQUIRE(d.ok());
                REQUIRE(ContentId::from_bytes(cid) == platform_id);
            }
            if (e.type == "cmd.denied") {
                codec::Decoder d(e.payload_bin);
                const auto reason = d.text();
                if (reason == "config:envelope") saw_denied_envelope = true;
                if (reason != "config:envelope" && reason != "config:decode") {
                    saw_denied_perm = true;   // 权限 denial（reason 为权限描述）
                }
            }
        }
        REQUIRE(saw_cfg_set);
        REQUIRE(decisions == 1);
        REQUIRE(saw_denied_envelope);
        REQUIRE(saw_denied_perm);
    }

    // ── 重启 recover：补丁依序回放 → 生效配置还原（同 ContentId）────
    {
        exec::TickWheel wheel2;
        session::SessionRuntime::Deps deps2{reg, *store, wheel2, 1000, {}};
        auto rt2 = session::SessionRuntime::recover(sid_of(kSess), deps2, 100000);
        REQUIRE(rt2.has_value());
        REQUIRE(rt2->effective().values.tau_base == 4.0f);
        REQUIRE(rt2->effective().content_id == rt->effective().content_id);
    }

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}
