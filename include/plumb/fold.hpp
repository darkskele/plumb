#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "plumb/counters.hpp"
#include "plumb/histogram.hpp"
#include "plumb/raw.hpp"
#include "plumb/summary.hpp"

namespace plumb {

/// @brief Per op figures in nanoseconds.
struct Figures {
    double min;
    double p50;
    double p90;
    double p99;
    double max;

    bool operator==(const Figures&) const = default;
};

/// @brief A histogram bucket's batches that were counted throughout, and their per op counts
/// added up.
struct BucketCounts {
    std::uint64_t  counted;
    CounterReading sum;

    bool operator==(const BucketCounts&) const = default;
};

/// @brief One repetition once its samples are gone. Exact figures, a histogram of per op
/// picoseconds, the p50 per op in nanoseconds of each drift window in time order, and the counts
/// of each filled bucket in bucket order, none when the raw file had none.
struct Repetition {
    std::size_t               round;
    RawHeader                 header;
    Figures                   figures;
    Histogram                 histogram;
    std::vector<double>       windows;
    std::vector<BucketCounts> counts = {};

    bool operator==(const Repetition&) const = default;
};

namespace detail {

inline constexpr std::size_t drift_windows = 10;

}  // namespace detail

/// @brief Folds repetition `round` from its raw file's contents.
inline Repetition fold(const Raw& raw, std::size_t round) pre(!raw.ns.empty()) pre(raw.header.n > 0)
    pre(raw.counts.empty() || raw.counts.size() == raw.ns.size()) {
    auto                n = static_cast<double>(raw.header.n);
    std::vector<double> per_op;
    per_op.reserve(raw.ns.size());
    for (double ns : raw.ns) per_op.push_back(ns / n);

    Repetition rep{
        .round = round, .header = raw.header, .figures = {}, .histogram = {}, .windows = {}};
    for (double ns : per_op) rep.histogram.record(std::llround(ns * 1000));

    if (!raw.counts.empty()) {
        std::vector<BucketCounts> by_bucket(Histogram::buckets, BucketCounts{0, {0, 0, 0}});
        for (std::size_t i = 0; i < per_op.size(); ++i) {
            auto        ps = std::llround(per_op[i] * 1000);
            const auto& c  = raw.counts[i];
            if (ps < 0 || std::isnan(c.cycles)) continue;
            auto& b = by_bucket[Histogram::index(ps)];
            ++b.counted;
            b.sum.cycles += c.cycles / n;
            b.sum.ref_cycles += c.ref_cycles / n;
            b.sum.instructions += c.instructions / n;
        }
        for (auto [i, count] : rep.histogram.filled()) rep.counts.push_back(by_bucket[i]);
    }

    // Consecutive slices of the batches, as even as the count allows.
    std::size_t windows = std::min(detail::drift_windows, per_op.size());
    for (std::size_t w = 0; w < windows; ++w) {
        std::vector<double> slice(per_op.begin() + w * per_op.size() / windows,
                                  per_op.begin() + (w + 1) * per_op.size() / windows);
        std::ranges::sort(slice);
        rep.windows.push_back(percentile(slice, 0.5));
    }

    std::ranges::sort(per_op);
    rep.figures = {.min = per_op.front(),
                   .p50 = percentile(per_op, 0.50),
                   .p90 = percentile(per_op, 0.90),
                   .p99 = percentile(per_op, 0.99),
                   .max = per_op.back()};
    return rep;
}

}  // namespace plumb
