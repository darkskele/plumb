#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "plumb/raw.hpp"

using namespace std::chrono_literals;
namespace fs = std::filesystem;

namespace {

struct Counter {
    std::size_t calls = 0;
};

constexpr auto count_op = [](Counter& c) { ++c.calls; };

constexpr plumb::detail::Warmup no_warmup{.time = {}, .batches = 0};

fs::path temp_file(const std::string& name) {
    return fs::path(testing::TempDir()) / ("plumb_" + name + ".raw");
}

plumb::Raw sample_raw() {
    auto clock = plumb::TscClock::calibrate(10ms);
    auto b = plumb::detail::run_batches<Counter, decltype(count_op)>(plumb::keep, 8, 16, no_warmup);
    return plumb::to_raw(b, clock, true);
}

void overwrite(const fs::path& p, std::streamoff at, const std::string& bytes) {
    std::fstream f(p, std::ios::in | std::ios::out | std::ios::binary);
    f.seekp(at);
    f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

}  // namespace

TEST(Raw, ConvertsTicksToNs) {
    auto clock = plumb::TscClock::calibrate(10ms);
    auto b = plumb::detail::run_batches<Counter, decltype(count_op)>(plumb::keep, 8, 16, no_warmup);
    auto raw = plumb::to_raw(b, clock, false);

    EXPECT_EQ(raw.header.version, 2u);
    EXPECT_EQ(raw.header.tsc_invariant, 0u);
    EXPECT_EQ(raw.header.measured, 0u);
    EXPECT_EQ(raw.header.n, 8u);
    EXPECT_EQ(raw.header.count, 16u);
    EXPECT_DOUBLE_EQ(raw.header.ghz, clock.ghz());
    ASSERT_EQ(raw.ns.size(), 16u);
    for (std::size_t i = 0; i < raw.ns.size(); ++i)
        EXPECT_DOUBLE_EQ(raw.ns[i], b.figures[i] / clock.ghz());

    double g = clock.ghz();
    EXPECT_EQ(raw.header.stamp_count, b.stamp_cost.count());
    EXPECT_DOUBLE_EQ(raw.header.stamp_min, b.stamp_cost.min() / g);
    EXPECT_DOUBLE_EQ(raw.header.stamp_max, b.stamp_cost.max() / g);
    EXPECT_DOUBLE_EQ(raw.header.stamp_mean, b.stamp_cost.mean() / g);
    EXPECT_DOUBLE_EQ(raw.header.stamp_variance, b.stamp_cost.variance() / (g * g));
}

TEST(Raw, KeepsAMeasuredOpsOwnNs) {
    constexpr auto op = [](Counter&) {
        return plumb::Measured{std::chrono::duration<double, std::nano>{1.5}};
    };
    auto clock = plumb::TscClock::at(3.0);
    auto b     = plumb::detail::run_batches<Counter, decltype(op)>(plumb::keep, 2, 4, no_warmup);
    auto raw   = plumb::to_raw(b, clock, true);
    EXPECT_EQ(raw.header.measured, 1u);
    EXPECT_EQ(raw.ns, (std::vector<double>{3.0, 3.0, 3.0, 3.0}));
}

TEST(Raw, RoundTrips) {
    auto raw  = sample_raw();
    auto path = temp_file("round_trip");
    ASSERT_TRUE(plumb::write_raw(path, raw));
    EXPECT_EQ(fs::file_size(path), sizeof(plumb::RawHeader) + 16 * sizeof(double));

    auto back = plumb::read_raw(path);
    ASSERT_TRUE(back) << back.error();
    EXPECT_EQ(*back, raw);
}

TEST(Raw, MissingFile) {
    auto back = plumb::read_raw(temp_file("missing"));
    ASSERT_FALSE(back);
    EXPECT_NE(back.error().find("cannot open"), std::string::npos) << back.error();
}

TEST(Raw, UnwritablePath) {
    auto done = plumb::write_raw(temp_file("no_dir") / "x.raw", sample_raw());
    ASSERT_FALSE(done);
    EXPECT_NE(done.error().find("cannot open"), std::string::npos) << done.error();
}

TEST(Raw, BadMagic) {
    auto path = temp_file("bad_magic");
    ASSERT_TRUE(plumb::write_raw(path, sample_raw()));
    overwrite(path, 0, "x");
    auto back = plumb::read_raw(path);
    ASSERT_FALSE(back);
    EXPECT_NE(back.error().find("not a plumb raw file"), std::string::npos) << back.error();
}

TEST(Raw, WrongVersion) {
    auto path = temp_file("wrong_version");
    ASSERT_TRUE(plumb::write_raw(path, sample_raw()));
    overwrite(path, 8, std::string("\x07\0\0\0", 4));
    auto back = plumb::read_raw(path);
    ASSERT_FALSE(back);
    EXPECT_NE(back.error().find("version 7"), std::string::npos) << back.error();
}

TEST(Raw, TruncatedHeader) {
    auto path = temp_file("short_header");
    ASSERT_TRUE(plumb::write_raw(path, sample_raw()));
    fs::resize_file(path, sizeof(plumb::RawHeader) - 1);
    auto back = plumb::read_raw(path);
    ASSERT_FALSE(back);
    EXPECT_NE(back.error().find("truncated"), std::string::npos) << back.error();
}

TEST(Raw, TruncatedSamples) {
    auto path = temp_file("short_samples");
    ASSERT_TRUE(plumb::write_raw(path, sample_raw()));
    fs::resize_file(path, fs::file_size(path) - 1);
    auto back = plumb::read_raw(path);
    ASSERT_FALSE(back);
    EXPECT_NE(back.error().find("truncated"), std::string::npos) << back.error();
}

TEST(Raw, TrailingBytes) {
    auto path = temp_file("trailing");
    ASSERT_TRUE(plumb::write_raw(path, sample_raw()));
    fs::resize_file(path, fs::file_size(path) + 1);
    auto back = plumb::read_raw(path);
    ASSERT_FALSE(back);
    EXPECT_NE(back.error().find("trailing"), std::string::npos) << back.error();
}
