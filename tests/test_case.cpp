#include <gtest/gtest.h>

#include <algorithm>
#include <concepts>

#include "plumb/plumb.hpp"

namespace {

struct Push {
    int count = 0;
};

struct Other {};

struct NoDefault {
    explicit NoDefault(int) {}
};

struct ThrowingDtor {
    ~ThrowingDtor() noexcept(false) {}
};

constexpr auto push       = [](Push& f) { return ++f.count; };
constexpr auto void_op    = [](Push& f) { ++f.count; };
constexpr auto other_op   = [](Other&) {};
constexpr auto generic_op = [](auto& f) { return f.count; };

int  captured  = 0;
auto capturing = [&c = captured](Push&) { ++c; };

static_assert(plumb::Fixture<Push>);
static_assert(!plumb::Fixture<NoDefault>);
static_assert(!plumb::Fixture<ThrowingDtor>);
static_assert(!plumb::Fixture<int&>);
static_assert(!plumb::Fixture<const Push>);

static_assert(plumb::Op<decltype(push), Push>);
static_assert(plumb::Op<decltype(void_op), Push>);
static_assert(plumb::Op<decltype(generic_op), Push>);
static_assert(!plumb::Op<decltype(other_op), Push>);
static_assert(!plumb::Op<decltype(capturing), Push>);

static_assert(std::convertible_to<decltype(plumb::keep), plumb::Reset<Push>>);
static_assert(std::convertible_to<decltype([](Push&) {}), plumb::Reset<Push>>);

static_assert(!std::copy_constructible<plumb::Case>);
static_assert(!std::move_constructible<plumb::Case>);

const plumb::Case test_registers = plumb::op<Push>("test_registers", push, {.max_batch = 8});
const plumb::Case test_defaults  = plumb::op<Push>("test_defaults", void_op);

const plumb::Entry* find(std::string_view name) {
    auto entries = plumb::registry().entries();
    auto it      = std::ranges::find(entries, name, &plumb::Entry::name);
    return it == entries.end() ? nullptr : &*it;
}

}  // namespace

TEST(Case, RegistersAtStartup) {
    const plumb::Entry* e = find("test_registers");
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->max_batch, 8u);
}

TEST(Case, MaxBatchDefaultsToOne) {
    const plumb::Entry* e = find("test_defaults");
    ASSERT_NE(e, nullptr);
    EXPECT_EQ(e->max_batch, 1u);
}
