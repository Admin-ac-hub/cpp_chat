#include "cpp_chat/protocol/message.h"

#include <cctype>
#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace cpp_chat::protocol {

namespace {

struct JsonValue {
    enum class Type { String, Number, Bool };

    Type type = Type::String;
    std::string string_value;
    std::uint64_t number_value = 0;
    bool bool_value = false;
};

using JsonObject = std::unordered_map<std::string, JsonValue>;

void trim_trailing_cr(std::string& value) {
    if (!value.empty() && value.back() == '\r') {
        value.pop_back();
    }
}

void skip_ws(const std::string& input, std::size_t& pos) {
    while (pos < input.size() && std::isspace(static_cast<unsigned char>(input[pos])) != 0) {
        ++pos;
    }
}

void expect_char(const std::string& input, std::size_t& pos, char expected) {
    skip_ws(input, pos);
    if (pos >= input.size() || input[pos] != expected) {
        throw std::runtime_error("malformed json");
    }
    ++pos;
}

std::string parse_string(const std::string& input, std::size_t& pos) {
    skip_ws(input, pos);
    if (pos >= input.size() || input[pos] != '"') {
        throw std::runtime_error("malformed json string");
    }
    ++pos;

    std::string value;
    while (pos < input.size()) {
        const char ch = input[pos++];
        if (ch == '"') {
            return value;
        }
        if (ch != '\\') {
            value.push_back(ch);
            continue;
        }
        if (pos >= input.size()) {
            throw std::runtime_error("malformed json escape");
        }
        const char escaped = input[pos++];
        switch (escaped) {
            case '"':
            case '\\':
            case '/':
                value.push_back(escaped);
                break;
            case 'n':
                value.push_back('\n');
                break;
            case 'r':
                value.push_back('\r');
                break;
            case 't':
                value.push_back('\t');
                break;
            default:
                throw std::runtime_error("unsupported json escape");
        }
    }

    throw std::runtime_error("unterminated json string");
}

std::uint64_t parse_number(const std::string& input, std::size_t& pos) {
    skip_ws(input, pos);
    const auto start = pos;
    while (pos < input.size() && std::isdigit(static_cast<unsigned char>(input[pos])) != 0) {
        ++pos;
    }
    if (start == pos) {
        throw std::runtime_error("malformed json number");
    }
    return std::stoull(input.substr(start, pos - start));
}

bool parse_bool(const std::string& input, std::size_t& pos) {
    skip_ws(input, pos);
    if (input.compare(pos, 4, "true") == 0) {
        pos += 4;
        return true;
    }
    if (input.compare(pos, 5, "false") == 0) {
        pos += 5;
        return false;
    }
    throw std::runtime_error("malformed json bool");
}

JsonObject parse_json_object(const std::string& line) {
    std::size_t pos = 0;
    JsonObject object;

    expect_char(line, pos, '{');
    skip_ws(line, pos);
    if (pos < line.size() && line[pos] == '}') {
        ++pos;
        skip_ws(line, pos);
        if (pos != line.size()) {
            throw std::runtime_error("trailing json content");
        }
        return object;
    }

    while (true) {
        const std::string key = parse_string(line, pos);
        expect_char(line, pos, ':');
        skip_ws(line, pos);
        if (pos >= line.size()) {
            throw std::runtime_error("missing json value");
        }

        JsonValue value;
        if (line[pos] == '"') {
            value.type = JsonValue::Type::String;
            value.string_value = parse_string(line, pos);
        } else if (std::isdigit(static_cast<unsigned char>(line[pos])) != 0) {
            value.type = JsonValue::Type::Number;
            value.number_value = parse_number(line, pos);
        } else if (line.compare(pos, 4, "true") == 0 || line.compare(pos, 5, "false") == 0) {
            value.type = JsonValue::Type::Bool;
            value.bool_value = parse_bool(line, pos);
        } else {
            throw std::runtime_error("unsupported json value");
        }
        object[key] = std::move(value);

        skip_ws(line, pos);
        if (pos < line.size() && line[pos] == ',') {
            ++pos;
            continue;
        }
        if (pos < line.size() && line[pos] == '}') {
            ++pos;
            break;
        }
        throw std::runtime_error("malformed json object");
    }

    skip_ws(line, pos);
    if (pos != line.size()) {
        throw std::runtime_error("trailing json content");
    }
    return object;
}

std::string get_string(const JsonObject& object, const std::string& name) {
    const auto it = object.find(name);
    if (it == object.end() || it->second.type != JsonValue::Type::String) {
        throw std::runtime_error("missing string field: " + name);
    }
    return it->second.string_value;
}

std::uint64_t get_uint64(const JsonObject& object, const std::string& name) {
    const auto it = object.find(name);
    if (it == object.end() || it->second.type != JsonValue::Type::Number) {
        throw std::runtime_error("missing number field: " + name);
    }
    return it->second.number_value;
}

std::uint64_t get_uint64_or(const JsonObject& object,
                            const std::string& name,
                            std::uint64_t default_value) {
    const auto it = object.find(name);
    if (it == object.end()) {
        return default_value;
    }
    if (it->second.type != JsonValue::Type::Number) {
        throw std::runtime_error("invalid number field: " + name);
    }
    return it->second.number_value;
}

bool empty(const std::string& value) {
    return value.empty();
}

std::string escape_json(const std::string& value) {
    std::string escaped;
    for (const char ch : value) {
        switch (ch) {
            case '"':
                escaped += "\\\"";
                break;
            case '\\':
                escaped += "\\\\";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\t':
                escaped += "\\t";
                break;
            default:
                escaped.push_back(ch);
                break;
        }
    }
    return escaped;
}

std::string json_string_field(const std::string& name, const std::string& value) {
    return "\"" + name + "\":\"" + escape_json(value) + "\"";
}

std::string json_number_field(const std::string& name, std::uint64_t value) {
    return "\"" + name + "\":" + std::to_string(value);
}

std::string json_bool_field(const std::string& name, bool value) {
    return "\"" + name + "\":" + (value ? "true" : "false");
}

std::string json_type(const std::string& type) {
    return "{\"type\":\"" + type + "\"";
}

std::string finish_json(const std::string& value) {
    return value + "}";
}

} // namespace

