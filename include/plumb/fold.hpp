#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

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

/// @brief One repetition once its samples are gone. Exact figures, a histogram of per op
/// picoseconds, and the p50 per op in nanoseconds of each drift window, in time order.
struct Repetition {
    std::size_t         round;
    RawHeader           header;
    Figures             figures;
    Histogram           histogram;
    std::vector<double> windows;

    bool operator==(const Repetition&) const = default;
};

namespace detail {

inline constexpr std::size_t drift_windows = 10;

}  // namespace detail

/// @brief Folds repetition `round` from its raw file's contents.
inline Repetition fold(const Raw& raw, std::size_t round) pre(!raw.ns.empty())
    pre(raw.header.n > 0) {
    auto                n = static_cast<double>(raw.header.n);
    std::vector<double> per_op;
    per_op.reserve(raw.ns.size());
    for (double ns : raw.ns) per_op.push_back(ns / n);

    Repetition rep{
        .round = round, .header = raw.header, .figures = {}, .histogram = {}, .windows = {}};
    for (double ns : per_op) rep.histogram.record(std::llround(ns * 1000));

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
