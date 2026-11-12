#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "plumb/results.hpp"

namespace fs = std::filesystem;

namespace {

plumb::Repetition rep(std::size_t round, double first) {
    plumb::Raw raw{};
    raw.header.magic      = plumb::raw_magic;
    raw.header.version    = plumb::raw_version;
    raw.header.n          = 2;
    raw.header.ghz        = 2.4;
    raw.header.stamp_mean = 17.5;
    for (int i = 0; i < 30; ++i) raw.ns.push_back(first + i);
    raw.header.count = raw.ns.size();
    return plumb::fold(raw, round);
}

plumb::Results sample() {
    return {
        .seed          = 7,
        .ghz           = 2.4,
        .tsc_invariant = true,
        .start_ns      = 1'762'000'000'000'000'000,
        .rounds        = 2,
        .cases         = {{.name        = "push",
                           .calibration = {.plan = {.n = 2, .count = 30}, .batch_ns = 5'100},
                           .voids       = 1,
                           .dropped     = false,
                           .reps        = {rep(0, 10), rep(1, -3)}},
                          {.name = "crash", .calibration = {}, .voids = 3, .dropped = true, .reps = {}}}};
}

fs::path temp_file(const std::string& name) {
    return fs::path(testing::TempDir()) / ("plumb_" + name + ".bin");
}

void cut(const fs::path& p, std::size_t bytes) { fs::resize_file(p, fs::file_size(p) - bytes); }

void expect_refused(const fs::path& p, std::string_view why) {
    auto back = plumb::read_results(p);
    ASSERT_FALSE(back);
    EXPECT_NE(back.error().find(why), std::string::npos) << back.error();
}

}  // namespace

TEST(Results, RoundTrips) {
    auto path = temp_file("results_round_trip");
    ASSERT_TRUE(plumb::write_results(path, sample()));
    auto back = plumb::read_results(path);
    ASSERT_TRUE(back) << back.error();
    EXPECT_EQ(*back, sample());
}

TEST(Results, KeepsOnlyTheFilledBuckets) {
    auto path = temp_file("results_sparse");
    ASSERT_TRUE(plumb::write_results(path, sample()));
    EXPECT_LT(fs::file_size(path), 4096u);
}

TEST(Results, MissingFile) { expect_refused(temp_file("results_missing"), "cannot open"); }

TEST(Results, BadMagic) {
    auto path = temp_file("results_magic");
    ASSERT_TRUE(plumb::write_results(path, sample()));
    std::fstream(path, std::ios::in | std::ios::out | std::ios::binary).write("x", 1);
    expect_refused(path, "not a plumb results file");
}

TEST(Results, OtherVersion) {
    auto path = temp_file("results_version");
    ASSERT_TRUE(plumb::write_results(path, sample()));
    std::fstream f(path, std::ios::in | std::ios::out | std::ios::binary);
    f.seekp(8);
    f.write("\x09\0\0\0", 4);
    f.close();
    expect_refused(path, "results version 9, expected 1");
}

TEST(Results, Truncated) {
    auto path = temp_file("results_short");
    ASSERT_TRUE(plumb::write_results(path, sample()));
    cut(path, 1);
    expect_refused(path, "truncated");
}

TEST(Results, TrailingBytes) {
    auto path = temp_file("results_trailing");
    ASSERT_TRUE(plumb::write_results(path, sample()));
    std::ofstream(path, std::ios::app | std::ios::binary) << 'x';
    expect_refused(path, "trailing bytes");
}

TEST(Results, BadBucket) {
    plumb::Results r = sample();
    r.cases.resize(1);
    r.cases[0].reps.resize(1);
    auto path = temp_file("results_bucket");
    ASSERT_TRUE(plumb::write_results(path, r));
    // The last rep ends with its windows, before them its histogram's last bucket.
    std::fstream f(path, std::ios::in | std::ios::out | std::ios::binary);
    auto         windows = 8 + r.cases[0].reps[0].windows.size() * 8;
    f.seekp(-static_cast<std::streamoff>(windows + 8 + 4), std::ios::end);
    f.write("\xff\xff\xff\x7f", 4);
    f.close();
    expect_refused(path, "bad bucket");
}
