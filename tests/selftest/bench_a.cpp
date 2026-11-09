#include "plumb/plumb.hpp"

namespace {

struct Push {
    int count = 0;
};

const plumb::Case selftest_a =
    plumb::op<Push>("selftest_a", [](Push& f) { return ++f.count; }, {.max_batch = 64});

}  // namespace
