// OLY · session —— 自治驱动接口（06 决策协议的 M0 形态）。
// 决策协程在此只留下"驱动"抽象：感知/估分（06 §2）与 LLM 装配（06 §3.0）M0 后接入；
// 本期提供 ScriptedDriver：确定性脚本（黄金回放的种子，13 §2 评测闸门依赖其确定性）。
#pragma once

#include "oly/core/id.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace oly::session {

class AutonomyDriver {
public:
    virtual ~AutonomyDriver() = default;

    // 一次会话拍：返回本拍要说的文本；nullopt = 沉默（沉默也是行动，06 §2.4）
    virtual std::optional<std::string> on_tick(uint64_t session_tick) = 0;
};

// 确定性脚本驱动：每 every 拍说一句（offset 错峰），说完即沉默。
class ScriptedDriver final : public AutonomyDriver {
public:
    ScriptedDriver(std::vector<std::string> lines, uint64_t every, uint64_t offset)
        : lines_(std::move(lines)), every_(every == 0 ? 1 : every), offset_(offset) {}

    std::optional<std::string> on_tick(const uint64_t session_tick) override {
        if (next_ >= lines_.size()) return std::nullopt;
        if (session_tick % every_ != offset_ % every_) return std::nullopt;
        return lines_[next_++];
    }

private:
    std::vector<std::string> lines_;
    uint64_t every_;
    uint64_t offset_;
    std::size_t next_ = 0;
};

}   // namespace oly::session
