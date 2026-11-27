// ULID：单调性 / 编解码往返 / 时钟推进 / 确定性工厂
#include <oly/core/ulid.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cctype>
#include <cstdint>
#include <set>
#include <string>

TEST_CASE("ULID 单毫秒内严格单调", "[core][ulid]") {
    const uint64_t t = 1'700'000'000'000ull;
    oly::UlidFactory f([&] { return t; });

    std::set<std::string> seen;
    oly::Ulid prev;
    for (int i = 0; i < 1000; ++i) {
        const auto u = f.next();
        REQUIRE(u.timestamp_ms() == t);
        if (i > 0) REQUIRE(prev < u);
        REQUIRE(seen.insert(u.str()).second);
        prev = u;
    }
}

TEST_CASE("ULID 字符串编解码往返（含小写）", "[core][ulid]") {
    oly::UlidFactory f([] { return 1'600'000'000'000ull; }, std::array<std::byte, 10>{});
    for (int i = 0; i < 8; ++i) {
        const auto u = f.next();
        const auto s = u.str();
        REQUIRE(s.size() == 26);
        auto back = oly::Ulid::parse(s);
        REQUIRE(back.has_value());
        REQUIRE(*back == u);

        std::string lower;
        for (char c : s) lower += static_cast<char>(::tolower(c));
        auto back2 = oly::Ulid::parse(lower);
        REQUIRE(back2.has_value());
        REQUIRE(*back2 == u);
    }
}

TEST_CASE("时钟推进产生新毫秒时间戳", "[core][ulid]") {
    uint64_t t = 1'000'000ull;
    oly::UlidFactory f([&] { return t; });
    const auto a = f.next();
    t += 5;
    const auto b = f.next();
    REQUIRE(a.timestamp_ms() == 1'000'000ull);
    REQUIRE(b.timestamp_ms() == 1'000'005ull);
    REQUIRE(a < b);
}

TEST_CASE("确定性工厂输出可复现", "[core][golden-support]") {
    std::array<std::byte, 10> seed{};
    for (std::size_t i = 0; i < seed.size(); ++i) seed[i] = std::byte{static_cast<uint8_t>(i + 1)};

    oly::UlidFactory f1([] { return 42; }, seed);
    oly::UlidFactory f2([] { return 42; }, seed);
    REQUIRE(f1.next().str() == f2.next().str());
    REQUIRE(f1.next().str() == f2.next().str());
}

TEST_CASE("非法字符串被拒绝", "[core][ulid]") {
    REQUIRE_FALSE(oly::Ulid::parse("").has_value());
    REQUIRE_FALSE(oly::Ulid::parse("0123456789ABCDEFGHJKMNPQRSTV").has_value());   // 25 位
    REQUIRE_FALSE(oly::Ulid::parse("0123456789ABCDEFGHIJKLMNOPTVWXYZ").has_value());   // 27 位含 P?—长度
    // 含非法字母 I/L/O/U
    REQUIRE_FALSE(oly::Ulid::parse("I123456789ABCDEFGHJKMNPQRSTVWX").has_value());
}
