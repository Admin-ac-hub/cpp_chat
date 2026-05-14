#include "cpp_chat/storage/group_member_store.h"

#include <algorithm>
#include <gtest/gtest.h>

namespace cpp_chat::storage {

TEST(GroupMemberStore, JoinGroupAddsMember) {
    GroupMemberStore store;

    const auto result = store.join_group(100, 1);

    EXPECT_TRUE(result.success);
    EXPECT_FALSE(result.already_member);
    EXPECT_TRUE(store.is_group_member(100, 1));
}

TEST(GroupMemberStore, DuplicateJoinIsIdempotent) {
    GroupMemberStore store;

    ASSERT_TRUE(store.join_group(100, 1).success);
    const auto result = store.join_group(100, 1);

    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.already_member);
    EXPECT_TRUE(store.is_group_member(100, 1));
}

TEST(GroupMemberStore, LeaveGroupRemovesMember) {
    GroupMemberStore store;
    ASSERT_TRUE(store.join_group(100, 1).success);

    const auto result = store.leave_group(100, 1);

    EXPECT_TRUE(result.success);
    EXPECT_FALSE(store.is_group_member(100, 1));
}

TEST(GroupMemberStore, LeaveMissingMemberFails) {
    GroupMemberStore store;

    const auto result = store.leave_group(100, 1);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.reason, "not a member of this group");
}

TEST(GroupMemberStore, LoadGroupMembersReturnsSortedMembers) {
    GroupMemberStore store;
    ASSERT_TRUE(store.join_group(100, 3).success);
    ASSERT_TRUE(store.join_group(100, 1).success);
    ASSERT_TRUE(store.join_group(100, 2).success);
    ASSERT_TRUE(store.join_group(200, 9).success);

    const auto members = store.load_group_members(100);

    ASSERT_EQ(members.size(), 3u);
    EXPECT_EQ(members[0], 1u);
    EXPECT_EQ(members[1], 2u);
    EXPECT_EQ(members[2], 3u);
}

TEST(GroupMemberStore, LoadUserGroupsReturnsSortedGroups) {
    GroupMemberStore store;
    ASSERT_TRUE(store.join_group(300, 1).success);
    ASSERT_TRUE(store.join_group(100, 1).success);
    ASSERT_TRUE(store.join_group(200, 2).success);

    const auto groups = store.load_user_groups(1);

    ASSERT_EQ(groups.size(), 2u);
    EXPECT_EQ(groups[0], 100u);
    EXPECT_EQ(groups[1], 300u);
}

} // namespace cpp_chat::storage
