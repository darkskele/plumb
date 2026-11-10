#pragma once

#include <unistd.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <print>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace plumb {

/// @brief One case's figures on the console. Per op figures and the stamp cost in nanoseconds.
struct Row {
    std::string_view name;
    std::size_t      n;
    std::size_t      count;
    double           min;
    double           p5;
    double           p50;
    double           p95;
    double           max;
    double           stamp_mean;
    double           stamp_sd;
};

namespace detail {

/// @brief `v` in digits, grouped in threes by commas.
inline std::string thousands(std::uint64_t v) {
    auto        digits = std::to_string(v);
    std::string out;
    for (std::size_t i = 0; i < digits.size(); ++i) {
        if (i > 0 && (digits.size() - i) % 3 == 0) out += ',';
        out += digits[i];
    }
    return out;
}

/// @brief Nanoseconds to about three significant figures, whole ones grouped past a thousand.
inline std::string ns_text(double ns) {
    if (ns < 10) return std::format("{:.2f}", ns);
    if (ns < 1000) return std::format("{:.1f}", ns);
    return thousands(static_cast<std::uint64_t>(std::llround(ns)));
}

/// @brief Whether `f` is a terminal that takes colour.
inline bool wants_colour(std::FILE* f) {
    const char* no = std::getenv("NO_COLOR");
    return isatty(fileno(f)) && (no == nullptr || *no == '\0');
}

inline constexpr std::string_view bold  = "\x1b[1m";
inline constexpr std::string_view dim   = "\x1b[2m";
inline constexpr std::string_view cyan  = "\x1b[36m";
inline constexpr std::string_view plain = "\x1b[0m";

/// @brief `text` in `style` when `colour` is on.
inline std::string paint(std::string_view text, std::string_view style, bool colour) {
    if (!colour || style.empty()) return std::string(text);
    return std::format("{}{}{}", style, text, plain);
}

}  // namespace detail

/// @brief Prints `rows` as a table, one case per row, columns grouped under captions.
inline void print_table(std::FILE* out, std::span<const Row> rows, bool colour) {
    constexpr std::size_t                           columns = 10;
    constexpr std::array<std::string_view, columns> headers = {
        "case", "n", "batches", "min", "p5", "p50", "p95", "max", "mean", "sd"};
    constexpr std::array<std::string_view, columns> styles = {
        detail::cyan, {}, {}, {}, {}, detail::bold, {}, {}, detail::dim, detail::dim};
    constexpr std::array<std::size_t, 4>      group_start = {0, 3, 8, columns};
    constexpr std::array<std::string_view, 3> captions    = {"", "ns per op", "stamp ns"};
    constexpr std::size_t                     groups      = captions.size();

    std::vector<std::array<std::string, columns>> cells;
    for (const Row& r : rows)
        cells.push_back({std::string(r.name), detail::thousands(r.n), detail::thousands(r.count),
                         detail::ns_text(r.min), detail::ns_text(r.p5), detail::ns_text(r.p50),
                         detail::ns_text(r.p95), detail::ns_text(r.max),
                         detail::ns_text(r.stamp_mean), detail::ns_text(r.stamp_sd)});

    std::array<std::size_t, columns> widths{};
    for (std::size_t c = 0; c < columns; ++c) {
        widths[c] = headers[c].size();
        for (auto& row : cells) widths[c] = std::max(widths[c], row[c].size());
    }
    auto group_width = [&](std::size_t g) {
        std::size_t w = 2 * (group_start[g + 1] - group_start[g] - 1);
        for (std::size_t c = group_start[g]; c < group_start[g + 1]; ++c) w += widths[c];
        return w;
    };
    // A caption wider than its columns widens the group's first column.
    for (std::size_t g = 0; g < groups; ++g)
        if (captions[g].size() > group_width(g))
            widths[group_start[g]] += captions[g].size() - group_width(g);

    auto paint = [&](std::string_view text, std::string_view style) {
        return detail::paint(text, style, colour);
    };

    // Each column's text from `piece(c)`, groups split by a bar.
    auto line = [&](auto piece) {
        std::string s;
        for (std::size_t g = 0; g < groups; ++g) {
            if (g > 0) s += paint(" │ ", detail::dim);
            for (std::size_t c = group_start[g]; c < group_start[g + 1]; ++c) {
                if (c > group_start[g]) s += "  ";
                s += piece(c);
            }
        }
        return s;
    };
    auto cell = [&](std::size_t c, std::string_view text, std::string_view style) {
        std::string pad(widths[c] - text.size(), ' ');
        return c == 0 ? paint(text, style) + pad : pad + paint(text, style);
    };

    std::string caption;
    for (std::size_t g = 0; g < groups; ++g) {
        if (g > 0) caption += "   ";
        std::size_t left = (group_width(g) - captions[g].size()) / 2;
        caption += std::string(left, ' ') + std::string(captions[g]) +
                   std::string(group_width(g) - left - captions[g].size(), ' ');
    }
    caption.erase(caption.find_last_not_of(' ') + 1);
    std::println(out, "{}", paint(caption, detail::dim));

    std::println(out, "{}", line([&](std::size_t c) { return cell(c, headers[c], detail::bold); }));

    std::string rule;
    for (std::size_t g = 0; g < groups; ++g) {
        if (g > 0) rule += "─┼─";
        for (std::size_t i = 0; i < group_width(g); ++i) rule += "─";
    }
    std::println(out, "{}", paint(rule, detail::dim));

    for (auto& row : cells)
        std::println(out, "{}", line([&](std::size_t c) { return cell(c, row[c], styles[c]); }));
}

}  // namespace plumb
