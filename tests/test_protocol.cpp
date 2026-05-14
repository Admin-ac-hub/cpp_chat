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

TEST(ParseClientCommand, CreateGroupSuccess) {
    ClientCommand cmd;
    std::string err;
    ASSERT_TRUE(parse_client_command(R"({"type":"create_group","name":"backend"})", cmd, err));
    EXPECT_EQ(cmd.type, MessageType::CreateGroup);
    EXPECT_EQ(cmd.group_name, "backend");
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
    ASSERT_TRUE(parse_client_command(R"({"type":"history","peer":"bob","limit":20,"before_id":123})", cmd, err));
    EXPECT_EQ(cmd.type, MessageType::HistoryQuery);
    EXPECT_FALSE(cmd.group_history);
    EXPECT_EQ(cmd.target_username, "bob");
    EXPECT_EQ(cmd.limit, 20u);
    EXPECT_EQ(cmd.before_id, 123u);
}

TEST(ParseClientCommand, GroupHistorySuccess) {
    ClientCommand cmd;
    std::string err;
    ASSERT_TRUE(parse_client_command(R"({"type":"group_history","group_id":100,"limit":200,"before_id":9})", cmd, err));
    EXPECT_EQ(cmd.type, MessageType::HistoryQuery);
    EXPECT_TRUE(cmd.group_history);
    EXPECT_EQ(cmd.group_id, 100u);
    EXPECT_EQ(cmd.limit, 100u);
    EXPECT_EQ(cmd.before_id, 9u);
}

TEST(ParseClientCommand, UnreadSuccess) {
    ClientCommand cmd;
    std::string err;
    ASSERT_TRUE(parse_client_command(R"({"type":"unread","last_seen_message_id":123,"limit":20})", cmd, err));
    EXPECT_EQ(cmd.type, MessageType::UnreadQuery);
    EXPECT_EQ(cmd.last_seen_message_id, 123u);
    EXPECT_EQ(cmd.limit, 20u);
}

TEST(ParseClientCommand, MalformedJsonFails) {
    ClientCommand cmd;
    std::string err;
    EXPECT_FALSE(parse_client_command(R"({"type":"login")", cmd, err));
    EXPECT_FALSE(err.empty());
}

TEST(ParseClientCommand, MissingTypeFails) {
    ClientCommand cmd;
    std::string err;
    EXPECT_FALSE(parse_client_command(R"({"username":"ys","password":"123456"})", cmd, err));
    EXPECT_EQ(err, "missing string field: type");
}

TEST(ParseClientCommand, NonStringTypeFails) {
    ClientCommand cmd;
    std::string err;
    EXPECT_FALSE(parse_client_command(R"({"type":123,"username":"ys","password":"123456"})", cmd, err));
    EXPECT_EQ(err, "missing string field: type");
}

TEST(ParseClientCommand, EmptyUsernameFails) {
    ClientCommand cmd;
    std::string err;
    EXPECT_FALSE(parse_client_command(R"({"type":"register","username":"","password":"123"})", cmd, err));
    EXPECT_EQ(err, "username and password are required");
}

TEST(ParseClientCommand, MissingRequiredFieldsFail) {
    ClientCommand cmd;
    std::string err;

    EXPECT_FALSE(parse_client_command(R"({"type":"login","username":"ys"})", cmd, err));
    EXPECT_EQ(err, "missing string field: password");

    err.clear();
    EXPECT_FALSE(parse_client_command(R"({"type":"dm","to":"bob"})", cmd, err));
    EXPECT_EQ(err, "missing string field: body");

    err.clear();
    EXPECT_FALSE(parse_client_command(R"({"type":"history"})", cmd, err));
    EXPECT_EQ(err, "missing string field: peer");
}

TEST(ParseClientCommand, InvalidFieldTypesFail) {
    ClientCommand cmd;
    std::string err;

    EXPECT_FALSE(parse_client_command(R"({"type":"register","username":42,"password":"123"})", cmd, err));
    EXPECT_EQ(err, "missing string field: username");

    err.clear();
    EXPECT_FALSE(parse_client_command(R"({"type":"dm","to":"bob","body":false})", cmd, err));
    EXPECT_EQ(err, "missing string field: body");

    err.clear();
    EXPECT_FALSE(parse_client_command(R"({"type":"history","peer":"bob","limit":"20"})", cmd, err));
    EXPECT_EQ(err, "invalid number field: limit");
}

