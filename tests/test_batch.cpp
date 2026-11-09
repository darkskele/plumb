#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>

#include "plumb/batch.hpp"

namespace {

struct Counter {
    std::size_t calls = 0;
};

struct Pure {
    int x = 3;
};

constexpr auto count_op = [](Counter& c) { ++c.calls; };
constexpr auto pure_op  = [](Pure& p) { return p.x * 7 + 1; };

using CountOp = decltype(count_op);
using PureOp  = decltype(pure_op);

}  // namespace

TEST(Batch, RunsTheOpExactlyNTimes) {
    for (std::size_t n : {0uz, 1uz, 7uz, 1000uz}) {
        Counter c;
        (void)plumb::detail::run_batch<Counter, CountOp>(c, n);
        EXPECT_EQ(c.calls, n);
    }
}

TEST(Batch, KeepsAPureOp) {
    Pure          p;
    std::uint64_t ticks = plumb::detail::run_batch<Pure, PureOp>(p, 1'000'000);
    // A removed loop costs one stamp pair, tens of ticks.
    EXPECT_GT(ticks, 100'000u);
}

TEST(Batch, EmptyBatchCostsAboutAStampPair) {
    Pure          p;
    std::uint64_t ticks = plumb::detail::run_batch<Pure, PureOp>(p, 0);
    EXPECT_LT(ticks, 10'000u);
}
