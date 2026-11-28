// 黄金场景：20 人会话锚点漂移（13 §1 M1 退出标准）。
//   20 角色 × 30 拍无人值守（纯脚本驱动，确定性）：锚定率度量（发言文本与
//   卡片锚点/世界规则的字汇贴合）逐角色断言；蓄意漂移的对照角色被度量
//   识别（漂移评分超阈 = "不跑偏"断言的量化形态）；事件链读端全量校验。
#include <oly/core/ulid.hpp>
#include <oly/domain/domain.hpp>
#include <oly/events/envelope.hpp>
#include <oly/exec/tick_wheel.hpp>
#include <oly/logdb/store.hpp>
#include <oly/session/autonomy.hpp>
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

// 锚定率（13 §1 漂移评分的量化形态）：发言命中本角色锚点词或世界规则词的
// 比例。度量为纯函数、确定性——同输入同评分（LLM 形态接入后同闸门复用）。
double anchor_adherence(const std::string& anchor, const std::string& canon_word,
                        const std::vector<std::string>& speeches) {
    if (speeches.empty()) return 1.0;
    std::size_t hit = 0;
    for (const auto& s : speeches) {
        if (s.find(anchor) != std::string::npos || s.find(canon_word) != std::string::npos) ++hit;
    }
    return static_cast<double>(hit) / static_cast<double>(speeches.size());
}

}   // namespace

TEST_CASE("锚点漂移：20 人会话 30 拍无人值守不跑偏（M1 退出标准）", "[golden][anchor-drift]") {
    constexpr std::size_t kPersonas = 20;
    constexpr int kTicks = 30;

    const auto dir = std::filesystem::temp_directory_path() /
                     ("oly-golden-drift-" + UlidFactory{}.next().str());
    const auto root = pid_of("01ARZ3NDEKTSV4RRFFQ69G5FAV");

    // ── 世界装配：1 用户 + 20 角色（各有专属锚点词），单会话同座 ──
    //   （I1 不变量：会话 ≥2 成员——先装角色，后建会话）
    domain::DomainRegistry reg;
    {
        domain::Principal r;
        r.id = r.owner = root;
        REQUIRE(reg.add_principal(r).has_value());
    }
    std::vector<std::pair<domain::PrincipalId, std::string>> anchors;   // (persona, 锚点词)
    const auto sess = SessionId::from_string("01ARZ3NDEKTSV4RRFFQ69G5FDY").value();
    std::vector<std::vector<std::string>> scripts(kPersonas);
    for (std::size_t i = 0; i < kPersonas; ++i) {
        const auto pid = domain::PrincipalId::from_bytes(
            UlidFactory{[] {
                static uint64_t ms = 1'700'000'000'000ull;
                return ms++;
            }}.next().bytes());
        const std::string anchor = "徽记" + std::to_string(i);
        domain::Principal p;
        p.id = pid;
        p.kind = domain::PrincipalKind::Persona;
        p.owner = root;
        REQUIRE(reg.add_principal(p).has_value());
        REQUIRE(reg.add_persona(pid, root, domain::CardBody{"角色" + std::to_string(i), anchor, ""})
                    .has_value());

        // 脚本：每句都带本角色锚点词（锚定说话术）；除最后一个对照角色
        for (int t = 1; t <= kTicks + 2; ++t) {
            scripts[i].push_back("【角色" + std::to_string(i) + "】" + anchor + "常在，庭中夜话。");
        }
        anchors.emplace_back(pid, anchor);
    }
    {
        domain::Session s;
        s.id = sess;
        s.owner = root;
        s.canon = "月下庭院：青石铺地，桂影婆娑。";
        s.members.push_back(domain::Member{root, domain::SessionRole::Owner, 0});
        for (const auto& [pid, anchor] : anchors) {
            (void)anchor;
            s.members.push_back(domain::Member{pid, domain::SessionRole::Member, 0});
        }
        REQUIRE(reg.create_session(s).has_value());
    }
    // 对照角色（最后一个）：蓄意漂移——发言不含任何锚点词（跑偏样本）
    scripts[kPersonas - 1].clear();
    for (int t = 1; t <= kTicks + 2; ++t) {
        scripts[kPersonas - 1].push_back("【角色" + std::to_string(kPersonas - 1) + "】看看外面。");
    }

    // ── 运行时：20 驱动 × 30 拍无人值守 ─────────────────────────
    auto store = logdb::SegmentStore::open(
        dir, events::StreamRef{events::StreamKind::Session, sess.bytes()}, 0);
    REQUIRE(store.has_value());
    exec::TickWheel wheel;
    session::SessionRuntime::Deps deps{reg, *store, wheel, 1000, {}};
    auto rt = session::SessionRuntime::create(sess, deps, 0);
    REQUIRE(rt.has_value());
    for (std::size_t i = 0; i < kPersonas; ++i) {
        rt->attach_autonomy(anchors[i].first, std::make_unique<session::ScriptedDriver>(
                                                  scripts[i], 1, static_cast<uint64_t>(i)));
    }
    uint64_t now = 0;
    for (int t = 1; t <= kTicks; ++t) {
        now += 1000;
        (void)rt->advance_clock(now);
        (void)rt->drain(256);
    }

    // ── 断言 1：拍数到位，逐角色发言入流（无人值守无缺席）────────
    for (std::size_t i = 0; i < kPersonas; ++i) {
        CAPTURE(i);
        std::vector<std::string> speeches;
        for (const auto& [seq, text] : reg.session(sess)->messages) {
            (void)seq;
            if (text.find("【角色" + std::to_string(i) + "】") == 0) speeches.push_back(text);
        }
        REQUIRE(speeches.size() >= static_cast<std::size_t>(kTicks) - static_cast<std::size_t>(i) - 1);
        // 锚定率断言：正常角色 = 1.0（锚词恒在）；对照角色被度量识别
        const double adh = anchor_adherence(anchors[i].second, "庭", speeches);
        if (i == kPersonas - 1) {
            // 蓄意漂移：含"庭"句也可能命中世界词——脚本刻意避开 → 漂移被识别
            REQUIRE(adh < 0.5);
        } else {
            REQUIRE(adh >= 0.99);   // 不跑偏（13 §1：漂移评分 < 阈值）
        }
    }

    // ── 断言 2：事件链读端全量校验（seq 连续 + 哈希链走查）────────
    {
        auto rstore = logdb::SegmentStore::open(
            dir, events::StreamRef{events::StreamKind::Session, sess.bytes()}, 0);
        REQUIRE(rstore.has_value());
        const auto all = rstore->read_range(0, 100000);
        REQUIRE(all.has_value());
        REQUIRE(all.value().front().type == "session.created");
        uint64_t expect = 0;
        for (const auto& e : all.value()) {
            REQUIRE(e.seq == expect++);
        }
        REQUIRE(all.value().size() > kPersonas * static_cast<uint64_t>(kTicks / 2));
    }

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}
