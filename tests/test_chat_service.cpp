#include "cpp_chat/chat/chat_service.h"

#include "cpp_chat/logging/logger.h"
#include "cpp_chat/session/session_manager.h"
#include "cpp_chat/storage/group_member_store.h"
#include "cpp_chat/storage/group_store.h"
#include "cpp_chat/storage/message_store.h"
#include "cpp_chat/storage/user_store.h"

#include <algorithm>
#include <gtest/gtest.h>

namespace cpp_chat::chat {

class ChatServiceTest : public ::testing::Test {
protected:
    session::SessionManager sessions;
    storage::MessageStore store;
    storage::UserStore users;
    storage::GroupStore groups;
    storage::GroupMemberStore group_members;
    logging::Logger logger;
    ChatService svc{sessions, store, users, groups, group_members, logger};

    void register_user(const std::string& username, const std::string& password = "secret") {
        const auto out = svc.handle_client_line(
            1000,
            R"({"type":"register","username":")" + username + R"(","password":")" + password + R"("})");
        ASSERT_EQ(out.size(), 1u);
        ASSERT_TRUE(out[0].data.find("register_success") != std::string::npos) << out[0].data;
    }

    void login(network::ConnectionId connection_id,
               const std::string& username,
               const std::string& password = "secret") {
        const auto out = svc.handle_client_line(
            connection_id,
            R"({"type":"login","username":")" + username + R"(","password":")" + password + R"("})");
        ASSERT_EQ(out.size(), 1u);
        ASSERT_TRUE(out[0].data.find("login_success") != std::string::npos) << out[0].data;
    }
};

TEST_F(ChatServiceTest, MalformedCommandReturnsJsonError) {
    const auto out = svc.handle_client_line(1, "QUIT");
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].connection_id, 1u);
    EXPECT_TRUE(out[0].data.find(R"("type":"error")") != std::string::npos);
}

TEST_F(ChatServiceTest, RegisterAndLoginSuccess) {
    const auto reg = svc.handle_client_line(
        1, R"({"type":"register","username":"alice","password":"secret"})");
    ASSERT_EQ(reg.size(), 1u);
    EXPECT_EQ(reg[0].data, R"({"type":"register_success","user_id":1})");

    const auto login_out = svc.handle_client_line(
        1, R"({"type":"login","username":"alice","password":"secret"})");
    ASSERT_EQ(login_out.size(), 1u);
    EXPECT_EQ(login_out[0].data, R"({"type":"login_success","user_id":1,"username":"alice"})");

    const auto session = sessions.find_by_username("alice");
    ASSERT_TRUE(session.has_value());
    EXPECT_EQ(session->connection_id, 1u);
}

TEST_F(ChatServiceTest, DuplicateLoginIsRejected) {
    register_user("alice");
    login(1, "alice");

    const auto out = svc.handle_client_line(
        2, R"({"type":"login","username":"alice","password":"secret"})");
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].data, R"({"type":"login_failed","reason":"user already online"})");
}

TEST_F(ChatServiceTest, LoggedInConnectionCannotLoginAgainAsAnotherUser) {
    register_user("alice");
    register_user("bob");
    login(1, "alice");

    const auto out = svc.handle_client_line(
        1, R"({"type":"login","username":"bob","password":"secret"})");
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].data, R"({"type":"login_failed","reason":"already logged in"})");
}

TEST_F(ChatServiceTest, LoggedInConnectionCannotRegister) {
    register_user("alice");
    login(1, "alice");

    const auto out = svc.handle_client_line(
        1, R"({"type":"register","username":"bob","password":"secret"})");
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].data, R"({"type":"error","reason":"already logged in"})");
}

TEST_F(ChatServiceTest, InvalidLoginFails) {
    register_user("alice");

    const auto out = svc.handle_client_line(
        1, R"({"type":"login","username":"alice","password":"wrong"})");
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].data, R"({"type":"login_failed","reason":"invalid username or password"})");
}

TEST_F(ChatServiceTest, PingAllowedWithoutLogin) {
    const auto out = svc.handle_client_line(1, R"({"type":"ping"})");
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].data, R"({"type":"pong"})");
}

TEST_F(ChatServiceTest, DmWithoutLoginReturnsError) {
    const auto out = svc.handle_client_line(
        1, R"({"type":"dm","to":"bob","body":"hello"})");
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].data, R"({"type":"error","reason":"please login first"})");
}

