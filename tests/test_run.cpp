#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <string>
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

// Its own time is 1us, however long the call takes.
constexpr auto measured_op = [](Counter&) { return plumb::Measured{1us}; };

TEST(Run, MeasuredOpIsSizedByItsOwnTime) {
    auto clock = plumb::TscClock::at(3.0);
    auto e     = plumb::detail::entry<Counter, decltype(measured_op)>("measured", 64, plumb::keep);
    EXPECT_EQ(plumb::size(e, clock),
              (plumb::Calibration{.plan = {.n = 8, .count = 125'000}, .batch_ns = 8'000}));
}

namespace {

plumb::Repetition rep_of(double stamp_mean, double stamp_variance, std::vector<double> ns) {
    plumb::Raw raw{};
    raw.header.n              = 2;
    raw.header.count          = ns.size();
    raw.header.stamp_mean     = stamp_mean;
    raw.header.stamp_variance = stamp_variance;
    raw.ns                    = std::move(ns);
    return plumb::fold(raw, 0);
}

}  // namespace

TEST(Run, RowAddsItsRepetitionsHistograms) {
    // Per op, 1 to 100 ns over two repetitions.
    std::vector<double> low, high;
    for (int i = 1; i <= 40; ++i) low.push_back(2.0 * i);
    for (int i = 41; i <= 100; ++i) high.push_back(2.0 * i);
    std::vector<plumb::Repetition> reps{rep_of(10, 4, low), rep_of(20, 16, high)};
    auto                           row = plumb::detail::row_of("x", {.n = 2, .count = 3}, reps);

    plumb::Histogram both = reps[0].histogram;
    both += reps[1].histogram;
    EXPECT_EQ(row.name, "x");
    EXPECT_EQ(row.n, 2u);
    EXPECT_EQ(row.count, 100u);
    EXPECT_EQ(row.min, 1.0);
    EXPECT_EQ(row.max, 100.0);
    EXPECT_EQ(row.p50, static_cast<double>(both.percentile(0.50)) / 1000);
    EXPECT_EQ(row.p90, static_cast<double>(both.percentile(0.90)) / 1000);
    EXPECT_EQ(row.p99, static_cast<double>(both.percentile(0.99)) / 1000);
    EXPECT_NEAR(row.p50, 50.0, 50.0 / 128);
    EXPECT_EQ(row.stamp_mean, 15.0);
    EXPECT_EQ(row.stamp_sd, std::sqrt(10.0));
}

TEST(Run, WarnsOfABuildThatIsNotRelease) {
    char*       buf = nullptr;
    std::size_t len = 0;
    std::FILE*  err = open_memstream(&buf, &len);
    plumb::detail::check_build(err);
    std::fclose(err);
    std::string said(buf, len);
    std::free(buf);

    std::string expected;
#ifndef __OPTIMIZE__
    expected += "plumb: warning: built without optimisation, timings may be affected\n";
#endif
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
    expected += "plumb: warning: built with a sanitizer, timings may be affected\n";
#endif
    EXPECT_EQ(said, expected);
}
