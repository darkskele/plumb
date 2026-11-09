#include "plumb/plumb.hpp"

// Outside an anonymous namespace, so it clashes with the other file's Push.
struct Push {
    int count = 0;
};

// Builds a Push outside any case, so its code reaches the link.
int touch_a() {
    Push p;
    return static_cast<int>(p.count);
}

namespace {

const plumb::Case odr_a = plumb::op<Push>("odr_a", [](Push& f) { return ++f.count; });

}  // namespace
