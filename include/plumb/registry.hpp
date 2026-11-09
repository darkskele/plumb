#pragma once

#include <algorithm>
#include <cstddef>
#include <functional>
#include <ranges>
#include <span>
#include <string_view>
#include <vector>

#include "plumb/name.hpp"

namespace plumb {

/// @brief What the registry holds of one case.
struct Entry {
    std::string_view name;
    std::size_t      max_batch;
};

/// @brief Every case of one binary, in the order they registered.
class Registry {
   public:
    void add(Entry e) pre(valid_name(e.name)) pre(e.max_batch >= 1) { entries_.push_back(e); }

    std::span<const Entry> entries() const noexcept { return entries_; }

    /// @brief Names registered more than once, sorted, each once.
    std::vector<std::string_view> duplicates() const {
        auto names =
            entries_ | std::views::transform(&Entry::name) | std::ranges::to<std::vector>();
        std::ranges::sort(names);

        return names | std::views::chunk_by(std::ranges::equal_to{}) |
               std::views::filter([](auto same) { return std::ranges::size(same) > 1; }) |
               std::views::transform([](auto same) { return same.front(); }) |
               std::ranges::to<std::vector>();
    }

   private:
    std::vector<Entry> entries_;
};

/// @brief The binary's one registry, filled as cases register before main.
inline Registry& registry() {
    static Registry r;
    return r;
}

}  // namespace plumb
