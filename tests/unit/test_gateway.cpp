// 网关基础通路（07）：优先级车道、P0 背压、P2/P3 削减、deadline 回收、确定性 FakeProvider
#include <oly/gateway/domain.hpp>
#include <oly/gateway/gateway.hpp>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <oly/core/id.hpp>
#include <oly/quota/quota.hpp>

#include <string>

using namespace oly;
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


// ── M1-2：配额硬墙 / 熔断器 / 成本台账 ─────────────────────────────

namespace {

// 可切换成败的供应商（熔断演练：先坏后好，确定性序列）
class ToggleProvider final : public LLMProvider {
public:
    std::string name() const override { return "toggle"; }
    Completion complete(const CompletionRequest& /*req*/) override {
        Completion c;
        c.provider = name();
        c.tokens_in = 10;
        c.tokens_out = 5;
        c.ok = !fail_;
        if (fail_) c.error = "toggle:down";
        ++calls_;
        return c;
    }
    bool fail_ = false;
    uint64_t calls_ = 0;
};

}   // namespace

TEST_CASE("网关：配额硬墙——P0 硬失败 / P2 拒绝走降级，预留随到期释放", "[gateway][quota]") {
    FakeProvider prov;
    quota::Ledger ledger(quota::Limits{
        .burst = {1e9, 1e9},
        .volume = {.cap = 40.0, .window_ms = 1000},   // 主体体量很小
        .world = {.cap = 1e9, .window_ms = 1000}});
    Gateway gw(prov, {}, &ledger);

    gateway::CompletionRequest r0 = make_req(Priority::P0_Interactive, "一段有点长的提示词");
    r0.max_tokens = 16;   // est = 粗估 12 + 16 = 28 < 40（首件预留成功）
    r0.on_behalf_of = oly::PrincipalId::from_string("01ARZ3NDEKTSV4RRFFQ69G5FAV").value();
    REQUIRE(gw.submit(r0, 0) != 0);
    (void)gw.pump(0);   // 结算 ≈ 28 → 桶见底

    // 体量见底 → 第二件 P0 硬失败（quota_exhausted 形状：ref=0 + 计数）
    REQUIRE(gw.submit(r0, 0) == 0);
    REQUIRE(gw.counters().quota_rejected == 1);

    // P2 同桶耗尽 → 拒绝走会话侧降级阶梯（不硬停，09 §5）
    gateway::CompletionRequest r2 = make_req(Priority::P2_Ambient, "内容");
    r2.on_behalf_of = r0.on_behalf_of;
    r2.max_tokens = 16;
    REQUIRE(gw.submit(r2, 0) == 0);
    REQUIRE(gw.counters().quota_dropped == 1);

    // 窗口滚动 → 恢复受理
    REQUIRE(gw.submit(r0, 2000) != 0);
    (void)gw.pump(2000);

    // deadline 到期 → 预留释放（无悬挂占用，07 §3）：独立主体桶
    gateway::CompletionRequest rd = make_req(Priority::P0_Interactive, "慢请求");
    rd.max_tokens = 16;
    rd.on_behalf_of = oly::PrincipalId::from_string("01ARZ3NDEKTSV4RRFFQ69G5FBW").value();
    rd.deadline_ms = 3000;
    REQUIRE(gw.submit(rd, 2000) != 0);
    (void)gw.pump(4000);
    REQUIRE(gw.counters().expired == 1);
    REQUIRE(gw.submit(rd, 4000) != 0);   // 已释放 → 可再次预留
}

TEST_CASE("网关：熔断器——滑窗失败率开断，冷却后半开探测恢复", "[gateway][breaker]") {
    ToggleProvider prov;
    Gateway::Caps caps;
    caps.breaker = {.window = 4, .fail_ratio = 0.5, .cooldown_ms = 100};
    Gateway gw(prov, caps);

    // 2 成功 + 2 失败 → 滑窗满且失败率达阈 → 熔断
    prov.fail_ = false;
    for (int i = 0; i < 2; ++i) {
        REQUIRE(gw.submit(make_req(Priority::P0_Interactive), 0) != 0);
        (void)gw.pump(0);
    }
    prov.fail_ = true;
    for (int i = 0; i < 2; ++i) {
        REQUIRE(gw.submit(make_req(Priority::P0_Interactive), 0) != 0);
        (void)gw.pump(0);
    }
    REQUIRE(gw.breaker_open());
    REQUIRE(gw.counters().breaker_openings == 1);

    // 开期：P2/P3 直接拒（P0 仍过——会话侧降级兜底，P0 永不丢）
    REQUIRE(gw.submit(make_req(Priority::P2_Ambient), 0) == 0);
    REQUIRE(gw.counters().breaker_dropped == 1);
    REQUIRE(gw.saturation() == 1.0f);   // 熔断开期 = 全饱和（07 §4 反馈上调）

    // 冷却期满 → 半开单件探测：失败 → 重新熔断（新冷却）
    REQUIRE(gw.submit(make_req(Priority::P2_Ambient), 100) != 0);   // 探测件放行
    REQUIRE_FALSE(gw.pump(100)->ok);
    REQUIRE(gw.breaker_open());
    REQUIRE(gw.submit(make_req(Priority::P2_Ambient), 150) == 0);   // 新冷却内仍拒

    // 再冷却 → 探测成功 → 闭合，滑窗重置
    prov.fail_ = false;
    REQUIRE(gw.submit(make_req(Priority::P2_Ambient), 300) != 0);
    REQUIRE(gw.pump(300)->ok);
    REQUIRE_FALSE(gw.breaker_open());
    REQUIRE(gw.submit(make_req(Priority::P3_Summary), 300) != 0);   // 正常受理
}

TEST_CASE("网关：成本台账累计（07 §7 计数雏形）", "[gateway][cost]") {
    FakeProvider prov;
    Gateway gw(prov);
    (void)gw.submit(make_req(Priority::P0_Interactive, "第一段"));
    (void)gw.pump(0);
    (void)gw.submit(make_req(Priority::P2_Ambient, "第二段更长的提示"));
    (void)gw.pump(0);
    const auto& c = gw.counters();
    REQUIRE(c.tokens_in_total > 0);
    REQUIRE(c.tokens_out_total > 0);
}
