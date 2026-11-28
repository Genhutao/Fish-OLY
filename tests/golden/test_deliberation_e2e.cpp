// 黄金场景：自治决策端到端（06 §2–3 M0 形态）。
//  场景 1：运行时全链 —— 用户点名 → 估分触发 → 提示板装配 → 网关（FakeProvider
//          脚本）→ speak + decision.recorded；后续无新消息 → 沉默不出事件。
//  场景 2：降级阶梯直查 —— 网关全失败 → 语气储备 ×5 → 沉默事件化。
//  场景 3：确定性闸门（13 §2）—— 同种子同输入 → 事件流逐字节一致。
#include <oly/core/ulid.hpp>
#include <oly/domain/domain.hpp>
#include <oly/events/envelope.hpp>
#include <oly/gateway/gateway.hpp>
#include <oly/logdb/store.hpp>
#include <oly/session/deliberation.hpp>
#include <oly/session/session_runtime.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <filesystem>
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
    for (const auto [id, name] : {std::pair{pid_of(kAlice), "苍月千鹤"}, std::pair{pid_of(kBob), "白昼·艾尔"}}) {
        domain::Principal p;
        p.id = id;
        p.kind = domain::PrincipalKind::Persona;
        p.owner = pid_of(kRoot);
        REQUIRE(reg.add_principal(p).has_value());
        REQUIRE(reg.add_persona(id, pid_of(kRoot), domain::CardBody{name, "锚", ""}).has_value());
    }
    domain::Session s;
    s.id = sid_of(kSess);
    s.owner = pid_of(kRoot);
    s.canon = "月下庭院。";
    s.members.push_back(domain::Member{s.owner, domain::SessionRole::Owner, 0});
    s.members.push_back(domain::Member{pid_of(kAlice), domain::SessionRole::Member, 0});
    s.members.push_back(domain::Member{pid_of(kBob), domain::SessionRole::Member, 0});
    REQUIRE(reg.create_session(s).has_value());
}

// 点名驱动（06 §2.2）：只留 mention 权重（显式零化其余项 —— NSDMI 默认值不是零）
// → 提到名字才发言：静默拍 attention ≤ ±0.15 < τ ≥ 0.3，永不越阈，事件流完全可控
session::DeliberationConfig mention_only() {
    session::DeliberationConfig cfg;
    session::AttentionWeights w{0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
    w.mention = 2.0f;
    cfg.weights = w;
    cfg.score.tau_base = 0.5f;
    cfg.score.hard_cooldown = 0;
    return cfg;
}

// 场景 1 的一整轮（场景 3 复用）：返回会话流全量事件
std::vector<events::EventEnvelope> run_scene(const std::filesystem::path& seg_dir) {
    domain::DomainRegistry reg;
    build_world(reg);
    auto store = logdb::SegmentStore::open(
        seg_dir, events::StreamRef{events::StreamKind::Session, sid_of(kSess).bytes()}, 0);
    REQUIRE(store.has_value());

    gateway::FakeProvider prov({"【千鹤】花开一瞬。", "【艾尔】火起一念。"});
    gateway::Gateway gw(prov);

    exec::TickWheel wheel;
    session::SessionRuntime::Deps deps{reg, *store, wheel, 1000, {}};
    auto rt = session::SessionRuntime::create(sid_of(kSess), deps, 0);
    REQUIRE(rt.has_value());

    const auto cfg = mention_only();
    rt->attach_autonomy(pid_of(kAlice),
                        std::make_unique<session::DeliberationDriver>(
                            pid_of(kAlice), gw, std::vector<std::string>{"……"}, cfg));
    rt->attach_autonomy(pid_of(kBob),
                        std::make_unique<session::DeliberationDriver>(
                            pid_of(kBob), gw, std::vector<std::string>{"……"}, cfg));

    session::Command user_cmd;
    user_cmd.id = CommandId::from_string("01ARZ3NDEKTSV4RRFFQ69G5G2Z").value_or(CommandId{});
    user_cmd.actor = pid_of(kRoot);
    user_cmd.action = session::CmdAction::SendMessage;
    user_cmd.text = "苍月千鹤、白昼·艾尔，开始吧。";
    REQUIRE(rt->enqueue(user_cmd) == session::SessionRuntime::Enqueue::Ok);
    (void)rt->drain(64);

    uint64_t now = 0;
    for (int t = 1; t <= 6; ++t) {   // 拍 1 双双应答；拍 2–6 无新消息 → 沉默
        now += 1000;
        (void)rt->advance_clock(now);
        (void)rt->drain(64);
    }

    auto all = store->read_range(0, 10000);
    REQUIRE(all.has_value());
    return *all;
}

}   // namespace

