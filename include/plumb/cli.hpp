#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <expected>
#include <filesystem>
#include <format>
#include <optional>
#include <print>
#include <random>
#include <regex>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "plumb/parent.hpp"
#include "plumb/process.hpp"
#include "plumb/registry.hpp"
#include "plumb/run.hpp"

namespace plumb {

/// @brief What the command line asks the binary to do.
struct Command {
    enum class Kind { list, check, run, child, calibrate };

    Kind                         kind;
    std::string_view             name       = {};
    bool                         in_process = false;
    bool                         keep_raw   = false;
    std::filesystem::path        out        = {};
    std::size_t                  n          = 0;
    std::size_t                  count      = 0;
    double                       ghz        = 0;
    std::optional<std::uint64_t> seed       = {};

    bool operator==(const Command&) const = default;
};

/// @brief Reads the command line, without the program name. With no command, every case runs.
inline std::expected<Command, std::string> parse(std::span<const std::string_view> args) {
    Command c{.kind = Command::Kind::run};
    bool    chosen    = false;
    bool    run_named = false;
    auto    value     = [&](std::size_t i) {
        return i + 1 < args.size() && !args[i + 1].starts_with("--");
    };
    auto choose = [&](Command::Kind k) {
        if (chosen) return false;
        c.kind        = k;
        return chosen = true;
    };

    for (std::size_t i = 0; i < args.size(); ++i) {
        std::string_view a = args[i];
        if (a == "--list" || a == "--check") {
            if (!choose(a == "--list" ? Command::Kind::list : Command::Kind::check))
                return std::unexpected("one command at a time");
        } else if (a == "--child" || a == "--calibrate") {
            if (!value(i)) return std::unexpected(std::string(a) + " needs a case name");
            if (!choose(a == "--child" ? Command::Kind::child : Command::Kind::calibrate))
                return std::unexpected("one command at a time");
            c.name = args[++i];
        } else if (a == "--run") {
            if (!value(i)) return std::unexpected("--run needs a pattern");
            if (run_named) return std::unexpected("--run given twice");
            c.name    = args[++i];
            run_named = true;
            try {
                std::regex{std::string(c.name)};
            } catch (const std::regex_error&) {
                return std::unexpected(std::format("--run {} is not a regex", c.name));
            }
        } else if (a == "--in-process") {
            c.in_process = true;
        } else if (a == "--keep-raw") {
            c.keep_raw = true;
        } else if (a == "--out") {
            if (!value(i)) return std::unexpected("--out needs a path");
            c.out = args[++i];
        } else if (a == "--n" || a == "--count") {
            std::size_t v =
                i + 1 < args.size() ? detail::number<std::size_t>(args[++i]).value_or(0) : 0;
            if (v == 0) return std::unexpected(std::string(a) + " needs a whole number above zero");
            (a == "--n" ? c.n : c.count) = v;
        } else if (a == "--ghz") {
            auto v = i + 1 < args.size() ? detail::number<double>(args[++i]) : std::nullopt;
            if (!v || !std::isfinite(*v) || *v <= 0)
                return std::unexpected("--ghz needs a rate above zero");
            c.ghz = *v;
        } else if (a == "--seed") {
            c.seed = i + 1 < args.size() ? detail::number<std::uint64_t>(args[++i]) : std::nullopt;
            if (!c.seed) return std::unexpected("--seed needs a whole number");
        } else {
            return std::unexpected("unknown argument " + std::string(a));
        }
    }

    using enum Command::Kind;
    if (c.kind != run && (run_named || c.in_process || c.keep_raw))
        return std::unexpected("--run, --in-process and --keep-raw go only with a run");
    if ((c.kind == list || c.kind == check) && !c.out.empty())
        return std::unexpected("--out goes only with a run");
    if (c.kind != child && (c.n != 0 || c.count != 0))
        return std::unexpected("--n and --count go only with --child");
    if (c.kind != child && c.kind != calibrate && c.ghz != 0)
        return std::unexpected("--ghz goes only with --child or --calibrate");
    if ((c.kind != run || c.in_process) && c.seed)
        return std::unexpected("--seed goes only with a run of children");
    if (c.kind == child && (c.n == 0 || c.count == 0 || c.ghz == 0 || c.out.empty()))
        return std::unexpected("--child needs --n, --count, --ghz and --out");
    if (c.kind == calibrate && (c.ghz == 0 || c.out.empty()))
        return std::unexpected("--calibrate needs --ghz and --out");
    return c;
}

/// @brief Runs the command line against a registry. Returns the exit code.
inline int run(std::span<const std::string_view> args, const Registry& r, std::FILE* out,
               std::FILE* err) {
    auto command = parse(args);
    if (!command) {
        std::println(err, "plumb: {}", command.error());
        std::println(
            err,
            "usage: [--run PATTERN] [--in-process] [--keep-raw] [--seed S] [--out DIR] | --list | "
            "--check");
        return 2;
    }

    auto dups = r.duplicates();
    for (std::string_view name : dups) std::println(err, "plumb: duplicate case name {}", name);
    if (!dups.empty()) return 1;

    if (command->kind == Command::Kind::child || command->kind == Command::Kind::calibrate) {
        auto entries = r.entries();
        auto it      = std::ranges::find(entries, command->name, &Entry::name);
        if (it == entries.end()) {
            std::println(err, "plumb: no case named {}", command->name);
            return 1;
        }
        if (command->kind == Command::Kind::calibrate)
            return run_calibrate(*it, command->ghz, command->out, err);
        return run_child(*it, {.n = command->n, .count = command->count}, command->ghz,
                         command->out, err);
    }

    if (command->kind == Command::Kind::run) {
        std::regex pattern{command->name.empty() ? ".*" : std::string(command->name)};
        std::vector<const Entry*> cases;
        for (const Entry& e : r.entries())
            if (std::regex_match(e.name.begin(), e.name.end(), pattern)) cases.push_back(&e);
        if (cases.empty()) {
            if (command->name.empty())
                std::println(err, "plumb: no cases registered");
            else
                std::println(err, "plumb: no case matches {}", command->name);
            return 1;
        }
        std::ranges::sort(cases, {}, &Entry::name);
        auto dir = command->out.empty() ? detail::default_out(std::chrono::system_clock::now())
                                        : command->out;
        if (command->in_process) return run_in_process(cases, dir, command->keep_raw, out, err);
        std::random_device random;
        auto seed = command->seed.value_or((std::uint64_t{random()} << 32) | random());
        return run_parent(cases, detail::self(), dir, seed, detail::default_rounds,
                          command->keep_raw, out, err);
    }

    if (command->kind == Command::Kind::list) {
        std::vector<std::string_view> names;
        for (const Entry& e : r.entries()) names.push_back(e.name);
        std::ranges::sort(names);
        for (std::string_view name : names) std::println(out, "{}", name);
    }
    return 0;
}

/// @brief The bench binary's main.
inline int main(int argc, char** argv) {
    std::vector<std::string_view> args(argv + 1, argv + argc);
    return run(args, registry(), stdout, stderr);
}

}  // namespace plumb
