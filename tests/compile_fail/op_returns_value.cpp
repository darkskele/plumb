// expect: op must return void or plumb::Measured, keep a value with plumb::sink
#include "plumb/plumb.hpp"

namespace {

struct Push {
    int count = 0;
};

const plumb::Case c = plumb::op<Push>("op_returns_value", [](Push& f) { return ++f.count; });

}  // namespace
