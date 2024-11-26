#pragma once

#include <thread>
#include <vector>
#include <functional>
#include <future>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <optional>
#include <chrono>
#include "../memory/memory_pool.hpp"

namespace async_toolkit::executor {

class ThreadPoolExecutor {
    struct Task {
        std::function<void()> func;
        int priority;
        std::chrono::steady_clock::time_point schedule_time;

        Task(std::function<void()>&& f, int p = 0,
             std::chrono::steady_clock::time_point t = std::chrono::steady_clock::now())
            : func(std::move(f)), priority(p), schedule_time(t) {}

        bool operator<(const Task& other) const {
            if (priority != other.priority)
                return priority < other.priority;
            return schedule_time > other.schedule_time;
        }
    };

    using TaskPool = memory::MemoryPool<Task>;

public:
    explicit ThreadPoolExecutor(size_t thread_count = std::thread::hardware_concurrency(),
                              size_t max_queue_size = 10000)
        : stop_(false), max_queue_size_(max_queue_size), task_pool_(std::make_unique<TaskPool>()) {
        for (size_t i = 0; i < thread_count; ++i) {
            workers_.emplace_back([this] { worker_loop(); });
        }
    }

    ~ThreadPoolExecutor() {
        {
            std::unique_lock lock(mutex_);
            stop_ = true;
        }
        condition_.notify_all();
        for (auto& worker : workers_) {
            worker.join();
        }
    }

    template<typename F, typename... Args>
    auto submit(F&& f, Args&&... args) {
        using return_type = std::invoke_result_t<F, Args...>;
        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        std::future<return_type> result = task->get_future();

        {
            std::unique_lock lock(mutex_);
            if (tasks_.size() >= max_queue_size_) {
                throw std::runtime_error("Task queue is full");
            }
            tasks_.push(task_pool_->allocate(
                [task]() { (*task)(); }));
        }
        condition_.notify_one();
        return result;
    }

    template<typename F, typename... Args>
    auto submit_with_priority(int priority, F&& f, Args&&... args) {
        using return_type = std::invoke_result_t<F, Args...>;
        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        std::future<return_type> result = task->get_future();

        {
            std::unique_lock lock(mutex_);
            if (tasks_.size() >= max_queue_size_) {
                throw std::runtime_error("Task queue is full");
            }
            tasks_.push(task_pool_->allocate(
                [task]() { (*task)(); }, priority));
        }
        condition_.notify_one();
        return result;
    }

    template<typename F, typename... Args>
    auto schedule_after(std::chrono::milliseconds delay, F&& f, Args&&... args) {
        using return_type = std::invoke_result_t<F, Args...>;
        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        std::future<return_type> result = task->get_future();

        auto schedule_time = std::chrono::steady_clock::now() + delay;
        {
            std::unique_lock lock(mutex_);
            if (tasks_.size() >= max_queue_size_) {
                throw std::runtime_error("Task queue is full");
            }
            tasks_.push(task_pool_->allocate(
                [task]() { (*task)(); }, 0, schedule_time));
        }
        condition_.notify_one();
        return result;
    }

    size_t queue_size() const {
        std::unique_lock lock(mutex_);
        return tasks_.size();
    }

    size_t thread_count() const {
        return workers_.size();
    }

private:
    void worker_loop() {
        while (true) {
            std::unique_lock lock(mutex_);
            condition_.wait(lock, [this] {
                return stop_ || !tasks_.empty();
            });

            if (stop_ && tasks_.empty()) {
                return;
            }

            auto current_time = std::chrono::steady_clock::now();
            if (!tasks_.empty() && tasks_.top().schedule_time <= current_time) {
                auto task = std::move(tasks_.top());
                tasks_.pop();
                lock.unlock();

                try {
                    task.func();
                } catch (...) {
                    // Handle exception
                }
            }
        }
    }

    std::vector<std::thread> workers_;
    std::priority_queue<Task> tasks_;
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::atomic<bool> stop_;
    const size_t max_queue_size_;
    std::unique_ptr<TaskPool> task_pool_;
};

} // namespace async_toolkit::executor
