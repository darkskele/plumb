#include <gtest/gtest.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "plumb/cli.hpp"
#include "plumb/plan.hpp"
#include "plumb/raw.hpp"

namespace {

struct Captured {
    int         code;
    std::string out;
    std::string err;
};

// Runs the command line with stdout and stderr captured in memory.
Captured run(const plumb::Registry& r, std::vector<std::string_view> args) {
    char*       out_buf = nullptr;
    char*       err_buf = nullptr;
    std::size_t out_len = 0;
    std::size_t err_len = 0;
    std::FILE*  out     = open_memstream(&out_buf, &out_len);
    std::FILE*  err     = open_memstream(&err_buf, &err_len);
    int         code    = plumb::run(args, r, out, err);
    std::fclose(out);
    std::fclose(err);
    Captured c{code, {out_buf, out_len}, {err_buf, err_len}};
    std::free(out_buf);
    std::free(err_buf);
    return c;
}

plumb::Registry two_cases() {
    plumb::Registry r;
    r.add({.name = "push_b", .max_batch = 1});
    r.add({.name = "push_a", .max_batch = 1});
    return r;
}

}  // namespace

TEST(Cli, ListPrintsNamesSorted) {
    auto c = run(two_cases(), {"--list"});
    EXPECT_EQ(c.code, 0);
    EXPECT_EQ(c.out, "push_a\npush_b\n");
    EXPECT_EQ(c.err, "");
}

TEST(Cli, CheckPassesWithoutDuplicates) {
    auto c = run(two_cases(), {"--check"});
    EXPECT_EQ(c.code, 0);
    EXPECT_EQ(c.out, "");
}

TEST(Cli, DuplicateStopsBeforeAnything) {
    plumb::Registry r = two_cases();
    r.add({.name = "push_a", .max_batch = 1});
    auto c = run(r, {"--list"});
    EXPECT_EQ(c.code, 1);
    EXPECT_EQ(c.out, "");
    EXPECT_EQ(c.err, "plumb: duplicate case name push_a\n");
}

TEST(Cli, UnknownArgumentIsUsageError) {
    auto c = run(two_cases(), {"--bogus"});
    EXPECT_EQ(c.code, 2);
    EXPECT_NE(c.err.find("unknown argument --bogus"), std::string::npos);
}

namespace {

using Kind = plumb::Command::Kind;

std::expected<plumb::Command, std::string> parse(std::vector<std::string_view> args) {
    return plumb::parse(args);
}

void expect_refused(std::vector<std::string_view> args, std::string_view why) {
    auto c = parse(args);
    ASSERT_FALSE(c);
    EXPECT_NE(c.error().find(why), std::string::npos) << c.error();
}

}  // namespace

TEST(Cli, NoArgumentsRunsEveryCase) {
    auto c = parse({});
    ASSERT_TRUE(c) << c.error();
    EXPECT_EQ(*c, (plumb::Command{.kind = Kind::run}));
}

TEST(Cli, ParsesRun) {
    auto c = parse({"--run", "push_a", "--in-process", "--out", "runs/a"});
    ASSERT_TRUE(c) << c.error();
    EXPECT_EQ(*c, (plumb::Command{
                      .kind = Kind::run, .name = "push_a", .in_process = true, .out = "runs/a"}));
}

TEST(Cli, RunOptionsInAnyOrder) {
    auto c = parse({"--out", "runs/a", "--run", "push_a"});
    ASSERT_TRUE(c) << c.error();
    EXPECT_EQ(c->name, "push_a");
    EXPECT_EQ(c->out, "runs/a");
}

TEST(Cli, RunNeedsAName) {
    expect_refused({"--run"}, "--run needs a case name");
    expect_refused({"--run", "--in-process"}, "--run needs a case name");
}

TEST(Cli, RunGivenOnce) { expect_refused({"--run", "a", "--run", "b"}, "--run given twice"); }

TEST(Cli, OutNeedsAPath) { expect_refused({"--out"}, "--out needs a path"); }

