// 单元：长期记忆册（02 §5 / 03 §6）——候选幂等写入、审阅覆盖、召回规则
// （scope 隐私 / 删除不召回 / 时效 / 打分排序）、重建协议（人工劳动永不丢失）。
#include <oly/core/id.hpp>
#include <oly/memory/memory.hpp>
#include <oly/memory/summarizer.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace oly;

namespace {

const auto kOwner = PrincipalId::from_string("01ARZ3NDEKTSV4RRFFQ69G5FAV").value();     // 用户
const auto kPersona = PrincipalId::from_string("01ARZ3NDEKTSV4RRFFQ69G5FBW").value();   // 角色
const auto kStranger = PrincipalId::from_string("01ARZ3NDEKTSV4RRFFQ69G5FCX").value();  // 丙（非归属）
const auto kSession = SessionId::from_string("01ARZ3NDEKTSV4RRFFQ69G5FDY").value();

memory::LongTermEntry cand(const std::string& content, const uint64_t seq0,
                           const double importance) {
    memory::LongTermEntry e;
    e.owner = kOwner;
    e.persona = kPersona;
    e.session = kSession;
    e.kind = memory::EntryKind::Summary;
    e.content = content;
    e.importance = importance;
    e.source_seqs = {seq0, seq0 + 1, seq0 + 2};
    e.valid = memory::TimeRange{seq0, ~0ull};
    return e;
}

}   // namespace

TEST_CASE("记忆册：候选幂等 / 覆盖 / 重建", "[unit][memory]") {
    memory::Store st;

    // ── 候选写入 + 幂等（同指纹去重）────────────────────────────
    const auto a = cand("月下初见，谈及锚点。", 1, 0.5);
    const auto b = cand("桂影下共饮，约定再会。", 5, 0.3);
    const std::vector<memory::LongTermEntry> batch{a, b};
    REQUIRE(st.submit_candidates(batch) == 2);
    REQUIRE(st.submit_candidates(batch) == 0);          // 重投递 = 零新增
    REQUIRE(st.candidate_count() == 2);
    st.publish();

    const auto fa = memory::fingerprint(a);
    REQUIRE(fa != memory::fingerprint(b));

    // ── 审阅覆盖：归属校验 / edit / delete ─────────────────────
    {
        memory::Review r;
        r.op = memory::ReviewOp::Edit;
        r.by = kStranger;                               // 非归属主体 → 拒绝（A5）
        r.content = "篡改";
        REQUIRE_FALSE(st.review(fa, r).has_value());
    }
    {
        memory::Review r;
        r.op = memory::ReviewOp::Edit;
        r.by = a.owner;
        r.content = "月下初见，锚点之约。（修订）";
        REQUIRE(st.review(fa, r).has_value());
        r.op = memory::ReviewOp::Delete;
        REQUIRE(st.review(memory::fingerprint(b), r).has_value());
    }
    st.publish();

    // ── 召回：在座 / 删除不召回 / 覆盖生效 ─────────────────────
    const PrincipalId in_room[] = {kOwner};
    {
        const auto view = st.view();
        REQUIRE(view != nullptr);
        std::vector<memory::Recalled> hits;
        view->recall(kPersona, in_room, "锚点 初见", 99, 10, hits);
        REQUIRE(hits.size() == 1);
        REQUIRE(hits[0].content == "月下初见，锚点之约。（修订）");   // edit 覆盖生效
        REQUIRE(hits[0].reviewed);
        view->recall(kPersona, in_room, "桂影 共饮 再会", 99, 10, hits);   // 已删：即便高匹配
        REQUIRE(hits.empty());
    }

    // ── 隐私 scope：不在座主体分册不可召回（02 §5.3）────────────
    {
        auto priv = cand("私密：与丙的耳语。", 20, 0.5);
        priv.owner = kStranger;
        const std::vector<memory::LongTermEntry> pb{priv};
        REQUIRE(st.submit_candidates(pb) == 1);
        st.publish();
        std::vector<memory::Recalled> hits;
        st.view()->recall(kPersona, in_room, "耳语 私密", 99, 10, hits);   // 丙不在座
        REQUIRE(hits.empty());
        const PrincipalId with_stranger[] = {kOwner, kStranger};
        st.view()->recall(kPersona, with_stranger, "耳语 私密", 99, 10, hits);
        REQUIRE(hits.size() == 1);
    }

    // ── latest-wins：pin 覆盖 delete → 复活 + 钉定重要度 ─────────
    {
        memory::Review r;
        r.op = memory::ReviewOp::PinImportance;
        r.by = b.owner;
        r.importance = 0.9;
        REQUIRE(st.review(memory::fingerprint(b), r).has_value());
    }
    st.publish();
    {
        std::vector<memory::Recalled> hits;
        st.view()->recall(kPersona, in_room, "月下 桂影", 99, 10, hits);
        REQUIRE(hits.size() == 2);
        REQUIRE(hits[0].content == "桂影下共饮，约定再会。");   // pin 0.9 → b 反超居首
        REQUIRE(hits[0].importance == 0.9);
        REQUIRE(hits[1].content == "月下初见，锚点之约。（修订）");
    }

    // ── 重建协议：同候选 + 覆盖 → 生效视图一致；人工劳动永不丢失 ──
    {
        const std::vector<std::pair<ContentId, memory::Review>> overlays{
            {fa, {memory::ReviewOp::Edit, a.owner, "月下初见，锚点之约。（修订）", {}, 0.0}},
            {memory::fingerprint(b), {memory::ReviewOp::PinImportance, b.owner, "", {}, 0.9}}};
        const auto reb = memory::Store::rebuild(batch, overlays);
        REQUIRE(reb->candidate_count() == 2);
        REQUIRE(reb->overlay_count() == 2);
        std::vector<memory::Recalled> hits;
        reb->view()->recall(kPersona, in_room, "月下 桂影", 99, 10, hits);
        REQUIRE(hits.size() == 2);
        REQUIRE(hits[0].content == "桂影下共饮，约定再会。");   // pin 0.9 → 居首
        REQUIRE(hits[1].content == "月下初见，锚点之约。（修订）");

        // 待挂覆盖：目标未归纳出也保留；候选恢复后自动复效
        const auto c = cand("尾声：夜阑人静。", 30, 0.4);
        const std::vector<std::pair<ContentId, memory::Review>> pending{
            {memory::fingerprint(c), {memory::ReviewOp::Delete, c.owner}}};
        const auto half = memory::Store::rebuild(batch, pending);
        REQUIRE(half->overlay_count() == 1);            // 覆盖保留（待挂）
        const std::vector<memory::LongTermEntry> cb{c};
        REQUIRE(half->submit_candidates(cb) == 1);      // 候选恢复
        half->publish();
        std::vector<memory::Recalled> hits2;
        half->view()->recall(kPersona, in_room, "夜阑 人静", 99, 10, hits2);
        REQUIRE(hits2.empty());                         // 待挂覆盖自动复效 → 不召回
    }

    // ── 时效：valid 不覆盖查询位点不召回（pin 条目不受相关性闸限制）──
    {
        auto closed = cand("旧事：已了结。", 1, 0.5);
        closed.valid = memory::TimeRange{1, 3};   // 仅 [1,3] 有效
        const std::vector<memory::LongTermEntry> xb{closed};
        REQUIRE(st.submit_candidates(xb) == 1);
        st.publish();
        std::vector<memory::Recalled> hits;
        // 位点 99：closed 失效不召回；仅 pin 的 b 强制浮现
        st.view()->recall(kPersona, in_room, "旧事 了结", 99, 10, hits);
        REQUIRE(hits.size() == 1);
        REQUIRE(hits[0].content == "桂影下共饮，约定再会。");   // pin 强制浮现
        // 位点 2：closed 有效入册；b 尚未生效（valid 起点 5 > 2）——pin 只豁免
        // 相关性闸，不豁免时效（两轴正交）
        st.view()->recall(kPersona, in_room, "旧事 了结", 2, 10, hits);
        REQUIRE(hits.size() == 1);
        REQUIRE(hits[0].content == "旧事：已了结。");
    }
}

