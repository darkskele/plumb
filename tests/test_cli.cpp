#include <gtest/gtest.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

#include "plumb/cli.hpp"

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

TEST(Cli, NoArgumentsIsUsageError) {
    auto c = run(two_cases(), {});
    EXPECT_EQ(c.code, 2);
    EXPECT_NE(c.err.find("usage"), std::string::npos);
}
