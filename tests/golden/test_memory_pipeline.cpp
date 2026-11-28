// 黄金场景：记忆管线端到端（02 §5 / 03 §6 / 06 §3.0–4 M1-1 形态）。
//   真会话流（10 条用户消息）→ 解码 message.appended → 确定性批化归纳（每 5 条一册）
//   → 覆盖（edit + pin）→ 重建协议（人工劳动永不丢失）→ 召回接入决策驱动：
//   Memory 槽真实进入提示板（board_manifest 可断言）；无召回源时槽缺席（M0 形态不变）。
#include <oly/core/ulid.hpp>
#include <oly/domain/domain.hpp>
#include <oly/events/envelope.hpp>
#include <oly/exec/tick_wheel.hpp>
#include <oly/gateway/gateway.hpp>
#include <oly/logdb/store.hpp>
#include <oly/memory/memory.hpp>
#include <oly/memory/summarizer.hpp>
#include <oly/session/deliberation.hpp>
#include <oly/session/prompt_board.hpp>
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
constexpr const char* kSess = "01ARZ3NDEKTSV4RRFFQ69G5FDY";

// 提到名字才发言（同决策 e2e 的静音装配；NSDMI 默认值不是零——显式零化）
session::DeliberationConfig mention_only(memory::Store* mem) {
    session::DeliberationConfig cfg;
    session::AttentionWeights w{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    w.mention = 2.0f;
    cfg.weights = w;
    cfg.score.tau_base = 0.5f;
    cfg.score.hard_cooldown = 0;
    cfg.memory = mem;
    cfg.recall_limit = 4;
    return cfg;
}

}   // namespace

