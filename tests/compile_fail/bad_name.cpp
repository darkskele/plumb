// expect: case names are lowercase letters, digits and underscores
#include "plumb/plumb.hpp"

namespace {

struct Push {};

const plumb::Case c = plumb::op<Push>("Bad Name", [](Push&) {});

}  // namespace
