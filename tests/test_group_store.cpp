#include "cpp_chat/storage/group_store.h"

#include <gtest/gtest.h>

namespace cpp_chat::storage {

TEST(GroupStore, CreateGroupReturnsRecord) {
    GroupStore store;

    const auto result = store.create_group("backend", 1);

    ASSERT_TRUE(result.success);
    EXPECT_EQ(result.group.id, 1u);
    EXPECT_EQ(result.group.name, "backend");
    EXPECT_EQ(result.group.owner_id, 1u);
}

TEST(GroupStore, CreateGroupRejectsEmptyName) {
    GroupStore store;

    const auto result = store.create_group("", 1);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.reason, "group name is required");
}

TEST(GroupStore, FindGroupReturnsStoredRecord) {
    GroupStore store;
    ASSERT_TRUE(store.create_group("backend", 1).success);

    const auto group = store.find_group(1);

    ASSERT_TRUE(group.has_value());
    EXPECT_EQ(group->name, "backend");
    EXPECT_TRUE(store.group_exists(1));
    EXPECT_FALSE(store.group_exists(999));
}

} // namespace cpp_chat::storage
