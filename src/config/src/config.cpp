#include "oly/config/config.hpp"

#include "oly/codec/codec.hpp"
#include "oly/core/error.hpp"
#include "oly/core/hash.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace oly::config {
namespace {

// 定点 q4（×10000 u32）：浮点进规范字节的确定性通路（同 session_runtime 的决策字段）
uint32_t q4(const float f) noexcept {
    return static_cast<uint32_t>(std::lround(static_cast<double>(f) * 10000.0));
}
float unq4(const uint32_t v) noexcept { return static_cast<float>(v) / 10000.0f; }

constexpr uint8_t kFormat = 1;

}   // namespace

bool Patch::empty() const noexcept {
    return !tau_base.has_value() && !max_decisions.has_value() && !hard_cooldown.has_value() &&
           !sat_gain.has_value() && !recall_topk.has_value() && !weights.has_value() &&
           !budgets.has_value();
}

bool touches(const Patch& p, const FieldGroup g) noexcept {
    switch (g) {
        case FieldGroup::Persona:
            // 人格类（08 §2·人格档）：外向度（τ 偏置）、话唠度（冷却）、主动性/好奇
            // （权重组）、记忆权重（召回上限）——塑造而非运行
            return p.tau_base.has_value() || p.hard_cooldown.has_value() ||
                   p.weights.has_value() || p.recall_topk.has_value() ||
                   p.max_decisions.has_value();
        case FieldGroup::Ops:
            // 操作类：回复形态/预算微调（提示板槽位再分配限内滑动）
            return p.budgets.has_value() || p.sat_gain.has_value();
    }
    return false;
}

Verdict authorize_persona(const Patch& p, const PrincipalId who, const PrincipalId creator,
                          const bool tune_self_granted) {
    if (p.empty()) return Verdict{false, "config:empty-patch"};
    // 人格类仅创建者——角色自调性格视为越权（denial 可见；演化走 card.propose，03 §5）
    if (touches(p, FieldGroup::Persona) && !(who == creator)) {
        return Verdict{false, "config:persona-fields-creator-only"};
    }
    // 操作类：创建者天然可调；角色本人需 config.tune_self 授权边
    if (touches(p, FieldGroup::Ops) && !(who == creator) && !tune_self_granted) {
        return Verdict{false, "config:ops-fields-tune-self-required"};
    }
    return Verdict{true, {}};
}

Values apply(const Values& base, const Patch& p) {
    Values v = base;
    if (p.tau_base.has_value()) v.tau_base = *p.tau_base;
    if (p.max_decisions.has_value()) v.max_decisions = *p.max_decisions;
    if (p.hard_cooldown.has_value()) v.hard_cooldown = *p.hard_cooldown;
    if (p.sat_gain.has_value()) v.sat_gain = *p.sat_gain;
    if (p.recall_topk.has_value()) v.recall_topk = *p.recall_topk;
    if (p.weights.has_value()) v.weights = *p.weights;
    if (p.budgets.has_value()) v.budgets = *p.budgets;
    return v;
}

Values clamp(const Values v, const Envelope& env) {
    Values out = v;
    out.tau_base = std::clamp(out.tau_base, env.tau_floor, std::numeric_limits<float>::max());
    out.max_decisions = std::clamp(out.max_decisions, 1u, env.k_max);
    out.hard_cooldown = std::clamp(out.hard_cooldown, 0u, env.cooldown_max);
    out.sat_gain = std::clamp(out.sat_gain, 0.0f, env.sat_gain_max);
    out.recall_topk = std::clamp(out.recall_topk, std::size_t{1}, env.recall_max);
    for (std::size_t i = 0; i < kSlotCount; ++i) {
        float& b = out.budgets[i];
        b = std::clamp(b, 0.0f, env.budget_slot_max);
    }
    // 地板锁死（平台承诺，任何配置层不可越——06 §3.0 / 08 §2）
    out.budgets[kIdentity] = std::max(out.budgets[kIdentity], env.budget_floor_identity);
    out.budgets[kOutput] = std::max(out.budgets[kOutput], env.budget_floor_output);
    // 预算和 > 1 → 等比缩回（地板保持，超出者等比让出）
    float sum = 0.0f;
    for (const auto b : out.budgets) sum += b;
    if (sum > 1.0f) {
        for (auto& b : out.budgets) b /= sum;
        out.budgets[kIdentity] = std::max(out.budgets[kIdentity], env.budget_floor_identity);
        out.budgets[kOutput] = std::max(out.budgets[kOutput], env.budget_floor_output);
    }
    return out;
}

