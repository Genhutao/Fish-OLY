// OLY · session —— 自治驱动接口（06）：角色自主行为的会话侧入口。
// 每拍一次 on_tick(TickContext)：感知 → 驱动内部估分/决策 → TickOutcome。
// M0 双实现：ScriptedDriver（确定性脚本，黄金回放种子）/ DeliberationDriver
// （感知 → 数值估分 → 提示板 → 网关 → 降级阶梯，见 deliberation.hpp）。
#pragma once

#include "oly/core/id.hpp"
#include "oly/domain/domain.hpp"

#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace oly::session {

// 感知缓冲条目：来自事实投影的最近消息（非事实态，重放可重建）
struct PerceivedMessage {
    uint64_t seq = 0;
    domain::PrincipalId actor;
    std::string text;
};

// 一拍的感知输入（06 §2：感知 = 事实事件与 tick；瞬态信令不入流、不参与打分）
struct TickContext {
    uint64_t tick = 0;
    uint64_t stream_key = 0;
    domain::DomainRegistry* registry = nullptr;   // 领域引用（卡片头/属主查询）
    const domain::Session* session = nullptr;
    std::span<const PerceivedMessage> window;        // 最近窗口（供 Recent/novelty）
    std::span<const PerceivedMessage> new_messages;  // 上一拍以来的尾部（供 mention/topic/urgency）
};

struct TickOutcome {
    bool speak = false;
    bool silence_noted = false;   // 沉默事件化（06 §2.4）：autonomy.silent
    bool fallback = false;        // 语气储备兜底（06 §3.2 第一级）
    std::string text;
    float attention = 0.0f;       // decision.recorded 雏形（M1 补全 board_manifest/引用记忆）
    float tau = 0.0f;
    std::string model;            // 非空 → 运行时记 decision.recorded（ScriptedDriver 不记）
    uint32_t tokens_in = 0;
    uint32_t tokens_out = 0;
};

class AutonomyDriver {
public:
    virtual ~AutonomyDriver() = default;
    virtual TickOutcome on_tick(const TickContext& ctx) = 0;
};

// 确定性脚本驱动：每 every 拍说一句（offset 错峰），说完即沉默。
// start_tick 供重启续演（05 §5：驱动进度由 tick 确定性推导，不进快照）。
class ScriptedDriver final : public AutonomyDriver {
public:
    ScriptedDriver(std::vector<std::string> lines, uint64_t every, uint64_t offset,
                   uint64_t start_tick = 0)
        : lines_(std::move(lines)), every_(every == 0 ? 1 : every), offset_(offset % every_) {
        for (uint64_t t = 1; t <= start_tick && next_ < lines_.size(); ++t) {
            if (t % every_ == offset_) ++next_;
        }
    }

    TickOutcome on_tick(const TickContext& ctx) override {
        TickOutcome oc;
        if (next_ >= lines_.size() || ctx.tick % every_ != offset_) return oc;
        oc.speak = true;
        oc.text = lines_[next_++];
        return oc;
    }

private:
    std::vector<std::string> lines_;
    uint64_t every_;
    uint64_t offset_;
    std::size_t next_ = 0;
};

}   // namespace oly::session
