#include "plumb/plumb.hpp"

namespace {

struct Push {};

const plumb::Case dup_case = plumb::op<Push>("dup_case", [](Push&) {});

}  // namespace
