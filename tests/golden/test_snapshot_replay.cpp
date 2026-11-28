// 黄金场景（13 §1 M0 退出标准）：重启不丢世界（05 §5–6）。
//  阶段 A：装配 → 用户插话 → 12 拍 → 拍快照 → 再走 4 拍（快照落后 head，合法）；
//  阶段 B：换注册表 + 重开存储 → recover（快照 + 补缺口）→ 幂等集重建校验
//          → 续演至 30 拍收尾；与"无快照全量重放"逐条一致；
//  阶段 C：最新快照损坏 → 回退旧锚 → 恢复仍完整（尾部重放补齐）。
#include <oly/core/ulid.hpp>
#include <oly/domain/domain.hpp>
#include <oly/events/envelope.hpp>
#include <oly/logdb/store.hpp>
#include <oly/session/autonomy.hpp>
#include <oly/session/session_runtime.hpp>
#include <oly/session/snapshot.hpp>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
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

// 世界装配（各阶段独立重建；canon/成员属身份域，由调用方先行装配）
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
    s.canon = "月下庭院：青石铺地，桂影婆娑。";
    s.members.push_back(domain::Member{s.owner, domain::SessionRole::Owner, 0});
    s.members.push_back(domain::Member{pid_of(kAlice), domain::SessionRole::Member, 0});
    s.members.push_back(domain::Member{pid_of(kBob), domain::SessionRole::Member, 0});
    REQUIRE(reg.create_session(s).has_value());
}

std::vector<std::string> lines(const std::string& tag, const std::string& tail) {
    std::vector<std::string> out;
    for (int i = 0; i < 15; ++i) out.push_back("【" + tag + "】第" + std::to_string(i + 1) + tail);
    return out;
}

}   // namespace

