#pragma once

#include <sched.h>

#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

#include "plumb/clock.hpp"
#include "plumb/fixture.hpp"
#include "plumb/summary.hpp"

namespace plumb::detail {

/// @brief Makes the optimiser assume `x` was read and changed here.
template <class T>
    requires std::is_scalar_v<T>
inline void opaque(T& x) noexcept {
    asm volatile("" : "+r"(x) : : "memory");
}

/// @brief Whether the op returns its own time.
template <class O, class F>
inline constexpr bool self_measured = std::same_as<std::invoke_result_t<const O&, F&>, Measured>;

/// @brief The figure for `n` calls of the op on `fixture`. Ticks between one stamp pair, or the
/// sum of a measured op's own nanoseconds.
/// @tparam F The fixture type.
/// @tparam O The op type.
template <Fixture F, Op<F> O>
[[gnu::noinline, gnu::aligned(64)]] double run_batch(F& fixture, std::size_t n) noexcept {
    F* f = &fixture;
    opaque(f);
    opaque(n);
    if constexpr (self_measured<O, F>) {
        double ns = 0;
        for (std::size_t i = 0; i < n; ++i) ns += O{}(*f).ns.count();
        return ns;
    } else {
        auto t0 = TscClock::now();
        for (std::size_t i = 0; i < n; ++i) O{}(*f);
        auto t1 = TscClock::now();
        return static_cast<double>(t1 - t0);
    }
}

/// @brief Batches run before the counted ones, until both the time and the count have passed.
struct Warmup {
    std::chrono::nanoseconds time    = std::chrono::milliseconds{100};
    std::size_t              batches = 10;
};

/// @brief One repetition's raw record. A figure per batch of `n` ops, the stamp cost, and the CPU
/// before the first counted batch and after the last, -1 when unknown. Figures are ticks, or
/// nanoseconds when `measured`.
struct Batches {
    std::size_t         n;
    bool                measured;
    Summary             stamp_cost;
    std::vector<double> figures;
    int                 cpu_start = -1;
    int                 cpu_end   = -1;
};

/// @brief Reads per stamp cost measurement.
inline constexpr std::size_t stamp_reads = 10'000;

/// @brief Warmup, the stamp cost, then `count` batches of `n` ops, all on one fixture.
/// The fixture is reset before every batch but the first.
/// @tparam F The fixture type.
/// @tparam O The op type.
template <Fixture F, Op<F> O>
Batches run_batches(Reset<F> reset, std::size_t n, std::size_t count, Warmup warmup = {}) noexcept
    pre(n > 0) pre(count > 0) {
    F    fixture{};
    bool first = true;
    auto batch = [&] {
        if (!first) reset.fn(fixture);
        first = false;
        return run_batch<F, O>(fixture, n);
    };

    using steady = std::chrono::steady_clock;
    auto start   = steady::now();
    for (std::size_t done = 0; done < warmup.batches || steady::now() - start < warmup.time; ++done)
        (void)batch();

    Batches out{.n          = n,
                .measured   = self_measured<O, F>,
                .stamp_cost = TscClock::stamp_cost(stamp_reads),
                .figures    = {}};
    out.figures.reserve(count);
    out.cpu_start = sched_getcpu();
    for (std::size_t i = 0; i < count; ++i) out.figures.push_back(batch());
    out.cpu_end = sched_getcpu();
    return out;
}

}  // namespace plumb::detail
