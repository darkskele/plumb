// expect: op must be a lambda with no captures
#include "plumb/plumb.hpp"

namespace {

struct Push {};

int n = 0;

const plumb::Case c = plumb::op<Push>("capturing_op", [&](Push&) { ++n; });

}  // namespace
