#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <cstddef>
#include <utility>
#include <vector>

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
    EXPECT_EQ(plumb::detail::count_for(1e2), plumb::detail::max_count);
}

TEST(Run, FastOpGrowsToMaxBatchExactly) {
    auto clock = plumb::TscClock::calibrate(20ms);
    auto e     = plumb::detail::entry<Counter, decltype(count_op)>("count", 10, plumb::keep);
    EXPECT_EQ(plumb::size(e, clock).plan.n, 10u);
}

TEST(Run, SlowOpStopsDoublingPastTheBatchTarget) {
    auto clock = plumb::TscClock::calibrate(20ms);
    auto e     = plumb::detail::entry<Spin, decltype(spin_op)>("spin", 1024, plumb::keep);
    auto c     = plumb::size(e, clock);
    EXPECT_GE(c.plan.n, 2u);
    EXPECT_LE(c.plan.n, 4u);
    EXPECT_GE(c.plan.count, plumb::detail::min_count);
    EXPECT_LE(c.plan.count, plumb::detail::max_count);
    // The fastest batch it saw, n spins of about 2us.
    EXPECT_GE(c.batch_ns, 2'000.0 * static_cast<double>(c.plan.n));
    EXPECT_EQ(c.plan.count, plumb::detail::count_for(c.batch_ns));
}

namespace {

plumb::Raw raw_of(double stamp_mean, double stamp_variance, std::vector<double> ns) {
    plumb::Raw raw{};
    raw.header.n              = 2;
    raw.header.count          = ns.size();
    raw.header.stamp_mean     = stamp_mean;
    raw.header.stamp_variance = stamp_variance;
    raw.ns                    = std::move(ns);
    return raw;
}

}  // namespace

TEST(Run, RowPoolsItsRepetitions) {
    auto                    a = raw_of(10, 4, {8, 2});
    auto                    b = raw_of(20, 16, {6, 4, 10});
    std::vector<plumb::Raw> raws{a, b};
    auto                    row = plumb::detail::row_of("x", {.n = 2, .count = 3}, raws);
    EXPECT_EQ(row.name, "x");
    EXPECT_EQ(row.n, 2u);
    EXPECT_EQ(row.count, 5u);
    EXPECT_EQ(row.min, 1.0);
    EXPECT_EQ(row.max, 5.0);
    EXPECT_EQ(row.p50, 3.0);
    EXPECT_EQ(row.stamp_mean, 15.0);
    EXPECT_EQ(row.stamp_sd, std::sqrt(10.0));
}
