// 黄金 e2e（10 / 13 §1 M0"API 最小集"）：真实 Beast 客户端全链。
//   REST：auth/tokens → me → persona 查询 → 消息投递（幂等重放）→ 命令状态
//         → 历史读取 → 读游标推进
//   WS  ：/v1/stream —— subscribe(since 重放→sync) → 实时 event → 瞬态 signal
#include <oly/api/server.hpp>
#include <oly/core/id.hpp>
#include <oly/core/ulid.hpp>
#include <oly/domain/domain.hpp>
#include <oly/session/autonomy.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/json.hpp>

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;
namespace bj = boost::json;

using tcp = net::ip::tcp;

using namespace oly;

namespace {

domain::PrincipalId pid_of(std::string_view s) {
    return domain::PrincipalId::from_string(s).value();
}

// 同步 HTTP 客户端：一次连接一发
http::response<http::string_body> http_call(const uint16_t port, const http::verb method,
                                            const std::string& target, const std::string& body = {},
                                            const std::vector<std::pair<std::string, std::string>>& hdrs = {}) {
    net::io_context ioc;
    tcp::socket sock{ioc};
    sock.connect({net::ip::make_address("127.0.0.1"), port});
    http::request<http::string_body> req{method, target, 11};
    req.set(http::field::host, "127.0.0.1");
    for (const auto& [k, v] : hdrs) req.set(k, v);
    if (!body.empty()) {
        req.set(http::field::content_type, "application/json");
        req.body() = body;
        req.prepare_payload();
    }
    http::write(sock, req);
    beast::flat_buffer buf;
    http::response<http::string_body> res;
    http::read(sock, buf, res);
    boost::system::error_code ec;
    sock.close(ec);
    return res;
}

std::string http_body(const http::response<http::string_body>& res) { return res.body(); }

// 同步 WS 客户端
struct WsClient {
    net::io_context ioc;
    beast::websocket::stream<tcp::socket> ws{ioc};
    beast::flat_buffer buf;

    void connect(const uint16_t port, const std::string& token) {
        ws.next_layer().connect({net::ip::make_address("127.0.0.1"), port});
        ws.set_option(beast::websocket::stream_base::decorator([&](http::request<http::empty_body>& req) {
            req.set(http::field::authorization, "Bearer " + token);
        }));
        ws.handshake("127.0.0.1", "/v1/stream");
    }

    void send(const std::string& frame) {
        ws.write(net::buffer(frame));
    }

    std::string read_frame(const int timeout_ms = 3000) {
        // async_read 与定时器竞速：超时取消读取，测试永不悬挂
        std::optional<std::string> out;
        auto bufp = std::make_shared<beast::flat_buffer>();
        net::steady_timer timer{ioc};
        timer.expires_after(std::chrono::milliseconds(timeout_ms));
        bool read_done = false, timed = false;
        ws.async_read(*bufp, [&](boost::system::error_code ec, std::size_t) {
            read_done = true;
            out = ec ? std::string{} : beast::buffers_to_string(bufp->data());
            timer.cancel();
        });
        timer.async_wait([&](boost::system::error_code ec) {
            if (!ec) {   // 自然到期：取消读取
                timed = true;
                out = std::string{};
                boost::system::error_code cec;
                ws.next_layer().cancel(cec);
            }
        });
        ioc.run();
        ioc.restart();
        (void)read_done;
        (void)timed;
        return out.value_or(std::string{});
    }
};

// 轮询直至谓词成立（命令异步生效）
template <typename F>
bool poll_until(F&& pred, const int timeout_ms = 3000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return pred();
}

}   // namespace

