#pragma once

#include <linux/perf_event.h>
#include <sched.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <array>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace plumb {

/// @brief Hardware counts over a stretch of this thread's user-space time. NaN where the PMU
/// could not count all of it.
struct CounterReading {
    double cycles;
    double ref_cycles;
    double instructions;

    bool operator==(const CounterReading&) const = default;
};

namespace detail {

/// @brief One read of a counter group: its times enabled and running, and its values in order.
struct GroupRead {
    std::uint64_t                enabled = 0;
    std::uint64_t                running = 0;
    std::array<std::uint64_t, 3> values  = {};
    bool                         ok      = true;
};

/// @brief The CPUs in a list such as `0-3,8`.
inline std::vector<int> cpu_list(std::string_view text) {
    std::vector<int> cpus;
    while (!text.empty() && text.front() != '\n') {
        int  first = 0, last = 0;
        auto end     = text.data() + text.size();
        auto [p, ec] = std::from_chars(text.data(), end, first);
        if (ec != std::errc{}) break;
        last = first;
        if (p != end && *p == '-') {
            auto [q, ec2] = std::from_chars(p + 1, end, last);
            if (ec2 != std::errc{}) break;
            p = q;
        }
        for (int c = first; c <= last; ++c) cpus.push_back(c);
        if (p != end && *p == ',') ++p;
        text.remove_prefix(static_cast<std::size_t>(p - text.data()));
    }
    return cpus;
}

/// @brief The type of the core PMU that counts on `cpu` when there is more than one, or nothing.
inline std::optional<std::uint32_t> pmu_for(const std::filesystem::path& sys, int cpu) {
    std::error_code ec;
    for (const auto& dev :
         std::filesystem::directory_iterator(sys / "bus/event_source/devices", ec)) {
        if (!dev.path().filename().string().starts_with("cpu")) continue;
        std::string list;
        if (!std::getline(std::ifstream(dev.path() / "cpus"), list)) continue;
        for (int c : cpu_list(list)) {
            std::uint32_t type = 0;
            if (c == cpu && (std::ifstream(dev.path() / "type") >> type)) return type;
        }
    }
    return std::nullopt;
}

/// @brief The counts between two reads, or NaN unless the group ran for all of it.
inline CounterReading counted(const GroupRead& a, const GroupRead& b) {
    constexpr double unknown = std::numeric_limits<double>::quiet_NaN();
    if (!a.ok || !b.ok || b.enabled - a.enabled != b.running - a.running)
        return {.cycles = unknown, .ref_cycles = unknown, .instructions = unknown};
    auto diff = [&](std::size_t i) { return static_cast<double>(b.values[i] - a.values[i]); };
    return {.cycles = diff(0), .ref_cycles = diff(1), .instructions = diff(2)};
}

}  // namespace detail

/// @brief Cycles, reference cycles and instructions, counted in user space for this thread on the
/// core type it was opened on.
class Counters {
   public:
    Counters() = default;

    Counters(Counters&& other) noexcept : fds_(std::exchange(other.fds_, {-1, -1, -1})) {}

    Counters& operator=(Counters&& other) noexcept {
        if (this != &other) {
            close_all();
            fds_ = std::exchange(other.fds_, {-1, -1, -1});
        }
        return *this;
    }

    ~Counters() { close_all(); }

    /// @brief The group counting from now, or one that is not open when the PMU refuses it.
    [[nodiscard]] static Counters open(const std::filesystem::path& sys = "/sys") {
        auto                                   pmu = detail::pmu_for(sys, sched_getcpu());
        Counters                               c;
        constexpr std::array<std::uint64_t, 3> events = {
            PERF_COUNT_HW_CPU_CYCLES, PERF_COUNT_HW_REF_CPU_CYCLES, PERF_COUNT_HW_INSTRUCTIONS};
        for (std::size_t i = 0; i < events.size(); ++i) {
            perf_event_attr attr{};
            attr.size           = sizeof attr;
            attr.type           = PERF_TYPE_HARDWARE;
            attr.config         = events[i] | (std::uint64_t{pmu.value_or(0)} << 32);
            attr.exclude_kernel = 1;
            attr.exclude_hv     = 1;
            attr.read_format =
                PERF_FORMAT_GROUP | PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;
            c.fds_[i] = static_cast<int>(syscall(SYS_perf_event_open, &attr, 0, -1,
                                                 i == 0 ? -1 : c.fds_[0], PERF_FLAG_FD_CLOEXEC));
            if (c.fds_[i] < 0) return Counters{};
        }
        return c;
    }

    [[nodiscard]] bool is_open() const noexcept { return fds_[0] >= 0; }

    /// @brief The group's times and values now, not ok when it is not open or the read fails.
    [[nodiscard]] detail::GroupRead read() const noexcept {
        struct {
            std::uint64_t                nr, enabled, running;
            std::array<std::uint64_t, 3> values;
        } buf{};

        if (!is_open() || ::read(fds_[0], &buf, sizeof buf) != sizeof buf || buf.nr != 3)
            return {.ok = false};
        return {.enabled = buf.enabled, .running = buf.running, .values = buf.values, .ok = true};
    }

   private:
    void close_all() noexcept {
        for (int& fd : fds_)
            if (fd >= 0) ::close(std::exchange(fd, -1));
    }

    std::array<int, 3> fds_ = {-1, -1, -1};
};

}  // namespace plumb
