#pragma once

#include <atomic>
#include <optional>
#include <memory>
#include "../memory/memory_pool.hpp"

namespace async_toolkit::lockfree {

template<typename T>
class MPMCQueue {
    struct Node {
        std::atomic<Node*> next{nullptr};
        T data;
    };

    using NodePool = memory::MemoryPool<Node>;

public:
    explicit MPMCQueue(size_t capacity = 1024)
        : capacity_(capacity), size_(0) {
        auto dummy = pool_.allocate();
        head_.store(dummy);
        tail_.store(dummy);
    }

    ~MPMCQueue() {
        while (auto node = head_.load()) {
            head_.store(node->next);
            pool_.deallocate(node);
        }
    }

    bool try_enqueue(const T& value) {
        if (size_.load() >= capacity_) {
            return false;
        }

        auto node = pool_.allocate();
        node->data = value;
        node->next.store(nullptr);

        Node* tail = tail_.load();
        Node* next;

        while (true) {
            next = tail->next.load();
            if (tail == tail_.load()) {
                if (next == nullptr) {
                    if (tail->next.compare_exchange_weak(next, node)) {
                        break;
                    }
                } else {
                    tail_.compare_exchange_weak(tail, next);
                }
            }
            tail = tail_.load();
        }

        tail_.compare_exchange_weak(tail, node);
        size_.fetch_add(1);
        return true;
    }

    bool try_dequeue(T& value) {
        if (size_.load() == 0) {
            return false;
        }

        Node* head = head_.load();
        Node* tail = tail_.load();
        Node* next;

        while (true) {
            next = head->next.load();
            if (head == head_.load()) {
                if (head == tail) {
                    if (next == nullptr) {
                        return false;
                    }
                    tail_.compare_exchange_weak(tail, next);
                } else {
                    value = next->data;
                    if (head_.compare_exchange_weak(head, next)) {
                        break;
                    }
                }
            }
            head = head_.load();
        }

        pool_.deallocate(head);
        size_.fetch_sub(1);
        return true;
    }

    size_t size() const {
        return size_.load();
    }

    bool empty() const {
        return size() == 0;
    }

private:
    const size_t capacity_;
    std::atomic<size_t> size_;
    std::atomic<Node*> head_;
    std::atomic<Node*> tail_;
    NodePool pool_;
};

} // namespace async_toolkit::lockfree
