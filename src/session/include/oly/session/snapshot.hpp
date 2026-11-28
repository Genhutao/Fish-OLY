// OLY · session —— 快照协议（05 §5）：每流一快照文件，内容 = 投影的版本化二进制。
//   清单锚 = {seq, blob 哈希}；落后于 head 永远合法（重放补缺口）；
//   RNG/评分状态不进快照（由 tick 与事件流重导出）。
//   文件 = [magic "OLYSNAP1"][u16 ver][u32 body_len][32B sha256(body)][body]
//   body = {u64 seq}{u64 ticks}{u32 count}{(u64 seq,text)*}
//   损坏跳过取更旧（异步拍允许半写）；全部损坏 → 无快照全量重放路径兜底。
#pragma once

#include "oly/core/id.hpp"
#include "oly/core/result.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace oly::session {

struct SnapshotData {
    uint64_t seq = 0;     // 快照锚：含此 seq 的投影（= 拍快照时的 committed_seq）
    uint64_t ticks = 0;   // 拍计数（会话流）
    ContentId body_hash;
    std::vector<std::pair<uint64_t, std::string>> messages;   // 投影：seq → 文本
};

// 拍快照：写 snap-{seq:016}.bin，返回内容哈希（清单锚；M1 归档 PG blobs 同值）。
Result<ContentId> write_snapshot(const std::filesystem::path& dir, const SnapshotData& snap);

// 取最新有效快照；无快照（或全部损坏）→ nullopt（启动走全量重放，05 §6）。
Result<std::optional<SnapshotData>> load_latest_snapshot(const std::filesystem::path& dir);

}   // namespace oly::session
