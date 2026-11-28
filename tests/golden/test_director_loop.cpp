// 黄金场景：导演控制 + 标记式撤回（11 §1–4、04 §2 M1-4 形态）。
//   pause(autonomy/full)/resume/step（轮式摘槽-回填-手动投拍）→ inject 走正常
//   感知（不伪装角色发言）→ message.recall（标记式，不删除）→ rollback
//   （投影层时间线重置：superseded 保留可审计、seq 单调不复用、recover 还原）。
#include <oly/codec/codec.hpp>
#include <oly/core/id.hpp>
#include <oly/core/ulid.hpp>
#include <oly/domain/domain.hpp>
#include <oly/events/envelope.hpp>
#include <oly/exec/tick_wheel.hpp>
#include <oly/gateway/gateway.hpp>
#include <oly/logdb/store.hpp>
#include <oly/session/autonomy.hpp>
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
constexpr const char* kBob = "01ARZ3NDEKTSV4RRFFQ69G5FCX";   // 普通成员
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

session::Command user_say(const std::string& text) {
    session::Command c;
    c.id = CommandId::from_bytes(UlidFactory{}.next().bytes());
    c.actor = pid_of(kRoot);
    c.action = session::CmdAction::SendMessage;
    c.text = text;
    return c;
}

session::Command director(session::CmdAction action, const domain::PrincipalId actor,
                          const std::string& text = {}, const uint64_t n = 0,
                          const std::string& kind = {}) {
    session::Command c;
    c.id = CommandId::from_bytes(UlidFactory{}.next().bytes());
    c.actor = actor;
    c.action = action;
    c.text = text;
    c.read_seq = n;
    c.director_kind = kind;
    return c;
}

bool has_type(const std::vector<events::EventEnvelope>& all, const std::string& type) {
    for (const auto& e : all) {
        if (e.type == type) return true;
    }
    return false;
}

}   // namespace

