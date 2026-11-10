#include <gtest/gtest.h>

#include <vector>

#include "plumb/summary.hpp"

TEST(Summary, KnownSeries) {
    plumb::Summary s;
    for (double x : {2.0, 4.0, 4.0, 4.0, 5.0, 5.0, 7.0, 9.0}) s.add(x);
    EXPECT_EQ(s.count(), 8u);
    EXPECT_DOUBLE_EQ(s.min(), 2.0);
    EXPECT_DOUBLE_EQ(s.max(), 9.0);
    EXPECT_DOUBLE_EQ(s.mean(), 5.0);
    EXPECT_DOUBLE_EQ(s.variance(), 32.0 / 7.0);
}

TEST(Summary, OneValue) {
    plumb::Summary s;
    s.add(3.5);
    EXPECT_EQ(s.count(), 1u);
    EXPECT_DOUBLE_EQ(s.min(), 3.5);
    EXPECT_DOUBLE_EQ(s.max(), 3.5);
    EXPECT_DOUBLE_EQ(s.mean(), 3.5);
}

TEST(Summary, LargeOffsetKeepsVariance) {
    plumb::Summary s;
    for (double x : {1e9 + 1, 1e9 + 2, 1e9 + 3}) s.add(x);
    EXPECT_DOUBLE_EQ(s.variance(), 1.0);
}

TEST(SummaryDeathTest, VarianceOfOneValueStops) {
    plumb::Summary s;
    s.add(1.0);
    EXPECT_DEATH((void)s.variance(), "count_ > 1");
}

TEST(SummaryDeathTest, MeanOfNothingStops) {
    plumb::Summary s;
    EXPECT_DEATH((void)s.mean(), "count_ > 0");
}

TEST(Percentile, NearestRank) {
    std::vector<double> v;
    for (int i = 1; i <= 100; ++i) v.push_back(i);
    EXPECT_EQ(plumb::percentile(v, 0.05), 5.0);
    EXPECT_EQ(plumb::percentile(v, 0.50), 50.0);
    EXPECT_EQ(plumb::percentile(v, 0.95), 95.0);
    EXPECT_EQ(plumb::percentile(v, 1.0), 100.0);
    EXPECT_EQ(plumb::percentile(v, 0.0), 1.0);
}

TEST(Percentile, OneValue) {
    std::vector<double> v{7.0};
    EXPECT_EQ(plumb::percentile(v, 0.5), 7.0);
}

TEST(PercentileDeathTest, NothingStops) {
    std::vector<double> v;
    EXPECT_DEATH((void)plumb::percentile(v, 0.5), "empty");
}
