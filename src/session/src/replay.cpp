#include "oly/session/replay.hpp"

#include "oly/config/config.hpp"
#include "oly/codec/codec.hpp"

#include <algorithm>
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
            // 配置即内容（08 §1）：补丁事件依序折叠 → 重放自动还原历史配置；
            // (seq, patch) 标签使回滚可剔除 superseded 区间的补丁
            r.config_patches.emplace_back(e.seq, e.payload_bin);
        }
        if (e.type == "director.injected") {
            // 注入走正常感知（11 §3）：恢复时与消息同构折叠
            codec::Decoder d(e.payload_bin);
            (void)d.text();   // kind（narration/ambient）
            const auto text = d.text();
            if (d.ok()) {
                r.messages.emplace_back(e.seq, text);
                r.perceived.push_back(PerceivedMessage{e.seq, domain::PrincipalId::from_bytes(e.actor.bytes()), text});
            }
        }
        if (e.type == "message.recalled") {
            codec::Decoder d(e.payload_bin);
            r.recalled_seqs.insert(d.u64());
        }
        if (e.type == "director.paused" || e.type == "director.resumed") {
            codec::Decoder d(e.payload_bin);
            const auto mode = d.text();
            r.paused = e.type == "director.paused";
            r.frozen = r.paused && mode == "full";
        }
        if (e.type == "session.rolled_back") {
            // 回滚 = 投影层时间线重置（11 §4）：superseded 区间保留日志、不进投影
            codec::Decoder d(e.payload_bin);
            const auto target = d.u64();
            r.superseded_from = d.u64();
            const auto cut_pair = [&](auto& vec) {   // messages: (seq, text)
                vec.erase(std::remove_if(vec.begin(), vec.end(),
                                         [&](const auto& m) { return m.first > target; }),
                          vec.end());
            };
            const auto cut_pm = [&](auto& vec) {     // perceived: PerceivedMessage.seq
                vec.erase(std::remove_if(vec.begin(), vec.end(),
                                         [&](const auto& m) { return m.seq > target; }),
                          vec.end());
            };
            cut_pair(r.messages);
            cut_pm(r.perceived);
            r.config_patches.erase(std::remove_if(r.config_patches.begin(),
                                                  r.config_patches.end(),
                                                  [&](const auto& cp) {
                                                      return cp.first > target;
                                                  }),
                                   r.config_patches.end());
            std::erase_if(r.recalled_seqs, [&](const auto& sq) { return sq > target; });
            r.tick_seq.erase(std::remove_if(r.tick_seq.begin(), r.tick_seq.end(),
                                            [&](const auto& ts) { return ts.second > target; }),
                             r.tick_seq.end());
            r.ticks = 0;
            for (const auto [t, seq] : r.tick_seq) r.ticks = std::max(r.ticks, t);
        }
        r.tick_seq.emplace_back(e.tick, e.seq);
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