TEST_CASE("导演/撤回 e2e：pause/step/resume + inject + recall + rollback", "[golden][director]") {
    const auto dir = std::filesystem::temp_directory_path() /
                     ("oly-golden-dir-" + UlidFactory{}.next().str());
    domain::DomainRegistry reg;
    build_world(reg);
    auto store = logdb::SegmentStore::open(
        dir, events::StreamRef{events::StreamKind::Session, sid_of(kSess).bytes()}, 0);
    REQUIRE(store.has_value());
    exec::TickWheel wheel;
    session::SessionRuntime::Deps deps{reg, *store, wheel, 1000, {}};
    auto rt = session::SessionRuntime::create(sid_of(kSess), deps, 0);
    REQUIRE(rt.has_value());

    std::vector<std::string> lines;
    for (int i = 1; i <= 20; ++i) lines.push_back("【千鹤】夜话" + std::to_string(i) + "。");
    rt->attach_autonomy(pid_of(kAlice), std::make_unique<session::ScriptedDriver>(lines, 1, 0));

    const auto speech_count = [&] {
        std::size_t n = 0;
        for (const auto& [seq, text] : reg.session(sid_of(kSess))->messages) {
            (void)seq;
            if (text.rfind("【千鹤】", 0) == 0) ++n;
        }
        return n;
    };

    // ── 基线：拍 1 自治发言（拍内命令由随后的 drain 落地）─────────
    REQUIRE(rt->advance_clock(1000) > 0);
    (void)rt->drain(64);
    REQUIRE(speech_count() == 1);

    // ── pause(autonomy)：拍进但不再自续（交互准入不变）───────────
    REQUIRE(rt->enqueue(director(session::CmdAction::DirectorPause, pid_of(kRoot), "autonomy")) ==
            session::SessionRuntime::Enqueue::Ok);
    (void)rt->drain(64);
    REQUIRE(rt->paused());
    REQUIRE(rt->advance_clock(2000) > 0);   // 已登记的最后一拍仍执行（脚本第 2 句）
    (void)rt->drain(64);
    REQUIRE(speech_count() == 2);
    REQUIRE(rt->advance_clock(3000) == 0);  // 槽位已摘除：不再自续
    REQUIRE(speech_count() == 2);
    REQUIRE(rt->enqueue(user_say("（暂停中）我还能说话。")) ==
            session::SessionRuntime::Enqueue::Ok);   // 交互准入不变（11 §2）
    (void)rt->drain(64);

    // ── step(2)：手动投拍（paused 下可用；拍完维持暂停）──────────
    REQUIRE(rt->enqueue(director(session::CmdAction::DirectorStep, pid_of(kRoot), {}, 2)) ==
            session::SessionRuntime::Enqueue::Ok);
    (void)rt->drain(64);
    (void)rt->drain(64);   // step 拍内入队的外发命令随下一批落地
    REQUIRE(rt->paused());
    REQUIRE(speech_count() == 4);   // 手动两拍，脚本第 3、4 句

    // ── resume：槽位回填，不补拍 ────────────────────────────────
    REQUIRE(rt->enqueue(director(session::CmdAction::DirectorResume, pid_of(kRoot))) ==
            session::SessionRuntime::Enqueue::Ok);
    (void)rt->drain(64);
    REQUIRE_FALSE(rt->paused());
    REQUIRE(rt->advance_clock(44000) > 0);   // 回填首拍（脚本第 5 句）
    (void)rt->drain(64);
    REQUIRE(speech_count() == 5);

    // ── inject：走正常感知，不伪装角色发言（source=director 独立事实）──
    REQUIRE(rt->enqueue(director(session::CmdAction::DirectorInject, pid_of(kRoot),
                                 "（导演旁白）夜幕落下，庭中起了雾。", 0, "narration")) ==
            session::SessionRuntime::Enqueue::Ok);
    (void)rt->drain(64);
    {
        bool injected_visible = false;
        for (const auto& [seq, text] : reg.session(sid_of(kSess))->messages) {
            if (text.find("夜幕落下") != std::string::npos) injected_visible = true;
        }
        REQUIRE(injected_visible);
        const auto all = store->read_range(0, 10000);
        REQUIRE(all.has_value());
        REQUIRE(has_type(all.value(), "director.injected"));
    }

    // ── message.recall：标记式撤回（不删除）─────────────────────
    {
        const auto& msgs = reg.session(sid_of(kSess))->messages;
        REQUIRE(msgs.size() >= 2);
        const auto recall_seq = msgs.front().first;   // 第一条用户消息
        REQUIRE(rt->enqueue(director(session::CmdAction::RecallMessage, pid_of(kRoot), {}, recall_seq)) ==
                session::SessionRuntime::Enqueue::Ok);
        (void)rt->drain(64);
        REQUIRE(rt->recalled_seqs().contains(recall_seq));   // 标记保留（事实仍在流中）

        // 普通成员撤他人消息 → denial（Admin+，04 §2）
        REQUIRE(rt->enqueue(director(session::CmdAction::RecallMessage, pid_of(kBob), {}, recall_seq)) ==
                session::SessionRuntime::Enqueue::Ok);
        (void)rt->drain(64);
        const auto all = store->read_range(0, 10000);
        REQUIRE(all.has_value());
        REQUIRE(has_type(all.value(), "message.recalled"));
        REQUIRE(has_type(all.value(), "cmd.denied"));
    }

    // ── rollback：投影层时间线重置（superseded 保留可审计）───────
    constexpr uint64_t kTarget = 2;
    REQUIRE(rt->enqueue(director(session::CmdAction::DirectorRollback, pid_of(kRoot), {}, kTarget)) ==
            session::SessionRuntime::Enqueue::Ok);
    (void)rt->drain(64);
    {
        REQUIRE(rt->superseded_from() == kTarget + 1);   // 有效时间线 = rolled_back 之后
        const auto& msgs = reg.session(sid_of(kSess))->messages;
        for (const auto& [seq, text] : msgs) REQUIRE(seq <= kTarget);   // 投影回退
        const uint64_t last_retained = msgs.back().first;
        // 新事件继续追加，seq 单调不复用
        REQUIRE(rt->enqueue(user_say("（回滚后）新的开始。")) ==
                session::SessionRuntime::Enqueue::Ok);
        (void)rt->drain(64);
        const auto& after = reg.session(sid_of(kSess))->messages;   // 同一投影（引用）
        REQUIRE(after.size() == msgs.size());
        REQUIRE(after.back().first > last_retained);   // seq 单调不复用（≥ 头部续接）
        REQUIRE(after.back().first >= rt->superseded_from());

        const auto all = store->read_range(0, 10000);
        REQUIRE(all.has_value());
        REQUIRE(has_type(all.value(), "session.rolled_back"));
        // superseded 区间事件保留在日志中（append-only 纪律，可审计可考古）
        bool superseded_kept = false;
        for (const auto& e : all.value()) {
            if (e.seq >= kTarget + 1 && e.seq < after.back().first) superseded_kept = true;
        }
        REQUIRE(superseded_kept);
    }

    // ── recover：投影按有效时间线还原（含撤回标记与回滚水位）──────
    {
        exec::TickWheel wheel2;
        session::SessionRuntime::Deps deps2{reg, *store, wheel2, 1000, {}};
        auto rt2 = session::SessionRuntime::recover(sid_of(kSess), deps2, 100000);
        REQUIRE(rt2.has_value());
        REQUIRE(rt2->superseded_from() == kTarget + 1);
        for (const auto& [seq, text] : reg.session(sid_of(kSess))->messages) {
            REQUIRE((seq <= kTarget || text == "（回滚后）新的开始。" ||
                     text.find("夜幕落下") != std::string::npos));
        }
        const auto& live = reg.session(sid_of(kSess))->messages;
        REQUIRE(rt2->session().messages.size() == live.size());
        REQUIRE(rt2->superseded_from() == rt->superseded_from());
    }

    // ── pause(full)：只读冻结——用户命令拒绝写 denial ─────────────
    {
        REQUIRE(rt->enqueue(director(session::CmdAction::DirectorPause, pid_of(kRoot), "full")) ==
                session::SessionRuntime::Enqueue::Ok);
        (void)rt->drain(64);
        REQUIRE(rt->frozen());
        REQUIRE(rt->enqueue(user_say("冻结中")) == session::SessionRuntime::Enqueue::Ok);
        (void)rt->drain(64);
        const auto all = store->read_range(0, 10000);
        REQUIRE(all.has_value());
        bool frozen_reject = false;
        for (const auto& e : all.value()) {
            if (e.type != "cmd.denied") continue;
            codec::Decoder d(e.payload_bin);
            while (d.ok()) {
                if (d.text() == "director:frozen") frozen_reject = true;
            }
        }
        REQUIRE(frozen_reject);
        REQUIRE(rt->enqueue(director(session::CmdAction::DirectorResume, pid_of(kRoot))) ==
                session::SessionRuntime::Enqueue::Ok);
        (void)rt->drain(64);
        REQUIRE_FALSE(rt->frozen());
    }

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}
