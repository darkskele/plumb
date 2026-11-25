#include <cstdint>

#include "plumb/plumb.hpp"

// Dummy cases across the decades, from about a nanosecond an op to about a millisecond.

namespace {

struct Chain {
    std::uint64_t x = 0x9e3779b97f4a7c15;
};

// `steps` dependent xorshift steps, about a nanosecond each.
inline void spin(Chain& c, int steps) {
    auto x = c.x;
    for (int i = 0; i < steps; ++i) {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
    }
    c.x = x;
    plumb::sink(x);
}

const plumb::Case demo_spin_1 =
    plumb::op<Chain>("demo_spin_1", [](Chain& c) { spin(c, 1); }, {.max_batch = 4096});
const plumb::Case demo_spin_10 =
    plumb::op<Chain>("demo_spin_10", [](Chain& c) { spin(c, 10); }, {.max_batch = 1024});
const plumb::Case demo_spin_100 =
    plumb::op<Chain>("demo_spin_100", [](Chain& c) { spin(c, 100); }, {.max_batch = 128});
const plumb::Case demo_spin_1k =
    plumb::op<Chain>("demo_spin_1k", [](Chain& c) { spin(c, 1'000); }, {.max_batch = 16});
const plumb::Case demo_spin_10k =
    plumb::op<Chain>("demo_spin_10k", [](Chain& c) { spin(c, 10'000); });
const plumb::Case demo_spin_100k =
    plumb::op<Chain>("demo_spin_100k", [](Chain& c) { spin(c, 100'000); });
const plumb::Case demo_spin_1m =
    plumb::op<Chain>("demo_spin_1m", [](Chain& c) { spin(c, 1'000'000); });

// 10 steps or 1000 an op, chosen by the chain, so batches of one op fall in two modes.
const plumb::Case demo_two_modes =
    plumb::op<Chain>("demo_two_modes", [](Chain& c) { spin(c, (c.x & 1) != 0 ? 1'000 : 10); },
                     {.max_batch = 1, .reset = plumb::keep});

}  // namespace
