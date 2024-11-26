#pragma once

#include <atomic>
#include <optional>
#include <memory>
#include <type_traits>
#include <chrono>
#include <thread>
#include "../memory/memory_pool.hpp"

namespace async_toolkit::channel {

template<typename T>
class MPMCChannel {
    struct Node;
    using NodePtr = Node*;
    using NodePool = memory::MemoryPool<Node>;

    struct Node {
        std::atomic<NodePtr> next;
        T data;
        std::atomic<bool> committed;

        Node() : next(nullptr), data(T{}), committed(false) {}
        explicit Node(const T& value) : next(nullptr), data(value), committed(true) {}
    };

public:
    explicit MPMCChannel(size_t capacity = 1024)
        : capacity_(capacity), size_(0) {
        auto dummy = pool_.allocate();
        head_.store(dummy);
        tail_.store(dummy);
    }

    ~MPMCChannel() {
        while (auto node = head_.load()) {
            head_.store(node->next);
            pool_.deallocate(node);
        }
    }

    template<typename U>
    bool try_send(U&& value, std::chrono::milliseconds timeout = std::chrono::milliseconds(0)) {
        auto end_time = std::chrono::steady_clock::now() + timeout;

        do {
            if (try_send_impl(std::forward<U>(value))) {
                return true;
            }
            
            if (timeout.count() > 0) {
                std::this_thread::yield();
            }
        } while (timeout.count() > 0 && std::chrono::steady_clock::now() < end_time);

        return false;
    }

    std::optional<T> try_receive(std::chrono::milliseconds timeout = std::chrono::milliseconds(0)) {
        auto end_time = std::chrono::steady_clock::now() + timeout;

        do {
            if (auto result = try_receive_impl()) {
                return result;
            }
            
            if (timeout.count() > 0) {
                std::this_thread::yield();
            }
        } while (timeout.count() > 0 && std::chrono::steady_clock::now() < end_time);

        return std::nullopt;
    }

    size_t size() const {
        return size_.load();
    }

    bool empty() const {
        return size() == 0;
    }

    size_t capacity() const {
        return capacity_;
    }

private:
    template<typename U>
    bool try_send_impl(U&& value) {
        if (size_.load() >= capacity_) {
            return false;
        }

        auto new_node = pool_.allocate(std::forward<U>(value));
        NodePtr tail = tail_.load();
        NodePtr next = tail->next.load();

        while (true) {
            if (tail == tail_.load()) {
                if (next == nullptr) {
                    if (tail->next.compare_exchange_weak(next, new_node)) {
                        tail_.compare_exchange_strong(tail, new_node);
                        size_.fetch_add(1);
                        return true;
                    }
                } else {
                    tail_.compare_exchange_strong(tail, next);
                }
            }
            tail = tail_.load();
            next = tail->next.load();
        }
    }

    std::optional<T> try_receive_impl() {
        NodePtr head = head_.load();
        NodePtr next = head->next.load();

        while (true) {
            if (head == head_.load()) {
                if (next == nullptr) {
                    return std::nullopt;
                }

                T result = std::move(next->data);
                if (head_.compare_exchange_weak(head, next)) {
                    pool_.deallocate(head);
                    size_.fetch_sub(1);
                    return result;
                }
            }
            head = head_.load();
            next = head->next.load();
        }
    }

    const size_t capacity_;
    std::atomic<size_t> size_;
    std::atomic<NodePtr> head_;
    std::atomic<NodePtr> tail_;
    NodePool pool_;
};

} // namespace async_toolkit::channel
