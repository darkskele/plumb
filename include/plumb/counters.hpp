#pragma once

#include <linux/perf_event.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <x86intrin.h>

#include <array>
#include <atomic>
#include <charconv>
#include <cstdint>
#include <cstring>
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

    /// @brief Byte for byte, so unknown counts compare equal.
    bool operator==(const CounterReading& other) const noexcept {
        return std::memcmp(this, &other, sizeof other) == 0;
    }
};

static_assert(sizeof(CounterReading) == 3 * sizeof(double));

namespace detail {

/// @brief The counter group's values at one moment, and the time it had spent off the hardware.
/// Not ok when it could not be read.
struct Snapshot {
    std::array<std::uint64_t, 3> values  = {};
    std::uint64_t                stopped = 0;
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

/// @brief The counts between two snapshots, or NaN unless the group counted throughout.
inline CounterReading counted(const Snapshot& a, const Snapshot& b) noexcept {
    constexpr double unknown = std::numeric_limits<double>::quiet_NaN();
    if (!a.ok || !b.ok || a.stopped != b.stopped)
        return {.cycles = unknown, .ref_cycles = unknown, .instructions = unknown};
    auto diff = [&](std::size_t i) { return static_cast<double>(b.values[i] - a.values[i]); };
    return {.cycles = diff(0), .ref_cycles = diff(1), .instructions = diff(2)};
}

}  // namespace detail

/// @brief Cycles, reference cycles and instructions, counted in user space for this thread on the
/// core type it was opened on, and read without a system call.
class Counters {
   public:
    Counters() = default;

    Counters(Counters&& other) noexcept
        : fds_(std::exchange(other.fds_, {-1, -1, -1})), pages_(std::exchange(other.pages_, {})) {}

    Counters& operator=(Counters&& other) noexcept {
        if (this != &other) {
            close_all();
            fds_   = std::exchange(other.fds_, {-1, -1, -1});
            pages_ = std::exchange(other.pages_, {});
        }
        return *this;
    }

    ~Counters() { close_all(); }

    /// @brief The group counting from now, or one that is not open when the PMU refuses it or
    /// does not let user space read it.
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
            c.fds_[i]           = static_cast<int>(syscall(SYS_perf_event_open, &attr, 0, -1,
                                                 i == 0 ? -1 : c.fds_[0], PERF_FLAG_FD_CLOEXEC));
            if (c.fds_[i] < 0) return Counters{};
            void* page = mmap(nullptr, page_size(), PROT_READ, MAP_SHARED, c.fds_[i], 0);
            if (page == MAP_FAILED) return Counters{};
            c.pages_[i] = static_cast<const perf_event_mmap_page*>(page);
            if (!c.pages_[i]->cap_user_rdpmc) return Counters{};
        }
        return c;
    }

    [[nodiscard]] bool is_open() const noexcept { return pages_[0] != nullptr; }

    /// @brief The counters now, not ok when the group is not open or not on the hardware.
    [[nodiscard]] detail::Snapshot read() const noexcept {
        if (!is_open()) return {.ok = false};
        detail::Snapshot s;
        for (std::size_t i = 0; i < pages_.size(); ++i) {
            auto v = read_one(pages_[i], i == 0 ? &s.stopped : nullptr);
            if (!v) return {.ok = false};
            s.values[i] = *v;
        }
        return s;
    }

   private:
    static std::size_t page_size() noexcept {
        return static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
    }

    // The kernel's user-page protocol: retry while it rewrote the page, an index of 0 is off the
    // hardware, and the counter's raw value is sign extended from its width.
    static std::optional<std::uint64_t> read_one(const perf_event_mmap_page* pc,
                                                 std::uint64_t*              stopped) noexcept {
        std::uint32_t seq = 0, index = 0;
        std::uint64_t count = 0, enabled = 0, running = 0;
        do {
            seq     = __atomic_load_n(&pc->lock, __ATOMIC_ACQUIRE);
            index   = pc->index;
            count   = static_cast<std::uint64_t>(pc->offset);
            enabled = pc->time_enabled;
            running = pc->time_running;
            if (index != 0) {
                int  shift = 64 - pc->pmc_width;
                auto raw   = static_cast<std::int64_t>(__rdpmc(static_cast<int>(index - 1)));
                count += static_cast<std::uint64_t>((raw << shift) >> shift);
            }
            std::atomic_signal_fence(std::memory_order_acq_rel);
        } while (__atomic_load_n(&pc->lock, __ATOMIC_ACQUIRE) != seq);
        if (index == 0) return std::nullopt;
        if (stopped) *stopped = enabled - running;
        return count;
    }

    void close_all() noexcept {
        for (auto& page : pages_)
            if (page)
                munmap(const_cast<perf_event_mmap_page*>(std::exchange(page, nullptr)),
                       page_size());
        for (int& fd : fds_)
            if (fd >= 0) ::close(std::exchange(fd, -1));
    }

    std::array<int, 3>                         fds_   = {-1, -1, -1};
    std::array<const perf_event_mmap_page*, 3> pages_ = {};
};

}  // namespace plumb
