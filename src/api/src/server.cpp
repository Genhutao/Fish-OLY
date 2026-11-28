#include "oly/api/server.hpp"

#include "oly/log/log.hpp"

#include "oly/codec/codec.hpp"
#include "oly/core/ulid.hpp"
#include "oly/logdb/store.hpp"

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/json.hpp>

#include <array>
#include <charconv>
#include <deque>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace bj = boost::json;

using net::awaitable;
using net::use_awaitable;
using tcp = net::ip::tcp;

namespace oly::api {
namespace {

constexpr std::size_t kReplayLimit = 10000;
constexpr std::size_t kDefaultMessageLimit = 100;

std::string id_str(const std::array<std::byte, 16>& b) { return encode_ulid(b); }

std::string kind_str(const events::EventKind k) {
    switch (k) {
        case events::EventKind::CommandAccepted: return "command";
        case events::EventKind::Fact: return "fact";
        case events::EventKind::Denied: return "denied";
        case events::EventKind::Notification: return "notification";
    }
    return "fact";
}

std::vector<std::string> split_path(const std::string_view path) {
    std::vector<std::string> seg;
    std::size_t i = 0;
    while (i < path.size()) {
        while (i < path.size() && path[i] == '/') ++i;
        const auto j = path.find('/', i);
        if (j == std::string_view::npos) {
            if (i < path.size()) seg.emplace_back(path.substr(i));
            break;
        }
        seg.emplace_back(path.substr(i, j - i));
        i = j + 1;
    }
    return seg;
}

uint64_t query_num(const std::string_view query, const std::string_view key,
                   const uint64_t fallback) {
    const std::string needle = std::string{key} + "=";
    std::size_t pos = 0;
    while ((pos = query.find(needle, pos)) != std::string_view::npos) {
        const bool clean_head = pos == 0 || query[pos - 1] == '&';
        pos += needle.size();
        if (clean_head) {
            uint64_t v = 0;
            const auto res = std::from_chars(query.data() + pos,
                                             query.data() + query.size(), v);
            if (res.ec == std::errc()) return v;
            return fallback;
        }
    }
    return fallback;
}

std::optional<domain::PrincipalId> parse_pid(const std::string_view s) {
    return domain::PrincipalId::from_string(s);
}

http::response<http::string_body> json_res(const unsigned status, bj::object body) {
    http::response<http::string_body> res{static_cast<http::status>(status), 11};
    res.set(http::field::server, "oly");
    res.set(http::field::content_type, "application/json");
    res.body() = bj::serialize(bj::value{std::move(body)});
    res.prepare_payload();
    res.keep_alive(true);
    return res;
}

http::response<http::string_body> err_res(const unsigned status, std::string code,
                                          std::string message) {
    bj::object err;
    err["code"] = std::move(code);
    err["message"] = std::move(message);
    bj::object body;
    body["error"] = std::move(err);
    return json_res(status, std::move(body));
}

// ── WS 连接（出站有界队列；单 io 线程序列化写）────────────────────────
struct Sub {
    std::array<std::byte, 16> sid{};
    uint64_t next_seq = 0;
};

struct WsConn : std::enable_shared_from_this<WsConn> {
    explicit WsConn(tcp::socket s, const std::size_t cap) : ws(std::move(s)), cap_(cap) {}

    beast::websocket::stream<tcp::socket> ws;
    beast::flat_buffer buf;
    domain::PrincipalId allowed_principal{};   // 订阅即鉴权（10 §5）
    std::mutex mu;                    // subs：io 线程写、发布线程读
    std::vector<Sub> subs;
    std::deque<std::string> out;      // io 线程独占
    bool sending = false;
    bool alive = true;
    const std::size_t cap_;

    // 任意线程 → io 入队（慢消费者超界断开，01 §4）
    void post_text(std::string s) {
        net::post(ws.get_executor(),
                  [self = shared_from_this(), s = std::move(s)]() mutable {
                      self->enqueue(std::move(s));
                  });
    }

    void enqueue(std::string s) {
        if (!alive) return;
        if (out.size() >= cap_) {
            alive = false;
            boost::system::error_code ec;
            ws.next_layer().close(ec);
            out.clear();
            return;
        }
        out.push_back(std::move(s));
        if (!sending) do_write();
    }

