#include "oly/session/session_runtime.hpp"

#include "oly/domain/permissions.hpp"
#include "oly/core/hash.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace oly::session {

using oly::domain::Action;
using oly::domain::Decision;
using oly::domain::DomainRegistry;
using oly::domain::SessionRole;

uint64_t SessionRuntime::key(const domain::SessionId id) noexcept {
    uint64_t k = 0;
    for (std::size_t i = 0; i < 8; ++i) {
        k = (k << 8) | std::to_integer<uint64_t>(id.bytes()[i]);
    }
    return k;
}

Result<SessionRuntime> SessionRuntime::create(const domain::SessionId id, Deps deps,
                                              const uint64_t now_ms) {
    const domain::Session* s = deps.registry.session(id);
    if (s == nullptr) return unexpected(err("session:not-found", "session missing in registry"));

    SessionRuntime rt;
    rt.id_ = id;
    rt.registry_ = &deps.registry;
    rt.store_ = &deps.store;
    rt.wheel_ = &deps.wheel;
    rt.tick_period_ms_ = deps.tick_period_ms;

    // seq 0 = session.created（04 §3 流开篇；prev_hash = 创世常量）
    codec::Encoder p;
    p.text(s->canon);
    p.u32(static_cast<uint32_t>(s->members.size()));
    for (const auto& m : s->members) {
        p.ulid(m.principal.bytes());
        p.u8(static_cast<uint8_t>(m.role));
    }
    rt.emit_fact(events::EventKind::Fact,
                 Command{CommandId{}, s->owner, s->owner, CmdAction::Tick, 0, {}},
                 "session.created", std::move(p).take());
    if (const auto r = rt.store_->append(rt.pending_facts_); !r) {
        return unexpected(r.error());
    }
    rt.pending_facts_.clear();

    // 登记首拍：wheel 到期 → advance_clock 重排下一拍（自续）
    rt.wheel_->insert(SessionRuntime::key(id), now_ms + rt.tick_period_ms_);
    return rt;
}

SessionRuntime::Enqueue SessionRuntime::enqueue(Command cmd) {
    if (seen_.contains(cmd.id)) return Enqueue::Duplicate;
    seen_.insert(cmd.id);
    return queue_.push(exec::LaneQueue<Command>::Lane::Interactive, std::move(cmd)) ==
                   exec::LaneQueue<Command>::Push::Ok
               ? Enqueue::Ok
               : Enqueue::InteractiveFull;
}

SessionRuntime::Enqueue SessionRuntime::enqueue_background(Command cmd) {
    if (seen_.contains(cmd.id)) return Enqueue::Duplicate;
    seen_.insert(cmd.id);
    return queue_.push(exec::LaneQueue<Command>::Lane::Background, std::move(cmd)) ==
                   exec::LaneQueue<Command>::Push::Ok
               ? Enqueue::Ok
               : Enqueue::Duplicate;   // 背景道丢弃已计入 dropped_
}

void SessionRuntime::attach_autonomy(const domain::PrincipalId persona,
                                     std::unique_ptr<AutonomyDriver> driver) {
    drivers_.emplace_back(persona, std::move(driver));
}

