#include <gtest/gtest.h>
#include <sched.h>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "plumb/counters.hpp"

namespace fs = std::filesystem;

namespace {

fs::path fresh_sys(const std::string& name) {
    auto root = fs::path(testing::TempDir()) / ("plumb_pmu_" + name);
    fs::remove_all(root);
    return root;
}

void put(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p) << text;
}

void pmu(const fs::path& sys, const std::string& name, const std::string& type,
         const std::optional<std::string>& cpus) {
    auto dir = sys / "bus/event_source/devices" / name;
    put(dir / "type", type + "\n");
    if (cpus) put(dir / "cpus", *cpus + "\n");
}

}  // namespace

TEST(Counters, ParsesACpuList) {
    EXPECT_EQ(plumb::detail::cpu_list("0-3,8,10-11\n"), (std::vector<int>{0, 1, 2, 3, 8, 10, 11}));
    EXPECT_EQ(plumb::detail::cpu_list("5"), (std::vector<int>{5}));
    EXPECT_EQ(plumb::detail::cpu_list(""), (std::vector<int>{}));
}

TEST(Counters, HybridCpuTakesThePmuOfItsCoreType) {
    auto sys = fresh_sys("hybrid");
    pmu(sys, "cpu_core", "4", "0-3");
    pmu(sys, "cpu_atom", "10", "4-11");
    pmu(sys, "uncore_imc_0", "20", std::nullopt);
    EXPECT_EQ(plumb::detail::pmu_for(sys, 2), 4u);
    EXPECT_EQ(plumb::detail::pmu_for(sys, 7), 10u);
    EXPECT_EQ(plumb::detail::pmu_for(sys, 12), std::nullopt);
}

TEST(Counters, OneCoreTypeHasNoPmuToChoose) {
    auto sys = fresh_sys("single");
    pmu(sys, "cpu", "4", std::nullopt);
    EXPECT_EQ(plumb::detail::pmu_for(sys, 0), std::nullopt);
    EXPECT_EQ(plumb::detail::pmu_for(fresh_sys("none"), 0), std::nullopt);
}

TEST(Counters, CountsAreTheDifference) {
    plumb::detail::Snapshot a{.values = {10, 20, 30}, .stopped = 5};
    plumb::detail::Snapshot b{.values = {15, 27, 39}, .stopped = 5};
    EXPECT_EQ(plumb::detail::counted(a, b),
              (plumb::CounterReading{.cycles = 5, .ref_cycles = 7, .instructions = 9}));
}

TEST(Counters, CountsWithAGapInCountingAreUnknown) {
    plumb::detail::Snapshot a{.values = {10, 20, 30}, .stopped = 5};
    plumb::detail::Snapshot b{.values = {15, 27, 39}, .stopped = 80};
    auto                    c = plumb::detail::counted(a, b);
    EXPECT_TRUE(std::isnan(c.cycles));
    EXPECT_TRUE(std::isnan(c.ref_cycles));
    EXPECT_TRUE(std::isnan(c.instructions));
}

TEST(Counters, CountsFromAReadOffTheHardwareAreUnknown) {
    plumb::detail::Snapshot a{.values = {10, 20, 30}, .stopped = 5};
    plumb::detail::Snapshot b{.values = {}, .stopped = 5, .ok = false};
    EXPECT_TRUE(std::isnan(plumb::detail::counted(a, b).cycles));
    EXPECT_TRUE(std::isnan(plumb::detail::counted(b, a).cycles));
}

TEST(Counters, ReadingsCompareByteForByte) {
    auto nan = std::numeric_limits<double>::quiet_NaN();
    EXPECT_EQ((plumb::CounterReading{nan, nan, nan}), (plumb::CounterReading{nan, nan, nan}));
    EXPECT_NE((plumb::CounterReading{1, 2, 3}), (plumb::CounterReading{1, 2, 4}));
}

TEST(Counters, NotOpenIsUnknown) {
    plumb::Counters none;
    EXPECT_FALSE(none.read().ok);
    EXPECT_TRUE(std::isnan(plumb::detail::counted(none.read(), none.read()).cycles));
}

TEST(Counters, CountThisThreadWhereThePmuAllows) {
    auto counters = plumb::Counters::open();
    if (!counters.is_open()) GTEST_SKIP() << "no PMU here";
    auto          before = counters.read();
    std::uint64_t x      = 0;
    for (int i = 0; i < 1'000'000; ++i) asm volatile("add $1, %0" : "+r"(x));
    auto after = counters.read();
    auto c     = plumb::detail::counted(before, after);
    if (std::isnan(c.cycles)) GTEST_SKIP() << "moved off the PMU's core type";
    EXPECT_GE(c.instructions, 1'000'000);
    EXPECT_GT(c.cycles, 0);
    EXPECT_GT(c.ref_cycles, 0);
}
