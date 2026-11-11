#include <chrono>

#include "plumb/plumb.hpp"

namespace {

struct Push {
    int count = 0;
};

const plumb::Case selftest_a =
    plumb::op<Push>("selftest_a", [](Push& f) { plumb::sink(++f.count); }, {.max_batch = 64});

// Its own time is 1us a call, so every figure is exactly that.
const plumb::Case selftest_m = plumb::op<Push>(
    "selftest_m", [](Push&) { return plumb::Measured{std::chrono::microseconds{1}}; },
    {.max_batch = 8});

}  // namespace
