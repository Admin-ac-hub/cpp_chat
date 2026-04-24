#include "cpp_chat/network/tcp_server.h"

#ifdef __linux__
#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <string>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace cpp_chat::network {

#ifdef __linux__
namespace {

constexpr int kBacklog = 128;
constexpr int kMaxEvents = 64;
constexpr int kBufferSize = 4096;

std::string last_error(const std::string& action) {
    return action + ": " + std::strerror(errno);
}

void set_non_blocking(int fd) {
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        throw std::runtime_error(last_error("fcntl(F_GETFL) failed"));
    }

    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        throw std::runtime_error(last_error("fcntl(F_SETFL) failed"));
    }
}

} // namespace
#endif

TcpServer::TcpServer(const core::ServerConfig& config, logging::Logger& logger)
    : config_(config), logger_(logger) {}

TcpServer::~TcpServer() {
    stop();
}

void TcpServer::start() {
#ifndef __linux__
    logger_.error("epoll server requires Linux; current platform does not provide epoll");
#else
    running_ = true;
    setup_listener();
    logger_.info("tcp server listening on " + config_.host + ":" + std::to_string(config_.port));
    run_event_loop();
#endif
}

void TcpServer::stop() {
#ifdef __linux__
    const bool had_resources = running_ || epoll_fd_ != -1 || listen_fd_ != -1;
#else
    const bool had_resources = running_;
#endif

    running_ = false;

#ifdef __linux__
    if (epoll_fd_ != -1) {
        close(epoll_fd_);
        epoll_fd_ = -1;
    }

    if (listen_fd_ != -1) {
        close(listen_fd_);
        listen_fd_ = -1;
    }
#endif

    if (had_resources) {
        logger_.info("tcp server stopped");
    }
}

#ifdef __linux__
void TcpServer::setup_listener() {
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ == -1) {
        throw std::runtime_error(last_error("socket failed"));
    }

    const int enabled = 1;
    if (setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled)) == -1) {
        throw std::runtime_error(last_error("setsockopt(SO_REUSEADDR) failed"));
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(config_.port);
    if (inet_pton(AF_INET, config_.host.c_str(), &address.sin_addr) != 1) {
        throw std::runtime_error("invalid listen host: " + config_.host);
    }

    if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == -1) {
        throw std::runtime_error(last_error("bind failed"));
    }

    if (listen(listen_fd_, kBacklog) == -1) {
        throw std::runtime_error(last_error("listen failed"));
    }

    set_non_blocking(listen_fd_);

    epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd_ == -1) {
        throw std::runtime_error(last_error("epoll_create1 failed"));
    }

    epoll_event event{};
    event.events = EPOLLIN;
    event.data.fd = listen_fd_;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, listen_fd_, &event) == -1) {
        throw std::runtime_error(last_error("epoll_ctl listen fd failed"));
    }
}

void TcpServer::run_event_loop() {
    epoll_event events[kMaxEvents];

    while (running_) {
        const int ready_count = epoll_wait(epoll_fd_, events, kMaxEvents, -1);
        if (ready_count == -1) {
            if (errno == EINTR) {
                continue;
            }
            throw std::runtime_error(last_error("epoll_wait failed"));
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
            throw std::runtime_error(last_error("accept4 failed"));
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
            close(client_fd);
            throw std::runtime_error(last_error("epoll_ctl client fd failed"));
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

        const ssize_t bytes_sent = send(client_fd, buffer, static_cast<std::size_t>(bytes_read), MSG_NOSIGNAL);
        if (bytes_sent == -1) {
            logger_.warn(last_error("send failed"));
            close_client(client_fd);
            return;
        }
    }
}

void TcpServer::close_client(int client_fd) {
    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, client_fd, nullptr);
    close(client_fd);
    logger_.info("client disconnected fd=" + std::to_string(client_fd));
}
#endif

} // namespace cpp_chat::network
