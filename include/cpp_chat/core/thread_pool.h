#pragma once

#include <cstddef>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace cpp_chat::core {

// 固定大小的任务线程池，用于将阻塞的业务逻辑从网络 I/O 线程中剥离。
//
// 网络线程调用 enqueue 提交任务，空闲工作线程取出并执行。
// 析构时自动等待所有工作线程退出。
class ThreadPool {
public:
    explicit ThreadPool(std::size_t num_threads, std::size_t max_queue_size = 10000);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // 提交一个可调用对象到任务队列。
    // 如果线程池已停止或队列已满，返回 false，调用方负责限流处理。
    template <typename F>
    bool enqueue(F&& task);

    // 返回工作线程数量。
    std::size_t size() const { return workers_.size(); }
    std::size_t queued_tasks() const;
    std::size_t max_queue_size() const { return max_queue_size_; }

private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::size_t max_queue_size_ = 10000;
    bool stop_ = false;
};

template <typename F>
bool ThreadPool::enqueue(F&& task) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stop_ || tasks_.size() >= max_queue_size_) {
            return false;
        }
        tasks_.emplace(std::forward<F>(task));
    }
    cv_.notify_one();
    return true;
}

} // namespace cpp_chat::core
