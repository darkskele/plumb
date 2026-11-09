// expect: op must be callable with the fixture: .*Push
#include "plumb/plumb.hpp"

namespace {

struct Push {};

struct Other {};

const plumb::Case c = plumb::op<Push>("wrong_fixture_op", [](Other&) {});

}  // namespace
