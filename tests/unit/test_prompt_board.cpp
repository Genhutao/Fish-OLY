// 提示板（06 §3.0）：槽位预算、地板锁死、降级顺序 Memory → Recent → Skills、manifest
#include <oly/session/prompt_board.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>

using namespace oly::session;

namespace {

std::string repeated(const std::string& s, const int n) {
    std::string out;
    for (int i = 0; i < n; ++i) out += s;
    return out;
}

}   // namespace

TEST_CASE("提示板：token 估算启发式", "[session][board]") {
    REQUIRE(estimate_tokens_heuristic("abcd") == 1);       // 4 ASCII → 1
    REQUIRE(estimate_tokens_heuristic("abcdefgh") == 2);
    REQUIRE(estimate_tokens_heuristic("月色未变。") == 8);  // 4 CJK + 句号 → (5*3+1)/2 = 8
    REQUIRE(estimate_tokens_heuristic("") == 0);
}

TEST_CASE("提示板：Identity/Output 地板锁死，超预算不截断", "[session][board]") {
    SlotInput in;
    in.identity = repeated("锚", 100);            // 远超 Identity 槽位预算
    in.output = repeated("约", 100);
    in.canon = "规则。";
    const auto b = assemble_board(100, in);       // 总预算 80，Identity 槽仅 ~9

    const auto est = estimate_tokens_heuristic(in.identity);
    REQUIRE(b.slots[static_cast<std::size_t>(Slot::Identity)] == in.identity);   // 逐字节完整
    REQUIRE(b.manifest.used[static_cast<std::size_t>(Slot::Identity)] == est);
    REQUIRE_FALSE(b.manifest.truncated[static_cast<std::size_t>(Slot::Identity)]);
    REQUIRE_FALSE(b.manifest.truncated[static_cast<std::size_t>(Slot::Output)]);
    REQUIRE(b.manifest.total > b.manifest.budget);   // 地板不可截 → 总量如实超支
}

TEST_CASE("提示板：降级顺序 Memory → Recent → Skills", "[session][board]") {
    // 记账（含 join 的每条一个 '\n'：f(n) = (3n+1)/2 + (n+3)/4 个 token）
    // 固定槽 52 tok（A=1 + 规×20=30 + 态×13=20 + O=1）
    SlotInput in;
    in.identity = "A";
    in.canon = repeated("规", 20);   // 超槽位预算：M0 只标记不截（地板/高优不动）
    in.state = repeated("态", 13);
    in.output = "O";
    for (int i = 0; i < 10; ++i) in.memory.emplace_back(1.0f - i * 0.1f, "记");
    for (int i = 0; i < 10; ++i) in.recent.push_back("话");
    for (int i = 0; i < 10; ++i) in.skills.emplace_back(1, "技");

    const auto count_lines = [](const std::string& s) {
        return static_cast<std::size_t>(std::count(s.begin(), s.end(), '\n'));
    };

    // ctx=100 → 预算 80：初始贪心 mem10/rec8/skl3（52+18+14+6=90）
    // → 逐条丢 Memory 至 n=4（52+7+14+6=79 ≤ 80）→ Recent/Skills 未动
    const auto b = assemble_board(100, in);
    REQUIRE(count_lines(b.slots[static_cast<std::size_t>(Slot::Memory)]) == 4);
    REQUIRE(count_lines(b.slots[static_cast<std::size_t>(Slot::Recent)]) == 8);
    REQUIRE(count_lines(b.slots[static_cast<std::size_t>(Slot::Skills)]) == 3);
    REQUIRE(b.manifest.truncated[static_cast<std::size_t>(Slot::Memory)]);
    REQUIRE_FALSE(b.manifest.truncated[static_cast<std::size_t>(Slot::Recent)]);
    REQUIRE_FALSE(b.manifest.truncated[static_cast<std::size_t>(Slot::Skills)]);
    REQUIRE(b.manifest.truncated[static_cast<std::size_t>(Slot::Canon)]);   // 超支仅标记
    REQUIRE(b.manifest.total == 79);
    REQUIRE(b.manifest.budget == 80);

    // ctx=40 → 预算 32：Memory 清空 → Recent 清空 → Skills 清空 → 只剩固定槽
    const auto b2 = assemble_board(40, in);
    REQUIRE(b2.slots[static_cast<std::size_t>(Slot::Memory)].empty());
    REQUIRE(b2.slots[static_cast<std::size_t>(Slot::Recent)].empty());
    REQUIRE(b2.slots[static_cast<std::size_t>(Slot::Skills)].empty());
    REQUIRE(b2.manifest.truncated[static_cast<std::size_t>(Slot::Skills)]);

    // 大上下文：什么都不丢
    const auto b3 = assemble_board(100000, in);
    REQUIRE(count_lines(b3.slots[static_cast<std::size_t>(Slot::Memory)]) == 10);
    REQUIRE_FALSE(b3.manifest.truncated[static_cast<std::size_t>(Slot::Memory)]);
}

TEST_CASE("提示板：槽内预算按分数/优先级取舍", "[session][board]") {
    SlotInput in;
    in.output = "O";
    // 分数降序：预算只装 2 条时留下的是高分的两条
    in.memory.emplace_back(0.1f, "低分条目xxxxxxxxxxxxxxxxxxxx");
    in.memory.emplace_back(0.9f, "高分一");
    in.memory.emplace_back(0.5f, "高分二");
    const auto b = assemble_board(1000, in);   // Memory 槽 ≈ 0.25×0.8×1000 = 200 tok，绰绰有余
    REQUIRE_FALSE(b.slots[static_cast<std::size_t>(Slot::Memory)].empty());

    // 用极小预算逼出分数序截断：低分条目先出局
    SlotInput in2;
    in2.output = "O";
    in2.memory.emplace_back(0.1f, repeated("长", 200));
    in2.memory.emplace_back(0.9f, repeated("短", 4));
    const auto b2 = assemble_board(30, in2);
    const auto& mem = b2.slots[static_cast<std::size_t>(Slot::Memory)];
    REQUIRE(mem.find("短") != std::string::npos);      // 高分保留
    REQUIRE(mem.find("长长长长长长") == std::string::npos);   // 低分被截
}

TEST_CASE("提示板：确定性 + 渲染槽序", "[session][board]") {
    SlotInput in;
    in.identity = "名字：千鹤";
    in.canon = "月下庭院。";
    in.recent.push_back("先前一句。");
    in.output = "一句中文。";
    const auto a = assemble_board(512, in);
    const auto b = assemble_board(512, in);
    REQUIRE(a.manifest.total == b.manifest.total);
    for (std::size_t i = 0; i < kSlotCount; ++i) REQUIRE(a.slots[i] == b.slots[i]);

    const auto text = render_prompt(a);
    const auto pos_id = text.find("[Identity]");
    const auto pos_canon = text.find("[Canon]");
    const auto pos_out = text.find("[Output]");
    REQUIRE(pos_id != std::string::npos);
    REQUIRE(pos_canon != std::string::npos);
    REQUIRE(pos_out != std::string::npos);
    REQUIRE(pos_id < pos_canon);
    REQUIRE(pos_canon < pos_out);
}
