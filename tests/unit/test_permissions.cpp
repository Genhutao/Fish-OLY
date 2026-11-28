// 权限模型：所有权链超集 / 会话层级 / 可见性门控 / Owner 保护
#include <oly/domain/domain.hpp>
#include <oly/domain/permissions.hpp>

#include <catch2/catch_test_macros.hpp>

#include <oly/core/ulid.hpp>

using namespace oly;
using namespace oly::domain;

namespace {

struct World {
    DomainRegistry reg;
    PrincipalId root;      // 链根用户
    PrincipalId persona;   // root 创建的角色 A
    PrincipalId stranger;  // 无关用户
    SessionId sid;

    World() {
        const auto rid = UlidId<TagPrincipal>::from_string("01ARZ3NDEKTSV4RRFFQ69G5FAV").value();
        const auto aid = UlidId<TagPrincipal>::from_string("01ARZ3NDEKTSV4RRFFQ69G5FBW").value();
        const auto sid3 = UlidId<TagPrincipal>::from_string("01ARZ3NDEKTSV4RRFFQ69G5FCX").value();
        root = rid;
        persona = aid;
        stranger = sid3;

        Principal r;
        r.id = rid;
        r.owner = rid;
        REQUIRE(reg.add_principal(r).has_value());

        Principal a;
        a.id = aid;
        a.kind = PrincipalKind::Persona;
        a.owner = root;
        REQUIRE(reg.add_principal(a).has_value());

        Principal s;
        s.id = sid3;
        REQUIRE(reg.add_principal(s).has_value());

        Session sess;
        sess.id = UlidId<TagSession>::from_string("01ARZ3NDEKTSV4RRFFQ69G5FDY").value();
        sess.owner = root;
        sess.canon = "测试世界观";
        sess.members.push_back(Member{root, SessionRole::Owner, 0});
        sess.members.push_back(Member{persona, SessionRole::Member, 0});
        sid = sess.id;
        REQUIRE(reg.create_session(sess).has_value());
    }
};

}   // namespace

TEST_CASE("会话成员可发言，非成员被拒", "[domain][perm]") {
    World w;
    const auto member = check(w.reg, Action::SendMessage, w.sid, w.persona);
    REQUIRE(member.allow);
    const auto stranger = check(w.reg, Action::SendMessage, w.sid, w.stranger);
    REQUIRE_FALSE(stranger.allow);
    REQUIRE(stranger.reason == "not a member");
}

TEST_CASE("所有权链超集：链根绕过会话层级", "[domain][perm]") {
    World w;
    // root 甚至不是 Admin（是 Owner），这里验证链根对任意动作放行
    REQUIRE(check(w.reg, Action::TransferSession, w.sid, w.root, w.stranger).allow);
    // 非 Owner 成员尝试转让 → 拒绝
    REQUIRE_FALSE(check(w.reg, Action::TransferSession, w.sid, w.persona, w.stranger).allow);
}

TEST_CASE("AddMember 需要 Admin+ 且目标可见", "[domain][perm]") {
    World w;
    // Member 角色邀请 → 层级拒绝
    REQUIRE_FALSE(check(w.reg, Action::AddMember, w.sid, w.persona, w.stranger).allow);
    // root（链上）邀请 stranger（Public 假设：默认 Private → 拒）
    w.reg.principal(w.stranger)->visibility = Visibility::Private;
    REQUIRE(check(w.reg, Action::AddMember, w.sid, w.root, w.stranger).allow == false);
    // 公开后放行
    w.reg.principal(w.stranger)->visibility = Visibility::Public;
    REQUIRE(check(w.reg, Action::AddMember, w.sid, w.root, w.stranger).allow);
}

TEST_CASE("角色由角色创建：链根拥有终控（Q4 裁决）", "[domain][perm]") {
    World w;
    // persona 不在所有权链上，无法编辑自己的卡片（除非显式授权，M1）
    REQUIRE_FALSE(check(w.reg, Action::EditCard, w.sid, w.persona, w.persona).allow);
    // 链根（root）拥有其下角色卡片的全部权限
    REQUIRE(check(w.reg, Action::EditCard, w.sid, w.root, w.persona).allow);
}

TEST_CASE("RemoveMember 不能移除 Owner；SetRole 不能降级 Owner", "[domain][perm]") {
    World w;
    REQUIRE_FALSE(check(w.reg, Action::RemoveMember, w.sid, w.root, w.root).allow);
    REQUIRE_FALSE(check(w.reg, Action::SetRole, w.sid, w.root, w.root, SessionRole::Member).allow);
    REQUIRE(check(w.reg, Action::RemoveMember, w.sid, w.root, w.persona).allow);
}
