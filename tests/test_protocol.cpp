#include "cpp_chat/protocol/message.h"

#include <gtest/gtest.h>

namespace cpp_chat::protocol {

TEST(ParseClientCommand, RegisterSuccess) {
    ClientCommand cmd;
    std::string err;
    ASSERT_TRUE(parse_client_command(R"({"type":"register","username":"ys","password":"123456"})", cmd, err));
    EXPECT_EQ(cmd.type, MessageType::Register);
    EXPECT_EQ(cmd.username, "ys");
    EXPECT_EQ(cmd.password, "123456");
}

TEST(ParseClientCommand, LoginSuccess) {
    ClientCommand cmd;
    std::string err;
    ASSERT_TRUE(parse_client_command(R"({"type":"login","username":"ys","password":"123456"})", cmd, err));
    EXPECT_EQ(cmd.type, MessageType::Login);
    EXPECT_EQ(cmd.username, "ys");
    EXPECT_EQ(cmd.password, "123456");
}

TEST(ParseClientCommand, LogoutSuccess) {
    ClientCommand cmd;
    std::string err;
    ASSERT_TRUE(parse_client_command(R"({"type":"logout"})", cmd, err));
    EXPECT_EQ(cmd.type, MessageType::Logout);
}

TEST(ParseClientCommand, PingSuccess) {
    ClientCommand cmd;
    std::string err;
    ASSERT_TRUE(parse_client_command(R"({"type":"ping"})", cmd, err));
    EXPECT_EQ(cmd.type, MessageType::Ping);
}

TEST(ParseClientCommand, DmSuccess) {
    ClientCommand cmd;
    std::string err;
    ASSERT_TRUE(parse_client_command(R"({"type":"dm","to":"bob","body":"hello world"})", cmd, err));
    EXPECT_EQ(cmd.type, MessageType::DirectChat);
    EXPECT_EQ(cmd.target_username, "bob");
    EXPECT_EQ(cmd.body, "hello world");
}

TEST(ParseClientCommand, JoinGroupSuccess) {
    ClientCommand cmd;
    std::string err;
    ASSERT_TRUE(parse_client_command(R"({"type":"join_group","group_id":100})", cmd, err));
    EXPECT_EQ(cmd.type, MessageType::GroupJoin);
    EXPECT_EQ(cmd.group_id, 100u);
}

TEST(ParseClientCommand, GroupMessageSuccess) {
    ClientCommand cmd;
    std::string err;
    ASSERT_TRUE(parse_client_command(R"({"type":"group_message","group_id":100,"body":"hi group"})", cmd, err));
    EXPECT_EQ(cmd.type, MessageType::GroupChat);
    EXPECT_EQ(cmd.group_id, 100u);
    EXPECT_EQ(cmd.body, "hi group");
}

TEST(ParseClientCommand, HistorySuccess) {
    ClientCommand cmd;
    std::string err;
    ASSERT_TRUE(parse_client_command(R"({"type":"history","peer":"bob"})", cmd, err));
    EXPECT_EQ(cmd.type, MessageType::HistoryQuery);
    EXPECT_FALSE(cmd.group_history);
    EXPECT_EQ(cmd.target_username, "bob");
}

TEST(ParseClientCommand, GroupHistorySuccess) {
    ClientCommand cmd;
    std::string err;
    ASSERT_TRUE(parse_client_command(R"({"type":"group_history","group_id":100})", cmd, err));
    EXPECT_EQ(cmd.type, MessageType::HistoryQuery);
    EXPECT_TRUE(cmd.group_history);
    EXPECT_EQ(cmd.group_id, 100u);
}

TEST(ParseClientCommand, MalformedJsonFails) {
    ClientCommand cmd;
    std::string err;
    EXPECT_FALSE(parse_client_command(R"({"type":"login")", cmd, err));
    EXPECT_FALSE(err.empty());
}

TEST(ParseClientCommand, EmptyUsernameFails) {
    ClientCommand cmd;
    std::string err;
    EXPECT_FALSE(parse_client_command(R"({"type":"register","username":"","password":"123"})", cmd, err));
    EXPECT_EQ(err, "username and password are required");
}

TEST(ParseClientCommand, OldTextProtocolFails) {
    ClientCommand cmd;
    std::string err;
    EXPECT_FALSE(parse_client_command("LOGIN 42 alice", cmd, err));
    EXPECT_FALSE(err.empty());
}

TEST(FormatJson, BasicResponses) {
    EXPECT_EQ(format_pong(), R"({"type":"pong"})" "\n");
    EXPECT_EQ(format_error("please login first"), R"({"type":"error","reason":"please login first"})" "\n");
    EXPECT_EQ(format_login_failed("bad"), R"({"type":"login_failed","reason":"bad"})" "\n");
    EXPECT_EQ(format_register_success(7), R"({"type":"register_success","user_id":7})" "\n");
}

TEST(FormatJson, ChatResponses) {
    EXPECT_EQ(format_direct_message("alice", "hi"), R"({"type":"dm","from":"alice","body":"hi"})" "\n");
    EXPECT_EQ(format_group_message(100, "alice", "hi"), R"({"type":"group_message","group_id":100,"from":"alice","body":"hi"})" "\n");
    EXPECT_EQ(format_history_item("dm", "alice", "bob", "hi"), R"({"type":"history_item","chat_type":"dm","from":"alice","to":"bob","body":"hi"})" "\n");
    EXPECT_EQ(format_history_end(), R"({"type":"history_end"})" "\n");
}

TEST(ToString, CoversAllTypes) {
    EXPECT_EQ(to_string(MessageType::Register),     "register");
    EXPECT_EQ(to_string(MessageType::Login),        "login");
    EXPECT_EQ(to_string(MessageType::Logout),       "logout");
    EXPECT_EQ(to_string(MessageType::DirectChat),   "direct_chat");
    EXPECT_EQ(to_string(MessageType::GroupChat),    "group_chat");
    EXPECT_EQ(to_string(MessageType::HistoryQuery), "history_query");
    EXPECT_EQ(to_string(MessageType::Ping),         "ping");
    EXPECT_EQ(to_string(MessageType::System),       "system");
}

} // namespace cpp_chat::protocol
