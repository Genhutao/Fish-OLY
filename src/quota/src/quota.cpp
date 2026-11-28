#include "oly/quota/quota.hpp"

#include <algorithm>

namespace oly::quota {
namespace {

constexpr double kEps = 1e-9;

}   // namespace

Ledger::Bucket& Ledger::bucket(const uint64_t key) { return buckets_[key]; }

Ledger::Bucket* Ledger::find(const uint64_t key) {
    const auto it = buckets_.find(key);
    return it == buckets_.end() ? nullptr : &it->second;
}

const Ledger::Bucket* Ledger::find(const uint64_t key) const {
    const auto it = buckets_.find(key);
    return it == buckets_.end() ? nullptr : &it->second;
}

double Ledger::burst_available(const Bucket& b, const uint64_t now_ms) const {
    if (limits_.burst.rate_per_s <= 0.0 || limits_.burst.burst <= 0.0) return limits_.burst.burst;
    const double elapsed_s = static_cast<double>(now_ms - b.last_ms) / 1000.0;
    return std::min(limits_.burst.burst, b.tokens + elapsed_s * limits_.burst.rate_per_s);
}

double Ledger::volume_available(const Bucket& b, const uint64_t now_ms, const VolumeConfig& vc) const {
    if (vc.cap <= 0.0 || vc.window_ms == 0) return vc.cap;
    const bool rolled = now_ms - b.window_start >= vc.window_ms;
    return rolled ? vc.cap : vc.cap - b.volume_used;
}

Verdict Ledger::try_take(Bucket& b, const VolumeConfig& vc, const uint64_t now_ms,
                         const double want, const Verdict::Code on_empty) {
    Verdict v;
    const double vol = volume_available(b, now_ms, vc);
    if (vol + kEps < want) {
        v.code = on_empty;
        v.reset_ms = (vc.window_ms == 0)
                         ? now_ms
                         : b.window_start + ((now_ms - b.window_start) / vc.window_ms + 1) *
                                               vc.window_ms;   // 下一窗口起点（retry-after 原料）
        return v;
    }
    // 新桶满桶起步（首用即有突发额度；last_ms 未播种时不应按 0 存量计）
    if (!b.seeded) {
        b.tokens = limits_.burst.burst;
        b.seeded = true;
        b.last_ms = now_ms;
    }
    const double br = burst_available(b, now_ms);
    if (br + kEps < want) {
        v.code = Verdict::Code::BurstLimited;
        v.reset_ms = now_ms + static_cast<uint64_t>(
                                (want - br) / std::max(limits_.burst.rate_per_s, kEps) * 1000.0);
        return v;
    }
    // 窗口滚动（只在成功消费时推进——失败裁决不得挪动窗口，保证重置时间稳定）
    if (vc.window_ms != 0 && now_ms - b.window_start >= vc.window_ms) {
        b.window_start += (now_ms - b.window_start) / vc.window_ms * vc.window_ms;
        b.volume_used = 0.0;
    }
    b.tokens = br - want;
    b.last_ms = now_ms;
    b.volume_used += want;
    v.granted = want;
    return v;
}

Verdict Ledger::reserve(const uint64_t now_ms, const uint64_t principal_key,
                        const uint64_t world_key, const double want) {
    Verdict out;
    if (want <= 0.0) return out;
    // 世界维先行（09 §2 第二维度）：min(主体余, 世界余)——任一处触顶即受限
    if (world_key != 0) {
        auto& wb = bucket(world_key);
        const auto wv = try_take(wb, limits_.world, now_ms, want, Verdict::Code::WorldExhausted);
        if (wv.code != Verdict::Code::Ok) return wv;
        out = wv;
    }
    if (principal_key != 0) {
        auto& pb = bucket(principal_key);
        const auto pv =
            try_take(pb, limits_.volume, now_ms, want, Verdict::Code::VolumeExhausted);
        if (pv.code != Verdict::Code::Ok) {
            if (world_key != 0) release(0, world_key, want);   // 回滚世界预留（原子性）
            return pv;
        }
        out = pv;
    }
    return out;
}

void Ledger::settle(const uint64_t now_ms, const uint64_t principal_key,
                    const uint64_t world_key, const double reserved, const double actual) {
    const auto adjust = [](Bucket& b, const double delta) {
        if (delta > 0.0) {
            b.volume_used = std::max(0.0, b.volume_used - delta);   // 多退
        } else if (delta < 0.0) {
            b.volume_used += -delta;   // 少补（实耗超预留，容忍小幅透支）
        }
    };
    const double delta = reserved - actual;
    if (world_key != 0) {
        if (auto* wb = find(world_key); wb != nullptr) adjust(*wb, delta);
    }
    if (principal_key != 0) {
        if (auto* pb = find(principal_key); pb != nullptr) adjust(*pb, delta);
    }
    (void)now_ms;
}

void Ledger::release(const uint64_t principal_key, const uint64_t world_key, const double want) {
    if (want <= 0.0) return;
    if (world_key != 0) {
        if (auto* wb = find(world_key); wb != nullptr)
            wb->volume_used = std::max(0.0, wb->volume_used - want);
    }
    if (principal_key != 0) {
        if (auto* pb = find(principal_key); pb != nullptr)
            pb->volume_used = std::max(0.0, pb->volume_used - want);
    }
}

float Ledger::world_pressure(const uint64_t now_ms, const uint64_t principal_key,
                             const uint64_t world_key) const {
    auto pressure_of = [&](const uint64_t key, const VolumeConfig& vc) {
        if (key == 0 || vc.cap <= 0.0 || vc.window_ms == 0) return 0.0f;
        const auto* b = find(key);
        const double avail = b != nullptr ? volume_available(*b, now_ms, vc) : vc.cap;
        return static_cast<float>(std::clamp(1.0 - avail / vc.cap, 0.0, 1.0));
    };
    const double pw = pressure_of(principal_key, limits_.volume);
    const double ww = pressure_of(world_key, limits_.world);
    return static_cast<float>(std::max(pw, ww));
}

}   // namespace oly::quota
