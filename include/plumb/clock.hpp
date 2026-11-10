#pragma once

#include <cpuid.h>
#include <x86intrin.h>

#include <chrono>
#include <cstddef>
#include <cstdint>

#include "plumb/summary.hpp"

namespace plumb {

/// @brief The time stamp counter as a clock, converted to nanoseconds at a rate measured once.
class TscClock {
   public:
    /// @brief Whether the TSC ticks at one rate in every power and frequency state.
    [[nodiscard]] static bool invariant() noexcept {
        unsigned eax = 0, ebx = 0, ecx = 0, edx = 0;
        if (!__get_cpuid(0x80000007, &eax, &ebx, &ecx, &edx)) return false;
        return (edx & (1u << 8)) != 0;
    }

    /// @brief The TSC now. Earlier instructions finish before the read, later ones start after it.
    [[nodiscard]] static std::uint64_t now() noexcept {
        unsigned aux   = 0;
        auto     ticks = __rdtscp(&aux);
        _mm_lfence();
        return ticks;
    }

    /// @brief Ticks an empty stamp pair costs, over `reads` back to back pairs.
    [[nodiscard]] static Summary stamp_cost(std::size_t reads) pre(reads > 0) {
        Summary cost;
        for (std::size_t i = 0; i < reads; ++i) {
            auto a = now();
            auto b = now();
            cost.add(static_cast<double>(b - a));
        }
        return cost;
    }

    /// @brief A clock whose rate is the ticks counted against the steady clock over `window`.
    [[nodiscard]] static TscClock calibrate(std::chrono::nanoseconds window)
        pre(window > std::chrono::nanoseconds::zero()) {
        using steady = std::chrono::steady_clock;
        auto start   = steady::now();
        auto ticks0  = now();
        auto end     = start;
        while ((end = steady::now()) - start < window) {
        }
        auto   ticks1 = now();
        double ns     = std::chrono::duration<double, std::nano>(end - start).count();
        return TscClock{static_cast<double>(ticks1 - ticks0) / ns};
    }

    /// @brief Nanoseconds in a signed tick count.
    [[nodiscard]] double ns(std::int64_t ticks) const noexcept {
        return static_cast<double>(ticks) / ticks_per_ns_;
    }

    /// @brief Ticks per nanosecond.
    [[nodiscard]] double ghz() const noexcept { return ticks_per_ns_; }

   private:
    explicit TscClock(double ticks_per_ns) : ticks_per_ns_{ticks_per_ns} {}

    double ticks_per_ns_;
};

}  // namespace plumb
