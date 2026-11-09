// expect: reset must be a lambda with no captures taking the fixture: .*Push
#include "plumb/plumb.hpp"

namespace {

struct Push {};

struct Other {};

const plumb::Case c = plumb::op<Push>("wrong_reset", [](Push&) {}, {.reset = [](Other&) {}});

}  // namespace