TEST_CASE("归纳器：确定性折叠 / 追加不改早前窗口", "[unit][memory]") {
    memory::SummarizerConfig cfg;
    cfg.window = 3;
    cfg.max_content = 30;   // 字节上限（多字节字内不劈）

    // 文本驻留（FoldedMessage 持 string_view，不悬垂）
    std::vector<std::string> storage;
    std::vector<memory::FoldedMessage> msgs;
    for (int i = 0; i < 7; ++i) {
        storage.push_back("夜话第" + std::to_string(i + 1) + "回合，桂影摇曳。");
        msgs.push_back({static_cast<uint64_t>(i + 1), kOwner, storage.back()});
    }

    const auto c1 = memory::fold_messages(kSession, kOwner, kPersona, 1, 0, cfg, msgs);
    const auto c2 = memory::fold_messages(kSession, kOwner, kPersona, 1, 0, cfg, msgs);
    REQUIRE(c1.size() == 2);              // 7 条 → 2 完整窗；尾段 1 条不产册
    REQUIRE(c2.size() == c1.size());
    for (std::size_t i = 0; i < c1.size(); ++i) {
        REQUIRE(memory::fingerprint(c1[i]) == memory::fingerprint(c2[i]));   // 逐字节同候选
        REQUIRE(c1[i].content.size() <= 30);
    }
    // 溯源窗口正确：册 1 = seq 1..3，册 2 = seq 4..6
    REQUIRE(c1[0].source_seqs == std::vector<uint64_t>{1, 2, 3});
    REQUIRE(c1[1].source_seqs == std::vector<uint64_t>{4, 5, 6});

    // 追加新消息不改早前候选（追加式流：窗口永不重排 → 覆盖重建可挂）
    storage.push_back("追加一条。");
    msgs.push_back({8, kOwner, storage.back()});
    const auto c3 = memory::fold_messages(kSession, kOwner, kPersona, 1, 0, cfg, msgs);
    REQUIRE(c3.size() == 2);              // 8 条仍只 2 完整窗
    REQUIRE(memory::fingerprint(c3[0]) == memory::fingerprint(c1[0]));

    // 码点边界：中文 3 字节/字——截点必落在字符边界（3 的倍数或短于上限）
    REQUIRE((c1[0].content.size() % 3 == 0 || c1[0].content.size() < 30));
}