TEST_F(ChatServiceTest, HistoryWithoutLoginReturnsError) {
    const auto direct = svc.handle_client_line(1, R"({"type":"history","peer":"bob"})");
    ASSERT_EQ(direct.size(), 1u);
    EXPECT_EQ(direct[0].data, R"({"type":"error","reason":"please login first"})");

    const auto group = svc.handle_client_line(1, R"({"type":"group_history","group_id":100})");
    ASSERT_EQ(group.size(), 1u);
    EXPECT_EQ(group[0].data, R"({"type":"error","reason":"please login first"})");
}

TEST_F(ChatServiceTest, GroupOperationsWithoutLoginReturnError) {
    const auto create = svc.handle_client_line(1, R"({"type":"create_group","name":"backend"})");
    ASSERT_EQ(create.size(), 1u);
    EXPECT_EQ(create[0].data, R"({"type":"error","reason":"please login first"})");

    const auto join = svc.handle_client_line(1, R"({"type":"join_group","group_id":100})");
    ASSERT_EQ(join.size(), 1u);
    EXPECT_EQ(join[0].data, R"({"type":"error","reason":"please login first"})");

    const auto leave = svc.handle_client_line(1, R"({"type":"leave_group","group_id":100})");
    ASSERT_EQ(leave.size(), 1u);
    EXPECT_EQ(leave[0].data, R"({"type":"error","reason":"please login first"})");
}

TEST_F(ChatServiceTest, DmToOnlineUserDeliversByUsername) {
    register_user("alice");
    register_user("bob");
    login(1, "alice");
    login(2, "bob");

    const auto out = svc.handle_client_line(
        1, R"({"type":"dm","to":"bob","body":"hello bob"})");

    ASSERT_EQ(out.size(), 2u);
    const auto bob_msg = std::find_if(out.begin(), out.end(),
        [](const OutboundMessage& msg) { return msg.connection_id == 2; });
    ASSERT_NE(bob_msg, out.end());
    EXPECT_EQ(bob_msg->data, R"({"type":"dm","from":"alice","body":"hello bob"})");

    const auto alice_msg = std::find_if(out.begin(), out.end(),
        [](const OutboundMessage& msg) { return msg.connection_id == 1; });
    ASSERT_NE(alice_msg, out.end());
    EXPECT_EQ(alice_msg->data, R"({"type":"message_ack","message_id":1,"status":"stored","stored":true,"delivered":true})");
}

TEST_F(ChatServiceTest, DmToOfflineUserPersistsAndReturnsUndeliveredAck) {
    register_user("alice");
    register_user("bob");
    login(1, "alice");

    const auto out = svc.handle_client_line(
        1, R"({"type":"dm","to":"bob","body":"stored"})");

    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].data, R"({"type":"message_ack","message_id":1,"status":"stored","stored":true,"delivered":false})");

    const auto messages = store.all();
    ASSERT_EQ(messages.size(), 1u);
    EXPECT_EQ(messages[0].sender_id, 1u);
    EXPECT_EQ(messages[0].receiver_id, 2u);
    EXPECT_EQ(messages[0].body, "stored");
}

TEST_F(ChatServiceTest, DmToMissingReceiverReturnsError) {
    register_user("alice");
    login(1, "alice");

    const auto out = svc.handle_client_line(
        1, R"({"type":"dm","to":"missing","body":"hello"})");
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].data, R"({"type":"error","reason":"receiver not found"})");
}

TEST_F(ChatServiceTest, LogoutClearsSession) {
    register_user("alice");
    login(1, "alice");

    const auto logout = svc.handle_client_line(1, R"({"type":"logout"})");
    ASSERT_EQ(logout.size(), 1u);
    EXPECT_EQ(logout[0].data, R"({"type":"ok","message":"logout"})");
    EXPECT_TRUE(logout[0].close_after_send);
    EXPECT_FALSE(sessions.find_by_username("alice").has_value());

    const auto dm = svc.handle_client_line(1, R"({"type":"dm","to":"bob","body":"nope"})");
    ASSERT_EQ(dm.size(), 1u);
    EXPECT_EQ(dm[0].data, R"({"type":"error","reason":"please login first"})");
}

TEST_F(ChatServiceTest, DisconnectClearsSession) {
    register_user("alice");
    login(1, "alice");
    svc.handle_disconnect(1);
    EXPECT_FALSE(sessions.find_by_username("alice").has_value());
}

