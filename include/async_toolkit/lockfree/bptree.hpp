#pragma once

#include <atomic>
#include <memory>
#include <optional>
#include <vector>
#include <algorithm>
#include "../memory/memory_pool.hpp"

namespace async_toolkit::lockfree {

template<typename Key, typename Value, size_t Order = 64>
class BPlusTree {
    static_assert(Order > 2, "B+ tree order must be greater than 2");

    struct Node;
    struct LeafNode;
    struct InternalNode;
    using NodePtr = std::atomic<Node*>;
    using LeafPtr = LeafNode*;
    using InternalPtr = InternalNode*;

    struct Node {
        std::atomic<bool> is_leaf;
        std::atomic<size_t> size;
        std::atomic<Node*> next;
        NodePtr parent;

        Node(bool leaf) : is_leaf(leaf), size(0), next(nullptr), parent(nullptr) {}
        virtual ~Node() = default;
    };

    struct LeafNode : Node {
        std::array<std::atomic<Key>, Order> keys;
        std::array<std::atomic<Value>, Order> values;
        std::atomic<LeafNode*> next_leaf;

        LeafNode() : Node(true), next_leaf(nullptr) {}

        bool insert(const Key& key, const Value& value) {
            size_t pos = find_position(key);
            if (pos < this->size && keys[pos] == key) {
                values[pos].store(value);
                return true;
            }

            if (this->size >= Order) {
                return false;  // Node is full
            }

            // Move elements to make space for new key-value pair
            for (size_t i = this->size; i > pos; --i) {
                keys[i].store(keys[i-1].load());
                values[i].store(values[i-1].load());
            }

            keys[pos].store(key);
            values[pos].store(value);
            this->size++;
            return true;
        }

        std::optional<Value> find(const Key& key) const {
            size_t pos = find_position(key);
            if (pos < this->size && keys[pos] == key) {
                return values[pos].load();
            }
            return std::nullopt;
        }

        bool remove(const Key& key) {
            size_t pos = find_position(key);
            if (pos >= this->size || keys[pos] != key) {
                return false;
            }

            // Move elements to fill the gap
            for (size_t i = pos; i < this->size - 1; ++i) {
                keys[i].store(keys[i+1].load());
                values[i].store(values[i+1].load());
            }

            this->size--;
            return true;
        }

    private:
        size_t find_position(const Key& key) const {
            size_t pos = 0;
            while (pos < this->size && keys[pos].load() < key) {
                ++pos;
            }
            return pos;
        }
    };

    struct InternalNode : Node {
        std::array<std::atomic<Key>, Order> keys;
        std::array<NodePtr, Order + 1> children;

        InternalNode() : Node(false) {}

        NodePtr find_child(const Key& key) const {
            size_t pos = 0;
            while (pos < this->size && keys[pos].load() <= key) {
                ++pos;
            }
            return children[pos].load();
        }

        bool insert_child(const Key& key, Node* child) {
            if (this->size >= Order) {
                return false;  // Node is full
            }

            size_t pos = 0;
            while (pos < this->size && keys[pos].load() < key) {
                ++pos;
            }

            // Move elements to make space for new key-value pair
            for (size_t i = this->size; i > pos; --i) {
                keys[i].store(keys[i-1].load());
                children[i+1].store(children[i].load());
            }

            keys[pos].store(key);
            children[pos+1].store(child);
            child->parent.store(this);
            this->size++;
            return true;
        }
    };

public:
    BPlusTree() : root_(create_leaf_node()) {}

    bool insert(const Key& key, const Value& value) {
        while (true) {
            auto [leaf, parent_path] = find_leaf_node(key);
            
            if (leaf->insert(key, value)) {
                return true;
            }

            // Node needs to be split
            auto new_leaf = split_leaf_node(leaf);
            
            // Update linked list
            new_leaf->next_leaf.store(leaf->next_leaf.load());
            leaf->next_leaf.store(new_leaf);

            // Insert middle key into parent node
            Key middle_key = new_leaf->keys[0].load();
            
            if (!insert_in_parent(leaf, middle_key, new_leaf, parent_path)) {
                // Retry entire insertion process
                continue;
            }
            
            return true;
        }
    }

    std::optional<Value> find(const Key& key) const {
        auto [leaf, _] = find_leaf_node(key);
        return leaf->find(key);
    }

