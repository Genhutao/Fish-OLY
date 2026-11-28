// OLY · session —— 会话运行时（02 §4 运行时部分 + 04 §5 准入协议 + 05 §3/§5/§6 落盘与恢复）。
// 绑定一个 loop 独占（I3）；持有：
//   两车道输入队列（01 §3）· 轮式调度登记（tick=背景事件）· 幂等集（04 §5）
//   领域注册表引用 · 段存储引用（事件日志）· 感知缓冲（最近窗口，驱动输入）
// 流程：enqueue（REST/WS/自治）→ drain（loop 上）→ 准入（幂等→权限）→ 应用 → 事实事件
//       → 批量 append（先持久后可见；一批 = 一拍的全部事实，组提交雏形）。
// seq 0 = session.created（04 §3 流开篇），由 create() 写入；
// recover() 走启动重放（快照 + 补缺口，05 §6），不重写 created。
#pragma once

#include "oly/config/config.hpp"
#include "oly/domain/domain.hpp"
#include "oly/exec/lane_queue.hpp"
#include "oly/exec/tick_wheel.hpp"
#include "oly/events/envelope.hpp"
#include "oly/logdb/store.hpp"
#include "oly/session/autonomy.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_set>
#include <vector>

namespace oly::session {

enum class CmdAction : uint8_t { SendMessage = 1, Tick = 2, AdvanceRead = 3, ConfigSet = 4 };

struct Command {
    CommandId id;
    domain::PrincipalId actor;
    domain::PrincipalId on_behalf_of;      // 链根用户（角色行动时）
    CmdAction action = CmdAction::SendMessage;
    uint64_t tick = 0;                     // Tick 命令携带拍号
    std::string text;                      // SendMessage payload
    uint64_t read_seq = 0;                 // AdvanceRead：推进读游标目标（10 §4.3）
    std::vector<std::byte> config_patch;   // ConfigSet：配置补丁规范编码（08 §1）
};

// 感知缓冲窗口上限（非事实态，重放可重建；超限从头淘汰）
constexpr std::size_t kPerceiveWindow = 64;

// 自治驱动句柄表：角色 → 驱动（每拍依次询问）
using AutonomyTable = std::vector<std::pair<domain::PrincipalId, std::unique_ptr<AutonomyDriver>>>;

class SessionRuntime {
public:
    struct Deps {
        domain::DomainRegistry& registry;
        logdb::SegmentStore& store;        // 本会话专属段存储（05 §2 每流一目录）
        exec::TickWheel& wheel;            // 所属 loop 的中央轮式调度器
        uint64_t tick_period_ms = 1000;    // 拍周期（配置面 08，M0 常量起步）
        std::filesystem::path snapshot_dir;   // 快照目录；空 = 快照禁用（05 §5）
        // 身份域只读快照（01 §1 I3 例外）：非空 → 权限检查走无锁快照路径；
        // 空 → 退回注册表直查（单 loop 单写者场景）。M1 随身份流事件刷新。
        std::shared_ptr<const domain::IdentitySnapshot> identity;
        // 提交外送钩子（10 §5 WS 出站）：一批事实 append 成功后回调（loop 线程）。
        // 实现方须自行跨线程转发（有界队列），回调内不做阻塞 IO。
        std::function<void(const std::vector<events::EventEnvelope>&)> on_commit;
    };

    // 创建：写 session.created（seq 0）并在轮式调度器登记首拍
    static Result<SessionRuntime> create(domain::SessionId id, Deps deps, uint64_t now_ms);

    // 启动恢复（05 §6）：最近快照 → 载入投影 → 补重放；无快照 → 全量 fold。
    // 会话的 canon/成员属身份域，由调用方先行装配；本函数灌事实侧
    // （投影/头游标/拍计数/幂等集/感知缓冲），不重写 created。
    static Result<SessionRuntime> recover(domain::SessionId id, Deps deps, uint64_t now_ms);

    SessionRuntime(SessionRuntime&&) noexcept = default;
    SessionRuntime& operator=(SessionRuntime&&) = delete;

    // 交互入口：交互道（满 → 调用方限流 429；幂等重复 → Duplicate 丢弃，04 §5）
    enum class Enqueue : uint8_t { Ok, Duplicate, InteractiveFull };
    Enqueue enqueue(Command cmd);

    // 背景/自治入口：满即丢（降级），幂等同样生效
    Enqueue enqueue_background(Command cmd);

    // 单会话一拍（宿主已从轮式摘槽，01 §2）：驱动询问 → 命令入背景道 → 自续登记。
    // 返回本拍产生的命令数
    std::size_t on_tick(uint64_t now_ms);

    // 调度推进（自管模式，loop 线程调用）：wheel 到期 → 本会话拍。
    // 分片托管模式走 on_tick（轮式由分片统一摘槽，避免二次消费）。
    std::size_t advance_clock(uint64_t now_ms);

    // 挂接自治驱动（角色必须在会话中）
    void attach_autonomy(domain::PrincipalId persona, std::unique_ptr<AutonomyDriver> driver);

    // 处理一批：准入（幂等→权限）→ 应用 → 事实事件批量落盘。
    // 返回本批事实数；denial 亦为事实（kind=Denied，可审计）
    Result<uint64_t> drain(std::size_t max_items);

    // 拍快照（05 §5）：投影 → 快照文件，返回内容哈希锚。
    // M0 同步拍；M1 换后台协程周期/定量拍，接口不变。
    Result<ContentId> snapshot_now() const;

    uint64_t committed_seq() const noexcept { return store_->committed_seq().value_or(0); }
    std::size_t pending() const noexcept { return queue_.interactive_size() + queue_.background_size(); }
    uint64_t ticks() const noexcept { return ticks_; }
    uint64_t dropped_background() const noexcept { return queue_.dropped_background(); }
    const domain::Session& session() const noexcept { return *registry_->session(id_); }

    // 生效配置（08 §1）：platform 基线 × 会话层补丁；config.set 热生效（下一拍可见）
    const config::Effective& effective() const noexcept { return effective_; }

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
    std::unordered_set<CommandId> seen_;   // 幂等集（04 §5 恰一次准入；恢复自事件重建）
    std::vector<events::EventEnvelope> pending_facts_;
    std::vector<PerceivedMessage> perceived_;   // 感知缓冲（最近 kPerceiveWindow 条）
    uint64_t perceived_seq_ = 0;                // 上一拍感知游标（新到消息的分界）
    std::filesystem::path snap_dir_;
    std::shared_ptr<const domain::IdentitySnapshot> identity_;   // 身份快照（无锁权限源）
    std::function<void(const std::vector<events::EventEnvelope>&)> on_commit_;
    config::Values session_values_{};    // 会话层解析值（config.set 依序应用；重放还原）
    config::Effective effective_{};      // 生效配置（含包络钳制 + 内容寻址 id）
};

}   // namespace oly::session
