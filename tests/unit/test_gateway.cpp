// 网关基础通路（07）：优先级车道、P0 背压、P2/P3 削减、deadline 回收、确定性 FakeProvider
#include <oly/gateway/domain.hpp>
#include <oly/gateway/gateway.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace oly::gateway;

namespace {

CompletionRequest make_req(const Priority p, const std::string prompt = "内容",
                                    const uint64_t deadline = 0) {
    CompletionRequest r;
    r.priority = p;
    r.prompt = prompt;
    r.deadline_ms = deadline;
    return r;
}

}   // namespace

TEST_CASE("网关：优先级出队 P0 → P1 → P2 → P3，ref 回填", "[gateway]") {
    FakeProvider prov;
    Gateway gw(prov);

    const auto r3 = gw.submit(make_req(Priority::P3_Summary));
    const auto r1 = gw.submit(make_req(Priority::P1_Drama));
    const auto r0 = gw.submit(make_req(Priority::P0_Interactive));
    const auto r2 = gw.submit(make_req(Priority::P2_Ambient));
    REQUIRE(r0 != 0);
    REQUIRE(r3 != 0);
    REQUIRE(gw.counters().submitted == 4);

    const auto c0 = gw.pump(0);
    REQUIRE(c0.has_value());
    REQUIRE(c0->request_ref == r0);   // P0 先行
    REQUIRE(c0->ok);
    REQUIRE(c0->provider == "fake");
    REQUIRE(gw.pump(0)->request_ref == r1);
    REQUIRE(gw.pump(0)->request_ref == r2);
    REQUIRE(gw.pump(0)->request_ref == r3);
    REQUIRE_FALSE(gw.pump(0).has_value());
    REQUIRE(gw.counters().completed == 4);
}

TEST_CASE("网关：P0/P1 满即拒绝（背压，不丢）；P2/P3 满即丢弃计数", "[gateway]") {
    FakeProvider prov;
    Gateway::Caps caps;
    caps.lane_depth = {2, 2, 1, 1};
    Gateway gw(prov, caps);

    REQUIRE(gw.submit(make_req(Priority::P0_Interactive)) != 0);
    REQUIRE(gw.submit(make_req(Priority::P0_Interactive)) != 0);
    REQUIRE(gw.submit(make_req(Priority::P0_Interactive)) == 0);   // 满 → 拒绝
    REQUIRE(gw.counters().rejected_hi == 1);

    REQUIRE(gw.submit(make_req(Priority::P1_Drama)) != 0);
    REQUIRE(gw.submit(make_req(Priority::P1_Drama)) != 0);   // 恰满（cap 2）
    REQUIRE(gw.submit(make_req(Priority::P1_Drama)) == 0);
    REQUIRE(gw.counters().rejected_hi == 2);   // 1×P0 + 1×P1

    REQUIRE(gw.submit(make_req(Priority::P2_Ambient)) != 0);
    REQUIRE(gw.submit(make_req(Priority::P2_Ambient)) == 0);   // 满 → 丢弃
    REQUIRE(gw.counters().dropped_p2 == 1);

    REQUIRE(gw.submit(make_req(Priority::P3_Summary)) != 0);
    REQUIRE(gw.submit(make_req(Priority::P3_Summary)) == 0);   // 第 2 条才触发丢弃
    REQUIRE(gw.counters().dropped_p3 == 1);
}

TEST_CASE("网关：deadline 到期回收，供应商零调用", "[gateway]") {
    FakeProvider prov;
    Gateway gw(prov);
    const auto ref = gw.submit(make_req(Priority::P0_Interactive, "慢请求", 100));
    const auto c = gw.pump(200);
    REQUIRE(c.has_value());
    REQUIRE_FALSE(c->ok);
    REQUIRE(c->error == "gateway:deadline");
    REQUIRE(c->request_ref == ref);
    REQUIRE(prov.calls() == 0);
    REQUIRE(gw.counters().expired == 1);
    REQUIRE(gw.saturation() == 0.0f);   // 回收后队列空
}

TEST_CASE("网关：饱和度水位", "[gateway]") {
    FakeProvider prov;
    Gateway::Caps caps;
    caps.lane_depth = {4, 4, 4, 4};
    Gateway gw(prov, caps);
    REQUIRE(gw.saturation() == 0.0f);
    for (int i = 0; i < 4; ++i) (void)gw.submit(make_req(Priority::P2_Ambient));   // 恰满 P2 道
    (void)gw.submit(make_req(Priority::P2_Ambient));                              // 第 5 条被丢弃
    REQUIRE(gw.counters().dropped_p2 == 1);
    REQUIRE(gw.saturation() == Catch::Approx(4.0f / 16.0f));   // 水位按已入队计
}

TEST_CASE("网关域：独立 loop 池托管（07 §1 域拆分就位）", "[gateway]") {
    FakeProvider prov;
    auto dom = Domain::create(prov, {}, {}).take();
    auto& gw = dom.gateway();

    const auto ref = gw.submit(make_req(Priority::P0_Interactive, "内容"));
    REQUIRE(ref != 0);

    dom.start();
    const auto c = gw.pump(0);   // M0 同步通路（泵协程化属 M1）
    REQUIRE(c.has_value());
    REQUIRE(c->ok);
    REQUIRE_FALSE(c->text.empty());   // 模板兜底出句
    dom.stop();
    REQUIRE(dom.gateway().counters().completed == 1);
}

TEST_CASE("FakeProvider：scripted 顺序 → 模板兜底 → fail_from 失败", "[gateway]") {
    SECTION("脚本按序消耗，耗尽后同一 prompt 确定性选句") {
        FakeProvider p1({"甲一", "甲二"});
        FakeProvider p2({"甲一", "甲二"});
        CompletionRequest r;
        r.prompt = "同一输入";
        REQUIRE(p1.complete(r).text == "甲一");
        REQUIRE(p1.complete(r).text == "甲二");
        REQUIRE(p2.complete(r).text == "甲一");
        REQUIRE(p2.complete(r).text == "甲二");
        const auto t1 = p1.complete(r);   // 双双进入模板兜底
        const auto t2 = p2.complete(r);
        REQUIRE(t1.ok);
        REQUIRE(t2.ok);
        REQUIRE(t1.text == t2.text);   // 同 prompt → 同句（确定性闸门 13 §2）
        REQUIRE(t1.tokens_out > 0);
    }
    SECTION("fail_from：第 N 次起一律失败") {
        FakeProvider p({"乙"}, 1);   // 第 1 次成功，第 2 次起失败
        CompletionRequest r;
        r.prompt = "x";
        REQUIRE(p.complete(r).ok);
        const auto bad = p.complete(r);
        REQUIRE_FALSE(bad.ok);
        REQUIRE(bad.error.find("fake:failed") != std::string::npos);
        REQUIRE(p.calls() == 2);
    }
}