TEST_CASE("记忆管线 e2e：会话流 → 归纳 → 覆盖 → 重建 → 决策召回", "[golden][memory]") {
    const auto dir = std::filesystem::temp_directory_path() /
                     ("oly-golden-mem-" + UlidFactory{}.next().str());

    // ── 世界装配 + 真会话流：用户连发 10 条（每 5 条折叠一册）────
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
        REQUIRE(reg.add_persona(p.id, pid_of(kRoot), domain::CardBody{"苍月千鹤", "锚", ""})
                    .has_value());
        domain::Session s;
        s.id = sid_of(kSess);
        s.owner = pid_of(kRoot);
        s.canon = "月下庭院。";
        s.members.push_back(domain::Member{s.owner, domain::SessionRole::Owner, 0});
        s.members.push_back(domain::Member{pid_of(kAlice), domain::SessionRole::Member, 0});
        REQUIRE(reg.create_session(s).has_value());
    }
    {
        auto store = logdb::SegmentStore::open(
            dir / "seg", events::StreamRef{events::StreamKind::Session, sid_of(kSess).bytes()},
            0);
        REQUIRE(store.has_value());
        exec::TickWheel wheel;
        session::SessionRuntime::Deps deps{reg, *store, wheel, 1000, {}};
        auto rt = session::SessionRuntime::create(sid_of(kSess), deps, 0);
        REQUIRE(rt.has_value());

        UlidFactory ulids;
        for (int i = 0; i < 10; ++i) {
            session::Command c;
            c.id = CommandId::from_bytes(ulids.next().bytes());
            c.actor = pid_of(kRoot);
            c.action = session::CmdAction::SendMessage;
            c.text = "夜话" + std::to_string(i + 1) + "：庭中桂影又深了一分，锚点未改。";
            REQUIRE(rt->enqueue(c) == session::SessionRuntime::Enqueue::Ok);
            (void)rt->drain(64);
        }
    }

    // ── 归纳输入 = 读端走查（05 §6 同源）：解码 message.appended ──
    auto store = logdb::SegmentStore::open(
        dir / "seg", events::StreamRef{events::StreamKind::Session, sid_of(kSess).bytes()}, 0);
    REQUIRE(store.has_value());
    const auto all = store->read_range(0, 10000);
    REQUIRE(all.has_value());
    REQUIRE(all->size() == 11);   // created + 10 消息

    std::vector<std::string> texts;   // 归纳输入的文本驻留（FoldedMessage 持 view）
    std::vector<memory::FoldedMessage> msgs;
    for (const auto& e : all.value()) {
        if (e.type != "message.appended") continue;
        codec::Decoder d(e.payload_bin);
        const auto actor = d.ulid();
        texts.push_back(d.text());
        REQUIRE(d.ok());
        msgs.push_back({e.seq, domain::PrincipalId::from_bytes(actor), texts.back()});
    }
    REQUIRE(msgs.size() == 10);

    // ── 确定性归纳：每 5 条一册 → 2 候选；同输入重跑逐字节一致 ──
    memory::SummarizerConfig fcfg;
    fcfg.window = 5;
    fcfg.max_content = 200;
    const auto c1 = memory::fold_messages(sid_of(kSess), pid_of(kRoot), pid_of(kAlice), 1, 0,
                                          fcfg, msgs);
    const auto c2 = memory::fold_messages(sid_of(kSess), pid_of(kRoot), pid_of(kAlice), 1, 0,
                                          fcfg, msgs);
    REQUIRE(c1.size() == 2);
    REQUIRE(c1.size() == c2.size());
    for (std::size_t i = 0; i < c1.size(); ++i) {
        REQUIRE(memory::fingerprint(c1[i]) == memory::fingerprint(c2[i]));
    }

    // ── 记忆册：候选入册 → 覆盖（edit + pin）→ 发布 ─────────────
    memory::Store st;
    REQUIRE(st.submit_candidates(c1) == 2);
    st.publish();

    const auto fp0 = memory::fingerprint(c1[0]);
    const auto fp1 = memory::fingerprint(c1[1]);
    {
        memory::Review r;
        r.op = memory::ReviewOp::Edit;
        r.by = pid_of(kRoot);
        r.content = "庭前十句夜话，锚点之约未改。（主人修订）";
        REQUIRE(st.review(fp0, r).has_value());
        r.op = memory::ReviewOp::PinImportance;
        r.content.clear();
        r.importance = 0.9;
        REQUIRE(st.review(fp1, r).has_value());
    }
    st.publish();

    // 召回规则（02 §5.3）：scope 过滤 + 时效 + 打分；provenance 随条目
    {
        const PrincipalId in_room[] = {pid_of(kRoot), pid_of(kAlice)};
        std::vector<memory::Recalled> hits;
        st.view()->recall(pid_of(kAlice), in_room, "锚点 夜话", 99, 10, hits);
        REQUIRE(hits.size() == 2);
        REQUIRE(hits[0].importance == 0.9);   // pin 0.9 → 后窗居首
        REQUIRE(hits[0].source_seqs.size() == 5);
        REQUIRE(hits[1].content == "庭前十句夜话，锚点之约未改。（主人修订）");
        REQUIRE(hits[1].reviewed);
    }

    // ── 重建协议：同候选 + 覆盖 → 生效视图一致（人工劳动永不丢失）──
    {
        const std::vector<std::pair<ContentId, memory::Review>> overlays{
            {fp0,
             {memory::ReviewOp::Edit, pid_of(kRoot),
              "庭前十句夜话，锚点之约未改。（主人修订）", {}, 0.0}},
            {fp1, {memory::ReviewOp::PinImportance, pid_of(kRoot), {}, {}, 0.9}}};
        const auto reb = memory::Store::rebuild(c1, overlays);
        const PrincipalId in_room[] = {pid_of(kRoot), pid_of(kAlice)};
        std::vector<memory::Recalled> hits;
        reb->view()->recall(pid_of(kAlice), in_room, "锚点 夜话", 99, 10, hits);
        REQUIRE(hits.size() == 2);
        REQUIRE(hits[0].importance == 0.9);
        REQUIRE(hits[1].content == "庭前十句夜话，锚点之约未改。（主人修订）");
    }

    // ── 决策接入：Memory 槽真实进入提示板（board_manifest 可断言）──
    {
        gateway::FakeProvider prov({"【千鹤】锚点未忘。"});
        gateway::Gateway gw(prov);
        const auto* sess = reg.session(sid_of(kSess));
        REQUIRE(sess != nullptr);

        session::DeliberationDriver driver(pid_of(kAlice), gw, {}, mention_only(&st));
        const std::string first_text{msgs.front().text};
        const std::string last_text = "苍月千鹤，还记得锚点之约吗？";
        std::vector<session::PerceivedMessage> win;
        win.push_back({msgs.front().seq, pid_of(kRoot), first_text});
        win.push_back({msgs.back().seq, pid_of(kRoot), last_text});
        std::vector<session::PerceivedMessage> tail{win.back()};

        session::TickContext ctx;
        ctx.tick = 1;
        ctx.stream_key = 7;
        ctx.registry = &reg;
        ctx.session = sess;
        ctx.window = win;
        ctx.new_messages = tail;
        const auto oc = driver.on_tick(ctx);
        REQUIRE(oc.speak);
        REQUIRE(oc.text == "【千鹤】锚点未忘。");
        const auto& man = driver.last_manifest();
        REQUIRE(man.used[static_cast<std::size_t>(session::Slot::Memory)] > 0);   // 召回已装配

        // 无召回源（M0 形态）：Memory 槽缺席，其余行为不变
        session::DeliberationDriver bare(pid_of(kAlice), gw, {}, mention_only(nullptr));
        const auto bare_oc = bare.on_tick(ctx);
        REQUIRE(bare_oc.speak);
        REQUIRE(bare.last_manifest().used[static_cast<std::size_t>(session::Slot::Memory)] == 0);
    }

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}