    bool remove(const Key& key) {
        while (true) {
            auto [leaf, parent_path] = find_leaf_node(key);
            
            if (leaf->remove(key)) {
                // Check if nodes need to be merged
                if (leaf->size < Order/2) {
                    merge_nodes(leaf, parent_path);
                }
                return true;
            }
            return false;
        }
    }

    // Range query
    template<typename OutputIterator>
    void range_query(const Key& start, const Key& end, OutputIterator out) const {
        auto [leaf, _] = find_leaf_node(start);
        
        while (leaf) {
            for (size_t i = 0; i < leaf->size; ++i) {
                Key current_key = leaf->keys[i].load();
                if (current_key > end) {
                    return;
                }
                if (current_key >= start) {
                    *out++ = std::make_pair(current_key, leaf->values[i].load());
                }
            }
            leaf = leaf->next_leaf.load();
        }
    }

private:
    NodePtr root_;
    memory::MemoryPool<Node> node_pool_;
    memory::MemoryPool<LeafNode> leaf_pool_;
    memory::MemoryPool<InternalNode> internal_pool_;

    LeafNode* create_leaf_node() {
        return leaf_pool_.allocate();
    }

    InternalNode* create_internal_node() {
        return internal_pool_.allocate();
    }

    std::pair<LeafNode*, std::vector<InternalNode*>> find_leaf_node(const Key& key) const {
        std::vector<InternalNode*> parent_path;
        Node* current = root_.load();
        
        while (!current->is_leaf) {
            auto internal = static_cast<InternalNode*>(current);
            parent_path.push_back(internal);
            current = internal->find_child(key);
        }
        
        return {static_cast<LeafNode*>(current), parent_path};
    }

    LeafNode* split_leaf_node(LeafNode* node) {
        auto new_node = create_leaf_node();
        size_t mid = node->size / 2;
        
        // Move latter half to new node
        for (size_t i = mid; i < node->size; ++i) {
            new_node->keys[i-mid].store(node->keys[i].load());
            new_node->values[i-mid].store(node->values[i].load());
            new_node->size++;
        }
        
        node->size = mid;
        return new_node;
    }

    bool insert_in_parent(Node* left, const Key& key, Node* right,
                         const std::vector<InternalNode*>& parent_path) {
        if (parent_path.empty()) {
            // Create new root node
            auto new_root = create_internal_node();
            new_root->children[0].store(left);
            new_root->insert_child(key, right);
            root_.store(new_root);
            return true;
        }

        auto parent = parent_path.back();
        if (parent->insert_child(key, right)) {
            return true;
        }

        // Internal node needs to be split
        auto new_parent = create_internal_node();
        size_t mid = parent->size / 2;
        
        // Move latter half to new node
        for (size_t i = mid + 1; i < parent->size; ++i) {
            new_parent->keys[i-mid-1].store(parent->keys[i].load());
            new_parent->children[i-mid-1].store(parent->children[i].load());
            new_parent->size++;
        }
        new_parent->children[parent->size-mid].store(parent->children[parent->size].load());
        
        Key middle_key = parent->keys[mid].load();
        parent->size = mid;

        // Recursive insert to higher level
        std::vector<InternalNode*> upper_path(parent_path.begin(), parent_path.end() - 1);
        return insert_in_parent(parent, middle_key, new_parent, upper_path);
    }

