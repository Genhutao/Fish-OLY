// oly-server —— M0 开发服务器入口：装配单世界（根用户 + 一角色 + 一会话），
// 监听 REST + WS（10）。Ctrl+C 终止（信号处理随 M1 统一接入进程流）。
#include <oly/api/server.hpp>
#include <oly/core/id.hpp>
#include <oly/core/ulid.hpp>
#include <oly/domain/domain.hpp>
#include <oly/identity/shard.hpp>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <thread>

using namespace oly;

int main(const int argc, char** argv) {
    const uint16_t port = argc > 1 ? static_cast<uint16_t>(std::atoi(argv[1])) : 8080;
    const std::filesystem::path root_dir = argc > 2 ? argv[2] : "data";

    oly::api::ServerConfig cfg;
    cfg.port = port;
    cfg.store_root = root_dir / "seg";
    cfg.snapshot_root = root_dir / "snap";
    cfg.session_loops = 2;
    cfg.tick_period_ms = 100;

    auto srv = oly::api::Server::create(cfg);
    if (!srv.has_value()) {
        std::fprintf(stderr, "server create failed: %s\n", srv.error().message.c_str());
        return 1;
    }
    auto& s = **srv;

    // 引导世界：根用户 + 角色 + 单会话
    const auto root_pid = PrincipalId::from_string("01ARZ3NDEKTSV4RRFFQ69G5FAV").value();
    const auto persona_pid = PrincipalId::from_string("01ARZ3NDEKTSV4RRFFQ69G5FBW").value();
    {
        domain::Principal r;
        r.id = r.owner = root_pid;
        (void)s.registry().add_principal(r);
        domain::Principal a;
        a.id = persona_pid;
        a.kind = domain::PrincipalKind::Persona;
        a.owner = root_pid;
        (void)s.registry().add_principal(a);
        (void)s.registry().add_persona(persona_pid, root_pid, domain::CardBody{"苍月千鹤", "锚", ""});

        // 身份分片同镜像（快照路径权限源；drain_once 直驱，池未启动前完成）
        (void)s.identity_shard().post(identity::AddPrincipalCmd{r});
        (void)s.identity_shard().post(identity::AddPrincipalCmd{a});
        (void)s.identity_shard().post(
            identity::AddPersonaCmd{persona_pid, root_pid, domain::CardBody{"苍月千鹤", "锚", ""}});
        s.identity_shard().drain_once();
    }

    auto& host = s.bootstrap_host();
    const auto sid = SessionId::from_bytes(UlidFactory{}.next().bytes());
    {
        domain::Session sess;
        sess.id = sid;
        sess.owner = root_pid;
        sess.canon = "月下庭院：青石铺地，桂影婆娑。";
        sess.members.push_back(domain::Member{root_pid, domain::SessionRole::Owner, 0});
        sess.members.push_back(domain::Member{persona_pid, domain::SessionRole::Member, 0});
        (void)s.registry().create_session(sess);
        (void)host.create_session(sid, 0);
    }
    s.allow(sid, root_pid);
    s.auth().register_token("dev-root", root_pid);

    const auto actual = s.start();
    if (!actual.has_value()) {
        std::fprintf(stderr, "listen failed: %s\n", actual.error().message.c_str());
        return 1;
    }
    std::printf("oly-server listening on 127.0.0.1:%u\n", static_cast<unsigned>(*actual));
    std::printf("token: dev-root   session: %s\n", sid.str().c_str());
    std::fflush(stdout);

    for (;;) {
        std::this_thread::sleep_for(std::chrono::hours(1));
    }
}
