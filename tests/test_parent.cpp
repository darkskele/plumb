#include <gtest/gtest.h>

#include <algorithm>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "plumb/abandon.hpp"
#include "plumb/parent.hpp"

using Order = std::vector<std::vector<std::size_t>>;
using Args  = std::vector<std::string>;

TEST(Parent, EveryRoundRunsEveryCaseOnce) {
    auto order = plumb::detail::round_order(5, 4, 42);
    ASSERT_EQ(order.size(), 4u);
    for (auto round : order) {
        std::ranges::sort(round);
        EXPECT_EQ(round, (std::vector<std::size_t>{0, 1, 2, 3, 4}));
    }
}

TEST(Parent, OrderReplaysFromItsSeed) {
    EXPECT_EQ(plumb::detail::round_order(5, 4, 42), plumb::detail::round_order(5, 4, 42));
    EXPECT_NE(plumb::detail::round_order(5, 4, 42), plumb::detail::round_order(5, 4, 43));
}

TEST(Parent, RoundsAreShuffledApart) {
    auto order = plumb::detail::round_order(5, 4, 42);
    EXPECT_FALSE(std::ranges::all_of(order, [&](auto& r) { return r == order[0]; }));
}

TEST(Parent, OrderIsTheSameOnEveryBuild) {
    // Fisher-Yates on mt19937_64, whose output the standard fixes. Worked out apart from plumb.
    EXPECT_EQ(plumb::detail::round_order(4, 2, 1), (Order{{1, 2, 3, 0}, {3, 1, 0, 2}}));
}

TEST(Parent, OneCaseOrNoRounds) {
    EXPECT_EQ(plumb::detail::round_order(1, 3, 7), (Order{{0}, {0}, {0}}));
    EXPECT_EQ(plumb::detail::round_order(3, 0, 7), Order{});
}

TEST(Parent, FilesInTheRunDirectory) {
    EXPECT_EQ(plumb::detail::plan_path("runs/x", "push_a"), "runs/x/push_a.plan");
    EXPECT_EQ(plumb::detail::raw_path("runs/x", "push_a", 3), "runs/x/push_a.3.raw");
}

TEST(Parent, ChildArgumentsCarryTheRateExactly) {
    double ghz = 2.611234567891234;
    EXPECT_EQ(plumb::detail::calibrate_args("push_a", ghz, "runs/x/push_a.plan"),
              (Args{"--calibrate", "push_a", "--ghz", "2.611234567891234", "--out",
                    "runs/x/push_a.plan"}));
    EXPECT_EQ(
        plumb::detail::child_args("push_a", {.n = 8, .count = 100}, ghz, "runs/x/push_a.3.raw"),
        (Args{"--child", "push_a", "--n", "8", "--count", "100", "--ghz", "2.611234567891234",
              "--out", "runs/x/push_a.3.raw"}));
}

TEST(Parent, DescribesAChildThatFailed) {
    using Kind = plumb::detail::Exit::Kind;
    EXPECT_EQ(plumb::detail::describe({Kind::nonzero, 3}), "exited 3");
    EXPECT_EQ(plumb::detail::describe({Kind::signal, SIGSEGV}), "killed by SIGSEGV");
    EXPECT_EQ(plumb::detail::describe({Kind::not_started, 2}),
              "did not start: No such file or directory");
}

TEST(Parent, WhyAChildIsVoid) {
    using Kind = plumb::detail::Exit::Kind;
    EXPECT_EQ(plumb::detail::void_reason({Kind::ok, 0}), std::nullopt);
    EXPECT_EQ(plumb::detail::void_reason({Kind::nonzero, plumb::detail::abandon_code}),
              "abandoned");
    EXPECT_EQ(plumb::detail::void_reason({Kind::nonzero, 3}), "exited 3");
    EXPECT_EQ(plumb::detail::void_reason({Kind::signal, SIGABRT}), "killed by SIGABRT");
}

TEST(AbandonDeathTest, SaysWhyAndExitsWithItsCode) {
    EXPECT_EXIT(plumb::abandon("rig not ready"),
                testing::ExitedWithCode(plumb::detail::abandon_code),
                "^plumb: abandoned: rig not ready\n$");
}
