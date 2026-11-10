#pragma once

#include <chrono>
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

/// @brief Makes the optimiser assume `x` was read here.
template <class T>
inline void sink(const T& x) noexcept {
    asm volatile("" : : "r,m"(x) : "memory");
}

/// @brief Ticks for `n` calls of the op on `fixture`, between one stamp pair.
/// @tparam F The fixture type.
/// @tparam O The op type.
/// @todo An op returning `plumb::Measured` takes its own samples.
template <Fixture F, Op<F> O>
[[gnu::noinline, gnu::aligned(64)]] std::uint64_t run_batch(F& fixture, std::size_t n) noexcept {
    F* f = &fixture;
    opaque(f);
    opaque(n);
    auto t0 = TscClock::now();
    for (std::size_t i = 0; i < n; ++i) {
        if constexpr (std::is_void_v<std::invoke_result_t<const O&, F&>>)
            O{}(*f);
        else
            sink(O{}(*f));
    }
    auto t1 = TscClock::now();
    return t1 - t0;
}

/// @brief Batches run before the counted ones, until both the time and the count have passed.
struct Warmup {
    std::chrono::nanoseconds time    = std::chrono::milliseconds{100};
    std::size_t              batches = 10;
};

/// @brief One repetition's raw record. The ticks of each batch of `n` ops, and the stamp cost.
struct Batches {
    std::size_t                n;
    Summary                    stamp_cost;
    std::vector<std::uint64_t> ticks;
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

    Batches out{.n = n, .stamp_cost = TscClock::stamp_cost(stamp_reads), .ticks = {}};
    out.ticks.reserve(count);
    for (std::size_t i = 0; i < count; ++i) out.ticks.push_back(batch());
    return out;
}

}  // namespace plumb::detail
