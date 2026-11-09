#pragma once

#include <algorithm>
#include <cstddef>
#include <string_view>

namespace plumb {

/// @brief Whether a case name is valid. Lowercase letters, digits and underscores, not empty.
constexpr bool valid_name(std::string_view name) noexcept {
    auto allowed = [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
    };
    return !name.empty() && std::ranges::all_of(name, allowed);
}

/// @brief A case's name. An invalid one fails to compile.
struct Name {
    std::string_view value;

    consteval Name(const char* name) : value(name) {
        if (!valid_name(value)) throw "case names are lowercase letters, digits and underscores";
    }
};

/// @brief The most ops one batch of a case may hold. Under one fails to compile.
struct MaxBatch {
    std::size_t value;

    consteval MaxBatch(std::size_t n) : value(n) {
        if (n < 1) throw "max_batch is at least one";
    }
};

}  // namespace plumb
