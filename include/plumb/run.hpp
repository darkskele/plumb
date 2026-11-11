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
#include "plumb/plan.hpp"
#include "plumb/raw.hpp"
#include "plumb/registry.hpp"
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

/// @brief A case's row from its repetitions' raw files, every batch pooled.
inline Row row_of(std::string_view name, Plan plan, std::span<const Raw> raws) pre(!raws.empty()) {
    std::vector<double> per_op;
    double              stamp_mean = 0, stamp_variance = 0;
    for (const Raw& raw : raws) {
        for (double ns : raw.ns) per_op.push_back(ns / static_cast<double>(plan.n));
        stamp_mean += raw.header.stamp_mean;
        stamp_variance += raw.header.stamp_variance;
    }
    std::ranges::sort(per_op);
    auto reps = static_cast<double>(raws.size());
    return Row{.name       = name,
               .n          = plan.n,
               .count      = per_op.size(),
               .min        = per_op.front(),
               .p5         = percentile(per_op, 0.05),
               .p50        = percentile(per_op, 0.50),
               .p95        = percentile(per_op, 0.95),
               .max        = per_op.back(),
               .stamp_mean = stamp_mean / reps,
               .stamp_sd   = std::sqrt(stamp_variance / reps)};
}

/// @brief Sizes and runs a case, writes its raw file to `path` and returns its row.
inline std::expected<Row, std::string> run_case(const Entry& e, const TscClock& clock,
                                                bool invariant, const std::filesystem::path& path) {
    auto plan = size(e, clock).plan;
    auto raw  = run_repetition(e, clock, invariant, plan, path);
    if (!raw) return std::unexpected(raw.error());
    return row_of(e.name, plan, std::span(&*raw, 1));
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

/// @brief Runs `cases` in this process, one raw file each in `dir`, then prints their table.
/// Returns the exit code.
inline int run_in_process(std::span<const Entry* const> cases, const std::filesystem::path& dir,
                          std::FILE* out, std::FILE* err) {
    if (!detail::make_dir(dir, err)) return 1;
    bool invariant = detail::check_invariant(err);
    auto clock     = TscClock::calibrate(detail::clock_window);

    std::vector<Row> rows;
    int              code = 0;
    for (const Entry* e : cases) {
        auto row = detail::run_case(*e, clock, invariant, dir / (std::string(e->name) + ".raw"));
        if (row)
            rows.push_back(*row);
        else {
            std::println(err, "plumb: {}", row.error());
            code = 1;
        }
    }
    bool colour = detail::wants_colour(out);
    print_table(out, rows, colour);
    std::println(out, "{}",
                 detail::paint(std::format("raw files in {}", dir.string()), detail::dim, colour));
    return code;
}

}  // namespace plumb