TEST(ParseClientCommand, InvalidGroupAndCursorTypesFail) {
    ClientCommand cmd;
    std::string err;

    EXPECT_FALSE(parse_client_command(R"({"type":"join_group","group_id":"100"})", cmd, err));
    EXPECT_EQ(err, "missing number field: group_id");

    err.clear();
    EXPECT_FALSE(parse_client_command(R"({"type":"group_history","group_id":true})", cmd, err));
    EXPECT_EQ(err, "missing number field: group_id");

    err.clear();
    EXPECT_FALSE(parse_client_command(R"({"type":"history","peer":"bob","before_id":"3"})", cmd, err));
    EXPECT_EQ(err, "invalid number field: before_id");
}

TEST(ParseClientCommand, UnknownCommandFails) {
    ClientCommand cmd;
    std::string err;
    EXPECT_FALSE(parse_client_command(R"({"type":"dance"})", cmd, err));
    EXPECT_EQ(err, "unknown command");
}

TEST(ParseClientCommand, OldTextProtocolFails) {
    ClientCommand cmd;
    std::string err;
    EXPECT_FALSE(parse_client_command("LOGIN 42 alice", cmd, err));
    EXPECT_FALSE(err.empty());
}

TEST(FormatJson, BasicResponses) {
    EXPECT_EQ(format_pong(), R"({"type":"pong"})");
    EXPECT_EQ(format_error("please login first"), R"({"type":"error","reason":"please login first"})");
    EXPECT_EQ(format_login_failed("bad"), R"({"type":"login_failed","reason":"bad"})");
    EXPECT_EQ(format_register_success(7), R"({"type":"register_success","user_id":7})");
    EXPECT_EQ(format_create_group_success(100, "backend"),
              R"({"type":"create_group_success","group_id":100,"name":"backend"})");
    EXPECT_EQ(format_message_ack(123, true, false),
              R"({"type":"message_ack","message_id":123,"status":"stored","stored":true,"delivered":false})");
}

TEST(FormatJson, ChatResponses) {
    EXPECT_EQ(format_direct_message("alice", "hi"), R"({"type":"dm","from":"alice","body":"hi"})");
    EXPECT_EQ(format_group_message(100, "alice", "hi"), R"({"type":"group_message","group_id":100,"from":"alice","body":"hi"})");
    EXPECT_EQ(format_history_item("dm", "alice", "bob", "hi"), R"({"type":"history_item","chat_type":"dm","from":"alice","to":"bob","body":"hi"})");
    EXPECT_EQ(format_history_item(122, "dm", "alice", "bob", "hi", "2026-05-13 12:00:00"),
              R"({"type":"history_item","message_id":122,"chat_type":"dm","from":"alice","to":"bob","body":"hi","created_at":"2026-05-13 12:00:00"})");
    EXPECT_EQ(format_history_end(), R"({"type":"history_end"})");
    EXPECT_EQ(format_history_end(true, 103), R"({"type":"history_end","has_more":true,"next_before_id":103})");
    EXPECT_EQ(format_unread_end(false, 122), R"({"type":"unread_end","has_more":false,"next_last_seen_message_id":122})");
}

TEST(ToString, CoversAllTypes) {
    EXPECT_EQ(to_string(MessageType::Register),     "register");
    EXPECT_EQ(to_string(MessageType::Login),        "login");
    EXPECT_EQ(to_string(MessageType::Logout),       "logout");
    EXPECT_EQ(to_string(MessageType::DirectChat),   "direct_chat");
    EXPECT_EQ(to_string(MessageType::GroupChat),    "group_chat");
    EXPECT_EQ(to_string(MessageType::CreateGroup),  "create_group");
    EXPECT_EQ(to_string(MessageType::HistoryQuery), "history_query");
    EXPECT_EQ(to_string(MessageType::UnreadQuery),  "unread_query");
    EXPECT_EQ(to_string(MessageType::Ping),         "ping");
    EXPECT_EQ(to_string(MessageType::System),       "system");
}

} // namespace cpp_chat::protocol
