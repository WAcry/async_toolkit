#pragma once

#include <atomic>
#include <random>
#include <memory>
#include <optional>
#include "../memory/memory_pool.hpp"

namespace async_toolkit::lockfree {

template<typename Key, typename Value, size_t MaxLevel = 32>
class SkipList {
    struct Node {
        Key key;
        std::atomic<Value> value;
        std::array<std::atomic<Node*>, MaxLevel> next;
        const int level;
        // Mark if node is logically deleted
        std::atomic<bool> marked;

        Node(const Key& k, const Value& v, int lvl)
            : key(k), value(v), level(lvl), marked(false) {
            for (int i = 0; i < level; ++i) {
                next[i].store(nullptr);
            }
        }
    };

    using NodePool = memory::MemoryPool<Node>;

public:
    SkipList() : head_(new Node(Key(), Value(), MaxLevel)), 
                 current_level_(1) {
        for (int i = 0; i < MaxLevel; ++i) {
            head_->next[i].store(nullptr);
        }
    }

    ~SkipList() {
        auto current = head_;
        while (current) {
            auto next = current->next[0].load();
            delete current;
            current = next;
        }
    }

    bool insert(const Key& key, const Value& value) {
        std::array<Node*, MaxLevel> update;
        auto current = head_;

        for (int i = current_level_.load() - 1; i >= 0; --i) {
            while (true) {
                auto next = current->next[i].load();
                if (!next || next->key > key) {
                    update[i] = current;
                    break;
                }
                if (next->key == key) {
                    if (!next->marked.load()) {
                        next->value.store(value);
                        return true;
                    }
                    break;
                }
                current = next;
            }
        }

        int new_level = random_level();
        auto new_node = pool_.allocate(key, value, new_level);

        while (new_level > current_level_.load()) {
            int old_level = current_level_.load();
            if (current_level_.compare_exchange_weak(old_level, new_level)) {
                break;
            }
        }

        int highest_locked = -1;
        try {
            for (int i = 0; i < new_level; ++i) {
                new_node->next[i].store(update[i]->next[i].load());
                if (!update[i]->next[i].compare_exchange_strong(
                        new_node->next[i].load(), new_node)) {
                    throw std::runtime_error("CAS failed");
                }
                highest_locked = i;
            }
        } catch (...) {
            for (int i = 0; i <= highest_locked; ++i) {
                update[i]->next[i].store(new_node->next[i].load());
            }
            pool_.deallocate(new_node);
            return false;
        }

        return true;
    }

    bool remove(const Key& key) {
        std::array<Node*, MaxLevel> update;
        auto current = head_;

        for (int i = current_level_.load() - 1; i >= 0; --i) {
            while (true) {
                auto next = current->next[i].load();
                if (!next || next->key >= key) {
                    update[i] = current;
                    break;
                }
                current = next;
            }
        }

        current = current->next[0].load();
        if (!current || current->key != key) {
            return false;
        }

        // If node is already marked as deleted by another thread
        if (current->marked.exchange(true)) {
            return false;
        }

        for (int i = 0; i < current_level_.load(); ++i) {
            while (true) {
                auto next = current->next[i].load();
                if (update[i]->next[i].compare_exchange_strong(current, next)) {
                    break;
                }
                current = update[i]->next[i].load();
                while (current && current->marked.load()) {
                    current = current->next[i].load();
                }
            }
        }

        return true;
    }

    std::optional<Value> find(const Key& key) const {
        auto current = head_;
        
        for (int i = current_level_.load() - 1; i >= 0; --i) {
            while (true) {
                auto next = current->next[i].load();
                if (!next || next->key >= key) {
                    break;
                }
                current = next;
            }
        }

        current = current->next[0].load();
        if (current && current->key == key && !current->marked.load()) {
            return current->value.load();
        }
        return std::nullopt;
    }

private:
    int random_level() {
        static thread_local std::random_device rd;
        static thread_local std::mt19937 gen(rd());
        static thread_local std::uniform_real_distribution<> dis(0, 1);

        int level = 1;
        while (dis(gen) < 0.5 && level < MaxLevel) {
            ++level;
        }
        return level;
    }

    Node* const head_;
    std::atomic<int> current_level_;
    NodePool pool_;
};

} // namespace async_toolkit::lockfree
