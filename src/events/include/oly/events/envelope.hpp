// OLY · events —— 事件包络（04 §3）：命令/事实两域、三种流、哈希链、创世常量。
// seq 流内无空洞单调；tick 仅会话流；hash = SHA-256(prev_hash ‖ 规范字节[不含 hash])。
#pragma once

#include "oly/codec/codec.hpp"
#include "oly/core/id.hpp"
#include "oly/core/result.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace oly::events {

enum class StreamKind : uint8_t { Session = 1, Identity = 2, Process = 3 };
enum class EventKind : uint8_t { CommandAccepted = 1, Fact = 2, Denied = 3, Notification = 4 };

struct StreamRef {
    StreamKind kind = StreamKind::Session;
    std::array<std::byte, 16> id{};   // ULID：会话 / 主体 / 进程节点

    friend bool operator==(const StreamRef&, const StreamRef&) = default;
};

struct EventEnvelope {
    StreamRef stream;
    uint64_t seq = 0;
    uint64_t tick = 0;                       // 仅会话流；其余流恒 0
    int64_t wall_ms = 0;                     // 信息性
    EventKind kind = EventKind::Fact;
    CommandId command;                       // Fact/Denied 必带
    std::vector<std::array<std::byte, 16>> causes;
    PrincipalId actor;
    PrincipalId on_behalf_of;
    std::string type;                        // 'message.appended' …（04 §2）
    uint16_t    schema_ver = 1;
    std::vector<std::byte> payload_bin;      // 规范编码（哈希链算它）
    ContentId   prev_hash;                   // 首事件 = genesis
    ContentId   hash;                        // seal() 后有效
};

inline constexpr ContentId genesis_prev_hash{};   // 创世常量（04 §3）

// 规范字节：全字段定序编码，**不含** hash。哈希链只算它。
std::vector<std::byte> canonical_bytes(const EventEnvelope& e);

// 计算 hash（写路径在插入前调用——05 §6：链在写入前算好）。
void seal(EventEnvelope& e);

// 段文件记录形态：canonical + hash。
std::vector<std::byte> encode_envelope(const EventEnvelope& e);

// 解码并校验本事件哈希（跨事件链校验由读端持有 prev 进行）。
Result<EventEnvelope> decode_envelope(std::span<const std::byte> wire);

}   // namespace oly::events
