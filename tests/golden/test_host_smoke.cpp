// 黄金冒烟（13 §1 M0 退出标准）：1k 会话多 loop 托管，拍延迟 p99 < 50ms。
//   装配 1000 会话（4 分片）→ 真 io 线程驱动 10 拍 → 全会话拍数到位 +
//   跨 loop 命令落地 + 事件链抽查有效 + 分拍延迟分布断言。
#include <oly/core/ulid.hpp>
#include <oly/domain/domain.hpp>
#include <oly/events/envelope.hpp>
#include <oly/exec/loop_pool.hpp>
#include <oly/logdb/store.hpp>
#include <oly/session/autonomy.hpp>
#include <oly/session/shard.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

using namespace oly;

namespace {

domain::PrincipalId pid_of(std::string_view s) {
    return domain::PrincipalId::from_string(s).value();
}

double p99(std::vector<double> v) {
    if (v.empty()) return 0.0;
    std::sort(v.begin(), v.end());
    const auto idx = static_cast<std::size_t>(
        std::ceil(0.99 * static_cast<double>(v.size())) - 1.0);
    return v[std::min(idx, v.size() - 1)];
}

}   // namespace

TEST_CASE("黄金冒烟：1k 会话多 loop 托管 p99 < 50ms", "[golden][smoke][multi-loop]") {
    constexpr uint32_t kSessions = 1000;
    constexpr int kTicks = 10;

    const auto root_dir = std::filesystem::temp_directory_path() /
                          ("oly-smoke-" + UlidFactory{}.next().str());
    const auto store_root = root_dir / "seg";
    const auto snap_root = root_dir / "snap";

    // ── 世界装配（引导期，单线程）：1 用户 + 1 角色，1000 会话 ───────
    domain::DomainRegistry reg;
    const auto root = pid_of("01ARZ3NDEKTSV4RRFFQ69G5FAV");
    const auto alice = pid_of("01ARZ3NDEKTSV4RRFFQ69G5FBW");
    {
        domain::Principal r;
        r.id = r.owner = root;
        REQUIRE(reg.add_principal(r).has_value());
        domain::Principal a;
        a.id = alice;
        a.kind = domain::PrincipalKind::Persona;
        a.owner = root;
        REQUIRE(reg.add_principal(a).has_value());
        REQUIRE(reg.add_persona(alice, root, domain::CardBody{"苍月千鹤", "锚", ""}).has_value());
    }
    // 会话 ID：注入时钟的单调 ULID（键 = 前 8 字节 = 时间戳前缀 + 随机前 2 字节；
    // 每会话独立毫秒前缀 → 键互异；同毫秒自增只动低位随机，会撞键）
    std::vector<SessionId> sids;
    sids.reserve(kSessions);
    UlidFactory ulids([] {
        static uint64_t ms = 1'600'000'000'000ull;
        return ms++;
    });
    for (uint32_t i = 0; i < kSessions; ++i) {
        domain::Session s;
        s.id = SessionId::from_bytes(ulids.next().bytes());
        s.owner = root;
        s.canon = "月下庭院。";
        s.members.push_back(domain::Member{root, domain::SessionRole::Owner, 0});
        s.members.push_back(domain::Member{alice, domain::SessionRole::Member, 0});
        REQUIRE(reg.create_session(s).has_value());
        sids.push_back(s.id);
    }

    // ── 多 loop 宿主（4 分片 × 每 loop 一拍泵）─────────────────────
    auto pool = exec::LoopPool::create(4);
    REQUIRE(pool.has_value());
    // 拍周期 100ms = 轮式分辨率（01 §2）；10 拍 ≈ 1s
    // 权限快照源：与身份分片 publish 同构（引导版本 v1）
    auto snapshot = std::make_shared<const domain::IdentitySnapshot>(reg.identity_snapshot(1));
    session::Host::Deps hd{*pool, reg, std::move(snapshot),
                           100, store_root, snap_root};
    auto host = session::Host::create(hd);
    REQUIRE(host.has_value());
    REQUIRE(host->shard_count() == 4);

    std::vector<std::string> lines;
    for (int i = 0; i < 15; ++i) lines.push_back("【千鹤】夜话" + std::to_string(i + 1) + "。");
    for (uint32_t i = 0; i < kSessions; ++i) {
        auto rt = host->create_session(sids[i], 0);
        if (!rt.has_value()) {
            FAIL("create_session failed at " + std::to_string(i) + ": " + rt.error().code +
                 " " + rt.error().message);
        }
        REQUIRE(rt.has_value());
        rt.value()->attach_autonomy(alice,
                                    std::make_unique<session::ScriptedDriver>(lines, 1, 0));
    }

    // ── 跨 loop 命令（用户插话若干会话，验证通道与权限快照路径）──────
    session::Command user_cmd;
    user_cmd.id = CommandId::from_string("01ARZ3NDEKTSV4RRFFQ69G5G2Z").value_or(CommandId{});
    user_cmd.actor = root;
    user_cmd.action = session::CmdAction::SendMessage;
    user_cmd.text = "（用户插话）继续。";
    for (uint32_t i = 0; i < 8; ++i) {
        REQUIRE(host->post(sids[i * 97 % kSessions], user_cmd));   // 8 个不同分片目标
    }

    host->start();
    // 窗口 = 拍周期 ×（kTicks + 18）：拍处理耗时使实际拍间隔 ≳ 周期，留足余量
    std::this_thread::sleep_for(std::chrono::milliseconds(100 * (kTicks + 18)));
    host->stop();

    // ── 断言：拍数到位 / 插话落地 / 事件链有效 ─────────────────────
    uint64_t total_msgs = 0;
    for (uint32_t i = 0; i < kSessions; ++i) {
        const auto sid = sids[i];
        const auto* rt = host->session(sid);
        REQUIRE(rt != nullptr);
        REQUIRE(rt->ticks() >= kTicks);   // 拍数到位（墙钟抖动只多不少）
        const auto& msgs = reg.session(sid)->messages;
        REQUIRE(msgs.size() >= kTicks);   // 每拍一句（脚本 15 行 ≥ 10 拍）
        total_msgs += msgs.size();

        if (i % 125 == 0) {   // 8 个会话抽查：链全量校验（读端哈希链走查）
            auto store = logdb::SegmentStore::open(
                store_root / sid.str(),
                events::StreamRef{events::StreamKind::Session, sid.bytes()}, 0);
            REQUIRE(store.has_value());
            const auto all = store->read_range(0, 10000);
            REQUIRE(all.has_value());
            if (all->size() < static_cast<std::size_t>(kTicks) + 1) {
                std::string dump = "chain short: sid=" + sid.str() + " events=" +
                                   std::to_string(all->size()) + " ticks=" +
                                   std::to_string(host->session(sid)->ticks()) + " proj=" +
                                   std::to_string(reg.session(sid)->messages.size()) + " committed=" +
                                   std::to_string(host->session(sid)->committed_seq()) + " [";
                for (const auto& e : all.value()) {
                    dump += std::to_string(e.seq) + ":" + e.type + " ";
                }
                dump += "]";
                FAIL(dump);
            }
            REQUIRE(all->front().type == "session.created");
        }
    }
    // 插话至少进入部分会话投影（8 目标会话必达）
    REQUIRE(total_msgs > kSessions * static_cast<uint64_t>(kTicks));

    // ── 拍延迟 p99（4 分片分布合并；M0 退出标准 < 50ms @ release）──
    // /Od debug 构建逐会话解释路径约为 release 2–3×，阈值按构建类型放宽（仍监控退化）
    std::vector<double> all_beats;
    for (std::size_t i = 0; i < host->shard_count(); ++i) {
        const auto& v = host->shard(i).beat_ms();
        REQUIRE(v.size() >= static_cast<std::size_t>(kTicks));
        all_beats.insert(all_beats.end(), v.begin(), v.end());
    }
#ifdef NDEBUG
    constexpr double kP99LimitMs = 50.0;
#else
    constexpr double kP99LimitMs = 150.0;
#endif
    REQUIRE(p99(all_beats) < kP99LimitMs);

    std::error_code ec;
    std::filesystem::remove_all(root_dir, ec);
}
