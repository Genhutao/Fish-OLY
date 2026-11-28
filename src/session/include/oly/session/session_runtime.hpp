// OLY · session —— 会话运行时（02 §4 运行时部分 + 04 §5 准入协议 + 05 §3 落盘）。
// 绑定一个 loop 独占（I3）；持有：
//   两车道输入队列（01 §3）· 轮式调度登记（tick=背景事件）· 幂等集（04 §5）
//   领域注册表引用 · 段存储引用（事件日志）
// 流程：enqueue（REST/WS/自治）→ drain（loop 上）→ 准入（幂等→权限）→ 应用 → 事实事件
//       → 批量 append（先持久后可见；一批 = 一拍的全部事实，组提交雏形）。
// seq 0 = session.created（04 §3 流开篇），由 create() 写入。
#pragma once

#include "oly/domain/domain.hpp"
#include "oly/exec/lane_queue.hpp"
#include "oly/exec/tick_wheel.hpp"
#include "oly/events/envelope.hpp"
#include "oly/logdb/store.hpp"
#include "oly/session/autonomy.hpp"

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_set>
#include <vector>

namespace oly::session {

enum class CmdAction : uint8_t { SendMessage = 1, Tick = 2 };

struct Command {
    CommandId id;
    domain::PrincipalId actor;
    domain::PrincipalId on_behalf_of;      // 链根用户（角色行动时）
    CmdAction action = CmdAction::SendMessage;
    uint64_t tick = 0;                     // Tick 命令携带拍号
    std::string text;                      // SendMessage payload
};

// 自治驱动句柄表：角色 → 驱动（每拍依次询问；M0-4 换 LLM 装配决策协程）
using AutonomyTable = std::vector<std::pair<domain::PrincipalId, std::unique_ptr<AutonomyDriver>>>;

class SessionRuntime {
public:
    struct Deps {
        domain::DomainRegistry& registry;
        logdb::SegmentStore& store;        // 本会话专属段存储（05 §2 每流一目录）
        exec::TickWheel& wheel;            // 所属 loop 的中央轮式调度器
        uint64_t tick_period_ms = 1000;    // 拍周期（配置面 08，M0 常量起步）
    };

    // 创建：写 session.created（seq 0）并在轮式调度器登记首拍
    static Result<SessionRuntime> create(domain::SessionId id, Deps deps, uint64_t now_ms);

    SessionRuntime(SessionRuntime&&) noexcept = default;
    SessionRuntime& operator=(SessionRuntime&&) = delete;

    // 交互入口：交互道（满 → 调用方限流 429；幂等重复 → Duplicate 丢弃，04 §5）
    enum class Enqueue : uint8_t { Ok, Duplicate, InteractiveFull };
    Enqueue enqueue(Command cmd);

    // 背景/自治入口：满即丢（降级），幂等同样生效
    Enqueue enqueue_background(Command cmd);

    // 调度推进（loop 线程调用）：wheel 到期 → 本会话拍 → 自治驱动产出命令入背景道
    // 返回本拍产生的命令数
    std::size_t advance_clock(uint64_t now_ms);

    // 挂接自治驱动（角色必须在会话中）
    void attach_autonomy(domain::PrincipalId persona, std::unique_ptr<AutonomyDriver> driver);

    // 处理一批：准入（幂等→权限）→ 应用 → 事实事件批量落盘。
    // 返回本批事实数；denial 亦为事实（kind=Denied，可审计）
    Result<uint64_t> drain(std::size_t max_items);

    uint64_t committed_seq() const noexcept { return store_->committed_seq().value_or(0); }
    std::size_t pending() const noexcept { return queue_.interactive_size() + queue_.background_size(); }
    uint64_t ticks() const noexcept { return ticks_; }
    uint64_t dropped_background() const noexcept { return queue_.dropped_background(); }
    const domain::Session& session() const noexcept { return *registry_->session(id_); }

    // 会话键（轮式调度器/路由表使用；ULID 前 8 字节）
    static uint64_t key(domain::SessionId id) noexcept;

private:
    SessionRuntime() = default;
    Result<uint64_t> admit_and_apply(const Command& cmd, exec::LaneQueue<Command>::Lane lane);
    void emit_fact(events::EventKind kind, const Command& cmd, std::string type,
                   std::vector<std::byte> payload);

    domain::SessionId id_;
    domain::DomainRegistry* registry_ = nullptr;
    logdb::SegmentStore* store_ = nullptr;
    exec::TickWheel* wheel_ = nullptr;
    uint64_t tick_period_ms_ = 1000;
    uint64_t ticks_ = 0;
    exec::LaneQueue<Command> queue_{256, 1024};
    AutonomyTable drivers_;
    std::unordered_set<CommandId> seen_;   // 幂等集（04 §5 恰一次准入）
    std::vector<events::EventEnvelope> pending_facts_;
};

}   // namespace oly::session
