#include <cstddef>

#include "plumb/batch.hpp"

struct Marker {
    unsigned x = 1;
};

constexpr auto marker_op = [](Marker& m) { plumb::sink(m.x ^ 0x5eed5eedu); };

int main(int argc, char**) {
    Marker m;
    double ticks =
        plumb::detail::run_batch<Marker, decltype(marker_op)>(m, static_cast<std::size_t>(argc));
    return ticks > 0 ? 0 : 1;
}
