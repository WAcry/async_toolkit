#pragma once

#include <coroutine>
#include <exception>
#include <stdexcept>
#include <concepts>
#include <type_traits>

namespace async_toolkit::coroutine {

template<typename T = void>
class [[nodiscard]] Task {
public:
    struct promise_type;
    using handle_type = std::coroutine_handle<promise_type>;

    struct promise_type {
        T result;
        std::exception_ptr exception;

        Task get_return_object() {
            return Task(handle_type::from_promise(*this));
        }

        std::suspend_never initial_suspend() { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }

        template<typename U>
        requires std::convertible_to<U, T>
        void return_value(U&& value) {
            result = std::forward<U>(value);
        }

        void unhandled_exception() {
            exception = std::current_exception();
        }
    };

    Task() : coro_(nullptr) {}
    Task(handle_type h) : coro_(h) {}
    Task(Task&& other) noexcept : coro_(other.coro_) {
        other.coro_ = nullptr;
    }

    ~Task() {
        if (coro_) coro_.destroy();
    }

    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            if (coro_) coro_.destroy();
            coro_ = other.coro_;
            other.coro_ = nullptr;
        }
        return *this;
    }

    T get() {
        if (coro_) {
            coro_.resume();
            if (coro_.promise().exception)
                std::rethrow_exception(coro_.promise().exception);
            return std::move(coro_.promise().result);
        }
        throw std::runtime_error("Task not initialized");
    }

    bool is_ready() const {
        return coro_ && coro_.done();
    }

private:
    handle_type coro_;
};

// Specialization for void type Task
template<>
class Task<void> {
public:
    struct promise_type {
        std::exception_ptr exception;

        Task<void> get_return_object() {
            return Task<void>(std::coroutine_handle<promise_type>::from_promise(*this));
        }

        std::suspend_never initial_suspend() { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() {}

        void unhandled_exception() {
            exception = std::current_exception();
        }
    };

    using handle_type = std::coroutine_handle<promise_type>;

    Task() : coro_(nullptr) {}
    Task(handle_type h) : coro_(h) {}
    Task(Task&& other) noexcept : coro_(other.coro_) {
        other.coro_ = nullptr;
    }

    ~Task() {
        if (coro_) coro_.destroy();
    }

    Task& operator=(Task&& other) noexcept {
        if (this != &other) {
            if (coro_) coro_.destroy();
            coro_ = other.coro_;
            other.coro_ = nullptr;
        }
        return *this;
    }

    void get() {
        if (coro_) {
            coro_.resume();
            if (coro_.promise().exception)
                std::rethrow_exception(coro_.promise().exception);
        }
    }

    bool is_ready() const {
        return coro_ && coro_.done();
    }

private:
    handle_type coro_;
};

} // namespace async_toolkit::coroutine
