// tests/test_session.cpp
//
// SessionManager 单元测试。
// 重点覆盖：bind / unbind / find 的正常路径，以及各种边界情况（重复登录、抢占连接等）。

#include "cpp_chat/session/session_manager.h"

#include <gtest/gtest.h>

namespace cpp_chat::session {

// 每个测试用例都从一个干净的 SessionManager 开始。
class SessionManagerTest : public ::testing::Test {
protected:
    SessionManager mgr;
};

// ─────────────────────────────────────────────
// bind & find
// ─────────────────────────────────────────────

TEST_F(SessionManagerTest, BindAndFindByUser) {
    mgr.bind({1, 10, "alice"});
    const auto session = mgr.find(1);
    ASSERT_TRUE(session.has_value());
    EXPECT_EQ(session->user_id, 1u);
    EXPECT_EQ(session->connection_id, 10u);
    EXPECT_EQ(session->username, "alice");
}

TEST_F(SessionManagerTest, FindByConnection) {
    mgr.bind({2, 20, "bob"});
    const auto session = mgr.find_by_connection(20);
    ASSERT_TRUE(session.has_value());
    EXPECT_EQ(session->user_id, 2u);
}

TEST_F(SessionManagerTest, FindByUsername) {
    mgr.bind({2, 20, "bob"});
    const auto session = mgr.find_by_username("bob");
    ASSERT_TRUE(session.has_value());
    EXPECT_EQ(session->user_id, 2u);
    EXPECT_EQ(session->connection_id, 20u);
    EXPECT_TRUE(mgr.is_username_online("bob"));
}

TEST_F(SessionManagerTest, FindNonExistentUser) {
    EXPECT_FALSE(mgr.find(999).has_value());
}

TEST_F(SessionManagerTest, FindNonExistentConnection) {
    EXPECT_FALSE(mgr.find_by_connection(999).has_value());
}

// ─────────────────────────────────────────────
// 重复 bind —— 用户换连接（重新登录）
// ─────────────────────────────────────────────

TEST_F(SessionManagerTest, RebindSameUserNewConnection) {
    mgr.bind({1, 10, "alice"});
    // alice 重连，使用新的 connection_id 100。
    mgr.bind({1, 100, "alice"});

    // 应使用新连接。
    const auto session = mgr.find(1);
    ASSERT_TRUE(session.has_value());
    EXPECT_EQ(session->connection_id, 100u);

    // 旧连接不应再关联任何用户。
    EXPECT_FALSE(mgr.find_by_connection(10).has_value());
    EXPECT_TRUE(mgr.is_username_online("alice"));
}

// ─────────────────────────────────────────────
// 重复 bind —— 连接切换用户（同一 socket 重新 LOGIN）
// ─────────────────────────────────────────────

TEST_F(SessionManagerTest, RebindSameConnectionNewUser) {
    mgr.bind({1, 10, "alice"});
    // connection_id=10 重新以 user_id=2 登录。
    mgr.bind({2, 10, "bob"});

    // 旧用户 1 不应再在线。
    EXPECT_FALSE(mgr.find(1).has_value());
    EXPECT_FALSE(mgr.is_username_online("alice"));

    // 新用户 2 应可查。
    const auto session = mgr.find(2);
    ASSERT_TRUE(session.has_value());
    EXPECT_EQ(session->connection_id, 10u);
    EXPECT_TRUE(mgr.is_username_online("bob"));
}

// ─────────────────────────────────────────────
// unbind（按用户 ID）
// ─────────────────────────────────────────────

TEST_F(SessionManagerTest, UnbindByUser) {
    mgr.bind({1, 10, "alice"});
    mgr.unbind(1);
    EXPECT_FALSE(mgr.find(1).has_value());
    EXPECT_FALSE(mgr.find_by_connection(10).has_value());
    EXPECT_FALSE(mgr.is_username_online("alice"));
}

TEST_F(SessionManagerTest, UnbindNonExistentUserIsNoOp) {
    // 下线一个从未登录的用户，不应 crash。
    EXPECT_NO_THROW(mgr.unbind(999));
}

// ─────────────────────────────────────────────
// unbind_connection（按 socket 断开）
// ─────────────────────────────────────────────

TEST_F(SessionManagerTest, UnbindConnection) {
    mgr.bind({1, 10, "alice"});
    mgr.unbind_connection(10);
    EXPECT_FALSE(mgr.find(1).has_value());
    EXPECT_FALSE(mgr.find_by_connection(10).has_value());
    EXPECT_FALSE(mgr.is_username_online("alice"));
}

TEST_F(SessionManagerTest, UnbindUnknownConnectionIsNoOp) {
    // 断开一个从未登录的连接，不应 crash。
    EXPECT_NO_THROW(mgr.unbind_connection(999));
}

// ─────────────────────────────────────────────
// 多用户并存
// ─────────────────────────────────────────────

TEST_F(SessionManagerTest, MultipleUsersCoexist) {
    mgr.bind({1, 10, "alice"});
    mgr.bind({2, 20, "bob"});
    mgr.bind({3, 30, "carol"});

    EXPECT_TRUE(mgr.find(1).has_value());
    EXPECT_TRUE(mgr.find(2).has_value());
    EXPECT_TRUE(mgr.find(3).has_value());

    mgr.unbind(2);
    EXPECT_FALSE(mgr.find(2).has_value());
    // alice 和 carol 不受影响。
    EXPECT_TRUE(mgr.find(1).has_value());
    EXPECT_TRUE(mgr.find(3).has_value());
}

} // namespace cpp_chat::session
