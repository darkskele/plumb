#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "plumb/machine.hpp"

namespace fs = std::filesystem;

namespace {

fs::path fresh_sys(const std::string& name) {
    auto root = fs::path(testing::TempDir()) / ("plumb_sys_" + name);
    fs::remove_all(root);
    return root;
}

void put(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream(p) << text;
}

void throttles(const fs::path& sys, int cpu, int core, int package) {
    auto dir = sys / "devices/system/cpu" / ("cpu" + std::to_string(cpu)) / "thermal_throttle";
    put(dir / "core_throttle_count", std::to_string(core) + "\n");
    put(dir / "package_throttle_count", std::to_string(package) + "\n");
}

}  // namespace

TEST(Machine, ThrottlesAreTheCoresOfEveryCpuAndTheFirstCpusPackage) {
    auto sys = fresh_sys("throttles");
    throttles(sys, 0, 1, 10);
    throttles(sys, 2, 4, 10);
    throttles(sys, 3, 100, 10);
    EXPECT_EQ(plumb::detail::throttle_count(sys, std::vector<int>{0, 2}), 1 + 4 + 10);
}

TEST(Machine, ThrottlesUnknownWithoutTheFiles) {
    auto sys = fresh_sys("no_throttles");
    throttles(sys, 0, 1, 10);
    EXPECT_TRUE(std::isnan(plumb::detail::throttle_count(sys, std::vector<int>{0, 1})));
}

TEST(Machine, PackageTemperatureFromCoretemp) {
    auto sys = fresh_sys("temp");
    put(sys / "class/hwmon/hwmon0/name", "acpitz\n");
    put(sys / "class/hwmon/hwmon0/temp1_input", "99000\n");
    put(sys / "class/hwmon/hwmon4/name", "coretemp\n");
    put(sys / "class/hwmon/hwmon4/temp2_label", "Core 0\n");
    put(sys / "class/hwmon/hwmon4/temp2_input", "61000\n");
    put(sys / "class/hwmon/hwmon4/temp1_label", "Package id 0\n");
    put(sys / "class/hwmon/hwmon4/temp1_input", "52500\n");
    EXPECT_EQ(plumb::detail::package_temp(sys), 52.5);
}

TEST(Machine, TemperatureUnknownWithoutCoretemp) {
    auto sys = fresh_sys("no_temp");
    put(sys / "class/hwmon/hwmon0/name", "acpitz\n");
    put(sys / "class/hwmon/hwmon0/temp1_input", "99000\n");
    EXPECT_TRUE(std::isnan(plumb::detail::package_temp(sys)));
    EXPECT_TRUE(std::isnan(plumb::detail::package_temp(fresh_sys("empty"))));
}

TEST(Machine, ReadsThisMachine) {
    auto r = plumb::read_machine();
    if (!std::isnan(r.throttles)) {
        EXPECT_GE(r.throttles, 0);
    }
    if (!std::isnan(r.temp_c)) {
        EXPECT_GT(r.temp_c, 0);
        EXPECT_LT(r.temp_c, 150);
    }
}

TEST(Machine, ARepetitionThatThrottledIsVoid) {
    plumb::RawHeader h{};
    h.throttles = 2;
    EXPECT_EQ(plumb::detail::machine_void(h), "throttled 2 times");
    h.throttles = 0;
    EXPECT_FALSE(plumb::detail::machine_void(h));
    h.throttles = std::nan("");
    EXPECT_FALSE(plumb::detail::machine_void(h));
}
