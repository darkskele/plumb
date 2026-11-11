#pragma once

#include <cstdio>
#include <cstdlib>
#include <string_view>

namespace plumb {

namespace detail {

inline constexpr int abandon_code = 113;

}  // namespace detail

/// @brief Ends this repetition as void, saying why. Nothing is destroyed on the way out.
[[noreturn]] inline void abandon(std::string_view why) noexcept {
    std::fprintf(stderr, "plumb: abandoned: %.*s\n", static_cast<int>(why.size()), why.data());
    std::fflush(stderr);
    std::_Exit(detail::abandon_code);
}

}  // namespace plumb
