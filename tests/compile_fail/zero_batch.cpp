// expect: max_batch is at least one
#include "plumb/plumb.hpp"

namespace {

struct Push {};

const plumb::Case c = plumb::op<Push>("zero_batch", [](Push&) {}, {.max_batch = 0});

}  // namespace