TEST(Cli, RunOptionsOnlyWithARun) {
    expect_refused({"--list", "--in-process"}, "only with a run");
    expect_refused({"--check", "--out", "runs/a"}, "only with a run");
    expect_refused({"--list", "--run", "push_a"}, "only with a run");
}

TEST(Cli, OneCommandAtATime) { expect_refused({"--list", "--check"}, "one command at a time"); }

TEST(Cli, DefaultRunDirectoryIsItsStartTime) {
    using namespace std::chrono;
    auto start = sys_days{2026y / November / 10} + 14h + 10min + 43s + 500ms;
    EXPECT_EQ(plumb::detail::default_out(start),
              std::filesystem::path("plumb_runs/20261110_141043"));
}

TEST(Cli, RunUnknownCaseStops) {
    auto c = run(two_cases(), {"--run", "push_c"});
    EXPECT_EQ(c.code, 1);
    EXPECT_EQ(c.out, "");
    EXPECT_EQ(c.err, "plumb: no case named push_c\n");
}

TEST(Cli, RunWithNoCasesStops) {
    auto c = run(plumb::Registry{}, {});
    EXPECT_EQ(c.code, 1);
    EXPECT_EQ(c.err, "plumb: no cases registered\n");
}

namespace {

struct Counter {
    std::size_t calls = 0;
};

constexpr auto count_op = [](Counter& c) { ++c.calls; };

plumb::Registry counters() {
    plumb::Registry r;
    r.add(plumb::detail::entry<Counter, decltype(count_op)>("count_b", 1, plumb::keep));
    r.add(plumb::detail::entry<Counter, decltype(count_op)>("count_a", 64, plumb::keep));
    return r;
}

std::filesystem::path fresh_dir(std::string_view name) {
    auto dir = std::filesystem::path(testing::TempDir()) / name;
    std::filesystem::remove_all(dir);
    return dir;
}

}  // namespace

TEST(Cli, RunsEveryCaseIntoOneTable) {
    auto dir = fresh_dir("plumb_cli_all");
    auto c   = run(counters(), {"--in-process", "--out", dir.string()});
    ASSERT_EQ(c.code, 0) << c.err;
    EXPECT_NE(c.out.find("ns per op"), std::string::npos) << c.out;
    EXPECT_EQ(c.out.find('\x1b'), std::string::npos) << c.out;
    auto a = c.out.find("\ncount_a ");
    auto b = c.out.find("\ncount_b ");
    EXPECT_NE(a, std::string::npos) << c.out;
    EXPECT_NE(b, std::string::npos) << c.out;
    EXPECT_LT(a, b) << c.out;
    EXPECT_NE(c.out.find(dir.string()), std::string::npos) << c.out;

    auto raw = plumb::read_raw(dir / "count_a.raw");
    ASSERT_TRUE(raw) << raw.error();
    EXPECT_EQ(raw->header.n, 64u);
    EXPECT_GE(raw->header.count, plumb::detail::min_count);
    EXPECT_LE(raw->header.count, plumb::detail::max_count);
    EXPECT_NE(c.out.find(std::format(" {} ", plumb::detail::thousands(raw->header.count))),
              std::string::npos)
        << c.out;
    EXPECT_TRUE(plumb::read_raw(dir / "count_b.raw")) << c.out;
}

TEST(Cli, RunOneCase) {
    auto dir = fresh_dir("plumb_cli_one");
    auto c   = run(counters(), {"--in-process", "--run", "count_b", "--out", dir.string()});
    ASSERT_EQ(c.code, 0) << c.err;
    EXPECT_NE(c.out.find("\ncount_b "), std::string::npos) << c.out;
    EXPECT_EQ(c.out.find("count_a"), std::string::npos) << c.out;
    EXPECT_TRUE(std::filesystem::exists(dir / "count_b.raw"));
    EXPECT_FALSE(std::filesystem::exists(dir / "count_a.raw"));
}

