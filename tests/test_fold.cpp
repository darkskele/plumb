#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "plumb/fold.hpp"

namespace {

plumb::Raw raw_of(std::size_t n, std::vector<double> ns) {
    plumb::Raw raw{};
    raw.header.n          = n;
    raw.header.count      = ns.size();
    raw.header.stamp_mean = 17.5;
    raw.ns                = std::move(ns);
    return raw;
}

// Batches of `n` ops whose per op figures are 1, 2, ... `count` ns.
plumb::Raw rising(std::size_t n, std::size_t count) {
    std::vector<double> ns;
    for (std::size_t i = 1; i <= count; ++i) ns.push_back(static_cast<double>(i * n));
    return raw_of(n, std::move(ns));
}

}  // namespace

TEST(Fold, FiguresAreExactPerOp) {
    auto rep = plumb::fold(rising(2, 100), 3);
    EXPECT_EQ(rep.round, 3u);
    EXPECT_EQ(rep.figures, (plumb::Figures{.min = 1, .p50 = 50, .p90 = 90, .p99 = 99, .max = 100}));
}

TEST(Fold, HistogramCountsPerOpPicoseconds) {
    auto             rep = plumb::fold(raw_of(4, {2.0, 4002.0, 6.5}), 0);
    plumb::Histogram expected;
    for (std::int64_t ps : {500, 1'000'500, 1625}) expected.record(ps);
    EXPECT_EQ(rep.histogram, expected);
}

TEST(Fold, WindowsAreTheMedianOfEachTenthInTimeOrder) {
    auto rep = plumb::fold(rising(1, 30), 0);
    EXPECT_EQ(rep.windows, (std::vector<double>{2, 5, 8, 11, 14, 17, 20, 23, 26, 29}));
}

TEST(Fold, FewerBatchesThanWindowsGiveAWindowEach) {
    auto rep = plumb::fold(raw_of(1, {3, 1, 2}), 0);
    EXPECT_EQ(rep.windows, (std::vector<double>{3, 1, 2}));
}

TEST(Fold, KeepsTheHeader) {
    auto raw = rising(2, 10);
    EXPECT_EQ(plumb::fold(raw, 0).header, raw.header);
}
