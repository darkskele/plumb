#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include "plumb/plan.hpp"

namespace fs = std::filesystem;

namespace {

const plumb::Calibration sample{.plan = {.n = 1023, .count = 100000}, .batch_ns = 4812.123456789};

fs::path temp_file(const std::string& name) {
    return fs::path(testing::TempDir()) / ("plumb_" + name + ".plan");
}

fs::path with_text(const std::string& name, const std::string& text) {
    auto          path = temp_file(name);
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    f << text;
    return path;
}

void expect_refused(const fs::path& path, std::string_view why) {
    auto back = plumb::read_plan(path);
    ASSERT_FALSE(back);
    EXPECT_NE(back.error().find(why), std::string::npos) << back.error();
}

}  // namespace

TEST(Plan, RoundTripsExactly) {
    auto path = temp_file("round_trip");
    ASSERT_TRUE(plumb::write_plan(path, sample));
    auto back = plumb::read_plan(path);
    ASSERT_TRUE(back) << back.error();
    EXPECT_EQ(*back, sample);
}

TEST(Plan, IsTextAPersonCanRead) {
    auto path = temp_file("text");
    ASSERT_TRUE(plumb::write_plan(path, sample));
    std::ifstream f(path);
    std::string   text((std::istreambuf_iterator<char>(f)), {});
    EXPECT_EQ(text, "plumb plan 1\nn 1023\ncount 100000\nbatch_ns 4812.123456789\n");
}

TEST(Plan, MissingFile) { expect_refused(temp_file("missing"), "cannot open"); }

TEST(Plan, UnwritablePath) {
    auto done = plumb::write_plan(temp_file("no_dir") / "x.plan", sample);
    ASSERT_FALSE(done);
    EXPECT_NE(done.error().find("cannot open"), std::string::npos) << done.error();
}

TEST(Plan, NotAPlan) { expect_refused(with_text("not_plan", "hello\n"), "not a plumb plan"); }

TEST(Plan, WrongVersion) {
    expect_refused(with_text("version", "plumb plan 7\nn 1\ncount 1\nbatch_ns 1\n"),
                   "plan version 7, expected 1");
}

TEST(Plan, MissingLine) {
    expect_refused(with_text("short", "plumb plan 1\nn 1\ncount 1\n"), "batch_ns");
}

TEST(Plan, BadValue) {
    expect_refused(with_text("bad_n", "plumb plan 1\nn 1x\ncount 1\nbatch_ns 1\n"), "n");
    expect_refused(with_text("zero_count", "plumb plan 1\nn 1\ncount 0\nbatch_ns 1\n"), "count");
    expect_refused(with_text("bad_ns", "plumb plan 1\nn 1\ncount 1\nbatch_ns -2\n"), "batch_ns");
}

TEST(Plan, WrongOrder) {
    expect_refused(with_text("order", "plumb plan 1\ncount 1\nn 1\nbatch_ns 1\n"), "n");
}

TEST(Plan, TrailingText) {
    expect_refused(with_text("trailing", "plumb plan 1\nn 1\ncount 1\nbatch_ns 1\nmore\n"),
                   "trailing");
}