TEST(Cli, RunReportsADirectoryItCannotMake) {
    auto       file = fresh_dir("plumb_cli_file");
    std::FILE* f    = std::fopen(file.c_str(), "w");
    std::fclose(f);
    auto c = run(counters(), {"--in-process", "--out", (file / "sub").string()});
    EXPECT_EQ(c.code, 1);
    EXPECT_EQ(c.out, "");
    EXPECT_NE(c.err.find("cannot make"), std::string::npos) << c.err;
}

TEST(Cli, ParsesChild) {
    auto c = parse({"--child", "push_a", "--n", "8", "--count", "100", "--ghz", "2.5", "--out",
                    "runs/a/push_a.0.raw"});
    ASSERT_TRUE(c) << c.error();
    EXPECT_EQ(*c, (plumb::Command{.kind  = Kind::child,
                                  .name  = "push_a",
                                  .out   = "runs/a/push_a.0.raw",
                                  .n     = 8,
                                  .count = 100,
                                  .ghz   = 2.5}));
}

TEST(Cli, ChildNeedsItsPlanRateAndFile) {
    constexpr std::string_view why = "--child needs --n, --count, --ghz and --out";
    expect_refused({"--child", "a", "--count", "1", "--ghz", "1", "--out", "f"}, why);
    expect_refused({"--child", "a", "--n", "1", "--ghz", "1", "--out", "f"}, why);
    expect_refused({"--child", "a", "--n", "1", "--count", "1", "--out", "f"}, why);
    expect_refused({"--child", "a", "--n", "1", "--count", "1", "--ghz", "1"}, why);
    expect_refused({"--child"}, "--child needs a case name");
}

TEST(Cli, ChildCountsAreWholeAndAboveZero) {
    for (std::string_view bad : {"0", "-1", "x", "8x", ""}) {
        auto c = parse({"--child", "a", "--n", bad, "--count", "1", "--ghz", "1", "--out", "f"});
        ASSERT_FALSE(c) << bad;
        EXPECT_NE(c.error().find("--n needs a whole number above zero"), std::string::npos)
            << c.error();
    }
    expect_refused({"--child", "a", "--n", "1", "--count", "0", "--ghz", "1", "--out", "f"},
                   "--count needs a whole number above zero");
}

TEST(Cli, RateIsFiniteAndAboveZero) {
    for (std::string_view bad : {"0", "-1", "x", "inf", "nan", "2.5x", ""}) {
        auto c = parse({"--calibrate", "a", "--ghz", bad, "--out", "f"});
        ASSERT_FALSE(c) << bad;
        EXPECT_NE(c.error().find("--ghz needs a rate above zero"), std::string::npos) << c.error();
    }
}

TEST(Cli, PlanOnlyWithChild) {
    expect_refused({"--n", "8"}, "--n and --count go only with --child");
    expect_refused({"--run", "a", "--count", "8"}, "--n and --count go only with --child");
    expect_refused(
        {"--child", "a", "--run", "a", "--n", "1", "--count", "1", "--ghz", "1", "--out", "f"},
        "only with a run");
    expect_refused({"--child", "a", "--list"}, "one command at a time");
}

TEST(Cli, RateOnlyForChildren) {
    expect_refused({"--ghz", "2.5"}, "--ghz goes only with --child or --calibrate");
    expect_refused({"--list", "--ghz", "2.5"}, "--ghz goes only with --child or --calibrate");
}

TEST(Cli, ParsesSeed) {
    auto c = parse({"--seed", "18446744073709551615"});
    ASSERT_TRUE(c) << c.error();
    EXPECT_EQ(c->seed, 18446744073709551615u);
    EXPECT_EQ(parse({"--seed", "0"})->seed, 0u);
    EXPECT_EQ(parse({})->seed, std::nullopt);
}

TEST(Cli, SeedOnlyWithRoundsOfChildren) {
    expect_refused({"--seed"}, "--seed needs a whole number");
    expect_refused({"--seed", "x"}, "--seed needs a whole number");
    expect_refused({"--seed", "1", "--in-process"}, "--seed goes only with a run of children");
    expect_refused({"--list", "--seed", "1"}, "--seed goes only with a run of children");
}

