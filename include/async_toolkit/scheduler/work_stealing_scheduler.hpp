#pragma once

#include <vector>
#include <deque>
#include <thread>
#include <atomic>
#include <random>
#include <optional>
#include <functional>
#include <condition_variable>
#include "../memory/memory_pool.hpp"

namespace async_toolkit::scheduler {

template<typename T>
class WorkStealingQueue {
public:
    WorkStealingQueue() = default;

    void push(T task) {
        std::lock_guard<std::mutex> lock(mutex_);
        tasks_.push_back(std::move(task));
    }

    bool try_pop(T& task) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (tasks_.empty()) {
            return false;
        }
        task = std::move(tasks_.back());
        tasks_.pop_back();
        return true;
    }

    bool try_steal(T& task) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (tasks_.empty()) {
            return false;
        }
        task = std::move(tasks_.front());
        tasks_.pop_front();
        return true;
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return tasks_.empty();
    }

private:
    mutable std::mutex mutex_;
    std::deque<T> tasks_;
};

class WorkStealingScheduler {
public:
    using Task = std::function<void()>;

    explicit WorkStealingScheduler(size_t thread_count = std::thread::hardware_concurrency())
        : queues_(thread_count), threads_(thread_count), running_(true) {
        
        // Initialize thread-local index
        thread_index_ = 0;
        
        // Create worker threads
        for (size_t i = 0; i < threads_.size(); ++i) {
            threads_[i] = std::thread([this, i] { worker_loop(i); });
        }
    }

    ~WorkStealingScheduler() {
        running_ = false;
        cv_.notify_all();
        
        for (auto& thread : threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
    }

    // Submit task, returns the current thread's queue index
    void submit(Task task) {
        size_t index = get_worker_index();
        queues_[index].push(std::move(task));
        cv_.notify_one();
    }

    // Submit task with priority
    void submit_with_priority(Task task, int priority) {
        auto wrapped_task = [task = std::move(task), priority] {
            std::this_thread::yield();  // Yield CPU to higher priority tasks
            task();
        };
        submit(std::move(wrapped_task));
    }

    // Submit batch of tasks
    template<typename Iterator>
    void submit_batch(Iterator begin, Iterator end) {
        size_t index = get_worker_index();
        size_t count = std::distance(begin, end);
        size_t tasks_per_queue = count / queues_.size();
        
        size_t current_queue = index;
        auto it = begin;
        
        for (size_t i = 0; i < queues_.size() && it != end; ++i) {
            size_t tasks_for_this_queue = (i == queues_.size() - 1) 
                ? (end - it) : tasks_per_queue;
                
            for (size_t j = 0; j < tasks_for_this_queue && it != end; ++j) {
                queues_[current_queue].push(std::move(*it));
                ++it;
            }
            
            current_queue = (current_queue + 1) % queues_.size();
        }
        
        cv_.notify_all();
    }

    // Get number of active tasks
    size_t active_tasks() const {
        size_t count = 0;
        for (const auto& queue : queues_) {
            if (!queue.empty()) {
                ++count;
            }
        }
        return count;
    }

private:
    void worker_loop(size_t index) {
        thread_local static std::random_device rd;
        thread_local static std::mt19937 gen(rd());
        
        while (running_) {
            Task task;
            
            // 1. Try to get task from own queue
            if (queues_[index].try_pop(task)) {
                task();
                continue;
            }
            
            // 2. Randomly steal tasks from other queues
            size_t victim_index = std::uniform_int_distribution<size_t>
                (0, queues_.size() - 1)(gen);
                
            if (victim_index != index && queues_[victim_index].try_steal(task)) {
                task();
                continue;
            }
            
            // 3. If no tasks available, wait on condition variable
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait_for(lock, std::chrono::milliseconds(100), 
                [this] { return !running_ || active_tasks() > 0; });
        }
    }

    size_t get_worker_index() {
        thread_local static size_t index = thread_index_++;
        return index % queues_.size();
    }

    std::vector<WorkStealingQueue<Task>> queues_;
    std::vector<std::thread> threads_;
    std::atomic<bool> running_;
    std::atomic<size_t> thread_index_;
    
    mutable std::mutex mutex_;
    std::condition_variable cv_;
};

} // namespace async_toolkit::scheduler