TEST_F(ChatServiceTest, GroupMessageRequiresLogin) {
    const auto out = svc.handle_client_line(
        1, R"({"type":"group_message","group_id":100,"body":"hello"})");
    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].data, R"({"type":"error","reason":"please login first"})");
}

TEST_F(ChatServiceTest, GroupMessageDeliversToOnlineMembers) {
    register_user("alice");
    register_user("bob");
    login(1, "alice");
    login(2, "bob");

    svc.handle_client_line(1, R"({"type":"join_group","group_id":100})");
    svc.handle_client_line(2, R"({"type":"join_group","group_id":100})");

    const auto out = svc.handle_client_line(
        1, R"({"type":"group_message","group_id":100,"body":"hello group"})");
    ASSERT_EQ(out.size(), 2u);

    const auto bob_msg = std::find_if(out.begin(), out.end(),
        [](const OutboundMessage& msg) { return msg.connection_id == 2; });
    ASSERT_NE(bob_msg, out.end());
    EXPECT_EQ(bob_msg->data, R"({"type":"group_message","group_id":100,"from":"alice","body":"hello group"})");

    const auto alice_msg = std::find_if(out.begin(), out.end(),
        [](const OutboundMessage& msg) { return msg.connection_id == 1; });
    ASSERT_NE(alice_msg, out.end());
    EXPECT_EQ(alice_msg->data, R"({"type":"message_ack","message_id":1,"status":"stored","stored":true,"delivered":true})");
}

TEST_F(ChatServiceTest, GroupMessageRequiresPersistentMembership) {
    register_user("alice");
    login(1, "alice");

    const auto out = svc.handle_client_line(
        1, R"({"type":"group_message","group_id":100,"body":"hello group"})");

    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].data, R"({"type":"error","reason":"not a member of this group"})");
}

TEST_F(ChatServiceTest, DuplicateJoinGroupIsOk) {
    register_user("alice");
    login(1, "alice");

    const auto first = svc.handle_client_line(1, R"({"type":"join_group","group_id":100})");
    const auto second = svc.handle_client_line(1, R"({"type":"join_group","group_id":100})");

    ASSERT_EQ(first.size(), 1u);
    ASSERT_EQ(second.size(), 1u);
    EXPECT_EQ(first[0].data, R"({"type":"ok","message":"joined group 100"})");
    EXPECT_EQ(second[0].data, R"({"type":"ok","message":"joined group 100"})");
}

TEST_F(ChatServiceTest, CreateGroupCreatesAndJoinsOwner) {
    register_user("alice");
    login(1, "alice");

    const auto out = svc.handle_client_line(1, R"({"type":"create_group","name":"backend"})");

    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].data, R"({"type":"create_group_success","group_id":1,"name":"backend"})");
    EXPECT_TRUE(group_members.is_group_member(1, 1));
    EXPECT_TRUE(groups.group_exists(1));
}


TEST_F(ChatServiceTest, LeaveGroupRemovesPersistentMembership) {
    register_user("alice");
    login(1, "alice");

    svc.handle_client_line(1, R"({"type":"join_group","group_id":100})");
    const auto leave = svc.handle_client_line(1, R"({"type":"leave_group","group_id":100})");
    const auto group_message = svc.handle_client_line(
        1, R"({"type":"group_message","group_id":100,"body":"nope"})");

    ASSERT_EQ(leave.size(), 1u);
    EXPECT_EQ(leave[0].data, R"({"type":"ok","message":"left group 100"})");
    ASSERT_EQ(group_message.size(), 1u);
    EXPECT_EQ(group_message[0].data, R"({"type":"error","reason":"not a member of this group"})");
}

TEST_F(ChatServiceTest, LeaveGroupRejectsMissingMember) {
    register_user("alice");
    login(1, "alice");

    const auto out = svc.handle_client_line(1, R"({"type":"leave_group","group_id":100})");

    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].data, R"({"type":"error","reason":"not a member of this group"})");
}

TEST_F(ChatServiceTest, GroupMembershipSurvivesNewSessionManager) {
    register_user("alice");
    login(1, "alice");
    svc.handle_client_line(1, R"({"type":"join_group","group_id":100})");

    session::SessionManager fresh_sessions;
    ChatService restarted{fresh_sessions, store, users, groups, group_members, logger};
    const auto login_out = restarted.handle_client_line(
        2, R"({"type":"login","username":"alice","password":"secret"})");
    ASSERT_EQ(login_out.size(), 1u);
    ASSERT_TRUE(login_out[0].data.find("login_success") != std::string::npos);

    const auto out = restarted.handle_client_line(
        2, R"({"type":"group_message","group_id":100,"body":"after restart"})");

    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].data, R"({"type":"message_ack","message_id":1,"status":"stored","stored":true,"delivered":false})");
}

