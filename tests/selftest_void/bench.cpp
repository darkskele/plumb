#include <cstdio>
#include <cstdlib>
#include <filesystem>

#include "plumb/plumb.hpp"

namespace {

struct Fine {
    int count = 0;
};

// Crashes in the first process to build it, found by the file named in PLUMB_TEST_MARK.
struct Flaky {
    int count = 0;

    Flaky() {
        const char* mark = std::getenv("PLUMB_TEST_MARK");
        if (mark && !std::filesystem::exists(mark)) {
            std::fclose(std::fopen(mark, "w"));
            std::abort();
        }
    }
};

struct Crash {
    Crash() { std::abort(); }
};

struct Abandon {
    Abandon() { plumb::abandon("rig not ready"); }
};

const plumb::Case void_fine    = plumb::op<Fine>("void_fine", [](Fine& f) { return ++f.count; });
const plumb::Case void_flaky   = plumb::op<Flaky>("void_flaky", [](Flaky& f) { return ++f.count; });
const plumb::Case void_crash   = plumb::op<Crash>("void_crash", [](Crash&) {});
const plumb::Case void_abandon = plumb::op<Abandon>("void_abandon", [](Abandon&) {});

}  // namespace
