// OLY · session —— 启动重放（05 §6）：最近快照 → 载入投影 → seq+1 重放补缺口；
// 无快照 → 全量重放（确定性 fold 事实事件）。会话迁移（换 loop/换节点）走同一份代码。
// fold 只折事实域：message.appended → 投影/感知缓冲；created/tick/denial 不改投影。
// 幂等集跨重启重建（04 §5 恰一次准入的事件侧来源：envelope.command）。
#pragma once

#include "oly/core/id.hpp"
#include "oly/core/result.hpp"
#include "oly/events/envelope.hpp"
#include "oly/logdb/store.hpp"
#include "oly/session/autonomy.hpp"
#include "oly/session/snapshot.hpp"

#include <cstdint>
#include <unordered_set>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace oly::session {

struct Recovered {
    uint64_t head_seq = 0;             // 下一待写 seq（= 最后已提交 + 1）
    uint64_t ticks = 0;                // 拍计数（事件 tick 最大值）
    std::vector<std::pair<uint64_t, std::string>> messages;   // 事实投影
    std::vector<PerceivedMessage> perceived;                  // 感知缓冲（驱动窗口用）
    std::vector<CommandId> command_ids;                       // 幂等集重建
    std::vector<std::pair<uint64_t, std::vector<std::byte>>> config_patches;   // (seq, patch)，08 §1
    std::unordered_set<uint64_t> recalled_seqs;               // message.recalled 标记集（04 §2）
    std::vector<std::pair<uint64_t, uint64_t>> tick_seq;      // (tick, seq) 轨迹（回滚重算 ticks）
    uint64_t superseded_from = 0;                             // 回滚水位（11 §4；0 = 未回滚）
    bool paused = false;                                      // director.paused（11 §2）
    bool frozen = false;                                      // pause full（只读冻结）
};

// store：本会话段存储（读端全链校验）。snap 为空 → 从创世全量重放。
Result<Recovered> recover(logdb::SegmentStore& store, std::optional<SnapshotData> snap);

}   // namespace oly::session
