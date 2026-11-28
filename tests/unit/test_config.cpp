// 单元：配置面（08）——分层合并 / 包络钳制 / 三级自调 / 规范编码 / canon_lock。
#include <oly/config/config.hpp>

#include <catch2/catch_test_macros.hpp>

#include <oly/core/id.hpp>

using namespace oly;
using namespace oly::config;

namespace {
const auto kCreator = PrincipalId::from_string("01ARZ3NDEKTSV4RRFFQ69G5FAV").value();
const auto kPersonaSelf = PrincipalId::from_string("01ARZ3NDEKTSV4RRFFQ69G5FBW").value();
}   // namespace

TEST_CASE("配置：分层合并 / 包络 / canon_lock", "[unit][config]") {
    // 下层覆盖上层
    Patch session_p;
    session_p.tau_base = 2.0f;
    Patch persona_p;
    persona_p.hard_cooldown = 7;
    const Layer eff_layers[] = {Layer{session_p}, Layer{persona_p}};
    const auto eff = merge(eff_layers);
    REQUIRE(eff.values.tau_base == 2.0f);
    REQUIRE(eff.values.hard_cooldown == 7);
    REQUIRE(eff.values.max_decisions == 2);   // 未触字段 = platform 基线

    // 包络：合并端防御钳制（历史 blob 越界兜底）
    Patch wild;
    wild.tau_base = 0.0001f;   // 低于 τ 地板
    wild.max_decisions = 999;  // 超 K 上限
    const Layer wild_layers[] = {Layer{wild}};
    const auto clamped = merge(wild_layers);
    REQUIRE(clamped.values.tau_base == 0.05f);
    REQUIRE(clamped.values.max_decisions == 8);

    // 提示板地板：Identity/Output 锁死（平台承诺）
    Patch floor_break;
    std::array<float, kSlotCount> b{0.01f, 0.13f, 0.08f, 0.25f, 0.22f, 0.08f, 0.01f};
    floor_break.budgets = b;
    const Layer fb_layers[] = {Layer{floor_break}};
    const auto fb = merge(fb_layers);
    REQUIRE(fb.values.budgets[kIdentity] == 0.12f);
    REQUIRE(fb.values.budgets[kOutput] == 0.12f);
    float sum = 0.0f;
    for (const auto x : fb.values.budgets) sum += x;
    REQUIRE(sum <= 1.0f + 0.001f);

    // 内容寻址：同值同 id、异值异 id（行为归因粒度）
    const auto eff2 = merge(eff_layers);   // 同输入重跑 → 同 id（内容寻址）
    REQUIRE(eff.content_id == eff2.content_id);
    Patch other;
    other.tau_base = 2.5f;
    const Layer only_session[] = {Layer{session_p}};
    REQUIRE(merge(only_session).content_id != eff.content_id);
    const Layer only_other[] = {Layer{other}};
    REQUIRE(merge(only_other).content_id != eff.content_id);

    // canon_lock（08 §3 世界锁定类）：world 层锁定 → session/persona 覆盖整体失效
    Layer world;
    Patch wp;
    wp.tau_base = 3.0f;
    world.patch = wp;
    world.canon_lock = true;
    const Layer locked_layers[] = {world, Layer{session_p}, Layer{persona_p}};
    const auto locked = merge(locked_layers);
    REQUIRE(locked.values.tau_base == 3.0f);
    REQUIRE(locked.values.hard_cooldown == 2);   // persona 覆盖被锁
}

TEST_CASE("配置：三级自调裁决（08 §3）", "[unit][config]") {
    Patch persona_fields;   // 人格类：外向度（τ）+ 记忆召回
    persona_fields.tau_base = 0.8f;
    persona_fields.recall_topk = 8;

    // 人格类仅创建者
    const auto by_creator = authorize_persona(persona_fields, kCreator, kCreator, false);
    REQUIRE(by_creator.allow);
    const auto by_self = authorize_persona(persona_fields, kPersonaSelf, kCreator, true);
    REQUIRE_FALSE(by_self.allow);   // 角色自调性格 = 越权（denial 可见）
    REQUIRE(by_self.reason == "config:persona-fields-creator-only");

    Patch ops_fields;   // 操作类：提示板预算微调
    std::array<float, kSlotCount> b{0.12f, 0.10f, 0.08f, 0.30f, 0.22f, 0.06f, 0.12f};
    ops_fields.budgets = b;

    // 操作类：创建者天然可调；角色需 tune_self 授权边
    REQUIRE(authorize_persona(ops_fields, kCreator, kCreator, false).allow);
    REQUIRE_FALSE(authorize_persona(ops_fields, kPersonaSelf, kCreator, false).allow);
    REQUIRE(authorize_persona(ops_fields, kPersonaSelf, kCreator, true).allow);

    // 空 patch 拒绝
    Patch none;
    REQUIRE_FALSE(authorize_persona(none, kCreator, kCreator, true).allow);
}

TEST_CASE("配置：规范编码——同输入逐字节同输出 / 事件往返", "[unit][config]") {
    Patch p;
    p.tau_base = 2.0f;
    p.hard_cooldown = 5;
    Weights w;
    w.mention = 2.2f;
    w.topic = 0.9f;
    p.weights = w;
    std::array<float, kSlotCount> b{0.12f, 0.10f, 0.08f, 0.30f, 0.22f, 0.06f, 0.12f};
    p.budgets = b;

    const auto raw1 = encode_patch(p);
    const auto raw2 = encode_patch(p);
    REQUIRE(raw1 == raw2);

    const auto back = decode_patch(raw1);
    REQUIRE(back.tau_base == p.tau_base);
    REQUIRE(back.hard_cooldown == p.hard_cooldown);
    REQUIRE(back.weights.has_value());
    REQUIRE(*back.weights == w);
    REQUIRE(back.budgets == p.budgets);

    // 损坏载荷 → 空 patch（解码一次置错语义）
    auto bad = raw1;
    bad[1] = static_cast<std::byte>(0xFF);
    REQUIRE(decode_patch(bad).empty());

    // Values 规范编码 → ContentId 稳定（行为归因键）
    const auto eff = platform_defaults();
    const auto again = finalize(eff.values);
    REQUIRE(eff.content_id == again.content_id);
}
