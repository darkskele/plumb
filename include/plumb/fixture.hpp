#pragma once

#include <concepts>
#include <memory>
#include <meta>
#include <string>
#include <string_view>
#include <type_traits>

namespace plumb {

/// @brief What a case measures around. Built with no arguments, destroyed without throwing.
template <class F>
concept Fixture = std::is_object_v<F> && !std::is_const_v<F> && std::default_initializable<F> &&
                  std::is_nothrow_destructible_v<F>;

/// @brief What a case measures. A lambda with no captures, called with the fixture.
template <class O, class F>
concept Op = Fixture<F> && std::is_empty_v<O> && std::default_initializable<O> &&
             std::invocable<const O&, F&>;

/// @brief A reset that leaves the fixture as the last batch left it.
inline constexpr auto keep = [](Fixture auto&) noexcept {};

namespace detail {

consteval std::string about(std::string_view what, std::meta::info type) {
    return std::string(what) + std::string(std::meta::display_string_of(type));
}

}  // namespace detail

/// @brief A reset that destroys the fixture and builds a new one in the same memory.
template <class F>
void rebuild(F& f) {
    std::destroy_at(&f);
    std::construct_at(&f);
}

/// @brief How the fixture is made ready between batches. Rebuilt unless a reset is given.
/// @tparam F The fixture type.
template <class F>
struct Reset {
    void (*fn)(F&) = &rebuild<F>;

    constexpr Reset() = default;

    template <class R>
    constexpr Reset(R r) {
        static_assert(
            std::convertible_to<R, void (*)(F&)>,
            detail::about("reset must be a lambda with no captures taking the fixture: ", ^^F));
        if constexpr (std::convertible_to<R, void (*)(F&)>) fn = r;
    }
};

}  // namespace plumb
