#include "oly/session/scoring.hpp"

#include <algorithm>
#include <cmath>

namespace oly::session {
namespace {

// splitmix64：种子化的确定性噪声（重放可复现；RNG 状态不进快照，05 §5）
constexpr uint64_t splitmix64(uint64_t x) noexcept {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

// [0,1) 均匀；salt 区分用途（注意力噪声 / 沉默采样）
float unit(uint64_t stream_key, uint64_t tick, uint32_t index, uint64_t salt) noexcept {
    const uint64_t h = splitmix64(stream_key ^ (tick * 0x9E3779B97F4A7C15ull) ^
                                  (uint64_t{index} << 32) ^ salt);
    return static_cast<float>(h >> 40) * (1.0f / 16777216.0f);   // 24 位尾数足够打分精度
}

}   // namespace

float speak_threshold(const ScoreConfig& cfg) noexcept {
    const float activity_factor = 0.6f + 1.8f * cfg.activity_ema;   // 冷场 0.6×，极热闹 2.4×
    return cfg.tau_base * activity_factor + cfg.mood_offset;
}

float attention_of(const AttentionWeights& w, const ScoreConfig& cfg, const uint64_t stream_key,
                   const uint64_t tick, const uint32_t index, const float mention,
                   const float topic, const float relation, const float reciprocity,
                   const float novelty, const float urgency, const float energy,
                   const float streak) noexcept {
    // 成本（06 §2.2）：base × (1 + streak×k) × 1/(0.2 + energy) —— 连续发言与疲惫都变贵
    const float cost = w.base_cost * (1.0f + streak * w.streak_k) / (0.2f + energy);
    const float signal = w.mention * mention + w.topic * topic + w.relation * relation +
                         w.reciprocity * reciprocity + w.novelty * novelty + w.urgency * urgency;
    const float n = (unit(stream_key, tick, index, 0xA77EU) - 0.5f) * 2.0f * cfg.noise;
    return signal - cost + n;
}

std::vector<Scored> score(const AttentionWeights& w, const ScoreConfig& cfg,
                          const uint64_t stream_key, const uint64_t tick,
                          const CandidateFeatures& f) {
    const float tau = speak_threshold(cfg);
    std::vector<Scored> out;
    out.reserve(f.count);
    for (uint32_t i = 0; i < f.count; ++i) {
        if (f.cooldown[i] != 0) continue;   // 硬冷却：不可发言（06 §2.3）
        const float att = attention_of(w, cfg, stream_key, tick, i, f.mention[i], f.topic[i],
                                       f.relation[i], f.reciprocity[i], f.novelty[i], f.urgency[i],
                                       f.energy[i], f.streak[i]);
        if (att >= tau) out.push_back(Scored{i, att});
    }
    std::stable_sort(out.begin(), out.end(), [](const Scored& a, const Scored& b) {
        return a.attention > b.attention;   // 稳定排序保 index 升序，确定性
    });
    if (out.size() > cfg.max_decisions) out.resize(cfg.max_decisions);   // 每拍 K 上限
    return out;
}

std::vector<uint32_t> collect_silences(const ScoreConfig& cfg, const uint64_t stream_key,
                                       const uint64_t tick, const std::span<const float> scores,
                                       const std::span<const float> mentioned) {
    const float tau = speak_threshold(cfg);
    const float band = 0.6f * tau;   // "差一点就说"带下沿
    std::vector<uint32_t> out;
    const auto n = std::min(scores.size(), mentioned.size());
    for (std::size_t i = 0; i < n; ++i) {
        if (scores[i] >= tau) continue;   // 发言了，不算沉默
        if (mentioned[i] > 0.0f) {
            out.push_back(static_cast<uint32_t>(i));   // 被@却沉默：必记（叙事上有意义）
            continue;
        }
        if (scores[i] >= band && unit(stream_key, tick, static_cast<uint32_t>(i), 0x51E7U) < 0.1f) {
            out.push_back(static_cast<uint32_t>(i));   // borderline 10% 采样
        }
    }
    return out;
}

}   // namespace oly::session
