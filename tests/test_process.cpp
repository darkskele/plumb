#include <gtest/gtest.h>

#include <csignal>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "plumb/process.hpp"

namespace {

using Kind = plumb::detail::Exit::Kind;

plumb::detail::Exit sh(const std::string& script) {
    return plumb::detail::spawn_wait("/bin/sh", std::vector<std::string>{"-c", script});
}

}  // namespace

TEST(Process, CleanExit) { EXPECT_EQ(sh("exit 0"), (plumb::detail::Exit{Kind::ok, 0})); }

TEST(Process, NonzeroExit) { EXPECT_EQ(sh("exit 3"), (plumb::detail::Exit{Kind::nonzero, 3})); }

TEST(Process, KilledBySignal) {
    EXPECT_EQ(sh("kill -SEGV $$"), (plumb::detail::Exit{Kind::signal, SIGSEGV}));
    EXPECT_EQ(sh("kill -ABRT $$"), (plumb::detail::Exit{Kind::signal, SIGABRT}));
}

TEST(Process, MissingProgramNeverStarts) {
    auto e = plumb::detail::spawn_wait("/no/such/program", std::vector<std::string>{});
    EXPECT_EQ(e.kind, Kind::not_started);
}

TEST(Process, ChildGetsItsArgumentsAsGiven) {
    auto path = std::filesystem::path(testing::TempDir()) / "plumb_process_args";
    std::filesystem::remove(path);
    auto e = plumb::detail::spawn_wait(
        "/bin/sh", std::vector<std::string>{"-c", R"(printf '%s|' "$@" > "$0")", path.string(),
                                            "a b", "", "c"});
    ASSERT_EQ(e.kind, Kind::ok);
    std::ifstream f(path);
    std::string   got((std::istreambuf_iterator<char>(f)), {});
    EXPECT_EQ(got, "a b||c|");
}

TEST(Process, SelfIsThisBinary) {
    auto self = plumb::detail::self();
    EXPECT_TRUE(self.is_absolute());
    EXPECT_EQ(self.filename(), "plumb_tests");
}
