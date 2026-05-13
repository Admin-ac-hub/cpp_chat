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

// 将 errno 转成带上下文的日志文本，便于定位失败的系统调用。
std::string last_error(const std::string& action) {
    return action + ": " + std::strerror(errno);
}

// 将 fd 设置为非阻塞模式。
// epoll 事件循环依赖非阻塞 I/O，否则单个慢连接可能卡住整个线程。
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
                     core::ThreadPool& thread_pool,
                     logging::Logger& logger)
    : config_(config), chat_service_(chat_service), thread_pool_(thread_pool), logger_(logger) {}

TcpServer::~TcpServer() {
    // 析构时兜底释放系统资源，允许 start 失败后安全退出。
    stop();
}

void TcpServer::start() {
    // 监听初始化失败时立即清理已创建资源，避免 fd 泄漏。
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
    // had_resources 用来避免重复 stop 时写出误导性的停止日志。
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
    // 创建 IPv4 TCP socket；后续 bind 到配置中的 host/port。
    listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd_ == -1) {
        logger_.error(last_error("socket failed"));
        return false;
    }

    const int enabled = 1;
    // 允许服务重启后快速复用端口，避免 TIME_WAIT 导致 bind 失败。
    if (setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled)) == -1) {
        logger_.error(last_error("setsockopt(SO_REUSEADDR) failed"));
        return false;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(config_.port);
    // 配置中使用字符串形式 IPv4 地址，这里转换成 sockaddr_in 需要的网络字节序地址。
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

    // EPOLL_CLOEXEC 避免未来 fork/exec 子进程时泄漏 epoll fd。
    epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
    if (epoll_fd_ == -1) {
        logger_.error(last_error("epoll_create1 failed"));
        return false;
    }

    epoll_event event{};
    event.events = EPOLLIN;
    event.data.fd = listen_fd_;
    // 监听 fd 可读表示有新连接到来。
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, listen_fd_, &event) == -1) {
        logger_.error(last_error("epoll_ctl listen fd failed"));
        return false;
    }

    return true;
}

bool TcpServer::run_event_loop() {
    epoll_event events[kMaxEvents];

    while (running_) {
        // 用心跳检查间隔作为超时，定时醒来扫描空闲连接。
        const int timeout_ms = config_.heartbeat_interval_seconds * 1000;
        const int ready_count = epoll_wait(epoll_fd_, events, kMaxEvents, timeout_ms);
        if (ready_count == -1) {
            if (errno == EINTR) {
                continue;
            }
            logger_.error(last_error("epoll_wait failed"));
            return false;
        }

        if (ready_count == 0) {
            check_heartbeats();
            drain_responses();
            continue;
        }

        for (int i = 0; i < ready_count; ++i) {
            const int fd = events[i].data.fd;
            if (fd == listen_fd_) {
                // 监听 fd 的读事件代表有一个或多个连接等待 accept。
                accept_connections();
                continue;
            }

            if ((events[i].events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0) {
                // 错误、挂起或对端半关闭都按断开处理，清理业务会话和 fd。
                close_client(fd);
                continue;
            }

            if ((events[i].events & EPOLLIN) != 0) {
                handle_client_read(fd);
            }

            if ((events[i].events & EPOLLOUT) != 0) {
                flush_write_buffer(fd);
            }
        }

        // 每轮事件处理完毕后，将工作线程产生的响应写回客户端。
        drain_responses();
    }

    return true;
}

void TcpServer::accept_connections() {
    while (true) {
        sockaddr_in client_address{};
        socklen_t client_address_len = sizeof(client_address);
        // accept4 直接把客户端 fd 设置成非阻塞并加 close-on-exec，少一次 fcntl。
        const int client_fd = accept4(
            listen_fd_,
            reinterpret_cast<sockaddr*>(&client_address),
            &client_address_len,
            SOCK_NONBLOCK | SOCK_CLOEXEC);

        if (client_fd == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 所有当前排队连接都接收完毕。
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

        last_activity_[client_fd] = std::time(nullptr);
    }
}

void TcpServer::handle_client_read(int client_fd) {
    char buffer[kBufferSize];

    while (true) {
        // 非阻塞 recv 循环读取，直到内核缓冲区暂时无数据。
        const ssize_t bytes_read = recv(client_fd, buffer, sizeof(buffer), 0);
        if (bytes_read == 0) {
            // recv 返回 0 表示对端正常关闭连接。
            close_client(client_fd);
            return;
        }

        if (bytes_read == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 当前可读数据已经全部取完，等待下一次 EPOLLIN。
                return;
            }
            logger_.warn(last_error("recv failed"));
            close_client(client_fd);
            return;
        }

        logger_.info("received " + std::to_string(bytes_read) +
                     " bytes from fd=" + std::to_string(client_fd));
        // 更新心跳检查时间戳，防止被标记为空闲连接。
        last_activity_[client_fd] = std::time(nullptr);

        auto& read_buffer = read_buffers_[client_fd];
        read_buffer.append(buffer, static_cast<std::size_t>(bytes_read));

        // TCP 不保留消息边界，所以把累计缓冲按换行拆成完整命令。
        // 最后一段没有换行的内容继续留在 read_buffer 里等待后续数据。
        std::vector<std::string> lines;
        std::size_t newline_pos = read_buffer.find('\n');
        while (newline_pos != std::string::npos) {
            lines.push_back(read_buffer.substr(0, newline_pos));
            read_buffer.erase(0, newline_pos + 1);
            newline_pos = read_buffer.find('\n');
        }

        // 将完整命令行批量提交到线程池，同一批内的命令保证顺序执行。
        // 跨批次的命令可能并发，但业务层通过 SessionManager 的互斥量保证一致性。
        if (!lines.empty()) {
            thread_pool_.enqueue([this, client_fd, lines = std::move(lines)]() {
                for (const auto& line : lines) {
                    process_line_async(client_fd, line);
                }
            });
        }
    }
}

void TcpServer::process_line_async(int client_fd, const std::string& line) {
    const auto outbound_messages = chat_service_.handle_client_line(
        static_cast<ConnectionId>(client_fd),
        line);

    // 业务层响应推入线程安全队列，由网络线程统一写回。
    std::lock_guard<std::mutex> lock(response_mutex_);
    for (const auto& message : outbound_messages) {
        response_queue_.push_back({
            static_cast<int>(message.connection_id),
            message.data,
            message.close_after_send,
        });
    }
}

void TcpServer::drain_responses() {
    std::vector<QueuedResponse> pending;
    {
        std::lock_guard<std::mutex> lock(response_mutex_);
        pending.swap(response_queue_);
    }

    for (const auto& response : pending) {
        // 连接可能在业务处理期间被关闭，跳过已失效的 fd。
        if (last_activity_.find(response.fd) == last_activity_.end()) {
            continue;
        }
        if (!send_to_client(response.fd, response.data)) {
            close_client(response.fd);
            continue;
        }
        if (response.close_after_send) {
            if (write_buffers_.find(response.fd) == write_buffers_.end()) {
                close_client(response.fd);
            } else {
                close_after_write_.insert(response.fd);
            }
        }
    }
}

bool TcpServer::send_to_client(int client_fd, const std::string& data) {
    // 若已有未发完数据，直接追加到写缓冲，保持发送顺序。
    auto& write_buffer = write_buffers_[client_fd];
    if (!write_buffer.empty()) {
        write_buffer += data;
        return true;
    }

    std::size_t total_sent = 0;
    while (total_sent < data.size()) {
        const ssize_t bytes_sent = send(
            client_fd,
            data.data() + total_sent,
            data.size() - total_sent,
            MSG_NOSIGNAL);

        if (bytes_sent == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 内核发送缓冲区满，将剩余数据写入应用层缓冲并监听可写事件。
                write_buffer = data.substr(total_sent);
                set_write_mode(client_fd, true);
                return true;
            }

            logger_.warn(last_error("send failed"));
            return false;
        }

        total_sent += static_cast<std::size_t>(bytes_sent);
    }

    return true;
}

