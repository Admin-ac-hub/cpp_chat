#include "cpp_chat/core/thread_pool.h"

#include <atomic>
#include <chrono>
#include <future>
#include <gtest/gtest.h>
#include <thread>

namespace cpp_chat::core {

TEST(ThreadPool, EnqueueRunsTask) {
    ThreadPool pool(1, 4);
    std::promise<int> promise;
    auto future = promise.get_future();

    ASSERT_TRUE(pool.enqueue([&promise]() {
        promise.set_value(42);
    }));

    ASSERT_EQ(future.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    EXPECT_EQ(future.get(), 42);
}

TEST(ThreadPool, EnqueueReturnsFalseWhenQueueIsFull) {
    ThreadPool pool(0, 1);

    EXPECT_TRUE(pool.enqueue([]() {}));
    EXPECT_FALSE(pool.enqueue([]() {}));
    EXPECT_EQ(pool.queued_tasks(), 1u);
    EXPECT_EQ(pool.max_queue_size(), 1u);
}

TEST(ThreadPool, DestructorWaitsForRunningTasks) {
    std::atomic<bool> ran{false};
    {
        ThreadPool pool(1, 2);
        ASSERT_TRUE(pool.enqueue([&ran]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            ran = true;
        }));
    }

    EXPECT_TRUE(ran.load());
}

} // namespace cpp_chat::core
