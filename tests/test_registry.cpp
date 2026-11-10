#include <gtest/gtest.h>

#include <cstddef>
#include <string_view>
#include <vector>

#include "plumb/registry.hpp"

using Names = std::vector<std::string_view>;

TEST(Registry, KeepsEntriesInOrder) {
    plumb::Registry r;
    r.add({.name = "b", .max_batch = 1});
    r.add({.name = "a", .max_batch = 4});
    ASSERT_EQ(r.entries().size(), 2u);
    EXPECT_EQ(r.entries()[0].name, "b");
    EXPECT_EQ(r.entries()[1].name, "a");
    EXPECT_EQ(r.entries()[1].max_batch, 4u);
}

TEST(Registry, NoDuplicatesWhenNamesDiffer) {
    plumb::Registry r;
    r.add({.name = "a", .max_batch = 1});
    r.add({.name = "b", .max_batch = 1});
    EXPECT_EQ(r.duplicates(), Names{});
}

TEST(Registry, DuplicatesSortedOncePerName) {
    plumb::Registry r;
    for (std::string_view n : {"c", "b", "a", "c", "b", "c"}) r.add({.name = n, .max_batch = 1});
    EXPECT_EQ(r.duplicates(), (Names{"b", "c"}));
}

TEST(RegistryDeathTest, InvalidNameStops) {
    plumb::Registry r;
    EXPECT_DEATH(r.add({.name = "Bad Name", .max_batch = 1}), "valid_name");
}

TEST(RegistryDeathTest, ZeroMaxBatchStops) {
    plumb::Registry r;
    EXPECT_DEATH(r.add({.name = "a", .max_batch = 0}), "max_batch >= 1");
}

namespace {

struct Counter {
    static inline int resets = 0;

    std::size_t calls = 0;
};

constexpr auto count_op = [](Counter& c) { ++c.calls; };

constexpr plumb::detail::Warmup no_warmup{.time = {}, .batches = 0};

}  // namespace

TEST(Registry, EntryRunsItsCase) {
    auto e = plumb::detail::entry<Counter, decltype(count_op)>("count", 4, plumb::keep);
    EXPECT_EQ(e.name, "count");
    EXPECT_EQ(e.max_batch, 4u);
    auto b = e.batches(4, 3, no_warmup);
    EXPECT_EQ(b.n, 4u);
    EXPECT_EQ(b.ticks.size(), 3u);
}

TEST(Registry, EntryKeepsItsReset) {
    Counter::resets = 0;
    auto e          = plumb::detail::entry<Counter, decltype(count_op)>("count", 1,
                                                                        [](Counter&) { ++Counter::resets; });
    (void)e.batches(1, 3, no_warmup);
    EXPECT_EQ(Counter::resets, 2);
}
