#pragma once

#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "plumb/raw.hpp"

namespace plumb {

/// @brief How a repetition runs. Batches of `n` ops, `count` of them.
struct Plan {
    std::size_t n;
    std::size_t count;

    bool operator==(const Plan&) const = default;
};

/// @brief A case's plan, and the fastest batch of `n` ops sizing saw, in nanoseconds.
struct Calibration {
    Plan   plan;
    double batch_ns;

    bool operator==(const Calibration&) const = default;
};

inline constexpr int plan_version = 1;

/// @brief Writes `c` to `path` as text, replacing any file there.
inline std::expected<void, std::string> write_plan(const std::filesystem::path& path,
                                                   const Calibration&           c) {
    detail::File f{std::fopen(path.c_str(), "w")};
    if (!f) return std::unexpected(std::format("cannot open {} to write", path.string()));
    auto text = std::format("plumb plan {}\nn {}\ncount {}\nbatch_ns {}\n", plan_version, c.plan.n,
                            c.plan.count, c.batch_ns);
    bool ok   = std::fwrite(text.data(), 1, text.size(), f.get()) == text.size();
    if (!ok || std::fclose(f.release()) != 0)
        return std::unexpected(std::format("cannot write {}", path.string()));
    return {};
}

namespace detail {

// The whole of `s` as a number, or nothing.
template <class T>
std::optional<T> number(std::string_view s) {
    T v{};
    auto [end, err] = std::from_chars(s.data(), s.data() + s.size(), v);
    if (err != std::errc{} || end != s.data() + s.size()) return {};
    return v;
}

}  // namespace detail

/// @brief Reads the plan at `path`, refusing one that is damaged or of another version.
inline std::expected<Calibration, std::string> read_plan(const std::filesystem::path& path) {
    auto          where = path.string();
    std::ifstream f(path, std::ios::binary);
    if (!f) return std::unexpected(std::format("cannot open {}", where));
    std::string text((std::istreambuf_iterator<char>(f)), {});

    auto lines = text | std::views::split('\n') |
                 std::views::transform([](auto l) { return std::string_view(l); }) |
                 std::ranges::to<std::vector>();
    if (!lines.empty() && lines.back().empty()) lines.pop_back();

    constexpr std::string_view head = "plumb plan ";
    if (lines.empty() || !lines[0].starts_with(head))
        return std::unexpected(std::format("{}: not a plumb plan", where));
    auto version = detail::number<int>(lines[0].substr(head.size()));
    if (version != plan_version)
        return std::unexpected(std::format("{}: plan version {}, expected {}", where,
                                           lines[0].substr(head.size()), plan_version));

    // Line `i`'s value after `key`, or nothing.
    auto value = [&](std::size_t i, std::string_view key) -> std::optional<std::string_view> {
        if (i >= lines.size() || !lines[i].starts_with(key) || lines[i].size() <= key.size() ||
            lines[i][key.size()] != ' ')
            return {};
        return lines[i].substr(key.size() + 1);
    };
    auto bad = [&](std::string_view key) {
        return std::unexpected(std::format("{}: bad or missing {}", where, key));
    };

    auto n = value(1, "n").and_then(detail::number<std::size_t>);
    if (!n || *n == 0) return bad("n");
    auto count = value(2, "count").and_then(detail::number<std::size_t>);
    if (!count || *count == 0) return bad("count");
    auto batch_ns = value(3, "batch_ns").and_then(detail::number<double>);
    if (!batch_ns || !std::isfinite(*batch_ns) || *batch_ns <= 0) return bad("batch_ns");
    if (lines.size() > 4)
        return std::unexpected(std::format("{}: trailing text after the plan", where));

    return Calibration{.plan = {.n = *n, .count = *count}, .batch_ns = *batch_ns};
}

}  // namespace plumb
