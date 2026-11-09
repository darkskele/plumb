#include "plumb/plumb.hpp"

namespace {

// Same name as the other file's fixture, a different type. Fine inside an anonymous namespace.
struct Push {
    double total = 0;
    long   count = 0;
};

const plumb::Case selftest_b = plumb::op<Push>("selftest_b", [](Push& f) { f.total += ++f.count; });

}  // namespace