TEST_CASE("API e2e：REST 全链 + WS 事件流", "[golden][api]") {
    const auto dir = std::filesystem::temp_directory_path() /
                     ("oly-api-e2e-" + UlidFactory{}.next().str());

    oly::api::ServerConfig cfg;
    cfg.port = 0;
    cfg.store_root = dir / "seg";
    cfg.snapshot_root = dir / "snap";
    cfg.session_loops = 2;
    cfg.tick_period_ms = 50;
    auto srv = oly::api::Server::create(cfg);
    REQUIRE(srv.has_value());
    auto& s = **srv;

    // ── 引导世界 ───────────────────────────────────────────────────
    const auto root = pid_of("01ARZ3NDEKTSV4RRFFQ69G5FAV");
    const auto persona = pid_of("01ARZ3NDEKTSV4RRFFQ69G5FBW");
    {
        domain::Principal r;
        r.id = r.owner = root;
        REQUIRE(s.registry().add_principal(r).has_value());
        domain::Principal a;
        a.id = persona;
        a.kind = domain::PrincipalKind::Persona;
        a.owner = root;
        REQUIRE(s.registry().add_principal(a).has_value());
        REQUIRE(s.registry()
                    .add_persona(persona, root, domain::CardBody{"苍月千鹤", "锚", ""})
                    .has_value());

        REQUIRE(s.identity_shard().post(oly::identity::AddPrincipalCmd{r}));
        REQUIRE(s.identity_shard().post(oly::identity::AddPrincipalCmd{a}));
        REQUIRE(s.identity_shard().post(
            oly::identity::AddPersonaCmd{persona, root, domain::CardBody{"苍月千鹤", "锚", ""}}));
        s.identity_shard().drain_once();
    }
    auto& host = s.bootstrap_host();
    const auto sid = SessionId::from_bytes(UlidFactory{}.next().bytes());
    {
        domain::Session sess;
        sess.id = sid;
        sess.owner = root;
        sess.canon = "月下庭院。";
        sess.members.push_back(domain::Member{root, domain::SessionRole::Owner, 0});
        sess.members.push_back(domain::Member{persona, domain::SessionRole::Member, 0});
        REQUIRE(s.registry().create_session(sess).has_value());
        const auto rt = host.create_session(sid, 0);
        REQUIRE(rt.has_value());
        // 角色自治（M0 脚本驱动）：拍驱动发言 → REST 阶段落若干句 → WS 重放可断言
        std::vector<std::string> lines;
        for (int i = 0; i < 12; ++i) lines.push_back("【千鹤】夜话" + std::to_string(i + 1) + "。");
        rt.value()->attach_autonomy(persona,
                                    std::make_unique<session::ScriptedDriver>(lines, 1, 0));
    }
    s.allow(sid, root);
    s.auth().register_token("tok-root", root);

    const auto port = s.start();
    REQUIRE(port.has_value());

    // ── REST：鉴权 + me ────────────────────────────────────────────
    {
        const auto res = http_call(*port, http::verb::post, "/v1/auth/tokens",
                                   R"({"principal_id":"01ARZ3NDEKTSV4RRFFQ69G5FAV"})");
        REQUIRE(res.result_int() == 200);
    }
    {
        const auto res = http_call(*port, http::verb::get, "/v1/me", {},
                                   {{"Authorization", "Bearer tok-root"}});
        REQUIRE(res.result_int() == 200);
        REQUIRE(http_body(res).find(root.str()) != std::string::npos);
    }
    {
        const auto res = http_call(*port, http::verb::get, "/v1/me", {});
        REQUIRE(res.result_int() == 401);   // 缺 token
    }
    {
        const auto res = http_call(*port, http::verb::get, "/v1/personas/" + persona.str(), {},
                                   {{"Authorization", "Bearer tok-root"}});
        REQUIRE(res.result_int() == 200);
        REQUIRE(http_body(res).find("苍月千鹤") != std::string::npos);
    }

    // ── REST：消息投递（幂等重放）+ 命令状态轮询 ──────────────────
    std::string cmd1;
    {
        const auto res = http_call(*port, http::verb::post, "/v1/sessions/" + sid.str() + "/messages",
                                   R"({"text":"（用户）庭院安好。"})",
                                   {{"Authorization", "Bearer tok-root"},
                                    {"Idempotency-Key", "idem-1"}});
        REQUIRE(res.result_int() == 202);
        const auto v = bj::parse(http_body(res));
        cmd1 = bj::value_to<std::string>(v.at("command_id"));
        REQUIRE(cmd1.size() == 26);
    }
    {
        // 幂等键重放：同一 command_id
        const auto res = http_call(*port, http::verb::post, "/v1/sessions/" + sid.str() + "/messages",
                                   R"({"text":"（用户）庭院安好。"})",
                                   {{"Authorization", "Bearer tok-root"},
                                    {"Idempotency-Key", "idem-1"}});
        REQUIRE(res.result_int() == 202);
        const auto v = bj::parse(http_body(res));
        REQUIRE(bj::value_to<std::string>(v.at("command_id")) == cmd1);
        REQUIRE(bj::value_to<bool>(v.at("replayed")));
    }
    {
        bool landed = false;
        std::string state;
        REQUIRE(poll_until([&] {
            const auto res = http_call(*port, http::verb::get, "/v1/commands/" + cmd1, {},
                                       {{"Authorization", "Bearer tok-root"}});
            const auto v = bj::parse(http_body(res));
            state = bj::value_to<std::string>(v.at("state"));
            const auto& evs = v.at("result_events").as_array();
            landed = !evs.empty() && state == "accepted";
            return landed;
        }));
    }
    {
        // 非成员发言 → denial 入审计（commands 状态 denied）
        const auto stranger_tok = "tok-stranger";
        s.auth().register_token(stranger_tok, pid_of("01ARZ3NDEKTSV4RRFFQ69G5FCX"));
        const auto res = http_call(*port, http::verb::post,
                                   "/v1/sessions/" + sid.str() + "/messages",
                                   R"({"text":"闯入"})",
                                   {{"Authorization", "Bearer " + std::string{stranger_tok}}});
        REQUIRE(res.result_int() == 202);
        const auto v = bj::parse(http_body(res));
        const auto cid = bj::value_to<std::string>(v.at("command_id"));
        REQUIRE(poll_until([&] {
            const auto r2 = http_call(*port, http::verb::get, "/v1/commands/" + cid, {},
                                      {{"Authorization", "Bearer tok-root"}});
            return http_body(r2).find("\"denied\"") != std::string::npos;
        }));
    }

    // ── REST：历史读取 ─────────────────────────────────────────────
    {
        bool ok = false;
        REQUIRE(poll_until([&] {
            const auto res = http_call(*port, http::verb::get,
                                       "/v1/sessions/" + sid.str() + "/messages", {},
                                       {{"Authorization", "Bearer tok-root"}});
            ok = res.result_int() == 200 &&
                 http_body(res).find("庭院安好") != std::string::npos &&
                 http_body(res).find("闯入") == std::string::npos;   // denial 非消息
            return ok;
        }));
    }

    // ── REST：读游标推进 ───────────────────────────────────────────
    {
        const auto res = http_call(*port, http::verb::put, "/v1/sessions/" + sid.str() + "/read",
                                   R"({"seq":2})",
                                   {{"Authorization", "Bearer tok-root"}});
        REQUIRE(res.result_int() == 202);
        const auto v = bj::parse(http_body(res));
        const auto cid = bj::value_to<std::string>(v.at("command_id"));
        REQUIRE(poll_until([&] {
            const auto r2 = http_call(*port, http::verb::get, "/v1/commands/" + cid, {},
                                      {{"Authorization", "Bearer tok-root"}});
            return http_body(r2).find("\"accepted\"") != std::string::npos &&
                   http_body(r2).find("result_events") != std::string::npos;
        }));
        // read.advance 不产事实 → 事件数为 0 但命令已受理
    }

    // ── WS：订阅重放 → 实时 → 瞬态信令 ─────────────────────────────
    {
        WsClient c1;
        c1.connect(*port, "tok-root");
        c1.send(bj::serialize(bj::value{bj::object{
            {"op", "subscribe"},
            {"streams", bj::array{"session:" + sid.str()}},
            {"since", bj::object{{"session:" + sid.str(), 0}}},
        }}));

        std::string sync;
        int replayed_msgs = 0;
        for (int i = 0; i < 50; ++i) {
            const auto f = c1.read_frame();
            if (f.empty()) break;
            const auto v = bj::parse(f);
            const auto op = bj::value_to<std::string>(v.at("op"));
            if (op == "sync") {
                sync = f;
                break;   // sync 之后进入实时
            }
            if (op == "event" &&
                bj::value_to<std::string>(v.at("type")) == "message.appended") {
                ++replayed_msgs;   // since=0 → seq0(created) 不重放，消息全部补齐
            }
        }
        REQUIRE(!sync.empty());
        REQUIRE(sync.find("\"to_seq\"") != std::string::npos);
        REQUIRE(replayed_msgs >= 3);   // 用户插话 + 两条角色应答

        // 实时事件：HTTP 投递 → WS 到达
        {
            (void)http_call(*port, http::verb::post, "/v1/sessions/" + sid.str() + "/messages",
                            R"({"text":"（用户）实时一条。"})",
                            {{"Authorization", "Bearer tok-root"}, {"Idempotency-Key", "idem-2"}});
            bool live = false;
            for (int i = 0; i < 200 && !live; ++i) {
                const auto f = c1.read_frame();
                if (f.empty()) continue;
                const auto v = bj::parse(f);
                if (bj::value_to<std::string>(v.at("op")) == "event" &&
                    bj::value_to<std::string>(v.at("type")) == "message.appended" &&
                    bj::value_to<std::string>(v.at("text")) == "（用户）实时一条。") {
                    live = true;
                }
            }
            REQUIRE(live);
        }

        // 瞬态信令：第二连接收到 typing（零持久化）
        {
            WsClient c2;
            c2.connect(*port, "tok-root");
            c2.send(bj::serialize(bj::value{bj::object{
                {"op", "subscribe"},
                {"streams", bj::array{"session:" + sid.str()}},
                {"since", bj::object{{"session:" + sid.str(), 0}}},
            }}));
            (void)c2.read_frame();   // 首帧（流静止后为 sync）

            c1.send(bj::serialize(bj::value{bj::object{
                {"op", "signal"},
                {"stream", "session:" + sid.str()},
                {"type", "typing"},
                {"payload", bj::object{{"who", "root"}}},
            }}));
            bool got = false;
            for (int i = 0; i < 200 && !got; ++i) {
                const auto f = c2.read_frame();
                if (f.empty()) continue;
                if (f.find("\"signal\"") != std::string::npos &&
                    f.find("\"typing\"") != std::string::npos) {
                    got = true;
                }
            }
            REQUIRE(got);
        }
    }

    s.stop();
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}