TEST_F(ChatServiceTest, DirectHistoryUsesUsernames) {
    register_user("alice");
    register_user("bob");
    register_user("carol");
    login(1, "alice");
    login(2, "bob");
    login(3, "carol");

    svc.handle_client_line(1, R"({"type":"dm","to":"bob","body":"hello bob"})");
    svc.handle_client_line(2, R"({"type":"dm","to":"alice","body":"hello alice"})");
    svc.handle_client_line(3, R"({"type":"dm","to":"alice","body":"not included"})");

    const auto out = svc.handle_client_line(1, R"({"type":"history","peer":"bob"})");
    ASSERT_EQ(out.size(), 3u);
    EXPECT_EQ(out[0].data, R"({"type":"history_item","message_id":1,"chat_type":"dm","from":"alice","to":"bob","body":"hello bob","created_at":""})");
    EXPECT_EQ(out[1].data, R"({"type":"history_item","message_id":2,"chat_type":"dm","from":"bob","to":"alice","body":"hello alice","created_at":""})");
    EXPECT_EQ(out[2].data, R"({"type":"history_end","has_more":false,"next_before_id":1})");
}

TEST_F(ChatServiceTest, DirectHistorySupportsLimitAndBeforeId) {
    register_user("alice");
    register_user("bob");
    login(1, "alice");
    login(2, "bob");

    svc.handle_client_line(1, R"({"type":"dm","to":"bob","body":"one"})");
    svc.handle_client_line(2, R"({"type":"dm","to":"alice","body":"two"})");
    svc.handle_client_line(1, R"({"type":"dm","to":"bob","body":"three"})");

    const auto first = svc.handle_client_line(1, R"({"type":"history","peer":"bob","limit":1})");
    ASSERT_EQ(first.size(), 2u);
    EXPECT_EQ(first[0].data, R"({"type":"history_item","message_id":3,"chat_type":"dm","from":"alice","to":"bob","body":"three","created_at":""})");
    EXPECT_EQ(first[1].data, R"({"type":"history_end","has_more":true,"next_before_id":3})");

    const auto second = svc.handle_client_line(1, R"({"type":"history","peer":"bob","limit":1,"before_id":3})");
    ASSERT_EQ(second.size(), 2u);
    EXPECT_EQ(second[0].data, R"({"type":"history_item","message_id":2,"chat_type":"dm","from":"bob","to":"alice","body":"two","created_at":""})");
    EXPECT_EQ(second[1].data, R"({"type":"history_end","has_more":true,"next_before_id":2})");
}

TEST_F(ChatServiceTest, GroupHistoryReturnsItems) {
    register_user("alice");
    register_user("bob");
    login(1, "alice");
    login(2, "bob");

    svc.handle_client_line(1, R"({"type":"join_group","group_id":100})");
    svc.handle_client_line(2, R"({"type":"join_group","group_id":100})");
    svc.handle_client_line(1, R"({"type":"group_message","group_id":100,"body":"hello"})");
    svc.handle_client_line(2, R"({"type":"group_message","group_id":100,"body":"hi"})");

    const auto out = svc.handle_client_line(1, R"({"type":"group_history","group_id":100})");
    ASSERT_EQ(out.size(), 3u);
    EXPECT_EQ(out[0].data, R"({"type":"history_item","message_id":1,"chat_type":"gc","from":"alice","to":"100","body":"hello","created_at":""})");
    EXPECT_EQ(out[1].data, R"({"type":"history_item","message_id":2,"chat_type":"gc","from":"bob","to":"100","body":"hi","created_at":""})");
    EXPECT_EQ(out[2].data, R"({"type":"history_end","has_more":false,"next_before_id":1})");
}

