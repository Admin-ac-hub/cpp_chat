# 使用 Ubuntu 作为构建和运行基础镜像，保证 epoll/Linux 网络 API 可用。
FROM ubuntu:24.04

# 禁用 apt 交互式提示，适合 Docker 构建环境。
ENV DEBIAN_FRONTEND=noninteractive

# 安装 C++ 编译工具、CMake、pkg-config、MySQL 客户端开发库和 GoogleTest。
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        cmake \
        libmysqlclient-dev \
        libgtest-dev \
        pkg-config \
    && rm -rf /var/lib/apt/lists/*

# 后续构建命令都在容器内 /app 目录执行。
WORKDIR /app

# 复制完整源码、配置、SQL 和构建脚本到镜像中。
COPY . .

# 编译所有目标（服务端 + 测试），然后立即运行单元测试。
# 测试失败会导致 docker build 报错，确保只有测试全绿的镜像才能继续。
RUN cmake -S . -B build \
    && cmake --build build \
    && ./build/cpp_chat_tests

# 聊天 TCP 服务默认监听 9000 端口。
EXPOSE 9000

# 容器启动时运行编译好的服务端程序。
CMD ["./build/cpp_chat_server"]
