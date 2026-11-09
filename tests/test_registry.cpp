#include <gtest/gtest.h>

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
