#pragma once

#include <coroutine>
#include <chrono>
#include <memory>
#include <queue>
#include <thread>
#include <variant>
#include <optional>
#include <functional>
#include "../memory/memory_pool.hpp"
#include "../lockfree/mpmc_queue.hpp"

namespace async_toolkit::coroutine {

// Scheduling Policies
enum class SchedulePolicy {
    FIFO,           // First In First Out
    PRIORITY,       // Priority-based Scheduling
    ROUND_ROBIN,    // Round Robin Scheduling
    WORK_STEALING   // Work Stealing Scheduling
};

// Coroutine States
enum class CoroutineState {
    READY,          // Ready to Execute
    RUNNING,        // Currently Running
    SUSPENDED,      // Suspended
    COMPLETED,      // Completed
    CANCELLED,      // Cancelled
    FAILED          // Failed
};

// Cancellation Token
class CancellationToken {
public:
    bool is_cancellation_requested() const { 
        return cancelled_; 
    }
    
    void request_cancellation() { 
        cancelled_ = true; 
    }

private:
    std::atomic<bool> cancelled_{false};
};

// Base Class for Coroutine Tasks
class TaskBase {
public:
    virtual ~TaskBase() = default;
    virtual void resume() = 0;
    virtual bool is_done() const = 0;
    virtual void cancel() = 0;
    
    CoroutineState state() const { return state_; }
    void set_state(CoroutineState state) { state_ = state; }
    
    int priority() const { return priority_; }
    void set_priority(int priority) { priority_ = priority; }

protected:
    CoroutineState state_{CoroutineState::READY};
    int priority_{0};
    CancellationToken cancellation_token_;
};

// Coroutine Task
template<typename T>
class Task : public TaskBase {
public:
    struct promise_type;
    using handle_type = std::coroutine_handle<promise_type>;

    struct promise_type {
        Task get_return_object() {
            return Task(handle_type::from_promise(*this));
        }
        
        std::suspend_never initial_suspend() { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        
        void return_value(T value) {
            result_ = std::move(value);
        }
        
        void unhandled_exception() {
            exception_ = std::current_exception();
        }

        T result_;
        std::exception_ptr exception_;
    };

    Task(handle_type handle) : handle_(handle) {}
    ~Task() {
        if (handle_) handle_.destroy();
    }

    void resume() override {
        if (handle_ && !handle_.done()) {
            handle_.resume();
        }
    }

    bool is_done() const override {
        return !handle_ || handle_.done();
    }

    void cancel() override {
        cancellation_token_.request_cancellation();
    }

    T get_result() {
        if (handle_.promise().exception_) {
            std::rethrow_exception(handle_.promise().exception_);
        }
        return std::move(handle_.promise().result_);
    }

private:
    handle_type handle_;
};

// Timeout Awaiter
template<typename Rep, typename Period>
auto timeout(std::chrono::duration<Rep, Period> duration) {
    struct TimeoutAwaiter {
        TimeoutAwaiter(std::chrono::duration<Rep, Period> d) 
            : duration_(d), start_(std::chrono::steady_clock::now()) {}

        bool await_ready() const { return false; }
        
        void await_suspend(std::coroutine_handle<> h) {
            handle_ = h;
            // Resume the coroutine after the timeout
            std::thread([this] {
                std::this_thread::sleep_for(duration_);
                if (handle_) handle_.resume();
            }).detach();
        }
        
        bool await_resume() {
            auto now = std::chrono::steady_clock::now();
            return (now - start_) >= duration_;
        }

        std::chrono::duration<Rep, Period> duration_;
        std::chrono::steady_clock::time_point start_;
        std::coroutine_handle<> handle_;
    };

    return TimeoutAwaiter{duration};
}

// Coroutine Scheduler
class Scheduler {
public:
    explicit Scheduler(size_t thread_count = std::thread::hardware_concurrency(),
                      SchedulePolicy policy = SchedulePolicy::FIFO)
        : thread_count_(thread_count), policy_(policy) {
        start();
    }

    ~Scheduler() {
        stop();
    }

    // Submit a task
    template<typename T>
    void submit(Task<T>& task, int priority = 0) {
        task.set_priority(priority);
        task.set_state(CoroutineState::READY);
        task_queue_.enqueue(&task);
    }

    // Wait for a task to complete
    template<typename T>
    T await(Task<T>& task) {
        while (!task.is_done()) {
            std::this_thread::yield();
        }
        return task.get_result();
    }

    // Cancel a task
    template<typename T>
    void cancel(Task<T>& task) {
        task.cancel();
        task.set_state(CoroutineState::CANCELLED);
    }

private:
    void start() {
        running_ = true;
        for (size_t i = 0; i < thread_count_; ++i) {
            workers_.emplace_back([this] { worker_loop(); });
        }
    }

    void stop() {
        running_ = false;
        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

    void worker_loop() {
        while (running_) {
            TaskBase* task = nullptr;
            if (task_queue_.try_dequeue(task)) {
                if (task->state() == CoroutineState::READY) {
                    task->set_state(CoroutineState::RUNNING);
                    try {
                        task->resume();
                        if (task->is_done()) {
                            task->set_state(CoroutineState::COMPLETED);
                        } else {
                            task->set_state(CoroutineState::SUSPENDED);
                            // Re-enqueue the task
                            task_queue_.enqueue(task);
                        }
                    } catch (...) {
                        task->set_state(CoroutineState::FAILED);
                    }
                }
            } else {
                std::this_thread::yield();
            }
        }
    }

    size_t thread_count_;
    SchedulePolicy policy_;
    std::atomic<bool> running_{false};
    std::vector<std::thread> workers_;
    lockfree::MPMCQueue<TaskBase*> task_queue_;
    memory::MemoryPool<TaskBase> task_pool_;
};

// Helper function: Create task with timeout
template<typename T, typename F>
Task<T> with_timeout(std::chrono::milliseconds timeout_duration, F&& func) {
    if (co_await timeout(timeout_duration)) {
        throw std::runtime_error("Task timeout");
    }
    co_return co_await func();
}

} // namespace async_toolkit::coroutine