TEST(Cli, ChildRunsOneRepetitionSilentlyAtTheGivenRate) {
    auto path = fresh_dir("plumb_cli_child.raw");
    auto c    = run(counters(), {"--child", "count_a", "--n", "16", "--count", "40", "--ghz", "2.5",
                                 "--out", path.string()});
    ASSERT_EQ(c.code, 0) << c.err;
    EXPECT_EQ(c.out, "");
    EXPECT_EQ(c.err, "");
    auto raw = plumb::read_raw(path);
    ASSERT_TRUE(raw) << raw.error();
    EXPECT_EQ(raw->header.n, 16u);
    EXPECT_EQ(raw->header.count, 40u);
    EXPECT_EQ(raw->header.ghz, 2.5);
}

TEST(Cli, ChildUnknownCaseStops) {
    auto c = run(counters(),
                 {"--child", "count_c", "--n", "1", "--count", "1", "--ghz", "1", "--out", "f"});
    EXPECT_EQ(c.code, 1);
    EXPECT_EQ(c.err, "plumb: no case named count_c\n");
}

TEST(Cli, ChildReportsAnUnwritableFile) {
    auto path = fresh_dir("plumb_cli_child_dir") / "x.raw";
    auto c    = run(counters(), {"--child", "count_a", "--n", "1", "--count", "1", "--ghz", "1",
                                 "--out", path.string()});
    EXPECT_EQ(c.code, 1);
    EXPECT_NE(c.err.find("cannot open"), std::string::npos) << c.err;
}

TEST(Cli, ParsesCalibrate) {
    auto c = parse({"--calibrate", "push_a", "--ghz", "2.5", "--out", "runs/a/push_a.plan"});
    ASSERT_TRUE(c) << c.error();
    EXPECT_EQ(
        *c,
        (plumb::Command{
            .kind = Kind::calibrate, .name = "push_a", .out = "runs/a/push_a.plan", .ghz = 2.5}));
}

TEST(Cli, CalibrateNeedsItsRateAndFile) {
    expect_refused({"--calibrate"}, "--calibrate needs a case name");
    expect_refused({"--calibrate", "a", "--ghz", "1"}, "--calibrate needs --ghz and --out");
    expect_refused({"--calibrate", "a", "--out", "f"}, "--calibrate needs --ghz and --out");
    expect_refused({"--calibrate", "a", "--ghz", "1", "--out", "f", "--n", "1"},
                   "--n and --count go only with --child");
    expect_refused({"--calibrate", "a", "--ghz", "1", "--out", "f", "--in-process"},
                   "only with a run");
    expect_refused({"--calibrate", "a", "--child", "a"}, "one command at a time");
}

TEST(Cli, CalibrateWritesThePlanSilently) {
    auto path = fresh_dir("plumb_cli_calibrate.plan");
    auto ghz  = std::format("{}", plumb::TscClock::calibrate(std::chrono::milliseconds{20}).ghz());
    auto c    = run(counters(), {"--calibrate", "count_a", "--ghz", ghz, "--out", path.string()});
    ASSERT_EQ(c.code, 0) << c.err;
    EXPECT_EQ(c.out, "");
    EXPECT_EQ(c.err, "");
    auto plan = plumb::read_plan(path);
    ASSERT_TRUE(plan) << plan.error();
    EXPECT_EQ(plan->plan.n, 64u);
    EXPECT_EQ(plan->plan.count, plumb::detail::count_for(plan->batch_ns));
}

TEST(Cli, CalibrateUnknownCaseStops) {
    auto c = run(counters(), {"--calibrate", "count_c", "--ghz", "1", "--out", "f"});
    EXPECT_EQ(c.code, 1);
    EXPECT_EQ(c.err, "plumb: no case named count_c\n");
}

TEST(Cli, CalibrateReportsAnUnwritableFile) {
    auto path = fresh_dir("plumb_cli_calibrate_dir") / "x.plan";
    auto c    = run(counters(), {"--calibrate", "count_a", "--ghz", "1", "--out", path.string()});
    EXPECT_EQ(c.code, 1);
    EXPECT_NE(c.err.find("cannot open"), std::string::npos) << c.err;
}
