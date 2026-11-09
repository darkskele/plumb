#pragma once

#include <algorithm>
#include <cstdio>
#include <expected>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "plumb/registry.hpp"

namespace plumb {

/// @brief What the command line asks the binary to do.
enum class Command { list, check };

/// @brief Reads the command line, without the program name.
inline std::expected<Command, std::string> parse(std::span<const std::string_view> args) {
    if (args.empty()) return std::unexpected("no command");
    if (args.size() > 1) return std::unexpected("one command at a time");
    if (args[0] == "--list") return Command::list;
    if (args[0] == "--check") return Command::check;
    return std::unexpected("unknown argument " + std::string(args[0]));
}

/// @brief Runs the command line against a registry. Returns the exit code.
inline int run(std::span<const std::string_view> args, const Registry& r, std::FILE* out,
               std::FILE* err) {
    auto command = parse(args);
    if (!command) {
        std::println(err, "plumb: {}", command.error());
        std::println(err, "usage: --list | --check");
        return 2;
    }

    auto dups = r.duplicates();
    for (std::string_view name : dups) std::println(err, "plumb: duplicate case name {}", name);
    if (!dups.empty()) return 1;

    if (*command == Command::list) {
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
