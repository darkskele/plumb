#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <format>
#include <fstream>
#include <numeric>
#include <print>
#include <random>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "plumb/plan.hpp"
#include "plumb/process.hpp"
#include "plumb/raw.hpp"
#include "plumb/registry.hpp"
#include "plumb/run.hpp"
#include "plumb/table.hpp"

namespace plumb {

namespace detail {

inline constexpr std::size_t               default_rounds = 5;
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

}  // namespace detail

/// @brief Runs `cases` as children of `exe`: one sizing child each, then `rounds` rounds of one
/// repetition each, in an order shuffled from `seed`. Prints their table. Returns the exit code.
inline int run_parent(std::span<const Entry* const> cases, const std::filesystem::path& exe,
                      const std::filesystem::path& dir, std::uint64_t seed, std::size_t rounds,
                      std::FILE* out, std::FILE* err) {
    if (!detail::make_dir(dir, err)) return 1;
    std::ofstream(dir / "seed") << seed << '\n';
    detail::check_invariant(err);
    double ghz  = TscClock::calibrate(detail::parent_clock_window).ghz();
    int    code = 0;

    struct Planned {
        const Entry*             entry;
        Plan                     plan;
        std::vector<std::size_t> rounds_done;
    };

    std::vector<Planned> planned;
    for (const Entry* e : cases) {
        auto path = detail::plan_path(dir, e->name);
        auto exit = detail::spawn_wait(exe, detail::calibrate_args(e->name, ghz, path));
        if (exit.kind != detail::Exit::Kind::ok) {
            std::println(err, "plumb: {} calibration {}", e->name, detail::describe(exit));
            code = 1;
            continue;
        }
        auto c = read_plan(path);
        if (!c) {
            std::println(err, "plumb: {}", c.error());
            code = 1;
            continue;
        }
        planned.push_back({.entry = e, .plan = c->plan, .rounds_done = {}});
    }

    auto order = detail::round_order(planned.size(), rounds, seed);
    for (std::size_t r = 0; r < order.size(); ++r) {
        for (std::size_t i : order[r]) {
            auto& p    = planned[i];
            auto  name = p.entry->name;
            auto  exit = detail::spawn_wait(
                exe, detail::child_args(name, p.plan, ghz, detail::raw_path(dir, name, r)));
            if (exit.kind != detail::Exit::Kind::ok) {
                std::println(err, "plumb: {} repetition {} {}", name, r, detail::describe(exit));
                code = 1;
                continue;
            }
            p.rounds_done.push_back(r);
        }
    }

    // One case's raw files in memory at a time.
    std::vector<Row> rows;
    for (const Planned& p : planned) {
        std::vector<Raw> raws;
        for (std::size_t r : p.rounds_done) {
            auto raw = read_raw(detail::raw_path(dir, p.entry->name, r));
            if (raw)
                raws.push_back(std::move(*raw));
            else {
                std::println(err, "plumb: {}", raw.error());
                code = 1;
            }
        }
        if (!raws.empty()) rows.push_back(detail::row_of(p.entry->name, p.plan, raws));
    }

    bool colour = detail::wants_colour(out);
    print_table(out, rows, colour);
    std::println(out, "{}",
                 detail::paint(std::format("raw files in {}, seed {}", dir.string(), seed),
                               detail::dim, colour));
    return code;
}

}  // namespace plumb
