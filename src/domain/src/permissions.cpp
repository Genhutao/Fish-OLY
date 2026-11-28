#include "oly/domain/permissions.hpp"

#include <utility>

namespace oly::domain {
namespace {

// 角色等级：数值小者权限高（Owner=1 < Admin=2 < Member=3）
int rank(SessionRole r) noexcept { return static_cast<int>(r); }

Decision require_member(const Session& s, PrincipalId actor, SessionRole min_role) {
    const Member* m = s.member(actor);
    if (m == nullptr) return Decision::deny("not a member");
    if (rank(m->role) > rank(min_role)) return Decision::deny("insufficient session role");
    return Decision::ok();
}

// 三源合一（02 §6）对"注册表或身份快照"共用：① 所有权链超集（I4）
// ② 会话角色层级 ③ 显式授权边（M1 接入 grants）
template <typename Src>
Decision check_impl(const Src& src, const Session& s, const Action action, const PrincipalId actor,
                    const PrincipalId target, const SessionRole new_role) {
    // 所有权链超集（I4）：链上主体绕过会话层级检查（仍是对象的最终控制人）
    const bool chain = in_ownership_chain(src, actor, s.owner);

    switch (action) {
        case Action::SendMessage:
        case Action::ReadSession:
            if (chain) return Decision::ok();
            if (s.member(actor) == nullptr) return Decision::deny("not a member");
            return Decision::ok();

        case Action::AddMember: {
            if (!chain) {
                if (const auto d = require_member(s, actor, SessionRole::Admin); !d.allow) return d;
            }
            if (!can_interact(src, actor, target)) return Decision::deny("target not visible to inviter");
            return Decision::ok();
        }

        case Action::RemoveMember: {
            if (!chain) {
                if (const auto d = require_member(s, actor, SessionRole::Admin); !d.allow) return d;
            }
            const Member* m = s.member(target);
            if (m == nullptr) return Decision::deny("target not a member");
            if (m->role == SessionRole::Owner) return Decision::deny("cannot remove owner (transfer first)");
            return Decision::ok();
        }

        case Action::SetRole: {
            if (!chain) {
                if (const auto d = require_member(s, actor, SessionRole::Owner); !d.allow) return d;
            }
            const Member* m = s.member(target);
            if (m == nullptr) return Decision::deny("target not a member");
            if (m->role == SessionRole::Owner && new_role != SessionRole::Owner) {
                return Decision::deny("cannot demote owner (transfer first)");
            }
            return Decision::ok();
        }

        case Action::TransferSession: {
            if (!chain && s.member(actor) != nullptr &&
                s.member(actor)->role != SessionRole::Owner) {
                return Decision::deny("only owner can transfer");
            }
            if (src.principal(target) == nullptr) return Decision::deny("target principal not found");
            return Decision::ok();
        }

        case Action::DirectorControl:
            if (!chain) {
                if (const auto d = require_member(s, actor, SessionRole::Admin); !d.allow) return d;
            }
            return Decision::ok();

        case Action::ConfigSet:
        case Action::RecallMessage:
            if (!chain) {
                if (const auto d = require_member(s, actor, SessionRole::Admin); !d.allow) return d;
            }
            return Decision::ok();

        case Action::Rollback:
            if (!chain && s.owner != actor) return Decision::deny("rollback is owner-only");
            return Decision::ok();

        case Action::EditCard: {
            // 卡片所有权链从**属主**起算（target 是卡片实体，不是权限来源）；
            // 角色自改的显式授权边随 M1 grants 接入（02 §6 ③）
            const Principal* card = src.principal(target);
            if (card == nullptr) return Decision::deny("card owner not found");
            if (in_ownership_chain(src, actor, card->owner)) return Decision::ok();
            return Decision::deny("outside card ownership chain");
        }
    }
    return Decision::deny("unknown action");
}

}   // namespace

Decision check(const DomainRegistry& reg, const Action action, const SessionId session,
               const PrincipalId actor, const PrincipalId target, const SessionRole new_role) {
    const Session* s = reg.session(session);
    if (s == nullptr) return Decision::deny("session not found");
    return check_impl(reg, *s, action, actor, target, new_role);
}

Decision check(const IdentitySnapshot& identity, const Session& session, const Action action,
               const PrincipalId actor, const PrincipalId target, const SessionRole new_role) {
    return check_impl(identity, session, action, actor, target, new_role);
}

}   // namespace oly::domain