TEST_CASE("决策 e2e：点名 → 提示板 → 网关 → speak + decision.recorded", "[golden][deliberation]") {
    const auto dir = std::filesystem::temp_directory_path() /
                     ("oly-golden-dlbr-" + UlidFactory{}.next().str());
    const auto all = run_scene(dir / "seg");

    // 事件序列：created(0) → 用户(1) → decision.A(2) → decision.B(3) → 消息 A(4) → 消息 B(5)
    // decision 先于其派生命令的消息事实 —— 因果序（04：命令由决策事件派生）
    REQUIRE(all.size() == 6);
    REQUIRE(all[0].type == "session.created");
    REQUIRE(all[1].type == "message.appended");
    REQUIRE(all[2].type == "decision.recorded");
    REQUIRE(all[3].type == "decision.recorded");
    REQUIRE(all[4].type == "message.appended");
    REQUIRE(all[5].type == "message.appended");

    REQUIRE(all[2].tick == 1);
    REQUIRE(all[3].tick == 1);

    // 消息事实载荷：actor + text（用户 → 千鹤 → 艾尔）
    {
        codec::Decoder d(all[4].payload_bin);
        const auto actor = d.ulid();
        const auto text = d.text();
        REQUIRE(d.ok());
        REQUIRE(actor == pid_of(kAlice).bytes());
        REQUIRE(text == "【千鹤】花开一瞬。");
    }
    {
        codec::Decoder d(all[5].payload_bin);
        const auto actor = d.ulid();
        const auto text = d.text();
        REQUIRE(d.ok());
        REQUIRE(actor == pid_of(kBob).bytes());
        REQUIRE(text == "【艾尔】火起一念。");
    }

    // decision.recorded 载荷：persona, model, speak, fallback, tokens_in, tokens_out, att, tau
    {
        codec::Decoder d(all[2].payload_bin);
        const auto actor = d.ulid();
        const auto model = d.text();
        const auto speak = d.u8();
        const auto fallback = d.u8();
        const auto tokens_in = d.u32();
        /* tokens_out */ (void)d.u32();
        const auto att = static_cast<float>(d.u32()) / 10000.0f;
        const auto tau = static_cast<float>(d.u32()) / 10000.0f;
        REQUIRE(d.ok());
        REQUIRE(actor == pid_of(kAlice).bytes());
        REQUIRE(model == "fake");
        REQUIRE(speak == 1);
        REQUIRE(fallback == 0);
        REQUIRE(tokens_in > 0);
        REQUIRE(att >= Catch::Approx(2.0f).margin(0.3f));    // mention × 2 ± 噪声
        REQUIRE(tau == Catch::Approx(0.3f).margin(0.01f));   // τ = 0.5 × 0.6（冷场）
    }

    // 全流无 silence 事件（噪声 ≤ 0.15 恒低于 borderline 带）
    for (const auto& e : all) REQUIRE(e.type != "autonomy.silent");

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("决策降级阶梯：网关失败 → 语气储备 ×5 → 沉默事件化", "[golden][deliberation]") {
    domain::DomainRegistry reg;
    build_world(reg);
    const auto* sess = reg.session(sid_of(kSess));

    gateway::FakeProvider prov({}, 0);   // 从第 0 次起全失败
    gateway::Gateway gw(prov);

    session::DeliberationDriver driver(pid_of(kAlice), gw,
                                       {"储备1。", "储备2。", "储备3。", "储备4。", "储备5。"},
                                       mention_only());

    std::vector<session::PerceivedMessage> buf;
    const session::PerceivedMessage callout{1, pid_of(kRoot), "苍月千鹤，你在吗？"};
    buf.push_back(callout);

    for (int t = 1; t <= 5; ++t) {
        session::TickContext ctx;
        ctx.tick = static_cast<uint64_t>(t);
        ctx.stream_key = 7;
        ctx.registry = &reg;
        ctx.session = sess;
        ctx.new_messages = buf;
        const auto oc = driver.on_tick(ctx);
        CAPTURE(t);
        REQUIRE(oc.speak);
        REQUIRE(oc.fallback);
        REQUIRE(oc.model == "reserve");
        REQUIRE(oc.text == std::string{"储备"} + std::to_string(t) + "。");
    }

    // 第 6 次点名：储备耗尽 → 沉默事件化（06 §3.2 第二级）
    session::TickContext ctx;
    ctx.tick = 6;
    ctx.stream_key = 7;
    ctx.registry = &reg;
    ctx.session = sess;
    ctx.new_messages = buf;
    const auto oc = driver.on_tick(ctx);
    REQUIRE_FALSE(oc.speak);
    REQUIRE(oc.silence_noted);

    // 无点名：既不发言也不记沉默（噪声恒低于 τ）
    ctx.tick = 7;
    ctx.new_messages = {};
    const auto quiet = driver.on_tick(ctx);
    REQUIRE_FALSE(quiet.speak);
    REQUIRE_FALSE(quiet.silence_noted);

    REQUIRE(gw.counters().failed == 6);   // 每次点名都曾尝试网关
}

TEST_CASE("决策 e2e：确定性闸门 —— 同输入 → 事件流逐字节一致", "[golden][deliberation]") {
    const auto dir1 = std::filesystem::temp_directory_path() /
                      ("oly-golden-dlbr-a-" + UlidFactory{}.next().str());
    const auto dir2 = std::filesystem::temp_directory_path() /
                      ("oly-golden-dlbr-b-" + UlidFactory{}.next().str());
    const auto a = run_scene(dir1 / "seg");
    const auto b = run_scene(dir2 / "seg");

    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        REQUIRE(a[i].seq == b[i].seq);
        REQUIRE(a[i].tick == b[i].tick);
        REQUIRE(a[i].kind == b[i].kind);
        REQUIRE(a[i].type == b[i].type);
        REQUIRE(a[i].command == b[i].command);
        REQUIRE(a[i].actor == b[i].actor);
        REQUIRE(a[i].on_behalf_of == b[i].on_behalf_of);
        REQUIRE(a[i].payload_bin == b[i].payload_bin);
        REQUIRE(a[i].prev_hash == b[i].prev_hash);
        REQUIRE(a[i].hash == b[i].hash);   // 哈希链逐位一致
        REQUIRE(a[i].wall_ms == b[i].wall_ms);
    }

    std::error_code ec;
    std::filesystem::remove_all(dir1, ec);
    std::filesystem::remove_all(dir2, ec);
}
