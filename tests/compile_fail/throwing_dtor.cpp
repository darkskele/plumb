// expect: fixture destructor may throw: .*Throws
#include "plumb/plumb.hpp"

namespace {

struct Throws {
    ~Throws() noexcept(false) {}
};

const plumb::Case c = plumb::op<Throws>("throwing_dtor", [](Throws&) {});

}  // namespace