std::string to_string(MessageType type) {
    switch (type) {
        case MessageType::Register:
            return "register";
        case MessageType::Login:
            return "login";
        case MessageType::Logout:
            return "logout";
        case MessageType::DirectChat:
            return "direct_chat";
        case MessageType::GroupChat:
            return "group_chat";
        case MessageType::CreateGroup:
            return "create_group";
        case MessageType::GroupJoin:
            return "group_join";
        case MessageType::GroupLeave:
            return "group_leave";
        case MessageType::HistoryQuery:
            return "history_query";
        case MessageType::UnreadQuery:
            return "unread_query";
        case MessageType::Ping:
            return "ping";
        case MessageType::System:
            return "system";
    }
    return "unknown";
}

bool parse_client_command(const std::string& line, ClientCommand& command, std::string& error) {
    try {
        std::string normalized = line;
        trim_trailing_cr(normalized);

        const auto object = parse_json_object(normalized);
        const auto type = get_string(object, "type");

        if (type == "register") {
            command.type = MessageType::Register;
            command.username = get_string(object, "username");
            command.password = get_string(object, "password");
            if (empty(command.username) || empty(command.password)) {
                error = "username and password are required";
                return false;
            }
            return true;
        }

        if (type == "login") {
            command.type = MessageType::Login;
            command.username = get_string(object, "username");
            command.password = get_string(object, "password");
            if (empty(command.username) || empty(command.password)) {
                error = "username and password are required";
                return false;
            }
            return true;
        }

        if (type == "logout") {
            command.type = MessageType::Logout;
            return true;
        }

        if (type == "ping") {
            command.type = MessageType::Ping;
            return true;
        }

        if (type == "dm") {
            command.type = MessageType::DirectChat;
            command.target_username = get_string(object, "to");
            command.body = get_string(object, "body");
            if (empty(command.target_username) || empty(command.body)) {
                error = "to and body are required";
                return false;
            }
            return true;
        }

        if (type == "create_group") {
            command.type = MessageType::CreateGroup;
            command.group_name = get_string(object, "name");
            if (empty(command.group_name)) {
                error = "group name is required";
                return false;
            }
            return true;
        }

        if (type == "join_group") {
            command.type = MessageType::GroupJoin;
            command.group_id = get_uint64(object, "group_id");
            command.receiver_id = command.group_id;
            return true;
        }

        if (type == "leave_group") {
            command.type = MessageType::GroupLeave;
            command.group_id = get_uint64(object, "group_id");
            command.receiver_id = command.group_id;
            return true;
        }

        if (type == "group_message") {
            command.type = MessageType::GroupChat;
            command.group_id = get_uint64(object, "group_id");
            command.receiver_id = command.group_id;
            command.body = get_string(object, "body");
            if (empty(command.body)) {
                error = "body is required";
                return false;
            }
            return true;
        }

        if (type == "history") {
            command.type = MessageType::HistoryQuery;
            command.target_username = get_string(object, "peer");
            command.limit = static_cast<std::uint32_t>(
                std::min<std::uint64_t>(get_uint64_or(object, "limit", 20), 100));
            command.before_id = get_uint64_or(object, "before_id", 0);
            if (empty(command.target_username)) {
                error = "peer is required";
                return false;
            }
            return true;
        }

        if (type == "group_history") {
            command.type = MessageType::HistoryQuery;
            command.group_history = true;
            command.group_id = get_uint64(object, "group_id");
            command.receiver_id = command.group_id;
            command.limit = static_cast<std::uint32_t>(
                std::min<std::uint64_t>(get_uint64_or(object, "limit", 20), 100));
            command.before_id = get_uint64_or(object, "before_id", 0);
            return true;
        }

        if (type == "unread") {
            command.type = MessageType::UnreadQuery;
            command.last_seen_message_id = get_uint64_or(object, "last_seen_message_id", 0);
            command.limit = static_cast<std::uint32_t>(
                std::min<std::uint64_t>(get_uint64_or(object, "limit", 20), 100));
            return true;
        }

        error = "unknown command";
        return false;
    } catch (const std::exception& ex) {
        error = ex.what();
        return false;
    }
}