    void do_write() {
        sending = true;
        auto self = shared_from_this();
        ws.async_write(net::buffer(out.front()),
                       [self](boost::system::error_code, std::size_t) {
                           self->sending = false;
                           if (!self->out.empty()) self->out.pop_front();
                           if (self->alive && !self->out.empty()) self->do_write();
                       });
    }
};

// 命令状态（10 §4.10）：由事件流索引（幂等键同源，重放同一结果）
struct CmdStatus {
    std::string state = "accepted";   // provisional：受理即登记，事件到达后校准
    std::vector<std::pair<uint64_t, std::string>> events;
};

}   // namespace

struct Server::Impl {
    explicit Impl(Server* s) : owner(s) {}

    Server* owner;
    net::io_context io;
    std::unique_ptr<tcp::acceptor> acceptor;
    std::jthread thread;
    std::optional<uint16_t> port;

    // 私有成员桥（嵌套类可访问 Server 私有面；自由函数经由这些访问器）
    Auth& auth() noexcept { return *owner->auth_; }
    identity::Shard& identity() noexcept { return *owner->identity_; }
    session::Host* host() noexcept { return owner->host_.has_value() ? &*owner->host_ : nullptr; }
    const ServerConfig& cfg() const noexcept { return owner->cfg_; }

    std::mutex mu;   // conns 快照
    std::vector<std::shared_ptr<WsConn>> conns;
    mutable std::mutex acl_mu;
    std::unordered_map<uint64_t, std::unordered_set<uint64_t>> acl;
    std::mutex cmd_mu;
    std::unordered_map<std::string, CmdStatus> cmds;
    std::unordered_map<std::string, std::string> idem;   // Idempotency-Key → command_id

    bool allowed(const domain::SessionId sid, const domain::PrincipalId who) const {
        const std::scoped_lock lk(acl_mu);   // acl_mu 为 mutable：const 查询加锁
        const auto it = acl.find(domain::ulid_key(sid));
        return it != acl.end() && it->second.contains(domain::ulid_key(who));
    }

    // 事件外送（会话 loop 线程，05 §3 提交之后）+ 命令索引
    void publish(const events::EventEnvelope& e) {
        bj::object f;
        f["op"] = "event";
        f["stream"] = "session:" + id_str(e.stream.id);
        f["seq"] = e.seq;
        f["tick"] = e.tick;
        f["kind"] = kind_str(e.kind);
        f["type"] = e.type;
        f["actor"] = id_str(e.actor.bytes());
        if (e.type == "message.appended") {
            codec::Decoder d(e.payload_bin);
            /* actor */ (void)d.ulid();
            f["text"] = d.text();   // 对外投影（10 §6）；完整 schema 随 M1
        }
        const auto frame = bj::serialize(bj::value{std::move(f)});

        std::vector<std::shared_ptr<WsConn>> snapshot;
        {
            const std::scoped_lock lk(mu);
            snapshot = conns;
        }
        for (auto& conn : snapshot) {
            bool hit = false;
            {
                const std::scoped_lock lk(conn->mu);
                for (auto& sub : conn->subs) {
                    if (sub.sid == e.stream.id && e.seq >= sub.next_seq) {
                        sub.next_seq = e.seq + 1;
                        hit = true;
                        break;
                    }
                }
            }
            if (hit) conn->post_text(frame);
        }
    }

    void record(const events::EventEnvelope& e) {
        if (e.command.is_nil()) return;
        const std::scoped_lock lk(cmd_mu);
        auto& st = cmds[id_str(e.command.bytes())];
        st.events.emplace_back(e.seq, e.type);
        if (e.type == "cmd.denied") st.state = "denied";
    }