TEST_CASE("黄金场景：快照 + 启动重放（重启不丢世界）", "[golden][snapshot]") {
    const auto dir = std::filesystem::temp_directory_path() /
                     ("oly-golden-snap-" + UlidFactory{}.next().str());
    const auto seg_dir = dir / "seg";
    const auto snap_dir = dir / "snap";
    constexpr uint64_t kPeriod = 1000;

    session::Command user_cmd;
    user_cmd.id = CommandId::from_string("01ARZ3NDEKTSV4RRFFQ69G5G2Z").value_or(CommandId{});
    user_cmd.actor = pid_of(kRoot);
    user_cmd.action = session::CmdAction::SendMessage;
    user_cmd.text = "（用户插话）两位继续。";

    // ── 阶段 A：创建 → 12 拍 → 快照 → 再 4 拍 ───────────────────────
    std::vector<std::pair<uint64_t, std::string>> phase_a_messages;
    {
        domain::DomainRegistry reg;
        build_world(reg);
        auto store = logdb::SegmentStore::open(
            seg_dir, events::StreamRef{events::StreamKind::Session, sid_of(kSess).bytes()}, 0);
        REQUIRE(store.has_value());

        exec::TickWheel wheel_a;
        session::SessionRuntime::Deps deps{reg, *store, wheel_a, kPeriod, snap_dir};
        auto rt = session::SessionRuntime::create(sid_of(kSess), deps, 0);
        REQUIRE(rt.has_value());

        rt->attach_autonomy(pid_of(kAlice),
                            std::make_unique<session::ScriptedDriver>(lines("千鹤", "夜：月色未变。"), 2, 0));
        rt->attach_autonomy(pid_of(kBob),
                            std::make_unique<session::ScriptedDriver>(lines("艾尔", "夜：光在燃烧！"), 2, 1));

        REQUIRE(rt->enqueue(user_cmd) == session::SessionRuntime::Enqueue::Ok);
        (void)rt->drain(64);

        uint64_t now = 0;
        for (int t = 1; t <= 12; ++t) {
            now += kPeriod;
            (void)rt->advance_clock(now);
            (void)rt->drain(64);
        }
        REQUIRE(rt->ticks() == 12);

        // 快照锚：seq=13（created+用户+12 消息），落后于未来 head 永远合法
        const auto anchor = rt->snapshot_now();
        REQUIRE(anchor.has_value());
        REQUIRE_FALSE(anchor->is_nil());
        auto snap = session::load_latest_snapshot(snap_dir);
        REQUIRE(snap.has_value());
        REQUIRE(snap->has_value());
        REQUIRE(snap->value().seq == 13);
        REQUIRE(snap->value().messages.size() == 13);
        REQUIRE(snap->value().ticks == 12);

        for (int t = 13; t <= 16; ++t) {
            now += kPeriod;
            (void)rt->advance_clock(now);
            (void)rt->drain(64);
        }
        phase_a_messages = reg.session(sid_of(kSess))->messages;
        REQUIRE(phase_a_messages.size() == 17);
        REQUIRE(store->committed_seq() == 17);
    }   // store/runtime 释放（文件锁）

    // ── 阶段 B：recover（快照 + 补缺口）→ 幂等集 → 续演至 30 拍 ──────
    std::vector<std::pair<uint64_t, std::string>> phase_b_messages;
    {
        domain::DomainRegistry reg;
        build_world(reg);
        auto store = logdb::SegmentStore::open(
            seg_dir, events::StreamRef{events::StreamKind::Session, sid_of(kSess).bytes()}, 0);
        REQUIRE(store.has_value());
        REQUIRE(store->committed_seq() == 17);   // 续链重建游标

        exec::TickWheel wheel_b;
        session::SessionRuntime::Deps deps{reg, *store, wheel_b, kPeriod, snap_dir};
        auto rt = session::SessionRuntime::recover(sid_of(kSess), deps, 16000);
        REQUIRE(rt.has_value());
        REQUIRE(rt->ticks() == 16);
        REQUIRE(reg.session(sid_of(kSess))->messages == phase_a_messages);   // 快照+重放 == 停机前

        // 幂等集跨重启：停机前的命令 ID 仍被识别（04 §5 恰一次）
        REQUIRE(rt->enqueue(user_cmd) == session::SessionRuntime::Enqueue::Duplicate);

        // 脚本驱动续演：进度由 tick 确定性推导（05 §5：驱动状态不进快照）
        rt->attach_autonomy(
            pid_of(kAlice), std::make_unique<session::ScriptedDriver>(lines("千鹤", "夜：月色未变。"), 2, 0, 16));
        rt->attach_autonomy(
            pid_of(kBob), std::make_unique<session::ScriptedDriver>(lines("艾尔", "夜：光在燃烧！"), 2, 1, 16));

        uint64_t now = 16000;
        for (int t = 17; t <= 30; ++t) {
            now += kPeriod;
            (void)rt->advance_clock(now);
            (void)rt->drain(64);
        }
        phase_b_messages = reg.session(sid_of(kSess))->messages;
        REQUIRE(phase_b_messages.size() == 31);   // 用户 + 30 轮
        REQUIRE(rt->ticks() == 30);
        const auto final_anchor = rt->snapshot_now();
        REQUIRE(final_anchor.has_value());   // 收尾再拍一版（seq=31）
    }

    // ── 全量读回：链完整（created + 用户 + 30 消息 = 32 条）──────────
    {
        auto store = logdb::SegmentStore::open(
            seg_dir, events::StreamRef{events::StreamKind::Session, sid_of(kSess).bytes()}, 0);
        REQUIRE(store.has_value());
        const auto all = store->read_range(0, 10000);
        REQUIRE(all.has_value());
        REQUIRE(all->size() == 32);
        REQUIRE(all->back().seq == 31);   // 链尾 = 最后一条消息事实
    }

    // ── 阶段 B2：无快照全量重放 == 快照路径结果 ─────────────────────
    {
        domain::DomainRegistry reg;
        build_world(reg);
        auto store = logdb::SegmentStore::open(
            seg_dir, events::StreamRef{events::StreamKind::Session, sid_of(kSess).bytes()}, 0);
        REQUIRE(store.has_value());
        exec::TickWheel wheel_c;
        session::SessionRuntime::Deps deps{reg, *store, wheel_c, kPeriod, {}};   // 快照禁用
        const auto rt = session::SessionRuntime::recover(sid_of(kSess), deps, 30000);
        REQUIRE(rt.has_value());
        REQUIRE(rt->ticks() == 30);
        REQUIRE(reg.session(sid_of(kSess))->messages == phase_b_messages);
    }

    // ── 阶段 C：最新快照损坏 → 回退旧锚（seq=13）→ 恢复仍完整 ────────
    {
        const auto newest = snap_dir / "snap-0000000000000031.bin";
        {
            std::fstream f(newest, std::ios::binary | std::ios::in | std::ios::out);
            REQUIRE(f.is_open());
            f.seekg(60);   // 落在 body 区（首条消息之后）
            char c;
            f.get(c);
            f.seekp(60);
            f.put(static_cast<char>(c ^ 0xFF));
        }
        auto snap = session::load_latest_snapshot(snap_dir);
        REQUIRE(snap.has_value());
        REQUIRE(snap->has_value());
        REQUIRE(snap->value().seq == 13);   // 回退旧锚

        domain::DomainRegistry reg;
        build_world(reg);
        auto store = logdb::SegmentStore::open(
            seg_dir, events::StreamRef{events::StreamKind::Session, sid_of(kSess).bytes()}, 0);
        REQUIRE(store.has_value());
        exec::TickWheel wheel_d;
        session::SessionRuntime::Deps deps{reg, *store, wheel_d, kPeriod, snap_dir};
        const auto rt = session::SessionRuntime::recover(sid_of(kSess), deps, 40000);
        REQUIRE(rt.has_value());
        REQUIRE(rt->ticks() == 30);   // 尾部重放补齐
        REQUIRE(reg.session(sid_of(kSess))->messages == phase_b_messages);
    }

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}
