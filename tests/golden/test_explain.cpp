// 黄金场景：行动解释（11 §5 / 10 §4.8 的内核形态）。
//   决策（发言）→ decision.recorded 事实 ↔ 驱动内 DecisionTrace 逐项一致：
//   attention/tau 与事件载荷同源；特征表/提示板清单/引用记忆溯源/生效配置
//   ContentId 全量可展开——审计、评测、客服、"记忆考古"共用同一份归因原料。
#include <oly/config/config.hpp>
#include <oly/core/id.hpp>
#include <oly/core/ulid.hpp>
#include <oly/domain/domain.hpp>
#include <oly/events/envelope.hpp>
#include <oly/exec/tick_wheel.hpp>
#include <oly/gateway/gateway.hpp>
#include <oly/logdb/store.hpp>
#include <oly/memory/memory.hpp>
#include <oly/memory/summarizer.hpp>
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

}   // namespace

TEST_CASE("行动解释 e2e：decision.recorded ↔ DecisionTrace 同源归因", "[golden][explain]") {
    const auto dir = std::filesystem::temp_directory_path() /
                     ("oly-golden-explain-" + UlidFactory{}.next().str());
    domain::DomainRegistry reg;
    {
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

    auto store = logdb::SegmentStore::open(
        dir, events::StreamRef{events::StreamKind::Session, sid_of(kSess).bytes()}, 0);
    REQUIRE(store.has_value());
    exec::TickWheel wheel;
    session::SessionRuntime::Deps deps{reg, *store, wheel, 1000, {}};
    auto rt = session::SessionRuntime::create(sid_of(kSess), deps, 0);
    REQUIRE(rt.has_value());

    // ── 驱动：点名触发 + 记忆召回 + 生效配置注入 ─────────────────
    memory::Store mem;
    {
        // 预置一条长期记忆（召回命中 → trace.cited_seqs 有料）
        memory::LongTermEntry e;
        e.owner = pid_of(kRoot);
        e.persona = pid_of(kAlice);
        e.session = sid_of(kSess);
        e.kind = memory::EntryKind::Summary;
        e.content = "月下锚点之约。";
        e.importance = 0.8;
        e.source_seqs = {3, 4, 5};
        e.valid = memory::TimeRange{1, ~0ull};   // 覆盖查询位点（at_seq=1）
        const std::vector<memory::LongTermEntry> batch{e};
        (void)mem.submit_candidates(batch);
        mem.publish();
    }

    gateway::FakeProvider prov({"【千鹤】锚点未忘。"});
    gateway::Gateway gw(prov);
    session::DeliberationConfig cfg;
    session::AttentionWeights w{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    w.mention = 2.0f;
    w.novelty = 0.4f;
    cfg.weights = w;
    cfg.score.tau_base = 0.5f;
    cfg.score.hard_cooldown = 0;
    cfg.memory = &mem;
    cfg.effective = &rt->effective();
    rt->attach_autonomy(
        pid_of(kAlice),
        std::make_unique<session::DeliberationDriver>(pid_of(kAlice), gw, std::vector<std::string>{}, cfg));

    // 点名 → 拍 1 发言
    {
        session::Command c;
        c.id = CommandId::from_bytes(UlidFactory{}.next().bytes());
        c.actor = pid_of(kRoot);
        c.action = session::CmdAction::SendMessage;
        c.text = "苍月千鹤，还记得月下之约吗？";
        REQUIRE(rt->enqueue(c) == session::SessionRuntime::Enqueue::Ok);
        (void)rt->drain(64);
    }
    (void)rt->on_tick(1000);
    (void)rt->drain(64);

    // ── 事件流：decision.recorded 载荷 ──────────────────────────
    std::optional<events::EventEnvelope> decision;
    {
        const auto all = store->read_range(0, 10000);
        REQUIRE(all.has_value());
        for (const auto& e : all.value()) {
            if (e.type == "decision.recorded") decision = e;
        }
        REQUIRE(decision.has_value());
    }

    // ── 驱动内 trace ↔ 事件载荷同源逐项比对 ─────────────────────
    const auto* drv = dynamic_cast<const session::DeliberationDriver*>(
        rt->autonomy_driver_of(pid_of(kAlice)));
    REQUIRE(drv != nullptr);
    REQUIRE(drv->last_trace().has_value());
    const auto& t = drv->last_trace().value();

    {
        codec::Decoder d(decision->payload_bin);
        const auto actor = d.ulid();
        const auto model = d.text();
        const auto speak = d.u8();
        /* fallback */ (void)d.u8();
        const auto tokens_in = d.u32();
        const auto tokens_out = d.u32();
        const auto att = static_cast<float>(d.u32()) / 10000.0f;
        const auto tau = static_cast<float>(d.u32()) / 10000.0f;
        const auto cid = d.content();
        REQUIRE(d.ok());

        REQUIRE(actor == pid_of(kAlice).bytes());
        REQUIRE(t.speak);
        REQUIRE(model == t.model);
        REQUIRE(speak == 1);
        REQUIRE(t.tokens_in == tokens_in);
        REQUIRE(t.tokens_out == tokens_out);
        REQUIRE(t.attention == Catch::Approx(att).margin(0.0001f));   // 同源
        REQUIRE(t.tau == Catch::Approx(tau).margin(0.0001f));
        REQUIRE(t.config_id == ContentId::from_bytes(cid));           // 生效配置归因
    }

    // 特征表 / 提示板清单 / 引用记忆溯源（11 §5 全量可展开）
    REQUIRE(t.tick == 1);
    REQUIRE(t.mention == Catch::Approx(1.0f));   // 被@
    REQUIRE(t.manifest.used[static_cast<std::size_t>(session::Slot::Memory)] > 0);
    REQUIRE_FALSE(t.cited_seqs.empty());
    REQUIRE(t.cited_seqs.front() == 3);   // 预置记忆的溯源起点

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}