std::string format_ok(const std::string& message) {
    return finish_json(json_type("ok") + "," + json_string_field("message", message));
}

std::string format_error(const std::string& message) {
    return finish_json(json_type("error") + "," + json_string_field("reason", message));
}

std::string format_register_success(std::uint64_t user_id) {
    return finish_json(json_type("register_success") + "," + json_number_field("user_id", user_id));
}

std::string format_register_failed(const std::string& reason) {
    return finish_json(json_type("register_failed") + "," + json_string_field("reason", reason));
}

std::string format_login_success(std::uint64_t user_id, const std::string& username) {
    return finish_json(json_type("login_success") + "," + json_number_field("user_id", user_id) +
                       "," + json_string_field("username", username));
}

std::string format_login_failed(const std::string& reason) {
    return finish_json(json_type("login_failed") + "," + json_string_field("reason", reason));
}

std::string format_create_group_success(std::uint64_t group_id, const std::string& name) {
    return finish_json(json_type("create_group_success") + "," +
                       json_number_field("group_id", group_id) + "," +
                       json_string_field("name", name));
}

std::string format_message_ack(std::uint64_t message_id, bool stored, bool delivered) {
    return finish_json(json_type("message_ack") + "," +
                       json_number_field("message_id", message_id) + "," +
                       json_string_field("status", stored ? "stored" : "failed") + "," +
                       json_bool_field("stored", stored) + "," +
                       json_bool_field("delivered", delivered));
}

std::string format_direct_message(const std::string& sender_username, const std::string& body) {
    return finish_json(json_type("dm") + "," + json_string_field("from", sender_username) +
                       "," + json_string_field("body", body));
}

std::string format_group_message(std::uint64_t group_id, const std::string& sender_username,
                                 const std::string& body) {
    return finish_json(json_type("group_message") + "," + json_number_field("group_id", group_id) +
                       "," + json_string_field("from", sender_username) +
                       "," + json_string_field("body", body));
}

std::string format_history_item(const std::string& chat_type,
                                const std::string& from,
                                const std::string& to,
                                const std::string& body) {
    return finish_json(json_type("history_item") + "," + json_string_field("chat_type", chat_type) +
                       "," + json_string_field("from", from) +
                       "," + json_string_field("to", to) +
                       "," + json_string_field("body", body));
}

std::string format_history_item(std::uint64_t message_id,
                                const std::string& chat_type,
                                const std::string& from,
                                const std::string& to,
                                const std::string& body,
                                const std::string& created_at) {
    return finish_json(json_type("history_item") + "," + json_number_field("message_id", message_id) +
                       "," + json_string_field("chat_type", chat_type) +
                       "," + json_string_field("from", from) +
                       "," + json_string_field("to", to) +
                       "," + json_string_field("body", body) +
                       "," + json_string_field("created_at", created_at));
}

std::string format_history_end() {
    return finish_json(json_type("history_end"));
}

std::string format_history_end(bool has_more, std::uint64_t next_before_id) {
    return finish_json(json_type("history_end") + "," + json_bool_field("has_more", has_more) +
                       "," + json_number_field("next_before_id", next_before_id));
}

std::string format_unread_end(bool has_more, std::uint64_t next_last_seen_message_id) {
    return finish_json(json_type("unread_end") + "," + json_bool_field("has_more", has_more) +
                       "," + json_number_field("next_last_seen_message_id", next_last_seen_message_id));
}

std::string format_pong() {
    return finish_json(json_type("pong"));
}

} // namespace cpp_chat::protocol