std::size_t SessionRuntime::advance_clock(const uint64_t now_ms) {
    std::size_t produced = 0;
    for (const auto due_key : wheel_->advance(now_ms)) {
        if (due_key != key(id_)) continue;   // 轮式调度器按 loop 共享：只处理本会话的到期键
        ++ticks_;
        for (auto& [persona, driver] : drivers_) {
            auto line = driver->on_tick(ticks_);
            if (!line) continue;   // 沉默也是行动（06 §2.4）：不产生命令即不写事件
            Command c;
            c.id = CommandId::from_bytes(
                [this, persona] {   // 确定性命令 ID：hash(stream, tick, persona) —— 重放可复现
                    oly::codec::Encoder e;
                    e.u64(key(id_));
                    e.u64(ticks_);
                    e.ulid(persona.bytes());
                    const ContentId h = sha256(e.span());
                    std::array<std::byte, 16> out{};
                    std::copy(h.bytes.begin(), h.bytes.begin() + 16, out.begin());
                    return out;
                }());
            c.actor = persona;
            if (const domain::Principal* pr = registry_->principal(persona);
                pr != nullptr) {
                c.on_behalf_of = pr->owner;
            }
            c.action = CmdAction::SendMessage;
            c.tick = ticks_;
            c.text = std::move(*line);
            enqueue_background(std::move(c));
            ++produced;
        }
        // 自续拍：下一拍登记（暂停 = remove，导演 11 §2）
        wheel_->insert(key(id_), now_ms + tick_period_ms_);
    }
    return produced;
}

void SessionRuntime::emit_fact(const events::EventKind kind, const Command& cmd, std::string type,
                               std::vector<std::byte> payload) {
    events::EventEnvelope e;
    e.stream.kind = events::StreamKind::Session;
    e.stream.id = id_.bytes();
    e.seq = registry_->session(id_)->head_seq++;
    e.tick = cmd.tick;
    e.kind = kind;
    e.command = cmd.id;
    e.actor = cmd.actor;
    e.on_behalf_of = cmd.on_behalf_of;
    e.type = std::move(type);
    e.payload_bin = std::move(payload);
    events::seal(e);   // store 会续接 prev_hash（链属主，05 §3）
    pending_facts_.push_back(std::move(e));
}

Result<uint64_t> SessionRuntime::admit_and_apply(const Command& cmd,
                                                 const exec::LaneQueue<Command>::Lane /*lane*/) {
    // 准入（04 §5）：幂等已在入队过滤；此处权限 → 应用/拒绝
    Decision d;
    switch (cmd.action) {
        case CmdAction::SendMessage:
            d = domain::check(*registry_, domain::Action::SendMessage, id_, cmd.actor);
            break;
        case CmdAction::Tick:
            d = Decision::ok();   // 系统事件（调度器来源）
            break;
    }

    if (!d.allow) {
        // 拒绝亦为事实（04 §5：denial 事件可审计）
        codec::Encoder p;
        p.text(d.reason);
        emit_fact(events::EventKind::Denied, cmd, "cmd.denied", std::move(p).take());
        return 0;
    }

    uint64_t produced = 0;
    switch (cmd.action) {
        case CmdAction::SendMessage: {
            codec::Encoder p;
            p.ulid(cmd.actor.bytes());
            p.text(cmd.text);
            emit_fact(events::EventKind::Fact, cmd, "message.appended", std::move(p).take());
            ++produced;
            if (auto* s = registry_->session(id_); s != nullptr) {
                s->messages.emplace_back(s->head_seq - 1, cmd.text);   // 投影更新
                if (auto* m = s->member(cmd.actor); m != nullptr) m->read_seq = s->head_seq - 1;
            }
            break;
        }
        case CmdAction::Tick:
            break;   // 拍本身无事实
    }
    return produced;
}

Result<uint64_t> SessionRuntime::drain(const std::size_t max_items) {
    const auto items = queue_.drain(max_items);
    uint64_t facts = 0;
    for (const auto& cmd : items) {
        auto r = admit_and_apply(cmd,
                                 cmd.action == CmdAction::Tick
                                     ? exec::LaneQueue<Command>::Lane::Background
                                     : exec::LaneQueue<Command>::Lane::Interactive);
        if (!r) return unexpected(r.error());
        facts += r.value();
    }
    // 一拍一提交（组提交雏形，05 §3）：本批全部事实一次落盘，成功后可见
    if (!pending_facts_.empty()) {
        if (const auto r = store_->append(pending_facts_); !r) return unexpected(r.error());
        pending_facts_.clear();
    }
    return facts;
}

}   // namespace oly::session
