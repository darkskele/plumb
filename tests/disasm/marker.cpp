#include <cstddef>

#include "plumb/batch.hpp"

struct Marker {
    unsigned x = 1;
};

constexpr auto marker_op = [](Marker& m) { return m.x ^ 0x5eed5eedu; };

int main(int argc, char**) {
    Marker m;
    return static_cast<int>(
        plumb::detail::run_batch<Marker, decltype(marker_op)>(m, static_cast<std::size_t>(argc)) &
        1);
}
