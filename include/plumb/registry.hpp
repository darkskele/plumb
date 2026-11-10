#pragma once

#include <algorithm>
#include <cstddef>
#include <functional>
#include <ranges>
#include <span>
#include <string_view>
#include <vector>

#include "plumb/batch.hpp"
#include "plumb/fixture.hpp"
#include "plumb/name.hpp"

namespace plumb {

/// @brief What the registry holds of one case, and how to run it.
struct Entry {
    std::string_view name;
    std::size_t      max_batch;
    void (*reset)()                                                              = nullptr;
    detail::Batches (*run)(void (*)(), std::size_t, std::size_t, detail::Warmup) = nullptr;

    /// @brief Runs the case's batches on a new fixture.
    detail::Batches batches(std::size_t n, std::size_t count, detail::Warmup warmup = {}) const
        pre(run != nullptr) {
        return run(reset, n, count, warmup);
    }
};

namespace detail {

template <Fixture F, Op<F> O>
Batches run_entry(void (*reset)(), std::size_t n, std::size_t count, Warmup warmup) noexcept {
    return run_batches<F, O>(Reset<F>(reinterpret_cast<void (*)(F&)>(reset)), n, count, warmup);
}

/// @brief The entry of a case that calls `O` on a fixture of type `F`.
/// @tparam F The fixture type.
/// @tparam O The op type.
template <Fixture F, Op<F> O>
Entry entry(std::string_view name, std::size_t max_batch, Reset<F> reset) {
    return {.name      = name,
            .max_batch = max_batch,
            .reset     = reinterpret_cast<void (*)()>(reset.fn),
            .run       = &run_entry<F, O>};
}

}  // namespace detail

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
