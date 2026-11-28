// OLY · exec —— 两车道有界输入队列（01 §3/§4）：会话 loop 独占，零锁。
//   interactive 车道：用户命令 / LLM 回复 / 导演指令 —— 满则拒收（不丢，调用方走限流）
//   background 车道：自治 tick / 环境事件 / 归纳 —— 满则丢新件并计数（优雅降级）
// drain(max)：交互道绝对优先；每次唤醒至多 K 条（01 §5 批处理纪律）。
#pragma once

#include <cstdint>
#include <stddef.h>
#include <deque>
#include <vector>

namespace oly::exec {

template <typename T>
class LaneQueue {
public:
    enum class Lane : uint8_t { Interactive, Background };
    enum class Push : uint8_t { Ok, InteractiveFull, BackgroundDropped };

    LaneQueue(std::size_t interactive_cap = 256, std::size_t background_cap = 1024)
        : interactive_cap_(interactive_cap), background_cap_(background_cap) {}

    Push push(Lane lane, T&& value) {
        if (lane == Lane::Interactive) {
            if (interactive_.size() >= interactive_cap_) return Push::InteractiveFull;
            interactive_.push_back(std::move(value));
            return Push::Ok;
        }
        if (background_.size() >= background_cap_) {
            ++dropped_;   // 背景道丢弃新件：自治降级的第一级（01 §3）
            return Push::BackgroundDropped;
        }
        background_.push_back(std::move(value));
        return Push::Ok;
    }
    Push push(Lane lane, const T& value) { return push(lane, T(value)); }

    // 批量排空：先交互后背景；返回条数 ≤ max_items
    std::vector<T> drain(std::size_t max_items) {
        std::vector<T> out;
        out.reserve(max_items < 16 ? 16 : max_items);
        while (out.size() < max_items && !interactive_.empty()) {
            out.push_back(std::move(interactive_.front()));
            interactive_.pop_front();
        }
        while (out.size() < max_items && !background_.empty()) {
            out.push_back(std::move(background_.front()));
            background_.pop_front();
        }
        return out;
    }

    std::size_t interactive_size() const noexcept { return interactive_.size(); }
    std::size_t background_size() const noexcept { return background_.size(); }
    uint64_t dropped_background() const noexcept { return dropped_; }
    bool empty() const noexcept { return interactive_.empty() && background_.empty(); }

private:
    std::deque<T> interactive_;
    std::deque<T> background_;
    std::size_t interactive_cap_;
    std::size_t background_cap_;
    uint64_t dropped_ = 0;
};

}   // namespace oly::exec
