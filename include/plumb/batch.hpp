#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

#include "plumb/case.hpp"
#include "plumb/clock.hpp"

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

/// @brief One repetition's raw samples, the ticks of each batch of `n` ops.
struct Batches {
    std::size_t                n;
    std::vector<std::uint64_t> ticks;
};

/// @brief `count` batches of `n` ops on one fixture, reset before every batch but the first.
/// @tparam F The fixture type.
/// @tparam O The op type.
template <Fixture F, Op<F> O>
Batches run_batches(Reset<F> reset, std::size_t n, std::size_t count) noexcept pre(n > 0)
    pre(count > 0) {
    Batches out{.n = n, .ticks = {}};
    out.ticks.reserve(count);
    F fixture{};
    for (std::size_t i = 0; i < count; ++i) {
        if (i > 0) reset.fn(fixture);
        out.ticks.push_back(run_batch<F, O>(fixture, n));
    }
    return out;
}

}  // namespace plumb::detail
