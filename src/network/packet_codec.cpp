#include "cpp_chat/network/packet_codec.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <sys/socket.h>

namespace cpp_chat::network {

namespace {

constexpr std::size_t kReadChunkSize = 4096;

} // namespace

std::string encode_packet(std::string_view payload) {
    if (payload.empty() || payload.size() > kMaxPacketPayloadSize) {
        throw std::runtime_error("payload size is invalid");
    }

    const auto payload_size = static_cast<std::uint32_t>(payload.size());
    const std::uint32_t net_length = htonl(payload_size);

    std::string packet;
    packet.resize(kPacketHeaderSize + payload.size());
    std::memcpy(packet.data(), &net_length, kPacketHeaderSize);
    std::memcpy(packet.data() + kPacketHeaderSize, payload.data(), payload.size());
    return packet;
}

RecvPacketResult recv_packets_et(int fd,
                                 std::string& read_buffer,
                                 std::size_t max_payload_size) {
    RecvPacketResult result;
    char temp[kReadChunkSize];

    while (true) {
        const ssize_t bytes_read = ::recv(fd, temp, sizeof(temp), 0);
        if (bytes_read > 0) {
            read_buffer.append(temp, static_cast<std::size_t>(bytes_read));
            continue;
        }

        if (bytes_read == 0) {
            result.status = RecvPacketStatus::Closed;
            return result;
        }

        if (errno == EINTR) {
            continue;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        }

        result.status = RecvPacketStatus::IoError;
        result.error_code = errno;
        return result;
    }

    while (read_buffer.size() >= kPacketHeaderSize) {
        std::uint32_t net_length = 0;
        std::memcpy(&net_length, read_buffer.data(), kPacketHeaderSize);

        const std::uint32_t payload_length = ntohl(net_length);
        if (payload_length == 0 || payload_length > max_payload_size) {
            result.status = RecvPacketStatus::ProtocolError;
            return result;
        }

        const std::size_t frame_size = kPacketHeaderSize + payload_length;
        if (read_buffer.size() < frame_size) {
            break;
        }

        Packet packet;
        packet.payload.assign(read_buffer.data() + kPacketHeaderSize, payload_length);
        result.packets.push_back(std::move(packet));
        read_buffer.erase(0, frame_size);
    }

    result.status = result.packets.empty() ? RecvPacketStatus::WouldBlock : RecvPacketStatus::Ok;
    return result;
}

SendResult send_all(int fd, const char* data, std::size_t size) {
    SendResult result;

    while (result.sent < size) {
        const ssize_t bytes_sent = ::send(
            fd,
            data + result.sent,
            size - result.sent,
            MSG_NOSIGNAL);

        if (bytes_sent > 0) {
            result.sent += static_cast<std::size_t>(bytes_sent);
            continue;
        }

        if (bytes_sent == -1 && errno == EINTR) {
            continue;
        }

        if (bytes_sent == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            result.status = SendStatus::WouldBlock;
            return result;
        }

        result.status = SendStatus::IoError;
        result.error_code = errno;
        return result;
    }

    result.status = SendStatus::Ok;
    return result;
}

} // namespace cpp_chat::network
