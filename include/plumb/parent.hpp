#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <numeric>
#include <optional>
#include <print>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "plumb/abandon.hpp"
#include "plumb/fold.hpp"
#include "plumb/plan.hpp"
#include "plumb/process.hpp"
#include "plumb/raw.hpp"
#include "plumb/registry.hpp"
#include "plumb/run.hpp"
#include "plumb/table.hpp"

namespace plumb {

namespace detail {

inline constexpr std::size_t               default_rounds = 5;
inline constexpr std::size_t               max_voids      = 3;
inline constexpr std::chrono::milliseconds parent_clock_window{500};

/// @brief Each round's order of `cases` cases, shuffled from `seed`.
inline std::vector<std::vector<std::size_t>> round_order(std::size_t cases, std::size_t rounds,
                                                         std::uint64_t seed) {
    std::mt19937_64                       engine(seed);
    std::vector<std::vector<std::size_t>> order(rounds, std::vector<std::size_t>(cases));
    for (auto& round : order) {
        std::iota(round.begin(), round.end(), std::size_t{0});
        // Fisher-Yates, from the engine's raw output.
        for (std::size_t i = cases; i > 1; --i) std::swap(round[i - 1], round[engine() % i]);
    }
    return order;
}

inline std::filesystem::path plan_path(const std::filesystem::path& dir, std::string_view name) {
    return dir / std::format("{}.plan", name);
}

inline std::filesystem::path raw_path(const std::filesystem::path& dir, std::string_view name,
                                      std::size_t round) {
    return dir / std::format("{}.{}.raw", name, round);
}

/// @brief Arguments that size `name` in a child at `ghz`.
inline std::vector<std::string> calibrate_args(std::string_view name, double ghz,
                                               const std::filesystem::path& out) {
    return {"--calibrate",          std::string(name), "--ghz",
            std::format("{}", ghz), "--out",           out.string()};
}

/// @brief Arguments that run one repetition of `name` to `plan` in a child at `ghz`.
inline std::vector<std::string> child_args(std::string_view name, Plan plan, double ghz,
                                           const std::filesystem::path& out) {
    return {"--child", std::string(name),
            "--n",     std::to_string(plan.n),
            "--count", std::to_string(plan.count),
            "--ghz",   std::format("{}", ghz),
            "--out",   out.string()};
}

/// @brief How a child that failed ended, in words.
inline std::string describe(Exit e) {
    switch (e.kind) {
        case Exit::Kind::ok:
            return "exited 0";
        case Exit::Kind::nonzero:
            return std::format("exited {}", e.code);
        case Exit::Kind::signal: {
            const char* abbrev = sigabbrev_np(e.code);
            return abbrev ? std::format("killed by SIG{}", abbrev)
                          : std::format("killed by signal {}", e.code);
        }
        case Exit::Kind::not_started:
            return std::format("did not start: {}", std::strerror(e.code));
    }
    return {};
}

/// @brief Why a child's repetition is void, or nothing when it ended well.
inline std::optional<std::string> void_reason(Exit e) {
    if (e.kind == Exit::Kind::ok) return {};
    if (e.kind == Exit::Kind::nonzero && e.code == abandon_code) return "abandoned";
    return describe(e);
}

}  // namespace detail

/// @brief Runs `cases` as children of `exe`: one sizing child each, then `rounds` rounds of one
/// repetition each, in an order shuffled from `seed`. A void child is rerun, and a case void
/// `max_voids` times is dropped. Each repetition is folded as it ends, its raw file kept if
/// `keep_raw`. Prints their table. Returns the exit code.
inline int run_parent(std::span<const Entry* const> cases, const std::filesystem::path& exe,
                      const std::filesystem::path& dir, std::uint64_t seed, std::size_t rounds,
                      bool keep_raw, std::FILE* out, std::FILE* err) {
    auto start = std::chrono::system_clock::now();
    if (!detail::make_dir(dir, err)) return 1;
    std::ofstream(dir / "seed") << seed << '\n';
    bool   invariant = detail::check_invariant(err);
    double ghz       = TscClock::calibrate(detail::parent_clock_window).ghz();
    int    code      = 0;

    struct Planned {
        const Entry*            entry;
        Calibration             calibration = {};
        std::size_t             voids       = 0;
        bool                    dropped     = false;
        std::vector<Repetition> reps        = {};
    };

    std::vector<Planned> planned;
    for (const Entry* e : cases) planned.push_back({.entry = e});

    // Runs a child until it ends well and `check` accepts its file, or its case is dropped.
    auto attempt = [&](Planned& p, std::string_view what, const std::vector<std::string>& args,
                       const std::filesystem::path& file, auto check) {
        while (!p.dropped) {
            auto why = detail::void_reason(detail::spawn_wait(exe, args));
            if (!why) why = check(file);
            if (!why) return true;
            std::error_code ignored;
            std::filesystem::remove(file, ignored);
            std::println(err, "plumb: {} {} void, {}", p.entry->name, what, *why);
            if (++p.voids == detail::max_voids) {
                std::println(err, "plumb: {} dropped after {} voids", p.entry->name, p.voids);
                p.dropped = true;
                code      = 1;
            }
        }
        return false;
    };

    for (Planned& p : planned) {
        auto path = detail::plan_path(dir, p.entry->name);
        (void)attempt(p, "calibration", detail::calibrate_args(p.entry->name, ghz, path), path,
                      [&](const std::filesystem::path& f) -> std::optional<std::string> {
                          auto c = read_plan(f);
                          if (!c) return c.error();
                          p.calibration = *c;
                          return {};
                      });
    }

    auto order = detail::round_order(planned.size(), rounds, seed);
    for (std::size_t r = 0; r < order.size(); ++r) {
        for (std::size_t i : order[r]) {
            auto& p    = planned[i];
            auto  path = detail::raw_path(dir, p.entry->name, r);
            (void)attempt(p, std::format("repetition {}", r),
                          detail::child_args(p.entry->name, p.calibration.plan, ghz, path), path,
                          [&](const std::filesystem::path& f) -> std::optional<std::string> {
                              auto rep = detail::fold_file(f, r, keep_raw);
                              if (!rep) return rep.error();
                              p.reps.push_back(std::move(*rep));
                              return {};
                          });
        }
    }

    Results          results{.seed          = seed,
                             .ghz           = ghz,
                             .tsc_invariant = invariant,
                             .start_ns      = detail::epoch_ns(start),
                             .rounds        = rounds,
                             .cases         = {}};
    std::vector<Row> rows;
    for (Planned& p : planned) {
        if (!p.dropped && !p.reps.empty())
            rows.push_back(detail::row_of(p.entry->name, p.calibration.plan, p.reps));
        results.cases.push_back({.name        = std::string(p.entry->name),
                                 .calibration = p.calibration,
                                 .voids       = p.voids,
                                 .dropped     = p.dropped,
                                 .reps        = std::move(p.reps)});
    }
    if (!detail::save_results(dir, results, err)) code = 1;

    bool colour = detail::wants_colour(out);
    print_table(out, rows, colour);
    std::println(
        out, "{}",
        detail::paint(std::format("run in {}, seed {}", dir.string(), seed), detail::dim, colour));
    return code;
}

}  // namespace plumb
