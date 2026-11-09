// expect: a case registers once
#include "plumb/plumb.hpp"

namespace {

struct Push {};

const plumb::Case c = plumb::op<Push>("copy_case", [](Push&) {});
const plumb::Case d = c;

}  // namespace