void TcpServer::flush_write_buffer(int client_fd) {
    auto it = write_buffers_.find(client_fd);
    if (it == write_buffers_.end() || it->second.empty()) {
        return;
    }

    auto& buffer = it->second;
    std::size_t total_sent = 0;
    while (total_sent < buffer.size()) {
        const ssize_t bytes_sent = send(
            client_fd,
            buffer.data() + total_sent,
            buffer.size() - total_sent,
            MSG_NOSIGNAL);

        if (bytes_sent == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 仍未可写，丢弃已发送前缀，保留剩余数据等待下次 EPOLLOUT。
                buffer.erase(0, total_sent);
                return;
            }

            logger_.warn(last_error("send failed during flush"));
            close_client(client_fd);
            return;
        }

        total_sent += static_cast<std::size_t>(bytes_sent);
    }

    // 全部发送完毕，清除缓冲并取消 EPOLLOUT 监听。
    write_buffers_.erase(it);
    set_write_mode(client_fd, false);
    if (close_after_write_.erase(client_fd) > 0) {
        close_client(client_fd);
    }
}

bool TcpServer::set_write_mode(int client_fd, bool enable) {
    epoll_event event{};
    event.data.fd = client_fd;
    event.events = EPOLLIN | EPOLLRDHUP;
    if (enable) {
        event.events |= EPOLLOUT;
    }

    if (epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, client_fd, &event) == -1) {
        logger_.warn(last_error("epoll_ctl MOD failed"));
        return false;
    }

    return true;
}

void TcpServer::close_client(int client_fd) {
    // 先通知业务层清理会话，再释放网络层保存的半包缓冲、写缓冲和心跳时间戳。
    chat_service_.handle_disconnect(static_cast<ConnectionId>(client_fd));
    read_buffers_.erase(client_fd);
    write_buffers_.erase(client_fd);
    last_activity_.erase(client_fd);
    close_after_write_.erase(client_fd);

    // 即使 fd 已经异常，删除 epoll 监听失败也不影响后续 close，因此忽略返回值。
    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, client_fd, nullptr);
    close(client_fd);
    logger_.info("client disconnected fd=" + std::to_string(client_fd));
}

void TcpServer::check_heartbeats() {
    const auto now = std::time(nullptr);
    const auto timeout = static_cast<std::time_t>(config_.heartbeat_timeout_seconds);

    // 收集超时 fd 后再关闭，避免在遍历中修改 map。
    std::vector<int> timed_out;
    for (const auto& [fd, last_time] : last_activity_) {
        if (now - last_time > timeout) {
            timed_out.push_back(fd);
        }
    }

    for (const int fd : timed_out) {
        logger_.info("heartbeat timeout fd=" + std::to_string(fd) +
                     " closing connection");
        close_client(fd);
    }
}

} // namespace cpp_chat::network