Effective merge(const std::span<const Layer> bottom_up) {
    Values v{};   // platform 基线
    bool locked = false;
    for (const auto& layer : bottom_up) {
        if (locked) break;   // canon_lock：下层覆盖整体失效（08 §3 世界锁定类）
        if (layer.patch.has_value()) v = apply(v, *layer.patch);
        locked = layer.canon_lock;
    }
    Effective out;
    out.values = clamp(v, Envelope{});
    out.content_id = ContentId::from_bytes(sha256(encode_values(out.values)).bytes);
    return out;
}

std::vector<std::byte> encode_values(const Values& v) {
    codec::Encoder enc;
    enc.u8(kFormat);
    enc.u32(q4(v.tau_base));
    enc.u32(v.max_decisions);
    enc.u32(v.hard_cooldown);
    enc.u32(q4(v.sat_gain));
    enc.u32(static_cast<uint32_t>(v.recall_topk));
    enc.u32(q4(v.weights.mention));
    enc.u32(q4(v.weights.topic));
    enc.u32(q4(v.weights.relation));
    enc.u32(q4(v.weights.reciprocity));
    enc.u32(q4(v.weights.novelty));
    enc.u32(q4(v.weights.urgency));
    enc.u32(q4(v.weights.base_cost));
    enc.u32(q4(v.weights.streak_k));
    for (const auto b : v.budgets) enc.u32(q4(b));
    return std::move(enc).take();
}

std::vector<std::byte> encode_patch(const Patch& p) {
    codec::Encoder enc;
    enc.u8(kFormat);
    uint16_t mask = 0;
    if (p.tau_base.has_value()) mask |= 1u << 0;
    if (p.max_decisions.has_value()) mask |= 1u << 1;
    if (p.hard_cooldown.has_value()) mask |= 1u << 2;
    if (p.sat_gain.has_value()) mask |= 1u << 3;
    if (p.recall_topk.has_value()) mask |= 1u << 4;
    if (p.weights.has_value()) mask |= 1u << 5;
    if (p.budgets.has_value()) mask |= 1u << 6;
    enc.u16(mask);
    if (p.tau_base.has_value()) enc.u32(q4(*p.tau_base));
    if (p.max_decisions.has_value()) enc.u32(*p.max_decisions);
    if (p.hard_cooldown.has_value()) enc.u32(*p.hard_cooldown);
    if (p.sat_gain.has_value()) enc.u32(q4(*p.sat_gain));
    if (p.recall_topk.has_value()) enc.u32(static_cast<uint32_t>(*p.recall_topk));
    if (p.weights.has_value()) {
        enc.u32(q4(p.weights->mention));
        enc.u32(q4(p.weights->topic));
        enc.u32(q4(p.weights->relation));
        enc.u32(q4(p.weights->reciprocity));
        enc.u32(q4(p.weights->novelty));
        enc.u32(q4(p.weights->urgency));
        enc.u32(q4(p.weights->base_cost));
        enc.u32(q4(p.weights->streak_k));
    }
    if (p.budgets.has_value()) {
        for (const auto b : *p.budgets) enc.u32(q4(b));
    }
    return std::move(enc).take();
}

Patch decode_patch(const std::span<const std::byte> bytes) {
    Patch p;
    codec::Decoder d(bytes);
    if (d.u8() != kFormat) return Patch{};
    const auto mask = d.u16();
    if (mask & (1u << 0)) p.tau_base = unq4(d.u32());
    if (mask & (1u << 1)) p.max_decisions = d.u32();
    if (mask & (1u << 2)) p.hard_cooldown = d.u32();
    if (mask & (1u << 3)) p.sat_gain = unq4(d.u32());
    if (mask & (1u << 4)) p.recall_topk = d.u32();
    if (mask & (1u << 5)) {
        Weights w;
        w.mention = unq4(d.u32());
        w.topic = unq4(d.u32());
        w.relation = unq4(d.u32());
        w.reciprocity = unq4(d.u32());
        w.novelty = unq4(d.u32());
        w.urgency = unq4(d.u32());
        w.base_cost = unq4(d.u32());
        w.streak_k = unq4(d.u32());
        p.weights = w;
    }
    if (mask & (1u << 6)) {
        std::array<float, kSlotCount> b{};
        for (auto& x : b) x = unq4(d.u32());
        p.budgets = b;
    }
    return d.ok() ? p : Patch{};
}

Effective platform_defaults() {
    return finalize(Values{});
}

Effective finalize(Values v) {
    Effective out;
    out.values = clamp(v, Envelope{});
    out.content_id = ContentId::from_bytes(sha256(encode_values(out.values)).bytes);
    return out;
}

}   // namespace oly::config
