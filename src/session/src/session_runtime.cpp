#include "oly/session/session_runtime.hpp"

#include "oly/log/log.hpp"

#include "oly/domain/permissions.hpp"
#include "oly/core/hash.hpp"
#include "oly/session/replay.hpp"
#include "oly/session/snapshot.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <utility>

namespace oly::session {

using oly::domain::Action;
using oly::domain::Decision;
using oly::domain::DomainRegistry;
using oly::domain::SessionRole;

namespace {

uint32_t q4(const float v) noexcept {   // 定点 4 位小数（attention/tau 入事实载荷）
    return static_cast<uint32_t>(std::max(0.0f, v) * 10000.0f);
}

}   // namespace

uint64_t SessionRuntime::key(const domain::SessionId id) noexcept {
    uint64_t k = 0;
    for (std::size_t i = 0; i < 8; ++i) {
        k = (k << 8) | std::to_integer<uint64_t>(id.bytes()[i]);
    }
    return k;
}

Result<SessionRuntime> SessionRuntime::create(const domain::SessionId id, const Deps deps,
                                              const uint64_t now_ms) {
    const domain::Session* s = deps.registry.session(id);
    if (s == nullptr) return unexpected(err("session:not-found", "session missing in registry"));

    SessionRuntime rt;
    rt.id_ = id;
    rt.registry_ = &deps.registry;
    rt.store_ = &deps.store;
    rt.wheel_ = &deps.wheel;
    rt.tick_period_ms_ = deps.tick_period_ms;
    rt.snap_dir_ = deps.snapshot_dir;
    rt.identity_ = deps.identity;
    rt.on_commit_ = deps.on_commit;
    rt.effective_ = config::finalize(config::Values{});

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

    // 感知游标 = "seq ≤ 游标者已消费"；create 时所有消息（seq ≥ 1）都是新到
    rt.perceived_seq_ = rt.registry_->session(id)->head_seq - 1;
    // 登记首拍：wheel 到期 → advance_clock 重排下一拍（自续）
    rt.wheel_->insert(SessionRuntime::key(id), now_ms + rt.tick_period_ms_);
    return rt;
}

Result<SessionRuntime> SessionRuntime::recover(const domain::SessionId id, const Deps deps,
                                               const uint64_t now_ms) {
    const domain::Session* s = deps.registry.session(id);
    if (s == nullptr) return unexpected(err("session:not-found", "session missing in registry"));

    // 快照（05 §5）：目录未启用/无快照 → nullopt → 全量重放
    Result<std::optional<SnapshotData>> snap = std::optional<SnapshotData>{};
    if (!deps.snapshot_dir.empty()) {
        snap = load_latest_snapshot(deps.snapshot_dir);
        if (!snap) return unexpected(snap.error());
    }

    auto rec = session::recover(deps.store, snap.value());
    if (!rec) return unexpected(rec.error());

    // 灌事实侧投影（canon/成员由调用方按身份域先行装配）
    domain::Session* s2 = deps.registry.session(id);
    s2->head_seq = rec->head_seq;
    s2->messages = std::move(rec->messages);

    SessionRuntime rt;
    rt.id_ = id;
    rt.registry_ = &deps.registry;
    rt.store_ = &deps.store;
    rt.wheel_ = &deps.wheel;
    rt.tick_period_ms_ = deps.tick_period_ms;
    rt.snap_dir_ = deps.snapshot_dir;
    rt.identity_ = deps.identity;
    rt.on_commit_ = deps.on_commit;
    rt.ticks_ = rec->ticks;
    rt.perceived_ = std::move(rec->perceived);
    rt.perceived_seq_ = rec->head_seq - 1;   // 锚内消息已在窗口中，后续增量才属新到
    for (const auto& cid : rec->command_ids) rt.seen_.insert(cid);

    // 配置即内容（08 §1）：补丁依序回放 → 生效配置还原（确定性叙事完整）
    rt.session_values_ = config::Values{};
    for (const auto& raw : rec->config_patches) {
        rt.session_values_ = config::apply(rt.session_values_, config::decode_patch(raw));
    }
    rt.effective_ = config::finalize(rt.session_values_);
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

std::size_t SessionRuntime::on_tick(const uint64_t now_ms) {
    std::size_t produced = 0;
    ++ticks_;
    const auto* s = registry_->session(id_);

        // 感知装配（06 §2：感知输入 = 事实事件与 tick）
        TickContext ctx;
        ctx.tick = ticks_;
        ctx.now_ms = now_ms;
        ctx.stream_key = key(id_);
        ctx.registry = registry_;
        ctx.session = s;
        if (!perceived_.empty()) {
            const auto w = std::min<std::size_t>(kPerceiveWindow, perceived_.size());
            ctx.window = std::span(perceived_).last(w);
            std::size_t begin = 0;
            while (begin < perceived_.size() && perceived_[begin].seq <= perceived_seq_) ++begin;
            ctx.new_messages = std::span(perceived_).subspan(begin);
        }
        perceived_seq_ = s->head_seq;   // 本拍感知消费至当前事实头

        const Command tick_cmd{CommandId{}, s->owner, s->owner, CmdAction::Tick, ticks_, {}};
        for (auto& [persona, driver] : drivers_) {
            auto oc = driver->on_tick(ctx);
            if (oc.speak) {
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
                if (const domain::Principal* pr = registry_->principal(persona); pr != nullptr) {
                    c.on_behalf_of = pr->owner;
                }
                c.action = CmdAction::SendMessage;
                c.tick = ticks_;
                c.text = std::move(oc.text);
                enqueue_background(std::move(c));
                ++produced;
            }
            // 沉默事件化（06 §2.4）：被@沉默必记 / borderline 采样
            if (oc.silence_noted) {
                codec::Encoder p;
                p.ulid(persona.bytes());
                p.u32(q4(oc.attention));
                p.u32(q4(oc.tau));
                emit_fact(events::EventKind::Notification, tick_cmd, "autonomy.silent",
                          std::move(p).take());
            }
            // decision.recorded（06 §3 第 5 步雏形；ScriptedDriver 无 model 不记）
            if (!oc.model.empty()) {
                codec::Encoder p;
                p.ulid(persona.bytes());
                p.text(oc.model);
                p.u8(oc.speak ? 1 : 0);
                p.u8(oc.fallback ? 1 : 0);
                p.u32(oc.tokens_in);
                p.u32(oc.tokens_out);
                p.u32(q4(oc.attention));
                p.u32(q4(oc.tau));
                p.content(oc.config_id.bytes);   // 08 §1：行为精确归因到参数值（尾部追加，读端向前兼容）
                emit_fact(events::EventKind::Fact, tick_cmd, "decision.recorded",
                          std::move(p).take());
            }
        }
        // 自续拍：下一拍登记（暂停 = remove，导演 11 §2）
        wheel_->insert(key(id_), now_ms + tick_period_ms_);
    return produced;
}

std::size_t SessionRuntime::advance_clock(const uint64_t now_ms) {
    // 自管模式：轮式到期 → 本会话拍（到期键每拍只消费一次——分片托管下
    // 由 Shard::on_beat 统一摘槽后调 on_tick，不再走此路径）
    std::size_t produced = 0;
    for (const auto due_key : wheel_->advance(now_ms)) {
        if (due_key == key(id_)) produced += on_tick(now_ms);
    }
    return produced;
}

void SessionRuntime::emit_fact(const events::EventKind kind, const Command& cmd, std::string type,
                               std::vector<std::byte> payload) {
    events::EventEnvelope e;
    e.stream.kind = events::StreamKind::Session;
    e.stream.id = id_.bytes();
    auto* reg_sess = registry_->session(id_);
    if (reg_sess->head_seq == 1 && !reg_sess->messages.empty()) {
        OLY_LOG_CRITICAL("seq regression sid={} sess@{:p} msgs={} ticks_={}", id_.str(),
                         static_cast<const void*>(reg_sess), reg_sess->messages.size(), ticks_);
    }
    e.seq = reg_sess->head_seq++;
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
    // 准入（04 §5）：幂等已在入队过滤；此处权限 → 应用/拒绝。
    // 权限源双轨：身份快照（多 loop 无锁）或注册表（单 loop 单写者直查）
    Decision d;
    auto* s = registry_->session(id_);
    switch (cmd.action) {
        case CmdAction::SendMessage:
        case CmdAction::AdvanceRead:
            d = identity_ != nullptr && s != nullptr
                    ? domain::check(*identity_, *s, domain::Action::SendMessage, cmd.actor)
                    : domain::check(*registry_, domain::Action::SendMessage, id_, cmd.actor);
            break;
        case CmdAction::ConfigSet:
            d = identity_ != nullptr && s != nullptr
                    ? domain::check(*identity_, *s, domain::Action::ConfigSet, cmd.actor)
                    : domain::check(*registry_, domain::Action::ConfigSet, id_, cmd.actor);
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
            if (s != nullptr) {
                s->messages.emplace_back(s->head_seq - 1, cmd.text);   // 投影更新
                if (auto* m = s->member(cmd.actor); m != nullptr) m->read_seq = s->head_seq - 1;
                // 感知缓冲（驱动窗口输入；非事实态，重放可重建）
                perceived_.push_back(
                    PerceivedMessage{s->head_seq - 1, cmd.actor, cmd.text});
                if (perceived_.size() > kPerceiveWindow) perceived_.erase(perceived_.begin());
            }
            break;
        }
        case CmdAction::AdvanceRead:
            // 读游标推进（10 §4.3 read.advance）：成员本地态，不产生事实事件
            if (s != nullptr) {
                if (auto* m = s->member(cmd.actor); m != nullptr) {
                    m->read_seq = std::max(m->read_seq, cmd.read_seq);
                }
            }
            break;
        case CmdAction::ConfigSet: {
            // 配置即内容（08 §1）：补丁 → 包络裁决（越界拒绝写 denial）→ 应用 → 事件
            const auto patch = config::decode_patch(cmd.config_patch);
            if (patch.empty()) {
                codec::Encoder p;
                p.text("config:decode");
                emit_fact(events::EventKind::Denied, cmd, "cmd.denied", std::move(p).take());
                return 0;
            }
            const auto candidate = config::apply(session_values_, patch);
            const auto clamped = config::clamp(candidate, config::Envelope{});
            if (!(candidate == clamped)) {
                // 包络是承诺不是默认值：越界拒绝（历史 blob 越界由合并端钳制兜底）
                codec::Encoder p;
                p.text("config:envelope");
                emit_fact(events::EventKind::Denied, cmd, "cmd.denied", std::move(p).take());
                return 0;
            }
            session_values_ = clamped;
            effective_ = config::finalize(session_values_);
            // 变更即命令事件：config.set 落会话流（载荷 = 补丁规范编码）→ 重放还原
            emit_fact(events::EventKind::Fact, cmd, "config.set",
                      config::encode_patch(patch));
            return 0;
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
        try {
            if (on_commit_) on_commit_(pending_facts_);   // 先持久后可见（05 §3）：游标后外送
        } catch (...) {
            OLY_LOG_ERROR("on_commit sink threw; facts {}..{} still committed",
                          pending_facts_.front().seq, pending_facts_.back().seq);
        }
        pending_facts_.clear();
    }
    return facts;
}

Result<ContentId> SessionRuntime::snapshot_now() const {
    if (snap_dir_.empty()) return unexpected(err("session:snapshot-off", "snapshot dir not set"));
    SnapshotData s;
    s.seq = committed_seq();
    s.ticks = ticks_;
    if (const auto* sess = registry_->session(id_); sess != nullptr) s.messages = sess->messages;
    return write_snapshot(snap_dir_, s);
}

}   // namespace oly::session
