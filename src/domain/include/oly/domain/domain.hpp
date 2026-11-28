// OLY · domain —— 领域内核（02）：主体 / 角色卡片 / 会话 / 成员 / 权限的纯类型与注册表。
// 纯数据 + 纯函数：不依赖 exec（无 io_context）、不依赖 logdb（状态可由事件投影重建——
// M0 先直接维护态，M1 归纳管线接入后本注册表改为事实事件的物化投影，接口不变）。
#pragma once

#include "oly/core/id.hpp"
#include "oly/core/result.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace oly::domain {

enum class PrincipalKind : uint8_t { User = 1, Persona = 2 };
enum class Visibility : uint8_t { Private = 1, Public = 2, Shared = 3 };
enum class SessionRole : uint8_t { Owner = 1, Admin = 2, Member = 3 };   // 数值即权限高低

// core 强类型 ID 在本命名空间的别名（02 §1：全部对象可寻址）
using PrincipalId = oly::PrincipalId;
using SessionId   = oly::SessionId;
using MemoryId    = oly::MemoryId;

// ── 主体（A1：用户与角色同壳）──────────────────────────────────────────
struct Principal {
    PrincipalId id;
    PrincipalKind kind = PrincipalKind::User;
    PrincipalId owner;                       // 创建者；链根必为 User
    Visibility visibility = Visibility::Private;
    std::vector<PrincipalId> shared_with;    // Visibility::Shared 时的授权名单
};

// ── 角色卡片（A2：纯设定，版本链 append-only）─────────────────────────
struct CardBody {
    std::string name;
    std::string core_anchor;                 // 不可违背的一句话（提示板 Identity 槽地板件）
    std::string description;
};
struct CardRef {
    uint64_t seq = 0;                        // 版本号从 1 起
    ContentId content;                       // 内容寻址（02 §1）
};

struct Persona {
    PrincipalId id;                          // 角色同时是主体
    PrincipalId owner;                       // 创建者
    std::vector<CardRef> versions;           // 只增；回滚 = 指针移动（03 §2）
    std::vector<CardBody> bodies;            // 与 versions 一一对应（M0 驻内存；落 blob 随 M1）
    const CardBody& head() const noexcept { return bodies.back(); }
};

// ── 会话与成员（A3：运行单元；≥2 成员）────────────────────────────────
struct Member {
    PrincipalId principal;
    SessionRole role = SessionRole::Member;
    uint64_t read_seq = 0;                   // 已读游标（02 §4）
};

struct Session {
    SessionId id;
    PrincipalId owner;                       // 创建者，可转让
    std::string canon;                       // 世界观（冻结内容；M0 直存文本，M1 换 blob 引用）
    bool canon_lock = false;                 // 冻结人格类配置（08 §3）
    std::vector<Member> members;             // ≥ 2（I1）
    uint64_t head_seq = 0;                   // 会话流头（含 seq 0 = created）
    std::vector<std::pair<uint64_t, std::string>> messages;   // 投影：seq → 文本

    Member* member(PrincipalId p) noexcept;
    const Member* member(PrincipalId p) const noexcept;
};

// ── 注册表：领域状态的唯一持有者（单写者域内使用；跨线程只读共享由上层保证）──
struct IdentitySnapshot;
class DomainRegistry {
public:
    // 主体
    Result<void> add_principal(Principal p);              // 重复 ID 拒绝
    Principal* principal(PrincipalId id) noexcept;
    const Principal* principal(PrincipalId id) const noexcept;

    // 角色与卡片
    Result<void> add_persona(PrincipalId id, PrincipalId owner, CardBody body);
    Result<uint64_t> publish_card(PrincipalId persona, CardBody body);   // 新版本 seq
    Persona* persona(PrincipalId id) noexcept;

    // 会话
    Result<void> create_session(Session s);
    Session* session(SessionId id) noexcept;
    const Session* session(SessionId id) const noexcept;
    Result<void> add_member(SessionId sid, PrincipalId who, SessionRole role);
    Result<void> set_role(SessionId sid, PrincipalId who, SessionRole role);

    // 身份域只读快照（01 §1 I3 例外）：身份分片单写者在每次变更后发布，
    // 会话 loop 经 atomic shared_ptr 无锁读取。version 随发布递增。
    IdentitySnapshot identity_snapshot(uint64_t version) const;

    std::size_t principal_count() const noexcept { return principals_.size(); }
    std::size_t session_count() const noexcept { return sessions_.size(); }

private:
    std::unordered_map<uint64_t, Principal> principals_;   // ULID 前 8 字节作键
    std::unordered_map<uint64_t, Persona> personas_;
    std::unordered_map<uint64_t, Session> sessions_;

    static uint64_t key(const PrincipalId& id) noexcept;
    static uint64_t key(const SessionId& id) noexcept;
};

// ── 身份域只读快照（01 §1：跨 loop 只有通道与只读版本化内容）────────────
// 会话侧权限检查用它替代直接读注册表 → 零锁、零共享可变状态。
struct IdentitySnapshot {
    uint64_t version = 0;
    std::unordered_map<uint64_t, Principal> principals;
    std::unordered_map<uint64_t, Persona> personas;

    const Principal* principal(PrincipalId id) const noexcept;
    const Persona* persona(PrincipalId id) const noexcept;
};

// ULID 前 8 字节键（注册表/快照/会话键共用约定；PrincipalId/SessionId 通用）
template <typename Tag>
inline uint64_t ulid_key(const UlidId<Tag>& id) noexcept {
    uint64_t k = 0;
    for (std::size_t i = 0; i < 8; ++i) {
        k = (k << 8) | std::to_integer<uint64_t>(id.bytes()[i]);
    }
    return k;
}

// 所有权链（02 §6 ①）：actor ∈ target 的创建链（含 target 自身）→ 全权
bool in_ownership_chain(const DomainRegistry& reg, PrincipalId actor, PrincipalId target);
bool in_ownership_chain(const IdentitySnapshot& snap, PrincipalId actor, PrincipalId target);

// 可见性裁决（② Visibility 决定能否建立关系）：viewer 能否"看见"目标
bool can_interact(const DomainRegistry& reg, PrincipalId viewer, PrincipalId target);
bool can_interact(const IdentitySnapshot& snap, PrincipalId viewer, PrincipalId target);

}   // namespace oly::domain
