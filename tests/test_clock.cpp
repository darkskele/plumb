#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <string>

#include "plumb/clock.hpp"

using namespace std::chrono_literals;

namespace {

bool cpuinfo_has(const std::string& flag) {
    std::ifstream cpuinfo("/proc/cpuinfo");
    std::string   word;
    while (cpuinfo >> word)
        if (word == flag) return true;
    return false;
}

}  // namespace

TEST(Clock, InvariantMatchesKernel) {
    EXPECT_EQ(plumb::TscClock::invariant(), cpuinfo_has("nonstop_tsc"));
}

TEST(Clock, NeverGoesBackwards) {
    std::uint64_t last = plumb::TscClock::now();
    for (int i = 0; i < 1'000'000; ++i) {
        std::uint64_t t = plumb::TscClock::now();
        ASSERT_GE(t, last);
        last = t;
    }
}

TEST(Clock, RateIsPlausible) {
    auto clock = plumb::TscClock::calibrate(50ms);
    EXPECT_GT(clock.ghz(), 0.5);
    EXPECT_LT(clock.ghz(), 10.0);
}

TEST(Clock, AgreesWithSteadyClock) {
    using steady = std::chrono::steady_clock;
    auto clock   = plumb::TscClock::calibrate(100ms);

    auto start  = steady::now();
    auto ticks0 = plumb::TscClock::now();
    auto end    = start;
    while ((end = steady::now()) - start < 200ms) {
    }
    auto ticks1 = plumb::TscClock::now();

    double steady_ns = std::chrono::duration<double, std::nano>(end - start).count();
    double tsc_ns    = clock.ns(static_cast<std::int64_t>(ticks1 - ticks0));
    EXPECT_NEAR(tsc_ns / steady_ns, 1.0, 0.01);
}

TEST(Clock, NsKeepsFractionsAndSign) {
    auto clock = plumb::TscClock::calibrate(10ms);
    EXPECT_DOUBLE_EQ(clock.ns(1), 1.0 / clock.ghz());
    EXPECT_DOUBLE_EQ(clock.ns(-1), -1.0 / clock.ghz());
}

TEST(ClockDeathTest, EmptyWindowStops) {
    EXPECT_DEATH((void)plumb::TscClock::calibrate(0ms), "window");
}
