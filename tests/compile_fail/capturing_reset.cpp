// expect: reset must be a lambda with no captures taking the fixture: .*Push
#include "plumb/plumb.hpp"

namespace {

struct Push {};

int n = 0;

const plumb::Case c =
    plumb::op<Push>("capturing_reset", [](Push&) {}, {.reset = [&](Push&) { ++n; }});

}  // namespace
