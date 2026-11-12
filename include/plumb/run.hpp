#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <expected>
#include <filesystem>
#include <format>
#include <print>
#include <ranges>
#include <span>
#include <string>
#include <vector>

#include "plumb/batch.hpp"
#include "plumb/clock.hpp"
#include "plumb/fold.hpp"
#include "plumb/histogram.hpp"
#include "plumb/plan.hpp"
#include "plumb/raw.hpp"
#include "plumb/registry.hpp"
#include "plumb/results.hpp"
#include "plumb/summary.hpp"
#include "plumb/table.hpp"

namespace plumb {

namespace detail {

inline constexpr double      batch_target_ns = 5'000;
inline constexpr double      run_target_ns   = 1e9;
inline constexpr std::size_t min_count       = 10;
inline constexpr std::size_t max_count       = 1'000'000;

/// @brief Batches that fill the run's target time, at `batch_ns` each.
inline std::size_t count_for(double batch_ns) pre(batch_ns > 0) {
    return std::clamp(static_cast<std::size_t>(run_target_ns / batch_ns), min_count, max_count);
}

/// @brief The fastest of a few batches of `n` ops, in nanoseconds.
inline double fastest_batch_ns(const Entry& e, std::size_t n, const TscClock& clock) {
    auto   b       = e.batches(n, 5, {.time = {}, .batches = 1});
    double fastest = std::ranges::min(b.figures);
    return b.measured ? fastest : fastest / clock.ghz();
}

}  // namespace detail

/// @brief The plan for a case. The batch doubles until it takes the batch target or reaches the
/// case's `max_batch`, then the count fills the run's target time.
inline Calibration size(const Entry& e, const TscClock& clock) {
    std::size_t n  = 1;
    double      ns = detail::fastest_batch_ns(e, n, clock);
    while (ns < detail::batch_target_ns && n < e.max_batch) {
        n  = std::min(n * 2, e.max_batch);
        ns = detail::fastest_batch_ns(e, n, clock);
    }
    return {.plan = {.n = n, .count = detail::count_for(ns)}, .batch_ns = ns};
}

namespace detail {

/// @brief The run directory named for its start, in UTC.
inline std::filesystem::path default_out(std::chrono::system_clock::time_point start) {
    return std::filesystem::path("plumb_runs") /
           std::format("{:%Y%m%d_%H%M%S}", std::chrono::floor<std::chrono::seconds>(start));
}

inline constexpr std::chrono::milliseconds clock_window{100};

/// @brief Runs one repetition of a case to `plan` and writes its raw file to `path`.
inline std::expected<Raw, std::string> run_repetition(const Entry& e, const TscClock& clock,
                                                      bool invariant, Plan plan,
                                                      const std::filesystem::path& path) {
    auto raw = to_raw(e.batches(plan.n, plan.count), clock, invariant);
    if (auto written = write_raw(path, raw); !written) return std::unexpected(written.error());
    return raw;
}

/// @brief A case's row from its repetitions. Exact min and max, percentiles from their histograms
/// added together.
inline Row row_of(std::string_view name, Plan plan, std::span<const Repetition> reps)
    pre(!reps.empty()) {
    Histogram   all;
    std::size_t count = 0;
    double      min = reps.front().figures.min, max = reps.front().figures.max;
    double      stamp_mean = 0, stamp_variance = 0;
    for (const Repetition& rep : reps) {
        all += rep.histogram;
        count += rep.header.count;
        min = std::min(min, rep.figures.min);
        max = std::max(max, rep.figures.max);
        stamp_mean += rep.header.stamp_mean;
        stamp_variance += rep.header.stamp_variance;
    }
    auto ns = [&](double p) { return static_cast<double>(all.percentile(p)) / 1000; };
    auto n  = static_cast<double>(reps.size());
    return Row{.name       = name,
               .n          = plan.n,
               .count      = count,
               .min        = min,
               .p50        = ns(0.50),
               .p90        = ns(0.90),
               .p99        = ns(0.99),
               .max        = max,
               .stamp_mean = stamp_mean / n,
               .stamp_sd   = std::sqrt(stamp_variance / n)};
}

/// @brief Folds the raw file at `path`, then removes it unless `keep_raw`.
inline std::expected<Repetition, std::string> fold_file(const std::filesystem::path& path,
                                                        std::size_t round, bool keep_raw) {
    auto raw = read_raw(path);
    if (!raw) return std::unexpected(raw.error());
    if (!keep_raw) {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
    }
    return fold(*raw, round);
}

/// @brief Sizes and runs a case once, its raw file written to `path` and kept if `keep_raw`.
inline std::expected<CaseResult, std::string> run_case(const Entry& e, const TscClock& clock,
                                                       bool                         invariant,
                                                       const std::filesystem::path& path,
                                                       bool                         keep_raw) {
    auto calibration = size(e, clock);
    auto raw         = run_repetition(e, clock, invariant, calibration.plan, path);
    if (!raw) return std::unexpected(raw.error());
    if (!keep_raw) {
        std::error_code ignored;
        std::filesystem::remove(path, ignored);
    }
    return CaseResult{.name        = std::string(e.name),
                      .calibration = calibration,
                      .voids       = 0,
                      .dropped     = false,
                      .reps        = {fold(*raw, 0)}};
}

/// @brief Nanoseconds from the Unix epoch to `t`.
inline std::int64_t epoch_ns(std::chrono::system_clock::time_point t) {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(t.time_since_epoch()).count();
}

/// @brief Writes `r` to the run directory's results file, or says why not.
inline bool save_results(const std::filesystem::path& dir, const Results& r, std::FILE* err) {
    auto written = write_results(dir / "results.bin", r);
    if (!written) std::println(err, "plumb: {}", written.error());
    return written.has_value();
}

/// @brief Makes the run directory, or says why not.
inline bool make_dir(const std::filesystem::path& dir, std::FILE* err) {
    std::error_code made;
    std::filesystem::create_directories(dir, made);
    if (made) std::println(err, "plumb: cannot make {}: {}", dir.string(), made.message());
    return !made;
}

/// @brief Warns when the TSC is not invariant, and says whether it is.
inline bool check_invariant(std::FILE* err) {
    bool invariant = TscClock::invariant();
    if (!invariant)
        std::println(err, "plumb: warning: the TSC is not invariant, figures may drift");
    return invariant;
}

}  // namespace detail

/// @brief Sizes a case in this process at `ghz` and writes its plan to `path`. Prints only errors.
/// Returns the exit code.
inline int run_calibrate(const Entry& e, double ghz, const std::filesystem::path& path,
                         std::FILE* err) {
    auto written = write_plan(path, size(e, TscClock::at(ghz)));
    if (!written) {
        std::println(err, "plumb: {}", written.error());
        return 1;
    }
    return 0;
}

/// @brief One repetition of a case to `plan` at `ghz`, its raw file written to `path`.
/// Prints only errors. Returns the exit code.
inline int run_child(const Entry& e, Plan plan, double ghz, const std::filesystem::path& path,
                     std::FILE* err) {
    auto raw = detail::run_repetition(e, TscClock::at(ghz), TscClock::invariant(), plan, path);
    if (!raw) {
        std::println(err, "plumb: {}", raw.error());
        return 1;
    }
    return 0;
}

/// @brief Runs `cases` in this process, then prints their table. A raw file each in `dir`, kept
/// if `keep_raw`. Returns the exit code.
inline int run_in_process(std::span<const Entry* const> cases, const std::filesystem::path& dir,
                          bool keep_raw, std::FILE* out, std::FILE* err) {
    auto start = std::chrono::system_clock::now();
    if (!detail::make_dir(dir, err)) return 1;
    bool invariant = detail::check_invariant(err);
    auto clock     = TscClock::calibrate(detail::clock_window);

    Results          results{.seed          = 0,
                             .ghz           = clock.ghz(),
                             .tsc_invariant = invariant,
                             .start_ns      = detail::epoch_ns(start),
                             .rounds        = 1,
                             .cases         = {}};
    std::vector<Row> rows;
    int              code = 0;
    for (const Entry* e : cases) {
        auto c =
            detail::run_case(*e, clock, invariant, dir / (std::string(e->name) + ".raw"), keep_raw);
        if (!c) {
            std::println(err, "plumb: {}", c.error());
            code = 1;
            continue;
        }
        rows.push_back(detail::row_of(e->name, c->calibration.plan, c->reps));
        results.cases.push_back(std::move(*c));
    }
    if (!detail::save_results(dir, results, err)) code = 1;
    bool colour = detail::wants_colour(out);
    print_table(out, rows, colour);
    std::println(out, "{}",
                 detail::paint(std::format("run in {}", dir.string()), detail::dim, colour));
    return code;
}

}  // namespace plumb
