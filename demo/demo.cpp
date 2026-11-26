#include <cstddef>
#include <cstdint>
#include <vector>

#include "plumb/plumb.hpp"

// Dummy cases across the decades, from about a nanosecond an op to about a millisecond, and one
// whose slowest ops stall on memory.

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

// Two arrays of chain values, one that stays in cache and one far larger than it.
struct Memory {
    std::vector<std::uint64_t> near = fill(std::size_t{1} << 9);
    std::vector<std::uint64_t> far  = fill(std::size_t{1} << 25);
    std::uint64_t              x    = 0x9e3779b97f4a7c15;

    static std::vector<std::uint64_t> fill(std::size_t n) {
        std::vector<std::uint64_t> v(n);
        Chain                      c;
        for (auto& e : v) {
            spin(c, 1);
            e = c.x;
        }
        return v;
    }
};

// One step, then a read the chain waits on. About 2% of reads go to the far array and miss the
// cache, the same instructions either way, so the slowest ops stall.
const plumb::Case demo_stall = plumb::op<Memory>("demo_stall",
                                                 [](Memory& m) {
                                                     auto x = m.x;
                                                     x ^= x << 13;
                                                     x ^= x >> 7;
                                                     x ^= x << 17;
                                                     const auto& from =
                                                         x % 50 == 0 ? m.far : m.near;
                                                     m.x = x ^ from[(x >> 8) & (from.size() - 1)];
                                                     plumb::sink(m.x);
                                                 },
                                                 {.max_batch = 1, .reset = plumb::keep});

}  // namespace
