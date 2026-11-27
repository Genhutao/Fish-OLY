#include "oly/events/envelope.hpp"

#include "oly/core/hash.hpp"

#include <algorithm>

namespace oly::events {

std::vector<std::byte> canonical_bytes(const EventEnvelope& e) {
    codec::Encoder enc;
    enc.u8(static_cast<uint8_t>(e.kind));
    enc.u8(static_cast<uint8_t>(e.stream.kind));
    enc.ulid(e.stream.id);
    enc.u64(e.seq);
    enc.u64(e.tick);
    enc.i64(e.wall_ms);
    enc.ulid(e.command.bytes());
    enc.u32(static_cast<uint32_t>(e.causes.size()));
    for (const auto& c : e.causes) enc.ulid(c);
    enc.ulid(e.actor.bytes());
    enc.ulid(e.on_behalf_of.bytes());
    enc.text(e.type);
    enc.u16(e.schema_ver);
    enc.bytes(e.payload_bin);
    enc.content(e.prev_hash.bytes);
    return std::move(enc).take();
}

void seal(EventEnvelope& e) { e.hash = chain_hash(e.prev_hash, canonical_bytes(e)); }

std::vector<std::byte> encode_envelope(const EventEnvelope& e) {
    codec::Encoder enc;
    enc.raw(canonical_bytes(e));
    enc.content(e.hash.bytes);
    return std::move(enc).take();
}

Result<EventEnvelope> decode_envelope(const std::span<const std::byte> wire) {
    codec::Decoder d(wire);
    EventEnvelope e;
    e.kind = static_cast<EventKind>(d.u8());
    e.stream.kind = static_cast<StreamKind>(d.u8());
    e.stream.id = d.ulid();
    e.seq = d.u64();
    e.tick = d.u64();
    e.wall_ms = d.i64();
    e.command = CommandId::from_bytes(d.ulid());
    const uint32_t cause_count = d.u32();
    e.causes.reserve(std::min<std::size_t>(cause_count, 1024));
    for (uint32_t i = 0; i < cause_count; ++i) e.causes.push_back(d.ulid());
    e.actor = PrincipalId::from_bytes(d.ulid());
    e.on_behalf_of = PrincipalId::from_bytes(d.ulid());
    e.type = d.text();
    e.schema_ver = d.u16();
    e.payload_bin = d.bytes();
    e.prev_hash = ContentId::from_bytes(d.content());
    e.hash = ContentId::from_bytes(d.content());

    if (!d.ok()) return unexpected(d.error());

    // 本事件哈希校验（04 §3）：hash == SHA-256(prev ‖ canonical[不含 hash])。
    if (chain_hash(e.prev_hash, canonical_bytes(e)) != e.hash) {
        return unexpected(err("events:hash-mismatch", "envelope hash verification failed"));
    }
    return e;
}

}   // namespace oly::events
