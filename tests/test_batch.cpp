#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "plumb/batch.hpp"

using namespace std::chrono_literals;

namespace {

struct Counter {
    std::size_t calls = 0;
};

struct Pure {
    int x = 3;
};

constexpr auto count_op = [](Counter& c) { ++c.calls; };
constexpr auto pure_op  = [](Pure& p) { plumb::sink(p.x * 7 + 1); };

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
    Pure   p;
    double ticks = plumb::detail::run_batch<Pure, PureOp>(p, 1'000'000);
    // A removed loop costs one stamp pair, tens of ticks.
    EXPECT_GT(ticks, 100'000);
}

TEST(Batch, EmptyBatchCostsAboutAStampPair) {
    Pure   p;
    double ticks = plumb::detail::run_batch<Pure, PureOp>(p, 0);
    EXPECT_LT(ticks, 10'000);
}

namespace {

constexpr auto measured_op = [](Counter& c) {
    ++c.calls;
    return plumb::Measured{std::chrono::duration<double, std::nano>{2.5}};
};

using MeasuredOp = decltype(measured_op);

}  // namespace

TEST(Batch, MeasuredBatchSumsItsOpsOwnTimes) {
    Counter c;
    EXPECT_EQ((plumb::detail::run_batch<Counter, MeasuredOp>(c, 4)), 10.0);
    EXPECT_EQ(c.calls, 4u);
}

namespace {

struct Tracked {
    static inline int         built     = 0;
    static inline int         destroyed = 0;
    static inline const void* last      = nullptr;
    static inline bool        moved     = false;
    static inline std::size_t ops       = 0;

    std::size_t calls = 0;

    Tracked() { ++built; }

    ~Tracked() { ++destroyed; }

    static void start() { built = destroyed = 0, last = nullptr, moved = false, ops = 0; }
};

constexpr auto tracked_op = [](Tracked& t) {
    if (Tracked::last && Tracked::last != &t) Tracked::moved = true;
    Tracked::last = &t;
    ++t.calls;
    ++Tracked::ops;
};

using TrackedOp = decltype(tracked_op);

int resets = 0;

constexpr plumb::detail::Warmup no_warmup{.time = {}, .batches = 0};

}  // namespace

TEST(Batches, RunsCountBatchesOfN) {
    auto b = plumb::detail::run_batches<Counter, CountOp>(plumb::keep, 10, 5, no_warmup);
    EXPECT_EQ(b.n, 10u);
    EXPECT_FALSE(b.measured);
    ASSERT_EQ(b.figures.size(), 5u);
    for (auto t : b.figures) EXPECT_GT(t, 0);
}

TEST(Batches, MeasuredBatchesAreMarked) {
    auto b = plumb::detail::run_batches<Counter, MeasuredOp>(plumb::keep, 4, 3, no_warmup);
    EXPECT_TRUE(b.measured);
    EXPECT_EQ(b.figures, (std::vector<double>{10.0, 10.0, 10.0}));
}

TEST(Batches, DefaultResetRebuildsBetweenBatches) {
    Tracked::start();
    (void)plumb::detail::run_batches<Tracked, TrackedOp>({}, 3, 4, no_warmup);
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
        3, 4, no_warmup);
    EXPECT_EQ(resets, 3);
    EXPECT_EQ(Tracked::built, 1);
    EXPECT_EQ(Tracked::destroyed, 1);
}

TEST(Batches, WarmupRunsItsBatchesUncounted) {
    Tracked::start();
    auto b = plumb::detail::run_batches<Tracked, TrackedOp>(plumb::keep, 3, 2,
                                                            {.time = {}, .batches = 5});
    EXPECT_EQ(b.figures.size(), 2u);
    EXPECT_EQ(Tracked::ops, (5u + 2u) * 3u);
}

TEST(Batches, WarmupRunsForItsTime) {
    auto start = std::chrono::steady_clock::now();
    (void)plumb::detail::run_batches<Counter, CountOp>(plumb::keep, 1, 1,
                                                       {.time = 50ms, .batches = 0});
    EXPECT_GE(std::chrono::steady_clock::now() - start, 50ms);
}

TEST(Batches, ResetBetweenEveryTwoBatchesWarmupIncluded) {
    resets = 0;
    (void)plumb::detail::run_batches<Tracked, TrackedOp>([](Tracked&) { ++resets; }, 3, 3,
                                                         {.time = {}, .batches = 2});
    EXPECT_EQ(resets, 2 + 3 - 1);
}

TEST(Batches, RecordsTheStampCost) {
    auto b = plumb::detail::run_batches<Counter, CountOp>(plumb::keep, 1, 1, no_warmup);
    EXPECT_GT(b.stamp_cost.count(), 1u);
    EXPECT_GT(b.stamp_cost.min(), 0.0);
}

TEST(BatchesDeathTest, EmptyBatchStops) {
    EXPECT_DEATH(((void)plumb::detail::run_batches<Counter, CountOp>(plumb::keep, 0, 1)), "n > 0");
}

TEST(BatchesDeathTest, NoBatchesStops) {
    EXPECT_DEATH(((void)plumb::detail::run_batches<Counter, CountOp>(plumb::keep, 1, 0)),
                 "count > 0");
}
