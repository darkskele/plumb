#include <gtest/gtest.h>

#include "plumb/name.hpp"

static_assert(plumb::valid_name("spsc_push_int_1024"));
static_assert(plumb::valid_name("a"));
static_assert(!plumb::valid_name(""));
static_assert(!plumb::valid_name("Upper"));
static_assert(!plumb::valid_name("with space"));
static_assert(!plumb::valid_name("dash-name"));
static_assert(!plumb::valid_name("dot.name"));

TEST(Name, KeepsTheLiteral) {
    constexpr plumb::Name n = "spsc_push_int_1024";
    EXPECT_EQ(n.value, "spsc_push_int_1024");
}

TEST(MaxBatch, KeepsTheCount) {
    constexpr plumb::MaxBatch b = 1024;
    EXPECT_EQ(b.value, 1024u);
}
