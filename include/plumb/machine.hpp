#pragma once

#include <sched.h>

#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "plumb/raw.hpp"

namespace plumb {

/// @brief What the machine reports at one moment. NaN where it cannot be read.
struct MachineReading {
    double throttles;
    double temp_c;
};

namespace detail {

inline constexpr double unknown = std::numeric_limits<double>::quiet_NaN();

/// @brief The number in the file at `path`, or NaN.
inline double read_number(const std::filesystem::path& path) {
    double        v = 0;
    std::ifstream f(path);
    return f >> v ? v : unknown;
}

/// @brief Throttle events so far: the core counts of `cpus`, and the package count of the first.
inline double throttle_count(const std::filesystem::path& sys, std::span<const int> cpus)
    pre(!cpus.empty()) {
    auto dir = [&](int cpu) {
        return sys / std::format("devices/system/cpu/cpu{}/thermal_throttle", cpu);
    };
    double sum = read_number(dir(cpus.front()) / "package_throttle_count");
    for (int cpu : cpus) sum += read_number(dir(cpu) / "core_throttle_count");
    return sum;
}

/// @brief The package temperature in degrees Celsius from the `coretemp` sensor.
inline double package_temp(const std::filesystem::path& sys) {
    std::error_code ec;
    for (const auto& hwmon : std::filesystem::directory_iterator(sys / "class/hwmon", ec)) {
        std::string name;
        if (!(std::ifstream(hwmon.path() / "name") >> name) || name != "coretemp") continue;
        for (const auto& file : std::filesystem::directory_iterator(hwmon.path(), ec)) {
            auto stem = file.path().filename().string();
            if (!stem.ends_with("_label")) continue;
            std::string label;
            std::getline(std::ifstream(file.path()), label);
            if (label.starts_with("Package id")) {
                stem.resize(stem.size() - std::string_view("_label").size());
                return read_number(hwmon.path() / (stem + "_input")) / 1000;
            }
        }
    }
    return unknown;
}

/// @brief The CPUs this thread may run on.
inline std::vector<int> allowed_cpus() {
    cpu_set_t set;
    if (sched_getaffinity(0, sizeof set, &set) != 0) return {};
    std::vector<int> cpus;
    for (int i = 0; i < CPU_SETSIZE; ++i)
        if (CPU_ISSET(i, &set)) cpus.push_back(i);
    return cpus;
}

/// @brief Why a repetition is void from what the machine reported, or nothing.
inline std::optional<std::string> machine_void(const RawHeader& h) {
    if (h.throttles > 0) return std::format("throttled {} times", h.throttles);
    return {};
}

}  // namespace detail

/// @brief The machine's throttle events over the CPUs this thread may run on, and its package
/// temperature.
inline MachineReading read_machine(const std::filesystem::path& sys = "/sys") {
    auto cpus = detail::allowed_cpus();
    return {.throttles = cpus.empty() ? detail::unknown : detail::throttle_count(sys, cpus),
            .temp_c    = detail::package_temp(sys)};
}

}  // namespace plumb
