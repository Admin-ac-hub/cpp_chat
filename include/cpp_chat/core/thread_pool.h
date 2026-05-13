#pragma once

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
    explicit ThreadPool(std::size_t num_threads);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // 提交一个可调用对象到任务队列。
    // 如果线程池已停止，任务不会被提交（静默丢弃）。
    template <typename F>
    void enqueue(F&& task);

    // 返回工作线程数量。
    std::size_t size() const { return workers_.size(); }

private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool stop_ = false;
};

template <typename F>
void ThreadPool::enqueue(F&& task) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stop_) return;
        tasks_.emplace(std::forward<F>(task));
    }
    cv_.notify_one();
}

} // namespace cpp_chat::core
