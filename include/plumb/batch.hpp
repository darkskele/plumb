#pragma once

#include <sched.h>

#include <chrono>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <type_traits>
#include <vector>

#include "plumb/clock.hpp"
#include "plumb/counters.hpp"
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

/// @brief One repetition's raw record. A figure and hardware counts per batch of `n` ops, the stamp
/// cost, the mean counts of an empty batch, and the CPU before the first counted batch and after
/// the last, -1 when unknown. Figures are ticks, or nanoseconds when `measured`.
struct Batches {
    std::size_t                 n;
    bool                        measured;
    Summary                     stamp_cost;
    std::vector<double>         figures;
    std::vector<CounterReading> counts       = {};
    CounterReading              counter_cost = {.cycles = std::numeric_limits<double>::quiet_NaN(),
                                                .ref_cycles = std::numeric_limits<double>::quiet_NaN(),
                                                .instructions = std::numeric_limits<double>::quiet_NaN()};
    int                         cpu_start    = -1;
    int                         cpu_end      = -1;
};

/// @brief Reads per stamp cost measurement.
inline constexpr std::size_t stamp_reads = 10'000;

/// @brief Empty batches per counter cost measurement.
inline constexpr std::size_t counter_reads = 1'000;

/// @brief The mean counts of an empty batch of `F` and `O`, from the batches counted throughout.
template <Fixture F, Op<F> O>
CounterReading counter_cost(const Counters& counters, F& fixture) noexcept {
    double      cycles = 0, ref_cycles = 0, instructions = 0;
    std::size_t counted = 0;
    for (std::size_t i = 0; i < counter_reads; ++i) {
        auto a = counters.read();
        (void)run_batch<F, O>(fixture, 0);
        auto c = detail::counted(a, counters.read());
        if (std::isnan(c.cycles)) continue;
        cycles += c.cycles;
        ref_cycles += c.ref_cycles;
        instructions += c.instructions;
        ++counted;
    }
    if (counted == 0) return detail::counted({.ok = false}, {.ok = false});
    auto n = static_cast<double>(counted);
    return {.cycles = cycles / n, .ref_cycles = ref_cycles / n, .instructions = instructions / n};
}

/// @brief Warmup, the stamp and counter costs, then `count` batches of `n` ops, all on one
/// fixture, each counted on `counters`. The fixture is reset before every batch but the first.
/// @tparam F The fixture type.
/// @tparam O The op type.
template <Fixture F, Op<F> O>
Batches run_batches(Reset<F> reset, std::size_t n, std::size_t count, Warmup warmup = {},
                    Counters counters = Counters::open()) noexcept pre(n > 0) pre(count > 0) {
    F    fixture{};
    bool first   = true;
    auto prepare = [&] {
        if (!first) reset.fn(fixture);
        first = false;
    };
    auto batch = [&] {
        prepare();
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
    out.counter_cost = counter_cost<F, O>(counters, fixture);
    out.figures.reserve(count);
    out.counts.reserve(count);
    out.cpu_start = sched_getcpu();
    for (std::size_t i = 0; i < count; ++i) {
        prepare();
        auto a = counters.read();
        out.figures.push_back(run_batch<F, O>(fixture, n));
        out.counts.push_back(counted(a, counters.read()));
    }
    out.cpu_end = sched_getcpu();
    return out;
}

}  // namespace plumb::detail