    void merge_nodes(LeafNode* node, const std::vector<InternalNode*>& parent_path) {
        if (parent_path.empty()) {
            // If root node, no need to merge
            if (node == root_.load()) {
                return;
            }
            return;
        }

        auto parent = parent_path.back();
        
        // Find current node's position in parent
        size_t node_pos = 0;
        while (node_pos <= parent->size && 
               parent->children[node_pos].load() != node) {
            node_pos++;
        }

        // Try borrowing from left sibling
        if (node_pos > 0) {
            auto left_sibling = static_cast<LeafNode*>(parent->children[node_pos - 1].load());
            if (left_sibling->size > Order/2) {
                // Borrow one key-value pair from left sibling
                size_t last_idx = left_sibling->size - 1;
                
                // Make space for new key-value pair
                for (size_t i = node->size; i > 0; --i) {
                    node->keys[i].store(node->keys[i-1].load());
                    node->values[i].store(node->values[i-1].load());
                }
                
                // Insert borrowed key-value pair
                node->keys[0].store(left_sibling->keys[last_idx].load());
                node->values[0].store(left_sibling->values[last_idx].load());
                node->size++;
                left_sibling->size--;
                
                // Update separator key in parent
                parent->keys[node_pos - 1].store(node->keys[0].load());
                return;
            }
        }

        // Try borrowing from right sibling
        if (node_pos < parent->size) {
            auto right_sibling = static_cast<LeafNode*>(parent->children[node_pos + 1].load());
            if (right_sibling->size > Order/2) {
                // Borrow one key-value pair from right sibling
                node->keys[node->size].store(right_sibling->keys[0].load());
                node->values[node->size].store(right_sibling->values[0].load());
                node->size++;
                
                // Move elements in right sibling
                for (size_t i = 0; i < right_sibling->size - 1; ++i) {
                    right_sibling->keys[i].store(right_sibling->keys[i+1].load());
                    right_sibling->values[i].store(right_sibling->values[i+1].load());
                }
                right_sibling->size--;
                
                // Update separator key in parent
                parent->keys[node_pos].store(right_sibling->keys[0].load());
                return;
            }
        }

        // If borrowing not possible, merge nodes
        if (node_pos > 0) {
            // Merge with left sibling
            auto left_sibling = static_cast<LeafNode*>(parent->children[node_pos - 1].load());
            
            // Copy current node's content to left sibling
            for (size_t i = 0; i < node->size; ++i) {
                left_sibling->keys[left_sibling->size + i].store(node->keys[i].load());
                left_sibling->values[left_sibling->size + i].store(node->values[i].load());
            }
            left_sibling->size += node->size;
            
            // Update leaf node linked list
            left_sibling->next_leaf.store(node->next_leaf.load());
            
            // Remove current node from parent
            for (size_t i = node_pos - 1; i < parent->size - 1; ++i) {
                parent->keys[i].store(parent->keys[i+1].load());
                parent->children[i+1].store(parent->children[i+2].load());
            }
            parent->size--;
            
            // If parent becomes too small, recursive merge
            if (parent->size < Order/2) {
                merge_internal_node(parent, std::vector<InternalNode*>(parent_path.begin(), parent_path.end() - 1));
            }
            
            // Free current node
            leaf_pool_.deallocate(node);
        } else {
            // Merge with right sibling
            auto right_sibling = static_cast<LeafNode*>(parent->children[node_pos + 1].load());
            
            // Copy right sibling's content to current node
            for (size_t i = 0; i < right_sibling->size; ++i) {
                node->keys[node->size + i].store(right_sibling->keys[i].load());
                node->values[node->size + i].store(right_sibling->values[i].load());
            }
            node->size += right_sibling->size;
            
            // Update leaf node linked list
            node->next_leaf.store(right_sibling->next_leaf.load());
            
            // Remove right sibling from parent
            for (size_t i = node_pos; i < parent->size - 1; ++i) {
                parent->keys[i].store(parent->keys[i+1].load());
                parent->children[i+1].store(parent->children[i+2].load());
            }
            parent->size--;
            
            // If parent becomes too small, recursive merge
            if (parent->size < Order/2) {
                merge_internal_node(parent, std::vector<InternalNode*>(parent_path.begin(), parent_path.end() - 1));
            }
            
            // Free right sibling node
            leaf_pool_.deallocate(right_sibling);
        }
    }