TEST_F(ChatServiceTest, GroupHistorySupportsLimitAndBeforeId) {
    register_user("alice");
    register_user("bob");
    login(1, "alice");
    login(2, "bob");

    svc.handle_client_line(1, R"({"type":"join_group","group_id":100})");
    svc.handle_client_line(2, R"({"type":"join_group","group_id":100})");
    svc.handle_client_line(1, R"({"type":"group_message","group_id":100,"body":"one"})");
    svc.handle_client_line(2, R"({"type":"group_message","group_id":100,"body":"two"})");
    svc.handle_client_line(1, R"({"type":"group_message","group_id":100,"body":"three"})");

    const auto first = svc.handle_client_line(1, R"({"type":"group_history","group_id":100,"limit":1})");
    ASSERT_EQ(first.size(), 2u);
    EXPECT_EQ(first[0].data, R"({"type":"history_item","message_id":3,"chat_type":"gc","from":"alice","to":"100","body":"three","created_at":""})");
    EXPECT_EQ(first[1].data, R"({"type":"history_end","has_more":true,"next_before_id":3})");

    const auto second = svc.handle_client_line(1, R"({"type":"group_history","group_id":100,"limit":1,"before_id":3})");
    ASSERT_EQ(second.size(), 2u);
    EXPECT_EQ(second[0].data, R"({"type":"history_item","message_id":2,"chat_type":"gc","from":"bob","to":"100","body":"two","created_at":""})");
    EXPECT_EQ(second[1].data, R"({"type":"history_end","has_more":true,"next_before_id":2})");
}

TEST_F(ChatServiceTest, GroupHistoryRejectsNonMember) {
    register_user("alice");
    login(1, "alice");

    const auto out = svc.handle_client_line(1, R"({"type":"group_history","group_id":100})");

    ASSERT_EQ(out.size(), 1u);
    EXPECT_EQ(out[0].data, R"({"type":"error","reason":"not a member of this group"})");
}

TEST_F(ChatServiceTest, UnreadReturnsDirectAndGroupMessagesAfterCursor) {
    register_user("alice");
    register_user("bob");
    register_user("carol");
    login(1, "alice");
    login(2, "bob");
    login(3, "carol");

    svc.handle_client_line(1, R"({"type":"dm","to":"bob","body":"old"})");
    svc.handle_client_line(1, R"({"type":"dm","to":"bob","body":"new dm"})");
    svc.handle_client_line(2, R"({"type":"dm","to":"alice","body":"sent by bob"})");
    svc.handle_client_line(1, R"({"type":"join_group","group_id":100})");
    svc.handle_client_line(2, R"({"type":"join_group","group_id":100})");
    svc.handle_client_line(3, R"({"type":"join_group","group_id":100})");
    svc.handle_client_line(3, R"({"type":"group_message","group_id":100,"body":"new group"})");
    svc.handle_client_line(2, R"({"type":"group_message","group_id":100,"body":"own group"})");

    const auto out = svc.handle_client_line(
        2, R"({"type":"unread","last_seen_message_id":1,"limit":10})");

    ASSERT_EQ(out.size(), 3u);
    EXPECT_EQ(out[0].data, R"({"type":"history_item","message_id":2,"chat_type":"dm","from":"alice","to":"bob","body":"new dm","created_at":""})");
    EXPECT_EQ(out[1].data, R"({"type":"history_item","message_id":4,"chat_type":"gc","from":"carol","to":"100","body":"new group","created_at":""})");
    EXPECT_EQ(out[2].data, R"({"type":"unread_end","has_more":false,"next_last_seen_message_id":4})");
}

TEST_F(ChatServiceTest, UnreadSupportsLimitAndCursor) {
    register_user("alice");
    register_user("bob");
    login(1, "alice");
    login(2, "bob");

    svc.handle_client_line(1, R"({"type":"dm","to":"bob","body":"one"})");
    svc.handle_client_line(1, R"({"type":"dm","to":"bob","body":"two"})");
    svc.handle_client_line(1, R"({"type":"dm","to":"bob","body":"three"})");

    const auto first = svc.handle_client_line(
        2, R"({"type":"unread","last_seen_message_id":0,"limit":1})");
    ASSERT_EQ(first.size(), 2u);
    EXPECT_EQ(first[0].data, R"({"type":"history_item","message_id":1,"chat_type":"dm","from":"alice","to":"bob","body":"one","created_at":""})");
    EXPECT_EQ(first[1].data, R"({"type":"unread_end","has_more":true,"next_last_seen_message_id":1})");

    const auto second = svc.handle_client_line(
        2, R"({"type":"unread","last_seen_message_id":1,"limit":1})");
    ASSERT_EQ(second.size(), 2u);
    EXPECT_EQ(second[0].data, R"({"type":"history_item","message_id":2,"chat_type":"dm","from":"alice","to":"bob","body":"two","created_at":""})");
    EXPECT_EQ(second[1].data, R"({"type":"unread_end","has_more":true,"next_last_seen_message_id":2})");
}

} // namespace cpp_chat::chat
