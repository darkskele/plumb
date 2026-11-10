#pragma once

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <expected>
#include <filesystem>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "plumb/registry.hpp"
#include "plumb/run.hpp"

namespace plumb {

/// @brief What the command line asks the binary to do.
struct Command {
    enum class Kind { list, check, run };

    Kind                  kind;
    std::string_view      name       = {};
    bool                  in_process = false;
    std::filesystem::path out        = {};

    bool operator==(const Command&) const = default;
};

/// @brief Reads the command line, without the program name. With no command, every case runs.
/// @todo Without `--in-process`, the parent runs each repetition in a child.
inline std::expected<Command, std::string> parse(std::span<const std::string_view> args) {
    Command c{.kind = Command::Kind::run};
    bool    chosen     = false;
    bool    run_option = false;
    auto    value      = [&](std::size_t i) {
        return i + 1 < args.size() && !args[i + 1].starts_with("--");
    };

    for (std::size_t i = 0; i < args.size(); ++i) {
        std::string_view a = args[i];
        if (a == "--list" || a == "--check") {
            if (chosen) return std::unexpected("one command at a time");
            c.kind = a == "--list" ? Command::Kind::list : Command::Kind::check;
            chosen = true;
        } else if (a == "--run") {
            if (!value(i)) return std::unexpected("--run needs a case name");
            if (!c.name.empty()) return std::unexpected("--run given twice");
            c.name     = args[++i];
            run_option = true;
        } else if (a == "--in-process") {
            c.in_process = run_option = true;
        } else if (a == "--out") {
            if (!value(i)) return std::unexpected("--out needs a path");
            c.out      = args[++i];
            run_option = true;
        } else {
            return std::unexpected("unknown argument " + std::string(a));
        }
    }

    if (c.kind != Command::Kind::run && run_option)
        return std::unexpected("--run, --in-process and --out go only with a run");
    return c;
}

/// @brief Runs the command line against a registry. Returns the exit code.
inline int run(std::span<const std::string_view> args, const Registry& r, std::FILE* out,
               std::FILE* err) {
    auto command = parse(args);
    if (!command) {
        std::println(err, "plumb: {}", command.error());
        std::println(err, "usage: [--run NAME] [--in-process] [--out DIR] | --list | --check");
        return 2;
    }

    auto dups = r.duplicates();
    for (std::string_view name : dups) std::println(err, "plumb: duplicate case name {}", name);
    if (!dups.empty()) return 1;

    if (command->kind == Command::Kind::run) {
        std::vector<const Entry*> cases;
        for (const Entry& e : r.entries())
            if (command->name.empty() || e.name == command->name) cases.push_back(&e);
        if (cases.empty()) {
            if (command->name.empty())
                std::println(err, "plumb: no cases registered");
            else
                std::println(err, "plumb: no case named {}", command->name);
            return 1;
        }
        std::ranges::sort(cases, {}, &Entry::name);
        auto dir = command->out.empty() ? detail::default_out(std::chrono::system_clock::now())
                                        : command->out;
        return run_in_process(cases, dir, out, err);
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
