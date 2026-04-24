#include "cpp_chat/network/tcp_server.h"

#include "cpp_chat/chat/chat_service.h"

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <vector>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace cpp_chat::network {

namespace {

constexpr int kBacklog = 128;
constexpr int kMaxEvents = 64;
constexpr int kBufferSize = 4096;

std::string last_error(const std::string& action) {
    return action + ": " + std::strerror(errno);
}

bool set_non_blocking(int fd, logging::Logger& logger) {
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        logger.error(last_error("fcntl(F_GETFL) failed"));
        return false;
    }

    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        logger.error(last_error("fcntl(F_SETFL) failed"));
        return false;
    }

    return true;
}

} // namespace

TcpServer::TcpServer(const core::ServerConfig& config,
                     chat::ChatService& chat_service,
                     logging::Logger& logger)
    : config_(config), chat_service_(chat_service), logger_(logger) {}

TcpServer::~TcpServer() {
    stop();
}

void TcpServer::start() {
    if (!setup_listener()) {
        stop();
        return;
    }

    logger_.info("tcp server listening on " + config_.host + ":" + std::to_string(config_.port));
    running_ = true;
    run_event_loop();
    stop();
}

void TcpServer::stop() {
    const bool had_resources = running_ || epoll_fd_ != -1 || listen_fd_ != -1;

    running_ = false;

    if (epoll_fd_ != -1) {
        close(epoll_fd_);
        epoll_fd_ = -1;
    }

    if (listen_fd_ != -1) {
        close(listen_fd_);
        listen_fd_ = -1;
    }

    if (had_resources) {
        logger_.info("tcp server stopped");
    }
}

bool TcpServer::setup_listener() {
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ == -1) {
        logger_.error(last_error("socket failed"));
        return false;
    }

    const int enabled = 1;
    if (setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled)) == -1) {
        logger_.error(last_error("setsockopt(SO_REUSEADDR) failed"));
        return false;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(config_.port);
    if (inet_pton(AF_INET, config_.host.c_str(), &address.sin_addr) != 1) {
        logger_.error("invalid listen host: " + config_.host);
        return false;
    }

    if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == -1) {
        logger_.error(last_error("bind failed"));
        return false;
    }

    if (listen(listen_fd_, kBacklog) == -1) {
        logger_.error(last_error("listen failed"));
        return false;
    }

    if (!set_non_blocking(listen_fd_, logger_)) {
        return false;
    }

    epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd_ == -1) {
        logger_.error(last_error("epoll_create1 failed"));
        return false;
    }

    epoll_event event{};
    event.events = EPOLLIN;
    event.data.fd = listen_fd_;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, listen_fd_, &event) == -1) {
        logger_.error(last_error("epoll_ctl listen fd failed"));
        return false;
    }

    return true;
}

bool TcpServer::run_event_loop() {
    epoll_event events[kMaxEvents];

    while (running_) {
        const int ready_count = epoll_wait(epoll_fd_, events, kMaxEvents, -1);
        if (ready_count == -1) {
            if (errno == EINTR) {
                continue;
            }
            logger_.error(last_error("epoll_wait failed"));
            return false;
        }

        for (int i = 0; i < ready_count; ++i) {
            const int fd = events[i].data.fd;
            if (fd == listen_fd_) {
                accept_connections();
                continue;
            }

            if ((events[i].events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0) {
                close_client(fd);
                continue;
            }

            if ((events[i].events & EPOLLIN) != 0) {
                handle_client_read(fd);
            }
        }
    }

    return true;
}

void TcpServer::accept_connections() {
    while (true) {
        sockaddr_in client_address{};
        socklen_t client_address_len = sizeof(client_address);
        const int client_fd = accept4(
            listen_fd_,
            reinterpret_cast<sockaddr*>(&client_address),
            &client_address_len,
            SOCK_NONBLOCK | SOCK_CLOEXEC);

        if (client_fd == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return;
            }
            logger_.warn(last_error("accept4 failed"));
            return;
        }

        char peer_ip[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, &client_address.sin_addr, peer_ip, sizeof(peer_ip));
        logger_.info("client connected fd=" + std::to_string(client_fd) +
                     " peer=" + std::string(peer_ip) + ":" +
                     std::to_string(ntohs(client_address.sin_port)));

        epoll_event event{};
        event.events = EPOLLIN | EPOLLRDHUP;
        event.data.fd = client_fd;
        if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, client_fd, &event) == -1) {
            logger_.warn(last_error("epoll_ctl client fd failed"));
            close(client_fd);
            continue;
        }
    }
}

void TcpServer::handle_client_read(int client_fd) {
    char buffer[kBufferSize];

    while (true) {
        const ssize_t bytes_read = recv(client_fd, buffer, sizeof(buffer), 0);
        if (bytes_read == 0) {
            close_client(client_fd);
            return;
        }

        if (bytes_read == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                return;
            }
            logger_.warn(last_error("recv failed"));
            close_client(client_fd);
            return;
        }

        logger_.info("received " + std::to_string(bytes_read) +
                     " bytes from fd=" + std::to_string(client_fd));

        auto& read_buffer = read_buffers_[client_fd];
        read_buffer.append(buffer, static_cast<std::size_t>(bytes_read));

        std::vector<std::string> lines;
        std::size_t newline_pos = read_buffer.find('\n');
        while (newline_pos != std::string::npos) {
            lines.push_back(read_buffer.substr(0, newline_pos));
            read_buffer.erase(0, newline_pos + 1);
            newline_pos = read_buffer.find('\n');
        }

        for (const auto& line : lines) {
            if (!handle_client_line(client_fd, line)) {
                return;
            }
        }
    }
}

bool TcpServer::handle_client_line(int client_fd, const std::string& line) {
    const auto outbound_messages = chat_service_.handle_client_line(
        static_cast<ConnectionId>(client_fd),
        line);

    for (const auto& message : outbound_messages) {
        const auto target_fd = static_cast<int>(message.connection_id);
        if (!send_to_client(target_fd, message.data)) {
            close_client(target_fd);
            if (target_fd == client_fd) {
                return false;
            }
        }
    }

    return true;
}

bool TcpServer::send_to_client(int client_fd, const std::string& data) {
    std::size_t total_sent = 0;
    while (total_sent < data.size()) {
        const ssize_t bytes_sent = send(
            client_fd,
            data.data() + total_sent,
            data.size() - total_sent,
            MSG_NOSIGNAL);

        if (bytes_sent == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                logger_.warn("send would block fd=" + std::to_string(client_fd));
                return false;
            }

            logger_.warn(last_error("send failed"));
            return false;
        }

        total_sent += static_cast<std::size_t>(bytes_sent);
    }

    return true;
}

void TcpServer::close_client(int client_fd) {
    chat_service_.handle_disconnect(static_cast<ConnectionId>(client_fd));
    read_buffers_.erase(client_fd);
    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, client_fd, nullptr);
    close(client_fd);
    logger_.info("client disconnected fd=" + std::to_string(client_fd));
}

} // namespace cpp_chat::network
