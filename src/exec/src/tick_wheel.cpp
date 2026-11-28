#include "oly/exec/tick_wheel.hpp"

#include <algorithm>
#include <utility>

namespace oly::exec {

TickWheel::TickWheel(const uint64_t resolution_ms, const std::size_t slots)
    : res_ms_(resolution_ms == 0 ? 1 : resolution_ms),
      slot_count_(slots == 0 ? 1 : slots),
      ring_(slot_count_) {}

// abs_slot 从 1 起（deadline=0 也要占一拍）；槽位映射 abs → ring[(abs-1) % count]。
// due 判定：advance(now) 的 target = now/res + 1，排空 (drained_upto_, target]。

void TickWheel::erase_from_location(const uint64_t key, const uint64_t abs_slot) {
    if (abs_slot <= drained_upto_) {
        // 过期插入挂在 immediate_
        std::erase(immediate_, key);
    } else if (abs_slot <= drained_upto_ + slot_count_) {
        std::erase(ring_[(abs_slot - 1) % slot_count_], key);
    } else if (const auto it = overflow_.find(abs_slot); it != overflow_.end()) {
        std::erase(it->second, key);
        if (it->second.empty()) overflow_.erase(it);
    }
}

void TickWheel::insert(const uint64_t key, const uint64_t deadline_ms) {
    if (const auto it = index_.find(key); it != index_.end()) {
        erase_from_location(key, it->second);   // 幂等重排：旧位置作废
    }
    const uint64_t abs = abs_slot_of(deadline_ms);
    index_[key] = abs;
    if (abs <= drained_upto_) {
        immediate_.push_back(key);              // 过期插入：下次 advance 立即返回
    } else if (abs <= drained_upto_ + slot_count_) {
        ring_[(abs - 1) % slot_count_].push_back(key);
    } else {
        overflow_[abs].push_back(key);          // 超出窗口：有序溢出表
    }
}

void TickWheel::remove(const uint64_t key) noexcept {
    if (const auto it = index_.find(key); it != index_.end()) {
        erase_from_location(key, it->second);
        index_.erase(it);
    }
}

std::vector<uint64_t> TickWheel::advance(const uint64_t now_ms) {
    const uint64_t target = now_ms / res_ms_ + 1;
    std::vector<uint64_t> due;

    // 1) 过期插入先行返回（即便时钟未跨槽：deadline 已过的会话必须立即被调度）
    if (!immediate_.empty()) {
        due = std::move(immediate_);
        immediate_.clear();
        for (const auto key : due) index_.erase(key);
    }

    if (target <= drained_upto_) return due;   // 时钟不回拨：环内无新到期

    // 2) 环内：排空 (drained_upto_, target]，溢出表条目随槽回收
    for (uint64_t s = drained_upto_ + 1; s <= target; ++s) {
        auto& bucket = ring_[(s - 1) % slot_count_];
        if (!bucket.empty()) {
            due.insert(due.end(), bucket.begin(), bucket.end());
            bucket.clear();
        }
        if (const auto it = overflow_.find(s); it != overflow_.end()) {
            due.insert(due.end(), it->second.begin(), it->second.end());
            overflow_.erase(it);
        }
    }
    drained_upto_ = target;

    // 3) 到期即出索引（调用方下拍重排时由 insert 重新登记）
    for (const auto key : due) index_.erase(key);

    return due;
}

}   // namespace oly::exec
