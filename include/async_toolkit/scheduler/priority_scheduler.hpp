#pragma once

#include <queue>
#include <mutex>
#include <functional>
#include <chrono>
#include <optional>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <memory>
#include <vector>

namespace async_toolkit::scheduler {

struct Task {
    std::function<void()> func;
    int priority;
    std::chrono::steady_clock::time_point schedule_time;
    size_t task_id;

    bool operator<(const Task& other) const {
        if (priority != other.priority)
            return priority < other.priority;
        return schedule_time > other.schedule_time;
    }
};

class PriorityScheduler {
public:
    explicit PriorityScheduler(size_t thread_count = std::thread::hardware_concurrency())
        : stop_(false), next_task_id_(0) {
        for (size_t i = 0; i < thread_count; ++i) {
            workers_.emplace_back([this] { worker_loop(); });
        }
    }

    ~PriorityScheduler() {
        {
            std::unique_lock lock(mutex_);
            stop_ = true;
        }
        condition_.notify_all();
        for (auto& worker : workers_) {
            worker.join();
        }
    }

    template<typename F>
    size_t schedule(F&& func, int priority = 0) {
        return schedule_at(std::forward<F>(func), 
                         std::chrono::steady_clock::now(),
                         priority);
    }

    template<typename F, typename Rep, typename Period>
    size_t schedule_after(F&& func,
                         const std::chrono::duration<Rep, Period>& delay,
                         int priority = 0) {
        return schedule_at(std::forward<F>(func),
                         std::chrono::steady_clock::now() + delay,
                         priority);
    }

    template<typename F>
    size_t schedule_at(F&& func,
                      const std::chrono::steady_clock::time_point& time,
                      int priority = 0) {
        Task task{
            .func = std::forward<F>(func),
            .priority = priority,
            .schedule_time = time,
            .task_id = next_task_id_++
        };

        {
            std::unique_lock lock(mutex_);
            tasks_.push(std::move(task));
        }
        condition_.notify_one();
        return task.task_id;
    }

    bool cancel(size_t task_id) {
        std::unique_lock lock(mutex_);
        std::vector<Task> temp;
        bool found = false;

        while (!tasks_.empty()) {
            auto task = std::move(const_cast<Task&>(tasks_.top()));
            tasks_.pop();
            
            if (task.task_id != task_id) {
                temp.push_back(std::move(task));
            } else {
                found = true;
            }
        }

        for (auto& task : temp) {
            tasks_.push(std::move(task));
        }

        return found;
    }

    size_t pending_tasks() const {
        std::unique_lock lock(mutex_);
        return tasks_.size();
    }

private:
    void worker_loop() {
        while (true) {
            std::unique_lock lock(mutex_);
            
            condition_.wait(lock, [this] {
                return stop_ || (!tasks_.empty() && 
                       tasks_.top().schedule_time <= std::chrono::steady_clock::now());
            });

            if (stop_ && tasks_.empty()) {
                return;
            }

            if (!tasks_.empty() && 
                tasks_.top().schedule_time <= std::chrono::steady_clock::now()) {
                auto task = std::move(const_cast<Task&>(tasks_.top()));
                tasks_.pop();
                lock.unlock();

                try {
                    task.func();
                } catch (...) {
                    // Log exception or handle error
                }
            }
        }
    }

    std::priority_queue<Task> tasks_;
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::vector<std::thread> workers_;
    std::atomic<bool> stop_;
    std::atomic<size_t> next_task_id_;
};

} // namespace async_toolkit::scheduler
