#include "oly/domain/domain.hpp"

#include "oly/codec/codec.hpp"
#include "oly/core/hash.hpp"

#include <algorithm>

namespace oly::domain {
namespace {

// 卡片体内容寻址（02 §1）：规范编码后 SHA-256
ContentId sha256_of_body(const CardBody& body) {
    codec::Encoder e;
    e.text(body.name);
    e.text(body.core_anchor);
    e.text(body.description);
    return sha256(e.span());
}

}   // namespace

Member* Session::member(const PrincipalId p) noexcept {
    const auto it = std::find_if(members.begin(), members.end(),
                                 [&](const Member& m) { return m.principal == p; });
    return it == members.end() ? nullptr : &*it;
}

const Member* Session::member(const PrincipalId p) const noexcept {
    const auto it = std::find_if(members.begin(), members.end(),
                                 [&](const Member& m) { return m.principal == p; });
    return it == members.end() ? nullptr : &*it;
}

uint64_t DomainRegistry::key(const PrincipalId& id) noexcept { return ulid_key(id); }

uint64_t DomainRegistry::key(const SessionId& id) noexcept { return ulid_key(id); }

Result<void> DomainRegistry::add_principal(Principal p) {
    const uint64_t k = key(p.id);
    if (principals_.contains(k)) return unexpected(err("domain:exists", "principal already exists"));
    principals_.emplace(k, std::move(p));
    return {};
}

Principal* DomainRegistry::principal(const PrincipalId id) noexcept {
    const auto it = principals_.find(key(id));
    return it == principals_.end() ? nullptr : &it->second;
}

const Principal* DomainRegistry::principal(const PrincipalId id) const noexcept {
    return const_cast<DomainRegistry*>(this)->principal(id);
}

Result<void> DomainRegistry::add_persona(const PrincipalId id, const PrincipalId owner, CardBody body) {
    const uint64_t k = key(id);
    if (personas_.contains(k)) return unexpected(err("domain:exists", "persona already exists"));
    Persona p;
    p.id = id;
    p.owner = owner;
    CardRef ref;
    ref.seq = 1;
    ref.content = sha256_of_body(body);
    p.versions.push_back(ref);
    p.bodies.push_back(std::move(body));
    personas_.emplace(k, std::move(p));
    return {};
}

Result<uint64_t> DomainRegistry::publish_card(const PrincipalId persona, CardBody body) {
    auto* p = this->persona(persona);
    if (p == nullptr) return unexpected(err("domain:not-found", "persona not found"));
    CardRef ref;
    ref.seq = p->versions.back().seq + 1;
    ref.content = sha256_of_body(body);
    p->versions.push_back(ref);
    p->bodies.push_back(std::move(body));
    return ref.seq;
}

Persona* DomainRegistry::persona(const PrincipalId id) noexcept {
    const auto it = personas_.find(key(id));
    return it == personas_.end() ? nullptr : &it->second;
}

Result<void> DomainRegistry::create_session(Session s) {
    const uint64_t k = key(s.id);
    if (sessions_.contains(k)) return unexpected(err("domain:exists", "session already exists"));
    if (s.members.size() < 2) return unexpected(err("domain:invalid", "session needs >= 2 members (I1)"));
    s.head_seq = 0;
    sessions_.emplace(k, std::move(s));
    return {};
}

Session* DomainRegistry::session(const SessionId id) noexcept {
    const auto it = sessions_.find(key(id));
    return it == sessions_.end() ? nullptr : &it->second;
}

const Session* DomainRegistry::session(const SessionId id) const noexcept {
    return const_cast<DomainRegistry*>(this)->session(id);
}

Result<void> DomainRegistry::add_member(const SessionId sid, const PrincipalId who, const SessionRole role) {
    auto* s = session(sid);
    if (s == nullptr) return unexpected(err("domain:not-found", "session not found"));
    if (s->member(who) != nullptr) return unexpected(err("domain:exists", "already a member"));
    s->members.push_back(Member{who, role, 0});
    return {};
}

Result<void> DomainRegistry::set_role(const SessionId sid, const PrincipalId who, const SessionRole role) {
    auto* s = session(sid);
    if (s == nullptr) return unexpected(err("domain:not-found", "session not found"));
    auto* m = s->member(who);
    if (m == nullptr) return unexpected(err("domain:not-found", "not a member"));
    m->role = role;
    return {};
}

IdentitySnapshot DomainRegistry::identity_snapshot(const uint64_t version) const {
    IdentitySnapshot s;
    s.version = version;
    s.principals = principals_;
    s.personas = personas_;
    return s;
}

const Principal* IdentitySnapshot::principal(const PrincipalId id) const noexcept {
    const auto it = principals.find(ulid_key(id));
    return it == principals.end() ? nullptr : &it->second;
}

const Persona* IdentitySnapshot::persona(const PrincipalId id) const noexcept {
    const auto it = personas.find(ulid_key(id));
    return it == personas.end() ? nullptr : &it->second;
}

namespace {

// 链/可见性对"注册表或快照"共用一份逻辑（两者暴露同形的 principal/persona 查询）
template <typename Src>
bool chain_impl(const Src& src, const PrincipalId actor, const PrincipalId target) {
    // 沿创建链向上：链上任一主体（含 target 自身）对 target 全权（I4 超集不变量）
    PrincipalId cur = target;
    constexpr int kMaxDepth = 32;   // 链深上限：防环（创建链构造上无环，此为防御）
    for (int depth = 0; depth < kMaxDepth; ++depth) {
        if (cur == actor) return true;
        const Principal* p = src.principal(cur);
        if (p == nullptr) return false;
        if (p->owner.is_nil()) return false;   // 链根（自身即根）仍不匹配
        cur = p->owner;
    }
    return false;
}

template <typename Src>
bool interact_impl(const Src& src, const PrincipalId viewer, const PrincipalId target) {
    if (viewer == target) return true;
    if (chain_impl(src, viewer, target)) return true;
    const Principal* t = src.principal(target);
    if (t == nullptr) return false;
    switch (t->visibility) {
        case Visibility::Public:
            return true;
        case Visibility::Private:
            return false;
        case Visibility::Shared:
            return std::find(t->shared_with.begin(), t->shared_with.end(), viewer) !=
                   t->shared_with.end();
    }
    return false;
}

}   // namespace

bool in_ownership_chain(const DomainRegistry& reg, const PrincipalId actor, const PrincipalId target) {
    return chain_impl(reg, actor, target);
}

bool in_ownership_chain(const IdentitySnapshot& snap, const PrincipalId actor,
                        const PrincipalId target) {
    return chain_impl(snap, actor, target);
}

bool can_interact(const DomainRegistry& reg, const PrincipalId viewer, const PrincipalId target) {
    return interact_impl(reg, viewer, target);
}

bool can_interact(const IdentitySnapshot& snap, const PrincipalId viewer, const PrincipalId target) {
    return interact_impl(snap, viewer, target);
}

}   // namespace oly::domain
