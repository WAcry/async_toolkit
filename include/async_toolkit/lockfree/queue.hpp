#pragma once

#include <atomic>
#include <memory>
#include <optional>

namespace async_toolkit::lockfree {

template<typename T>
class Queue {
private:
    struct Node {
        std::shared_ptr<T> data;
        std::atomic<Node*> next;
        
        Node() : next(nullptr) {}
        explicit Node(const T& value) : data(std::make_shared<T>(value)), next(nullptr) {}
    };

    std::atomic<Node*> head_;
    std::atomic<Node*> tail_;
    std::atomic<size_t> size_;

public:
    Queue() : size_(0) {
        auto dummy = new Node();
        head_.store(dummy);
        tail_.store(dummy);
    }

    ~Queue() {
        while (auto node = head_.load()) {
            head_.store(node->next);
            delete node;
        }
    }

    void push(const T& value) {
        auto new_node = new Node(value);
        while (true) {
            auto tail = tail_.load();
            auto next = tail->next.load();
            
            if (tail == tail_.load()) {
                if (next == nullptr) {
                    if (tail->next.compare_exchange_weak(next, new_node)) {
                        tail_.compare_exchange_weak(tail, new_node);
                        size_.fetch_add(1);
                        return;
                    }
                } else {
                    tail_.compare_exchange_weak(tail, next);
                }
            }
        }
    }

    std::optional<T> pop() {
        while (true) {
            auto head = head_.load();
            auto tail = tail_.load();
            auto next = head->next.load();

            if (head == head_.load()) {
                if (head == tail) {
                    if (next == nullptr) {
                        return std::nullopt;
                    }
                    tail_.compare_exchange_weak(tail, next);
                } else {
                    if (next) {
                        auto result = *next->data;
                        if (head_.compare_exchange_weak(head, next)) {
                            size_.fetch_sub(1);
                            delete head;
                            return result;
                        }
                    }
                }
            }
        }
    }

    size_t size() const {
        return size_.load();
    }

    bool empty() const {
        return size() == 0;
    }
};

} // namespace async_toolkit::lockfree