    // 只读重放（REST 历史 / WS sync；与写端共享段文件）
    Result<std::vector<events::EventEnvelope>> replay(const domain::SessionId sid,
                                                      const uint64_t from,
                                                      const std::size_t limit) const {
        auto store = logdb::SegmentStore::open(
            owner->cfg_.store_root / sid.str(),
            events::StreamRef{events::StreamKind::Session, sid.bytes()}, 0);
        if (!store) return unexpected(store.error());
        return store->read_range(from, limit);
    }
};

// ── HTTP 路由（M0 最小面，10 §4）─────────────────────────────────────

namespace {

std::optional<domain::PrincipalId> auth_actor(Server::Impl& impl,
                                              const http::request<http::string_body>& req) {
    const auto bearer = bearer_of(req[http::field::authorization]);
    if (!bearer.has_value()) return std::nullopt;
    return impl.auth().resolve(*bearer);
}

std::optional<bj::object> body_object(const http::request<http::string_body>& req) {
    try {
        auto v = bj::parse(req.body());
        return std::move(v).as_object();
    } catch (...) {
        return std::nullopt;
    }
}

http::response<http::string_body> handle_impl(Server::Impl& impl,
                                              const http::request<http::string_body>& req) {
    const std::string target{req.target()};
    const auto qpos = target.find('?');
    const auto seg = split_path(std::string_view{target}.substr(0, qpos));
    const std::string_view query =
        std::string_view{target}.substr(qpos == std::string::npos ? target.size() : qpos + 1);

    if (seg.empty() || seg[0] != "v1") return err_res(404, "not_found", "unknown path");

    // ── POST /v1/auth/tokens（10 §4.1；M0 = 主体存在即签发）──────────
    if (req.method() == http::verb::post && seg.size() == 3 && seg[1] == "auth" &&
        seg[2] == "tokens") {
        auto body = body_object(req);
        if (!body || !body->contains("principal_id")) {
            return err_res(400, "validation_failed", "principal_id required");
        }
        const auto pid = parse_pid(bj::value_to<std::string>((*body)["principal_id"]));
        if (!pid || impl.identity().snapshot()->principal(*pid) == nullptr) {
            return err_res(404, "not_found", "principal not found");
        }
        const auto token = "tok-" + UlidFactory{}.next().str();
        impl.auth().register_token(token, *pid);
        bj::object out{{"token", token}, {"principal_id", pid->str()}};
        return json_res(200, std::move(out));
    }

    // ── 鉴权门槛（以下端点全部需要 Bearer）─────────────────────────
    const auto actor = auth_actor(impl, req);
    if (!actor.has_value()) return err_res(401, "unauthorized", "missing or unknown token");

    // GET /v1/me
    if (req.method() == http::verb::get && seg.size() == 2 && seg[1] == "me") {
        const bool known = impl.identity().snapshot()->principal(*actor) != nullptr;
        bj::object out{{"principal_id", actor->str()}, {"identity_known", known}};
        return json_res(200, std::move(out));
    }

    // POST /v1/personas —— 创建（主体 + 卡片 v1，经身份分片异步受理）
    if (req.method() == http::verb::post && seg.size() == 2 && seg[1] == "personas") {
        auto body = body_object(req);
        if (!body || !body->contains("name")) {
            return err_res(400, "validation_failed", "name required");
        }
        const auto pid = PrincipalId::from_bytes(UlidFactory{}.next().bytes());
        domain::Principal p;
        p.id = pid;
        p.kind = domain::PrincipalKind::Persona;
        p.owner = *actor;
        domain::CardBody card{bj::value_to<std::string>((*body)["name"]),
                              body->contains("core_anchor")
                                  ? bj::value_to<std::string>((*body)["core_anchor"])
                                  : std::string{},
                              body->contains("description")
                                  ? bj::value_to<std::string>((*body)["description"])
                                  : std::string{}};
        if (!impl.identity().post(identity::AddPrincipalCmd{p}) ||
            !impl.identity().post(identity::AddPersonaCmd{pid, *actor, card})) {
            return err_res(429, "rate_limited", "identity shard busy");
        }
        bj::object out{{"persona_id", pid.str()}};
        return json_res(202, std::move(out));
    }

    // GET /v1/personas/{id}
    if (req.method() == http::verb::get && seg.size() == 3 && seg[1] == "personas") {
        const auto pid = parse_pid(seg[2]);
        if (!pid) return err_res(400, "validation_failed", "bad id");
        const auto snap = impl.identity().snapshot();
        const auto* persona = snap->persona(*pid);
        if (persona == nullptr) return err_res(404, "not_found", "persona not found");
        bj::object out{{"persona_id", pid->str()},
                       {"name", persona->head().name},
                       {"versions", persona->versions.size()},
                       {"visibility", static_cast<uint64_t>(snap->principal(*pid) != nullptr
                                                               ? snap->principal(*pid)->visibility
                                                               : domain::Visibility::Private)},
                       {"owner", id_str(persona->owner.bytes())}};
        return json_res(200, std::move(out));
    }

    // ── 会话族（M0：会话经引导装配存在）────────────────────────────
    if (seg.size() >= 3 && seg[1] == "sessions") {
        const auto sid = SessionId::from_string(seg[2]);
        if (!sid.has_value()) return err_res(400, "validation_failed", "bad session id");
        auto* host = impl.host();
        if (host == nullptr || host->session(*sid) == nullptr) {
            return err_res(404, "not_found", "session not found");
        }

        // POST /v1/sessions/{id}/messages（10 §4.4：202 + command_id）
        if (req.method() == http::verb::post && seg.size() == 4 && seg[3] == "messages") {
            auto body = body_object(req);
            if (!body || !body->contains("text")) {
                return err_res(400, "validation_failed", "text required");
            }
            const std::string idem_key{req["Idempotency-Key"]};
            {
                const std::scoped_lock lk(impl.cmd_mu);
                if (const auto it = impl.idem.find(idem_key); it != impl.idem.end()) {
                    bj::object out{{"command_id", it->second}, {"replayed", true}};
                    return json_res(202, std::move(out));   // 幂等重放：同一结果
                }
            }
            const auto cid = CommandId::from_bytes(UlidFactory{}.next().bytes());
            session::Command c;
            c.id = cid;
            c.actor = *actor;
            c.action = session::CmdAction::SendMessage;
            c.text = bj::value_to<std::string>((*body)["text"]);
            {
                const std::scoped_lock lk(impl.cmd_mu);
                if (!idem_key.empty()) impl.idem[idem_key] = id_str(cid.bytes());
                impl.cmds[id_str(cid.bytes())].state = "accepted";
            }
            if (!host->post(*sid, std::move(c))) {
                return err_res(429, "rate_limited", "session inbox full");
            }
            bj::object out{{"command_id", id_str(cid.bytes())}};
            return json_res(202, std::move(out));
        }

        // GET /v1/sessions/{id}/messages?before_seq=&limit=（keyset）
        if (req.method() == http::verb::get && seg.size() == 4 && seg[3] == "messages") {
            const auto replay = impl.replay(*sid, 0, kReplayLimit);
            if (!replay) return err_res(500, "server_overloaded", replay.error().message);
            const auto before = query_num(query, "before_seq", ~0ull);
            const auto limit = query_num(query, "limit", kDefaultMessageLimit);
            bj::array msgs;
            for (const auto& e : replay.value()) {
                if (e.type != "message.appended" || e.seq >= before) continue;
                codec::Decoder d(e.payload_bin);
                const auto actor_b = d.ulid();
                const auto text = d.text();
                if (!d.ok()) continue;
                bj::object m{{"seq", e.seq},
                             {"actor", id_str(actor_b)},
                             {"text", text}};
                msgs.emplace_back(std::move(m));
                if (msgs.size() > limit) msgs.erase(msgs.begin());   // 取末尾 limit 条
            }
            bj::object out{{"messages", std::move(msgs)}};
            return json_res(200, std::move(out));
        }

        // GET /v1/sessions/{id}（created 事实 → 元数据）
        if (req.method() == http::verb::get && seg.size() == 3) {
            const auto replay = impl.replay(*sid, 0, kReplayLimit);
            if (!replay || replay->empty() || replay->front().type != "session.created") {
                return err_res(404, "not_found", "session stream empty");
            }
            const auto& created = replay->front();
            codec::Decoder d(created.payload_bin);
            const auto canon = d.text();
            const auto count = d.u32();
            bj::array members;
            for (uint32_t i = 0; i < count && d.ok(); ++i) {
                const auto m = d.ulid();
                const auto role = d.u8();
                bj::object mm{{"principal_id", id_str(m)}, {"role", role}};
                members.emplace_back(std::move(mm));
            }
            bj::object out{{"session_id", sid->str()},
                           {"owner", id_str(created.actor.bytes())},
                           {"canon", canon},
                           {"members", std::move(members)},
                           {"head_seq", replay->back().seq + 1}};
            return json_res(200, std::move(out));
        }

        // PUT /v1/sessions/{id}/read {seq}（read.advance → 未读清零）
        if (req.method() == http::verb::put && seg.size() == 4 && seg[3] == "read") {
            auto body = body_object(req);
            if (!body || !body->contains("seq")) {
                return err_res(400, "validation_failed", "seq required");
            }
            const auto cid = CommandId::from_bytes(UlidFactory{}.next().bytes());
            session::Command c;
            c.id = cid;
            c.actor = *actor;
            c.action = session::CmdAction::AdvanceRead;
            c.read_seq = bj::value_to<uint64_t>((*body)["seq"]);
            {
                const std::scoped_lock lk(impl.cmd_mu);
                impl.cmds[id_str(cid.bytes())].state = "accepted";
            }
            if (!host->post(*sid, std::move(c), true)) {
                return err_res(429, "rate_limited", "session inbox full");
            }
            bj::object out{{"command_id", id_str(cid.bytes())}};
            return json_res(202, std::move(out));
        }
    }

    // GET /v1/commands/{command_id}（10 §4.10）
    if (req.method() == http::verb::get && seg.size() == 3 && seg[1] == "commands") {
        const std::scoped_lock lk(impl.cmd_mu);
        const auto it = impl.cmds.find(seg[2]);
        if (it == impl.cmds.end()) {
            bj::object out{{"state", "unknown"}};
            return json_res(200, std::move(out));
        }
        bj::array evs;
        for (const auto& [seq, type] : it->second.events) {
            bj::object ev{{"seq", seq}, {"type", type}};
            evs.emplace_back(std::move(ev));
        }
        bj::object out{{"state", it->second.state}, {"result_events", std::move(evs)}};
        return json_res(200, std::move(out));
    }

    return err_res(404, "not_found", "unknown route");
}

// ── WS 协议（10 §5）─────────────────────────────────────────────────

awaitable<void> run_ws(std::shared_ptr<WsConn> conn, Server::Impl& impl) {
    // 注册进连接表
    {
        const std::scoped_lock lk(impl.mu);
        impl.conns.push_back(conn);
    }

    for (;;) {
        boost::system::error_code ec;
        co_await conn->ws.async_read(conn->buf, net::redirect_error(use_awaitable, ec));
        if (ec) {
            OLY_LOG_DEBUG("ws read end: {}", ec.message());
            break;
        }

        bj::value v;
        const auto raw = beast::buffers_to_string(conn->buf.data());
        try {
            v = bj::parse(raw);
        } catch (const std::exception& e) {
            OLY_LOG_WARN("ws parse failed: {}", e.what());
            conn->buf.clear();
            continue;
        }
        conn->buf.clear();
        if (!v.is_object()) continue;
        auto msg = v.as_object();
        if (!msg.contains("op") || !msg["op"].is_string()) continue;
        const auto op = bj::value_to<std::string>(msg["op"]);
        try {

        if (op == "ping") {
            conn->post_text(bj::serialize(bj::value{bj::object{{"op", "pong"}}}));
        } else if (op == "subscribe" && msg.contains("streams")) {
            const auto& streams = msg["streams"].as_array();
            for (const auto& s : streams) {
                const auto str = bj::value_to<std::string>(s);
                if (!str.starts_with("session:")) continue;
                const auto sid = SessionId::from_string(str.substr(8));
                if (!sid.has_value()) continue;
                if (!impl.allowed(*sid, conn->allowed_principal)) {
                    OLY_LOG_WARN("ws subscribe denied sid={} principal={}", sid->str(),
                                 conn->allowed_principal.str());
                    continue;   // 订阅即鉴权（10 §5）
                }
                uint64_t since = 0;
                if (msg.contains("since")) {
                    const auto& since_map = msg["since"].as_object();
                    if (const auto it = since_map.find(str); it != since_map.end()) {
                        since = bj::value_to<uint64_t>(it->value());
                    }
                }
                // 重放补齐 → sync → 实时（10 §5 续传规则）
                const auto replay = impl.replay(*sid, since + 1, kReplayLimit);
                uint64_t last = since;
                if (replay) {
                    for (const auto& e : replay.value()) {
                        if (e.type == "message.appended" || e.type == "session.created") {
                            bj::object f{{"op", "event"},
                                         {"stream", str},
                                         {"seq", e.seq},
                                         {"tick", e.tick},
                                         {"type", e.type},
                                         {"actor", id_str(e.actor.bytes())}};
                            if (e.type == "message.appended") {
                                codec::Decoder d(e.payload_bin);
                                /* actor */ (void)d.ulid();
                                f["text"] = d.text();
                            }
                            conn->post_text(bj::serialize(bj::value{std::move(f)}));
                        }
                        last = e.seq;
                    }
                } else {
                    bj::object errf{{"op", "error"}, {"code", "gap_too_large"}, {"stream", str}};
                    conn->post_text(bj::serialize(bj::value{std::move(errf)}));
                }
                bj::object sync{{"op", "sync"},
                                {"stream", str},
                                {"from_seq", since + 1},
                                {"to_seq", last}};
                conn->post_text(bj::serialize(bj::value{std::move(sync)}));
                const std::scoped_lock lk(conn->mu);
                auto& sub = conn->subs.emplace_back();
                sub.sid = sid->bytes();
                sub.next_seq = last + 1;
            }
        } else if (op == "unsubscribe" && msg.contains("streams")) {
            const std::scoped_lock lk(conn->mu);
            for (const auto& s : msg["streams"].as_array()) {
                const auto str = bj::value_to<std::string>(s);
                if (!str.starts_with("session:")) continue;
                const auto sid = SessionId::from_string(str.substr(8));
                if (!sid.has_value()) continue;
                std::erase_if(conn->subs, [&](const Sub& sub) { return sub.sid == sid->bytes(); });
            }
        } else if (op == "signal" && msg.contains("stream")) {
            // 瞬态信令（01 §4.1）：零持久化、不占 seq、不重放
            bj::object f{{"op", "signal"},
                         {"stream", bj::value_to<std::string>(msg["stream"])},
                         {"type", msg.contains("type") ? bj::value_to<std::string>(msg["type"])
                                                       : std::string{"typing"}}};
            if (msg.contains("payload")) f["payload"] = msg["payload"];
            const auto frame = bj::serialize(bj::value{std::move(f)});
            std::vector<std::shared_ptr<WsConn>> snapshot;
            {
                const std::scoped_lock lk(impl.mu);
                snapshot = impl.conns;
            }
            for (auto& other : snapshot) {
                if (other.get() == conn.get()) continue;
                bool hit = false;
                {
                    const std::scoped_lock lk(other->mu);
                    for (const auto& sub : other->subs) {
                        if (msg.contains("stream") &&
                            bj::value_to<std::string>(msg["stream"]) ==
                                "session:" + id_str(sub.sid)) {
                            hit = true;
                            break;
                        }
                    }
                }
                if (hit) other->post_text(frame);
            }
        }
        } catch (...) {
            // 坏帧丢弃（10 §5：协议错误不杀连接，M1 补 error 帧）
            continue;
        }
    }

    const std::scoped_lock lk(impl.mu);
    std::erase_if(impl.conns, [&](const auto& c) { return c.get() == conn.get(); });
}

awaitable<void> run_conn(Server::Impl& impl, tcp::socket sock) {
    beast::flat_buffer buf;
    for (;;) {
        boost::system::error_code ec;
        http::request<http::string_body> req;
        co_await http::async_read(sock, buf, req, net::redirect_error(use_awaitable, ec));
        if (ec) co_return;

        const bool is_ws_upgrade =
            beast::iequals(std::string{req[http::field::upgrade]}, "websocket");
        if (is_ws_upgrade) {
            // /v1/stream（10 §5）
            auto conn = std::make_shared<WsConn>(std::move(sock), impl.cfg().ws_outbound_capacity);
            // 订阅 ACL 的 principal 在 accept 前由 Authorization 头给出：
            boost::system::error_code wec;
            co_await conn->ws.async_accept(req, net::redirect_error(use_awaitable, wec));
            if (wec) co_return;
            if (const auto a = auth_actor(impl, req); a.has_value()) {
                conn->allowed_principal = *a;
            }
            co_await run_ws(std::move(conn), impl);
            co_return;
        }

        auto res = handle_impl(impl, req);
        co_await http::async_write(sock, res, net::redirect_error(use_awaitable, ec));
        if (ec) co_return;
    }
}

awaitable<void> listener(Server::Impl& impl) {
    for (;;) {
        boost::system::error_code ec;
        tcp::socket sock(impl.io);
        co_await impl.acceptor->async_accept(sock, net::redirect_error(use_awaitable, ec));
        if (ec) break;
        net::co_spawn(impl.io, run_conn(impl, std::move(sock)), net::detached);
    }
}

}   // namespace

// ── Server 装配 ─────────────────────────────────────────────────────

Result<std::unique_ptr<Server>> Server::create(const ServerConfig cfg) {
    std::unique_ptr<Server> s(new Server());
    s->cfg_ = cfg;
    auto pool = exec::LoopPool::create(cfg.session_loops);
    if (!pool) return unexpected(pool.error());
    s->session_pool_ = std::make_unique<exec::LoopPool>(std::move(pool.value()));
    auto ipool = exec::LoopPool::create(1);
    if (!ipool) return unexpected(ipool.error());
    s->identity_pool_ = std::make_unique<exec::LoopPool>(std::move(ipool.value()));

    auto shard = identity::Shard::create({s->identity_pool_->at(0), 4096});
    if (!shard) return unexpected(shard.error());
    s->identity_ = std::move(shard.value());
    s->auth_ = std::make_unique<Auth>();
    s->impl_ = std::make_unique<Impl>(s.get());
    return s;
}

Server::~Server() { stop(); }

session::Host& Server::bootstrap_host() {
    if (!host_) {
        session::Host::Deps d{*session_pool_, registry_, identity_->snapshot(),
                              cfg_.tick_period_ms, cfg_.store_root, cfg_.snapshot_root};
        d.sink = [impl = impl_.get()](const events::EventEnvelope& e) {
            impl->record(e);
            impl->publish(e);
        };
        auto h = session::Host::create(d);
        if (!h) throw std::runtime_error("bootstrap_host failed: " + h.error().message);
        host_.emplace(std::move(h.value()));
    }
    return *host_;
}

void Server::allow(const domain::SessionId sid, const domain::PrincipalId who) {
    const std::scoped_lock lk(impl_->acl_mu);
    impl_->acl[domain::ulid_key(sid)].insert(domain::ulid_key(who));
}

Result<uint16_t> Server::start() {
    if (impl_->port.has_value()) return impl_->port.value();

    boost::system::error_code ec;
    const tcp::endpoint ep{net::ip::make_address(cfg_.bind, ec), cfg_.port};
    if (ec) return unexpected(err("api:bind", "bad bind address"));
    impl_->acceptor = std::make_unique<tcp::acceptor>(impl_->io);
    impl_->acceptor->open(ep.protocol(), ec);
    if (ec) return unexpected(err("api:bind", ec.message()));
    impl_->acceptor->set_option(net::socket_base::reuse_address(true), ec);
    impl_->acceptor->bind(ep, ec);
    if (ec) return unexpected(err("api:bind", ec.message()));
    impl_->acceptor->listen(net::socket_base::max_listen_connections, ec);
    if (ec) return unexpected(err("api:bind", ec.message()));
    impl_->port = impl_->acceptor->local_endpoint().port();

    net::co_spawn(impl_->io, listener(*impl_), net::detached);
    session_pool_->start();
    identity_pool_->start();
    impl_->thread = std::jthread([this] { impl_->io.run(); });
    return impl_->port.value();
}

void Server::stop() noexcept {
    if (impl_ == nullptr) return;
    impl_->io.stop();
    if (impl_->thread.joinable()) impl_->thread.join();
    if (session_pool_ != nullptr) session_pool_->stop();
    if (identity_pool_ != nullptr) identity_pool_->stop();
}

}   // namespace oly::api
