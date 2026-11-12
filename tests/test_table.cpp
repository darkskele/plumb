#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <ranges>
#include <string>
#include <string_view>
#include <vector>

#include "plumb/table.hpp"

namespace {

std::string print(const std::vector<plumb::Row>& rows, bool colour) {
    char*       buf = nullptr;
    std::size_t len = 0;
    std::FILE*  out = open_memstream(&buf, &len);
    plumb::print_table(out, rows, colour);
    std::fclose(out);
    std::string s(buf, len);
    std::free(buf);
    return s;
}

std::string strip_colour(std::string_view s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\x1b')
            while (i < s.size() && s[i] != 'm') ++i;
        else
            out += s[i];
    }
    return out;
}

// Characters on screen, counting a multibyte character once.
std::size_t width(std::string_view s) {
    return std::ranges::count_if(s, [](char c) { return (c & 0xC0) != 0x80; });
}

std::vector<std::string> lines(const std::string& s) {
    std::vector<std::string> out;
    for (auto l : s | std::views::split('\n')) out.emplace_back(l.begin(), l.end());
    if (!out.empty() && out.back().empty()) out.pop_back();
    return out;
}

// Screen column of the first `mark` in `line`.
std::size_t column(std::string_view line, std::string_view mark) {
    return width(line.substr(0, line.find(mark)));
}

plumb::Row row(std::string_view name, std::size_t n, std::size_t count) {
    return {.name       = name,
            .n          = n,
            .count      = count,
            .min        = 0.62,
            .p50        = 0.634,
            .p90        = 0.635,
            .p99        = 0.641,
            .max        = 1.2,
            .stamp_mean = 17.84,
            .stamp_sd   = 0.41};
}

}  // namespace

TEST(Table, Numbers) {
    EXPECT_EQ(plumb::detail::thousands(0), "0");
    EXPECT_EQ(plumb::detail::thousands(999), "999");
    EXPECT_EQ(plumb::detail::thousands(1000), "1,000");
    EXPECT_EQ(plumb::detail::thousands(1234567), "1,234,567");
    EXPECT_EQ(plumb::detail::ns_text(0.634), "0.63");
    EXPECT_EQ(plumb::detail::ns_text(17.84), "17.8");
    EXPECT_EQ(plumb::detail::ns_text(12345.6), "12,346");
}

TEST(Table, OneRowUnderItsHeaders) {
    auto out = print({row("selftest_a", 64, 100000)}, false);
    auto ls  = lines(out);
    ASSERT_EQ(ls.size(), 4u) << out;
    EXPECT_NE(ls[0].find("ns per op"), std::string::npos) << out;
    EXPECT_NE(ls[0].find("stamp ns"), std::string::npos) << out;
    for (auto h : {"case", "n", "batches", "min", "p50", "p90", "p99", "max", "mean", "sd"})
        EXPECT_NE(ls[1].find(h), std::string::npos) << h;
    EXPECT_TRUE(ls[3].starts_with("selftest_a ")) << out;
    for (auto v : {" 64 ", " 100,000 ", " 0.62 ", " 0.63 ", " 0.64 ", " 1.20 ", " 17.8 ", " 0.41"})
        EXPECT_NE(ls[3].find(v), std::string::npos) << v << "\n" << out;
}

TEST(Table, ColumnsLineUp) {
    auto out = print({row("a", 1, 10), row("much_longer_name", 1024, 100000)}, false);
    auto ls  = lines(out);
    ASSERT_EQ(ls.size(), 5u) << out;
    for (auto& l : ls | std::views::drop(1)) EXPECT_EQ(width(l), width(ls[1])) << out;
    for (auto i : {1, 3, 4}) EXPECT_EQ(column(ls[i], "│"), column(ls[2], "┼")) << out;
    EXPECT_TRUE(ls[3].starts_with("a ")) << out;
    EXPECT_TRUE(ls[4].starts_with("much_longer_name ")) << out;
    // Numbers end on their header's last character.
    auto end = ls[1].find("batches") + 7;
    EXPECT_EQ(ls[3].find(" 10 ") + 3, end) << out;
    EXPECT_EQ(ls[4].find("100,000") + 7, end) << out;
}

TEST(Table, NoColourIsPlainText) {
    EXPECT_EQ(print({row("a", 1, 10)}, false).find('\x1b'), std::string::npos);
}

TEST(Table, ColourOnlyAddsEscapes) {
    auto plain  = print({row("a", 1, 10)}, false);
    auto colour = print({row("a", 1, 10)}, true);
    EXPECT_NE(colour.find("\x1b["), std::string::npos);
    EXPECT_EQ(strip_colour(colour), plain);
}
