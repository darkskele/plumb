// expect: fixture must be default constructible: .*NoDefault
#include "plumb/plumb.hpp"

namespace {

struct NoDefault {
    explicit NoDefault(int) {}
};

const plumb::Case c = plumb::op<NoDefault>("no_default", [](NoDefault&) {});

}  // namespace
