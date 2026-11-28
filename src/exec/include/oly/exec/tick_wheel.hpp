// OLY · exec —— 中央轮式 tick 调度器（01 §2）：每 loop 一个，loop 线程独占（零锁）。
// 分辨率默认 100ms、环 2048 槽（= 204.8s 窗口）；窗口外的截止时间落有序溢出表，
// 推进时自动回收进环。语义：
//   insert(key, deadline)  幂等重排（同 key 旧位置作废）—— 会话改拍周期即此调用
//   remove(key)            摘除（导演暂停 = 摘槽，11 §2）
//   advance(now)           返回 (上次推进, now] 内到期 key 的批；同 key 一拍只出现一次；
//                          过期插入（deadline 已过）在下次 advance 立即返回
// advance 的返回批即"整槽到期会话"，由会话调度器批量投递 tick 输入（01 §2 批处理纪律）。
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <stddef.h>
#include <unordered_map>
#include <vector>

namespace oly::exec {

class TickWheel {
public:
    TickWheel(uint64_t resolution_ms = 100, std::size_t slots = 2048);

    void insert(uint64_t key, uint64_t deadline_ms);
    void remove(uint64_t key) noexcept;
    bool scheduled(uint64_t key) const noexcept { return index_.contains(key); }

    // now_ms 必须单调不减（虚拟时钟纪律：过载时拍可合并跳过，时钟不回拨）
    std::vector<uint64_t> advance(uint64_t now_ms);

    std::size_t size() const noexcept { return index_.size(); }

private:
    uint64_t abs_slot_of(uint64_t deadline_ms) const noexcept { return deadline_ms / res_ms_ + 1; }

    void erase_from_location(uint64_t key, uint64_t abs_slot);

    uint64_t res_ms_;
    uint64_t slot_count_;
    uint64_t drained_upto_ = 0;   // 已排空的最高绝对槽位
    std::vector<uint64_t> immediate_;                  // 过期插入（deadline 已过）
    std::vector<std::vector<uint64_t>> ring_;
    std::map<uint64_t, std::vector<uint64_t>> overflow_;   // 窗口外：abs_slot → keys
    std::unordered_map<uint64_t, uint64_t> index_;         // key → abs_slot
};

}   // namespace oly::exec
