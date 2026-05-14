#include "cpp_chat/network/packet_codec.h"

#include <arpa/inet.h>
#include <cstring>
#include <fcntl.h>
#include <gtest/gtest.h>
#include <stdexcept>
#include <sys/socket.h>
#include <unistd.h>

namespace cpp_chat::network {

namespace {

class SocketPair {
public:
    SocketPair() {
        EXPECT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, fds_), 0);
        set_non_blocking(fds_[0]);
        set_non_blocking(fds_[1]);
    }

    ~SocketPair() {
        if (fds_[0] != -1) {
            close(fds_[0]);
        }
        if (fds_[1] != -1) {
            close(fds_[1]);
        }
    }

    int reader() const {
        return fds_[0];
    }

    int writer() const {
        return fds_[1];
    }

    void close_writer() {
        close(fds_[1]);
        fds_[1] = -1;
    }

private:
    static void set_non_blocking(int fd) {
        const int flags = fcntl(fd, F_GETFL, 0);
        ASSERT_NE(flags, -1);
        ASSERT_NE(fcntl(fd, F_SETFL, flags | O_NONBLOCK), -1);
    }

    int fds_[2] = {-1, -1};
};

void write_all_blocking(int fd, const std::string& data) {
    std::size_t sent = 0;
    while (sent < data.size()) {
        const ssize_t n = write(fd, data.data() + sent, data.size() - sent);
        ASSERT_GT(n, 0);
        sent += static_cast<std::size_t>(n);
    }
}

std::string make_header(std::uint32_t length) {
    const std::uint32_t net_length = htonl(length);
    std::string header(kPacketHeaderSize, '\0');
    std::memcpy(header.data(), &net_length, kPacketHeaderSize);
    return header;
}

} // namespace

TEST(PacketCodec, EncodePacketUsesNetworkByteOrderAndPreservesPayload) {
    const std::string payload = R"({"type":"ping"})";
    const auto packet = encode_packet(payload);

    ASSERT_EQ(packet.size(), kPacketHeaderSize + payload.size());

    std::uint32_t net_length = 0;
    std::memcpy(&net_length, packet.data(), kPacketHeaderSize);
    EXPECT_EQ(ntohl(net_length), payload.size());
    EXPECT_EQ(packet.substr(kPacketHeaderSize), payload);
}

TEST(PacketCodec, EncodeRejectsEmptyAndTooLargePayload) {
    EXPECT_THROW(encode_packet(""), std::runtime_error);

    const std::string too_large(kMaxPacketPayloadSize + 1, 'x');
    EXPECT_THROW(encode_packet(too_large), std::runtime_error);
}

TEST(PacketCodec, HalfHeaderProducesNoPacket) {
    SocketPair sockets;
    std::string read_buffer;

    const auto packet = encode_packet(R"({"type":"ping"})");
    write_all_blocking(sockets.writer(), packet.substr(0, 2));

    const auto result = recv_packets_et(sockets.reader(), read_buffer);
    EXPECT_EQ(result.status, RecvPacketStatus::WouldBlock);
    EXPECT_TRUE(result.packets.empty());
    EXPECT_EQ(read_buffer.size(), 2u);
}

TEST(PacketCodec, FullHeaderWithHalfPayloadProducesNoPacket) {
    SocketPair sockets;
    std::string read_buffer;

    const std::string payload = R"({"type":"ping"})";
    const auto packet = encode_packet(payload);
    write_all_blocking(sockets.writer(), packet.substr(0, kPacketHeaderSize + 3));

    const auto result = recv_packets_et(sockets.reader(), read_buffer);
    EXPECT_EQ(result.status, RecvPacketStatus::WouldBlock);
    EXPECT_TRUE(result.packets.empty());
    EXPECT_EQ(read_buffer.size(), kPacketHeaderSize + 3);
}

TEST(PacketCodec, StickyPacketsProduceMultiplePayloads) {
    SocketPair sockets;
    std::string read_buffer;

    const auto first = encode_packet(R"({"type":"ping"})");
    const auto second = encode_packet(R"({"type":"logout"})");
    write_all_blocking(sockets.writer(), first + second);

    const auto result = recv_packets_et(sockets.reader(), read_buffer);
    ASSERT_EQ(result.status, RecvPacketStatus::Ok);
    ASSERT_EQ(result.packets.size(), 2u);
    EXPECT_EQ(result.packets[0].payload, R"({"type":"ping"})");
    EXPECT_EQ(result.packets[1].payload, R"({"type":"logout"})");
    EXPECT_TRUE(read_buffer.empty());
}

TEST(PacketCodec, CompletePacketAndNextHalfPacketKeepsRemainder) {
    SocketPair sockets;
    std::string read_buffer;

    const auto first = encode_packet(R"({"type":"ping"})");
    const auto second = encode_packet(R"({"type":"logout"})");
    write_all_blocking(sockets.writer(), first + second.substr(0, kPacketHeaderSize + 2));

    const auto result = recv_packets_et(sockets.reader(), read_buffer);
    ASSERT_EQ(result.status, RecvPacketStatus::Ok);
    ASSERT_EQ(result.packets.size(), 1u);
    EXPECT_EQ(result.packets[0].payload, R"({"type":"ping"})");
    EXPECT_EQ(read_buffer.size(), kPacketHeaderSize + 2);
}

TEST(PacketCodec, ZeroLengthIsProtocolError) {
    SocketPair sockets;
    std::string read_buffer;

    write_all_blocking(sockets.writer(), make_header(0));

    const auto result = recv_packets_et(sockets.reader(), read_buffer);
    EXPECT_EQ(result.status, RecvPacketStatus::ProtocolError);
}

TEST(PacketCodec, OversizedLengthIsProtocolError) {
    SocketPair sockets;
    std::string read_buffer;

    write_all_blocking(sockets.writer(), make_header(kMaxPacketPayloadSize + 1));

    const auto result = recv_packets_et(sockets.reader(), read_buffer);
    EXPECT_EQ(result.status, RecvPacketStatus::ProtocolError);
}

TEST(PacketCodec, PeerCloseReturnsClosed) {
    SocketPair sockets;
    std::string read_buffer;

    sockets.close_writer();

    const auto result = recv_packets_et(sockets.reader(), read_buffer);
    EXPECT_EQ(result.status, RecvPacketStatus::Closed);
}

TEST(PacketCodec, SendAllWritesPacketBytes) {
    SocketPair sockets;
    const auto packet = encode_packet(R"({"type":"ping"})");

    const auto send_result = send_all(sockets.writer(), packet.data(), packet.size());
    ASSERT_EQ(send_result.status, SendStatus::Ok);
    EXPECT_EQ(send_result.sent, packet.size());

    std::string read_buffer;
    const auto recv_result = recv_packets_et(sockets.reader(), read_buffer);
    ASSERT_EQ(recv_result.status, RecvPacketStatus::Ok);
    ASSERT_EQ(recv_result.packets.size(), 1u);
    EXPECT_EQ(recv_result.packets[0].payload, R"({"type":"ping"})");
}

} // namespace cpp_chat::network
