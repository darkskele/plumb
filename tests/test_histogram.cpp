#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <tuple>
#include <vector>

#include "plumb/histogram.hpp"

using plumb::Histogram;

TEST(Histogram, SmallValuesHaveABucketEach) {
    for (std::int64_t v = 0; v < Histogram::per_group; ++v) {
        auto i = Histogram::index(v);
        EXPECT_EQ(Histogram::lower(i), v);
        EXPECT_EQ(Histogram::upper(i), v);
    }
}

TEST(Histogram, BucketsTileTheRangeInOrder) {
    for (std::size_t i = 0; i + 1 < Histogram::buckets; ++i) {
        ASSERT_EQ(Histogram::index(Histogram::lower(i)), i);
        ASSERT_EQ(Histogram::index(Histogram::upper(i)), i);
        ASSERT_EQ(Histogram::upper(i) + 1, Histogram::lower(i + 1));
    }
}

TEST(Histogram, ABucketIsWithinOnePartInPerGroup) {
    for (std::size_t i = Histogram::per_group; i < Histogram::buckets; ++i) {
        auto width = Histogram::upper(i) - Histogram::lower(i) + 1;
        ASSERT_LE(width * Histogram::per_group, Histogram::lower(i));
    }
}

TEST(Histogram, ValuesPastTheRangeShareTheLastBucket) {
    std::int64_t top = std::int64_t{1} << Histogram::range_bits;
    EXPECT_EQ(Histogram::index(top - 1), Histogram::buckets - 1);
    EXPECT_EQ(Histogram::index(top), Histogram::buckets - 1);
    EXPECT_EQ(Histogram::index(INT64_MAX), Histogram::buckets - 1);
}

TEST(Histogram, Figures) {
    Histogram h;
    for (std::int64_t v = 1; v <= 100; ++v) h.record(v);
    EXPECT_EQ(h.count(), 100u);
    EXPECT_EQ(h.min(), 1);
    EXPECT_EQ(h.max(), 100);
    EXPECT_EQ(h.percentile(0), 1);
    EXPECT_EQ(h.percentile(0.5), 50);
    EXPECT_EQ(h.percentile(0.99), 99);
    EXPECT_EQ(h.percentile(1), 100);
}

TEST(Histogram, PercentileIsTheTopOfItsBucketNeverPastTheMax) {
    Histogram h;
    h.record(1000);
    auto i = Histogram::index(1000);
    ASSERT_GT(Histogram::upper(i), 1000);
    EXPECT_EQ(h.percentile(0.5), 1000);
    h.record(100'000);
    EXPECT_EQ(h.percentile(0.5), Histogram::upper(i));
}

TEST(Histogram, NegativeValuesAreCountedApart) {
    Histogram h;
    h.record(-5);
    h.record(7);
    EXPECT_EQ(h.negative(), 1u);
    EXPECT_EQ(h.count(), 1u);
    EXPECT_EQ(h.min(), 7);
}

TEST(Histogram, EachVisitsTheFilledBucketsLowestFirst) {
    Histogram h;
    h.record(3);
    h.record(3);
    h.record(1000);
    std::vector<std::tuple<std::int64_t, std::int64_t, std::uint64_t>> seen;
    h.each(
        [&](std::int64_t lo, std::int64_t hi, std::uint64_t n) { seen.emplace_back(lo, hi, n); });
    auto i = Histogram::index(1000);
    EXPECT_EQ(seen, (std::vector<std::tuple<std::int64_t, std::int64_t, std::uint64_t>>{
                        {3, 3, 2}, {Histogram::lower(i), Histogram::upper(i), 1}}));
}

TEST(Histogram, AddingIsRecordingBoth) {
    Histogram a, b, both;
    for (std::int64_t v : {5, 900, 70'000}) {
        a.record(v);
        both.record(v);
    }
    for (std::int64_t v : {-1, 2, 900, 1'000'000}) {
        b.record(v);
        both.record(v);
    }
    a += b;
    EXPECT_EQ(a, both);
}

TEST(Histogram, EmptyHasNoFigures) {
    Histogram h;
    EXPECT_EQ(h.count(), 0u);
    EXPECT_EQ(h, Histogram{});
}

TEST(HistogramDeathTest, PercentileOutOfRangeStops) {
    Histogram h;
    h.record(1);
    EXPECT_DEATH((void)h.percentile(1.5), "p <= 1");
}

TEST(HistogramDeathTest, EmptyHasNoPercentile) {
    EXPECT_DEATH((void)Histogram{}.percentile(0.5), "count_ > 0");
}
