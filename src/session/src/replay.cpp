#include "oly/session/replay.hpp"

#include "oly/config/config.hpp"
#include "oly/codec/codec.hpp"

#include <limits>

namespace oly::session {

// 注：快照只含 (seq,text) 投影，不含 actor —— 锚前消息不回感知窗口（Recent 变薄），
// 属 M0 已知简化；M1 快照体加入 actor（或感知缓冲独立成态）后消除。
Result<Recovered> recover(logdb::SegmentStore& store, const std::optional<SnapshotData> snap) {
    Recovered r;
    uint64_t from = 0;
    if (snap.has_value()) {
        from = snap->seq + 1;
        r.messages = snap->messages;
        r.ticks = snap->ticks;
    }

    // 全流读回（读端本就自创世走全链校验）：投影只折锚后事件，幂等集折全流 ——
    // 快照锚前的命令 ID 同样要参与恰一次判定（04 §5 跨重启）
    auto events = store.read_range(0, std::numeric_limits<std::size_t>::max());
    if (!events) return unexpected(events.error());

    for (auto& e : events.value()) {
        if (e.type == "message.appended" && e.seq >= from) {
            codec::Decoder d(e.payload_bin);
            const auto actor = d.ulid();
            auto text = d.text();
            if (!d.ok()) return unexpected(err("session:corrupt", "bad message.appended payload"));
            const auto seq = e.seq;
            r.messages.emplace_back(seq, text);
            r.perceived.push_back(PerceivedMessage{seq, domain::PrincipalId::from_bytes(actor),
                                                   r.messages.back().second});
        }
        if (e.type == "config.set") {
            // 配置即内容（08 §1）：补丁事件依序折叠 → 重放自动还原历史配置
            r.config_patches.push_back(e.payload_bin);
        }
        if (e.tick > r.ticks) r.ticks = e.tick;
        if (!e.command.is_nil()) r.command_ids.push_back(e.command);
    }

    // 头游标锚定：必须严格续接完成游标（快照超前/落后于日志 = 锚不一致）
    r.head_seq = events.value().empty() ? (snap.has_value() ? snap->seq + 1 : 0)
                                        : events.value().back().seq + 1;
    const uint64_t committed = store.committed_seq().value_or(0);
    if (r.head_seq != committed + 1) {
        return unexpected(err("session:anchor-mismatch",
                              "recovered head_seq " + std::to_string(r.head_seq) +
                                  " != committed " + std::to_string(committed) + " + 1"));
    }
    return r;
}

}   // namespace oly::session
