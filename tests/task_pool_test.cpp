#include <gtest/gtest.h>
#include <async_toolkit/task_pool.hpp>
#include <chrono>
#include <string>

using namespace async_toolkit;
using namespace std::chrono_literals;

TEST(TaskPool, BasicTaskExecution) {
    TaskPool pool(4);
    
    auto future = pool.submit([]() { return 42; });
    EXPECT_EQ(future.get(), 42);
}

TEST(TaskPool, MultipleTasksExecution) {
    TaskPool pool(4);
    std::vector<std::future<int>> futures;
    
    for(int i = 0; i < 100; ++i) {
        futures.push_back(pool.submit([i]() { return i * 2; }));
    }
    
    for(int i = 0; i < 100; ++i) {
        EXPECT_EQ(futures[i].get(), i * 2);
    }
}

TEST(TaskPool, TaskWithArguments) {
    TaskPool pool(4);
    
    auto future = pool.submit([](int x, int y) { return x + y; }, 20, 22);
    EXPECT_EQ(future.get(), 42);
}

TEST(TaskPool, ExceptionHandling) {
    TaskPool pool(4);
    
    auto future = pool.submit([]() -> int {
        throw std::runtime_error("Test exception");
        return 42;
    });
    
    EXPECT_THROW(future.get(), std::runtime_error);
}

TEST(TaskPool, ThreadCount) {
    const size_t thread_count = 4;
    TaskPool pool(thread_count);
    
    EXPECT_EQ(pool.thread_count(), thread_count);
}

TEST(TaskPool, QueuedTasksCount) {
    TaskPool pool(1);
    std::vector<std::future<void>> futures;
    
    // Add a long-running task to block the worker
    futures.push_back(pool.submit([]() {
        std::this_thread::sleep_for(100ms);
    }));
    
    // Queue up some quick tasks
    for(int i = 0; i < 5; ++i) {
        futures.push_back(pool.submit([]() {}));
    }
    
    // Wait a bit for tasks to be queued
    std::this_thread::sleep_for(10ms);
    
    // Check that tasks are queued
    EXPECT_GT(pool.queued_tasks(), 0);
    
    // Wait for all tasks to complete
    for(auto& future : futures) {
        future.wait();
    }
    
    // Check that queue is empty
    EXPECT_EQ(pool.queued_tasks(), 0);
}