    void merge_internal_node(InternalNode* node, const std::vector<InternalNode*>& parent_path) {
        if (parent_path.empty()) {
            // If root node, update root
            if (node == root_.load() && node->size == 0) {
                root_.store(node->children[0].load());
                internal_pool_.deallocate(node);
            }
            return;
        }

        auto parent = parent_path.back();
        
        // Find current node's position in parent
        size_t node_pos = 0;
        while (node_pos <= parent->size && 
               parent->children[node_pos].load() != node) {
            node_pos++;
        }

        // Try borrowing from left sibling
        if (node_pos > 0) {
            auto left_sibling = static_cast<InternalNode*>(parent->children[node_pos - 1].load());
            if (left_sibling->size > Order/2) {
                // Borrow one key-value pair from left sibling
                // Make space for new key-value pair
                for (size_t i = node->size; i > 0; --i) {
                    node->keys[i].store(node->keys[i-1].load());
                    node->children[i+1].store(node->children[i].load());
                }
                node->children[1].store(node->children[0].load());
                
                // Move separator key in parent down
                node->keys[0].store(parent->keys[node_pos - 1].load());
                
                // Move left sibling's last key up to parent
                parent->keys[node_pos - 1].store(left_sibling->keys[left_sibling->size - 1].load());
                
                // Move child node pointer
                node->children[0].store(left_sibling->children[left_sibling->size].load());
                node->children[0].load()->parent.store(node);
                
                node->size++;
                left_sibling->size--;
                return;
            }
        }

        // Try borrowing from right sibling
        if (node_pos < parent->size) {
            auto right_sibling = static_cast<InternalNode*>(parent->children[node_pos + 1].load());
            if (right_sibling->size > Order/2) {
                // Move separator key in parent down
                node->keys[node->size].store(parent->keys[node_pos].load());
                
                // Move right sibling's first key up to parent
                parent->keys[node_pos].store(right_sibling->keys[0].load());
                
                // Move child node pointer
                node->children[node->size + 1].store(right_sibling->children[0].load());
                node->children[node->size + 1].load()->parent.store(node);
                
                // Move elements in right sibling
                for (size_t i = 0; i < right_sibling->size - 1; ++i) {
                    right_sibling->keys[i].store(right_sibling->keys[i+1].load());
                    right_sibling->children[i].store(right_sibling->children[i+1].load());
                }
                right_sibling->children[right_sibling->size - 1].store(
                    right_sibling->children[right_sibling->size].load());
                
                node->size++;
                right_sibling->size--;
                return;
            }
        }

        // If borrowing not possible, merge nodes
        if (node_pos > 0) {
            // Merge with left sibling
            auto left_sibling = static_cast<InternalNode*>(parent->children[node_pos - 1].load());
            
            // Move separator key in parent down
            left_sibling->keys[left_sibling->size].store(parent->keys[node_pos - 1].load());
            left_sibling->size++;
            
            // Copy current node's content to left sibling
            for (size_t i = 0; i < node->size; ++i) {
                left_sibling->keys[left_sibling->size + i].store(node->keys[i].load());
                left_sibling->children[left_sibling->size + i].store(node->children[i].load());
                left_sibling->children[left_sibling->size + i].load()->parent.store(left_sibling);
            }
            left_sibling->children[left_sibling->size + node->size].store(node->children[node->size].load());
            left_sibling->children[left_sibling->size + node->size].load()->parent.store(left_sibling);
            left_sibling->size += node->size;
            
            // Remove current node from parent
            for (size_t i = node_pos - 1; i < parent->size - 1; ++i) {
                parent->keys[i].store(parent->keys[i+1].load());
                parent->children[i+1].store(parent->children[i+2].load());
            }
            parent->size--;
            
            // If parent becomes too small, recursive merge
            if (parent->size < Order/2) {
                merge_internal_node(parent, std::vector<InternalNode*>(parent_path.begin(), parent_path.end() - 1));
            }
            
            // Free current node
            internal_pool_.deallocate(node);
        } else {
            // Merge with right sibling
            auto right_sibling = static_cast<InternalNode*>(parent->children[node_pos + 1].load());
            
            // Move separator key in parent down
            node->keys[node->size].store(parent->keys[node_pos].load());
            node->size++;
            
            // Copy right sibling's content to current node
            for (size_t i = 0; i < right_sibling->size; ++i) {
                node->keys[node->size + i].store(right_sibling->keys[i].load());
                node->children[node->size + i].store(right_sibling->children[i].load());
                node->children[node->size + i].load()->parent.store(node);
            }
            node->children[node->size + right_sibling->size].store(right_sibling->children[right_sibling->size].load());
            node->children[node->size + right_sibling->size].load()->parent.store(node);
            node->size += right_sibling->size;
            
            // Remove right sibling from parent
            for (size_t i = node_pos; i < parent->size - 1; ++i) {
                parent->keys[i].store(parent->keys[i+1].load());
                parent->children[i+1].store(parent->children[i+2].load());
            }
            parent->size--;
            
            // If parent becomes too small, recursive merge
            if (parent->size < Order/2) {
                merge_internal_node(parent, std::vector<InternalNode*>(parent_path.begin(), parent_path.end() - 1));
            }
            
            // Free right sibling node
            internal_pool_.deallocate(right_sibling);
        }
    }
};

} // namespace async_toolkit::lockfree
