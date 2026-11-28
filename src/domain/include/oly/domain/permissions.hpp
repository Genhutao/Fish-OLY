// OLY · domain —— 权限检查（02 §6 / 04 §5）：三源合一的纯函数。
//   ① 所有权链（隐式超集，I4） ② 会话角色层级 Owner ⊇ Admin ⊇ Member  ③ 显式授权边（M1）
// 准入点唯一：所有命令（04 §2 动作域）在会话/身份域 dequeue 时经由此处，拒绝写 denial 事件。
#pragma once

#include "oly/domain/domain.hpp"

namespace oly::domain {

enum class Action : uint8_t {
    SendMessage,     // 会话成员
    ReadSession,     // 会话成员
    AddMember,       // Admin+，且目标可交互（Visibility 裁决）
    RemoveMember,    // Admin+；不能移除 Owner
    SetRole,         // Owner；不能改动 Owner 自身
    TransferSession, // Owner
    DirectorControl, // Admin+（pause/step/inject/schedule，11 §1）
    Rollback,        // Owner（11 §1）
    EditCard,        // 卡片所有权链（角色自改走显式授权边，M1 接入 grants）
};

struct Decision {
    bool allow = false;
    std::string reason;   // 拒绝原因（denial 事件的 payload 来源，可审计）

    static Decision ok() { return {true, {}}; }
    static Decision deny(std::string why) { return {false, std::move(why)}; }
};

// 各动作的检查入口（object 在参数中显式给出；ctx 预留配额/包络扩展位）
Decision check(const DomainRegistry& reg, Action action, SessionId session, PrincipalId actor,
               PrincipalId target = PrincipalId{}, SessionRole new_role = SessionRole::Member);

}   // namespace oly::domain
