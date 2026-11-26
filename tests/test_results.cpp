#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
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
    raw.header.cpu_start  = 3;
    raw.header.cpu_end    = 5;
    raw.header.throttles  = 0;
    raw.header.temp_start = 48.5;
    raw.header.temp_end   = 51;
    for (int i = 0; i < 30; ++i) raw.ns.push_back(first + i);
    raw.header.count = raw.ns.size();
    for (int i = 0; i < 30; ++i)
        raw.counts.push_back({.cycles = 100.0 + i, .ref_cycles = 90.0 + i, .instructions = 50});
    return plumb::fold(raw, round);
}

plumb::Results sample() {
    return {.seed          = 7,
            .ghz           = 2.4,
            .tsc_invariant = true,
            .start_ns      = 1'762'000'000'000'000'000,
            .rounds        = 2,
            .cases         = {{.name        = "push",
                               .calibration = {.plan = {.n = 2, .count = 30}, .batch_ns = 5'100},
                               .voids       = {"repetition 1: killed by SIGABRT"},
                               .dropped     = false,
                               .reps        = {rep(0, 10), rep(1, -3)}},
                              {.name        = "crash",
                               .calibration = {},
                               .voids       = {"calibration: exited 2", "calibration: abandoned",
                                               "calibration: killed by SIGSEGV"},
                               .dropped     = true,
                               .reps        = {}}}};
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
    EXPECT_LT(fs::file_size(path), 8192u);
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
    expect_refused(path, "results version 9, expected 6");
}

TEST(Results, OtherSchema) {
    auto path = temp_file("results_schema");
    ASSERT_TRUE(plumb::write_results(path, sample()));
    // The schema's first character follows the magic, the version and the schema's length.
    std::fstream f(path, std::ios::in | std::ios::out | std::ios::binary);
    f.seekp(8 + 4 + 8);
    f.write("x", 1);
    f.close();
    expect_refused(path, "results schema differs from this build's");
}

TEST(Results, SchemaNamesEachField) {
    const auto& schema = plumb::detail::results_schema();
    EXPECT_TRUE(
        schema.starts_with("Results: seed u64, ghz f64, tsc_invariant bool, start_ns i64, "
                           "rounds u64, cases list<CaseResult>\n"))
        << schema;
    EXPECT_NE(
        schema.find("Histogram: negative u64, min i64, max i64, buckets list<StoredBucket>\n"),
        std::string::npos)
        << schema;
    EXPECT_NE(schema.find("RawHeader: magic char[8], version u32, "), std::string::npos) << schema;
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
    // The last rep ends with its windows and bucket counts, before them its histogram's last
    // bucket: lower, upper and count. Its upper no longer matches its lower.
    std::fstream f(path, std::ios::in | std::ios::out | std::ios::binary);
    const auto&  rep  = r.cases[0].reps[0];
    auto         tail = 8 + rep.windows.size() * 8 + 8 + rep.counts.size() * 32;
    f.seekp(-static_cast<std::streamoff>(tail + 16), std::ios::end);
    f.write("\xff\xff\xff\x7f\0\0\0\0", 8);
    f.close();
    expect_refused(path, "bad bucket");
}

TEST(Results, ABucketBelowZeroIsBad) {
    plumb::Results r = sample();
    r.cases.resize(1);
    r.cases[0].reps.resize(1);
    auto path = temp_file("results_negative_bucket");
    ASSERT_TRUE(plumb::write_results(path, r));
    std::fstream f(path, std::ios::in | std::ios::out | std::ios::binary);
    const auto&  rep  = r.cases[0].reps[0];
    auto         tail = 8 + rep.windows.size() * 8 + 8 + rep.counts.size() * 32;
    f.seekp(-static_cast<std::streamoff>(tail + 24), std::ios::end);
    f.write("\xff\xff\xff\xff\xff\xff\xff\xff", 8);
    f.close();
    expect_refused(path, "bad bucket");
}

// The committed file the tools' reader is tested against. PLUMB_WRITE_GOLDEN rewrites it.
TEST(Results, WritesTheGoldenFile) {
    fs::path golden = PLUMB_GOLDEN_RESULTS;
    if (std::getenv("PLUMB_WRITE_GOLDEN")) {
        fs::create_directories(golden.parent_path());
        ASSERT_TRUE(plumb::write_results(golden, sample()));
    }
    auto path = temp_file("results_golden");
    ASSERT_TRUE(plumb::write_results(path, sample()));
    auto bytes = [](const fs::path& p) {
        std::ifstream f(p, std::ios::binary);
        return std::string((std::istreambuf_iterator<char>(f)), {});
    };
    ASSERT_TRUE(fs::exists(golden)) << golden;
    EXPECT_TRUE(bytes(path) == bytes(golden)) << "the results format changed, rewrite " << golden;
}
