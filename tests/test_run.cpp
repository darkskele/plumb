#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>

#include "plumb/run.hpp"

using namespace std::chrono_literals;

namespace {

struct Counter {
    std::size_t calls = 0;
};

struct Spin {};

constexpr auto count_op = [](Counter& c) { ++c.calls; };

// About 2us per call, whatever the build.
constexpr auto spin_op = [](Spin&) {
    auto end = std::chrono::steady_clock::now() + 2us;
    while (std::chrono::steady_clock::now() < end) {
    }
};

}  // namespace

TEST(Run, CountFillsTheRunTime) { EXPECT_EQ(plumb::detail::count_for(1e6), 1000u); }

TEST(Run, CountHasAFloorAndACap) {
    EXPECT_EQ(plumb::detail::count_for(1e8), plumb::detail::min_count);
    EXPECT_EQ(plumb::detail::count_for(1e3), plumb::detail::max_count);
}

TEST(Run, FastOpGrowsToMaxBatchExactly) {
    auto clock = plumb::TscClock::calibrate(20ms);
    auto e     = plumb::detail::entry<Counter, decltype(count_op)>("count", 10, plumb::keep);
    EXPECT_EQ(plumb::size(e, clock).n, 10u);
}

TEST(Run, SlowOpStopsDoublingPastTheBatchTarget) {
    auto clock = plumb::TscClock::calibrate(20ms);
    auto e     = plumb::detail::entry<Spin, decltype(spin_op)>("spin", 1024, plumb::keep);
    auto plan  = plumb::size(e, clock);
    EXPECT_GE(plan.n, 2u);
    EXPECT_LE(plan.n, 4u);
    EXPECT_GE(plan.count, plumb::detail::min_count);
    EXPECT_LE(plan.count, plumb::detail::max_count);
}
