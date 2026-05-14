#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace cpp_chat::network {

constexpr std::size_t kPacketHeaderSize = 4;
constexpr std::size_t kMaxPacketPayloadSize = 1024 * 1024;

struct Packet {
    std::string payload;
};

enum class RecvPacketStatus {
    Ok,
    WouldBlock,
    Closed,
    ProtocolError,
    IoError
};

struct RecvPacketResult {
    RecvPacketStatus status = RecvPacketStatus::WouldBlock;
    std::vector<Packet> packets;
    int error_code = 0;
};

enum class SendStatus {
    Ok,
    WouldBlock,
    IoError
};

struct SendResult {
    SendStatus status = SendStatus::Ok;
    std::size_t sent = 0;
    int error_code = 0;
};

std::string encode_packet(std::string_view payload);

RecvPacketResult recv_packets_et(int fd,
                                 std::string& read_buffer,
                                 std::size_t max_payload_size = kMaxPacketPayloadSize);

SendResult send_all(int fd, const char* data, std::size_t size);

} // namespace cpp_chat::network
