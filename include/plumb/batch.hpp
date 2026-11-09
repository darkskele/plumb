#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>

#include "plumb/case.hpp"
#include "plumb/clock.hpp"

namespace plumb::detail {

/// @brief Makes the optimiser assume `x` was read and changed here.
template <class T>
inline void opaque(T& x) noexcept {
    asm volatile("" : "+r,m"(x) : : "memory");
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

}  // namespace plumb::detail
