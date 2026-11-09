#include "plumb/plumb.hpp"

// Outside an anonymous namespace, so it clashes with the other file's Push.
struct Push {
    double total = 0;
    long   count = 0;
};

// Builds a Push outside any case, so its code reaches the link.
int touch_b() {
    Push p;
    return static_cast<int>(p.count);
}

namespace {

const plumb::Case odr_b = plumb::op<Push>("odr_b", [](Push& f) { f.total += ++f.count; });

}  // namespace
