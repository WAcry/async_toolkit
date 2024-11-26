#pragma once

#include <atomic>
#include <memory>
#include <optional>
#include <array>
#include <functional>
#include "../memory/memory_pool.hpp"

namespace async_toolkit::lockfree {

template<typename Key, typename Value, size_t Buckets = 1024>
class HashMap {
    struct Node {
        Key key;
        std::atomic<Value> value;
        std::atomic<Node*> next;
        std::atomic<bool> marked;  // Mark if node is deleted

        Node(const Key& k, const Value& v)
            : key(k), value(v), next(nullptr), marked(false) {}
    };

    using NodePool = memory::MemoryPool<Node>;

public:
    HashMap() : size_(0) {
        for (auto& head : buckets_) {
            head.store(nullptr);
        }
    }

    ~HashMap() {
        for (auto& head : buckets_) {
            auto current = head.load();
            while (current) {
                auto next = current->next.load();
                pool_.deallocate(current);
                current = next;
            }
        }
    }

    bool insert(const Key& key, const Value& value) {
        size_t bucket = hash_(key) % Buckets;
        auto new_node = pool_.allocate(key, value);

        while (true) {
            auto head = buckets_[bucket].load();
            new_node->next.store(head);

            if (buckets_[bucket].compare_exchange_weak(head, new_node)) {
                size_.fetch_add(1);
                return true;
            }

            // If CAS fails, check if key already exists
            auto current = head;
            while (current) {
                if (current->key == key && !current->marked.load()) {
                    pool_.deallocate(new_node);
                    return false;
                }
                current = current->next.load();
            }
        }
    }

    bool remove(const Key& key) {
        size_t bucket = hash_(key) % Buckets;
        auto head = buckets_[bucket].load();
        
        Node* prev = nullptr;
        auto current = head;

        while (current) {
            if (current->key == key && !current->marked.load()) {
                if (current->marked.exchange(true)) {
                    return false;  // Already deleted by another thread
                }

                if (prev) {
                    auto next = current->next.load();
                    prev->next.compare_exchange_strong(current, next);
                } else {
                    auto next = current->next.load();
                    buckets_[bucket].compare_exchange_strong(current, next);
                }

                size_.fetch_sub(1);
                return true;
            }
            prev = current;
            current = current->next.load();
        }
        return false;
    }

    std::optional<Value> find(const Key& key) const {
        size_t bucket = hash_(key) % Buckets;
        auto current = buckets_[bucket].load();

        while (current) {
            if (current->key == key && !current->marked.load()) {
                return current->value.load();
            }
            current = current->next.load();
        }
        return std::nullopt;
    }

    bool update(const Key& key, const Value& new_value) {
        size_t bucket = hash_(key) % Buckets;
        auto current = buckets_[bucket].load();

        while (current) {
            if (current->key == key && !current->marked.load()) {
                current->value.store(new_value);
                return true;
            }
            current = current->next.load();
        }
        return false;
    }

    size_t size() const {
        return size_.load();
    }

    bool empty() const {
        return size() == 0;
    }

private:
    std::array<std::atomic<Node*>, Buckets> buckets_;
    std::atomic<size_t> size_;
    NodePool pool_;
    std::hash<Key> hash_;
};

} // namespace async_toolkit::lockfree
