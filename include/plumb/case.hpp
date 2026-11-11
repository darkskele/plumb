#pragma once

#include <concepts>
#include <type_traits>

#include "plumb/fixture.hpp"
#include "plumb/name.hpp"
#include "plumb/registry.hpp"

namespace plumb {

class Case;

namespace detail {

Case make_case(Entry e);

}  // namespace detail

/// @brief A case's options beside its fixture and op.
/// @tparam F The fixture type.
template <class F>
struct Options {
    MaxBatch max_batch = 1;
    Reset<F> reset     = {};
};

/// @brief A registered case. Declared once as a const variable at namespace scope.
class Case {
   public:
    Case(const Case&) = delete("a case registers once, declare it as one const variable");
    Case& operator=(const Case&) =
        delete("a case registers once, declare it as one const variable");

   private:
    explicit Case(Entry e) { registry().add(e); }

    friend Case detail::make_case(Entry e);
};

inline Case detail::make_case(Entry e) { return Case(e); }

/// @brief Registers a case that calls `o` on a fixture of type `F`.
template <class F, class O>
Case op(Name name, O, Options<F> opts = {}) {
    static_assert(std::default_initializable<F>,
                  detail::about("fixture must be default constructible: ", ^^F));
    static_assert(std::is_nothrow_destructible_v<F>,
                  detail::about("fixture destructor may throw: ", ^^F));
    static_assert(std::is_empty_v<O> && std::default_initializable<O>,
                  "op must be a lambda with no captures");
    static_assert(std::invocable<const O&, F&>,
                  detail::about("op must be callable with the fixture: ", ^^F));
    if constexpr (std::invocable<const O&, F&>) {
        using R = std::invoke_result_t<const O&, F&>;
        static_assert(std::is_void_v<R> || std::same_as<R, Measured>,
                      "op must return void or plumb::Measured, keep a value with plumb::sink");
    }
    return detail::make_case(detail::entry<F, O>(name.value, opts.max_batch.value, opts.reset));
}

}  // namespace plumb
