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

namespace {

struct Tracked {
    static inline int         built     = 0;
    static inline int         destroyed = 0;
    static inline const void* last      = nullptr;
    static inline bool        moved     = false;

    std::size_t calls = 0;

    Tracked() { ++built; }

    ~Tracked() { ++destroyed; }

    static void start() { built = destroyed = 0, last = nullptr, moved = false; }
};

constexpr auto tracked_op = [](Tracked& t) {
    if (Tracked::last && Tracked::last != &t) Tracked::moved = true;
    Tracked::last = &t;
    ++t.calls;
};

using TrackedOp = decltype(tracked_op);

int resets = 0;

}  // namespace

TEST(Batches, RunsCountBatchesOfN) {
    auto b = plumb::detail::run_batches<Counter, CountOp>(plumb::keep, 10, 5);
    EXPECT_EQ(b.n, 10u);
    ASSERT_EQ(b.ticks.size(), 5u);
    for (auto t : b.ticks) EXPECT_GT(t, 0u);
}

TEST(Batches, DefaultResetRebuildsBetweenBatches) {
    Tracked::start();
    (void)plumb::detail::run_batches<Tracked, TrackedOp>({}, 3, 4);
    EXPECT_EQ(Tracked::built, 4);
    EXPECT_EQ(Tracked::destroyed, 4);
    EXPECT_FALSE(Tracked::moved);
}

TEST(Batches, ResetRunsBetweenBatchesOnly) {
    Tracked::start();
    resets = 0;
    (void)plumb::detail::run_batches<Tracked, TrackedOp>(
        [](Tracked& t) {
            EXPECT_EQ(t.calls % 3, 0u);
            ++resets;
        },
        3, 4);
    EXPECT_EQ(resets, 3);
    EXPECT_EQ(Tracked::built, 1);
    EXPECT_EQ(Tracked::destroyed, 1);
}

TEST(BatchesDeathTest, EmptyBatchStops) {
    EXPECT_DEATH(((void)plumb::detail::run_batches<Counter, CountOp>(plumb::keep, 0, 1)), "n > 0");
}

TEST(BatchesDeathTest, NoBatchesStops) {
    EXPECT_DEATH(((void)plumb::detail::run_batches<Counter, CountOp>(plumb::keep, 1, 0)),
                 "count > 0");
}
