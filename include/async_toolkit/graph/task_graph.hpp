#pragma once

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <memory>
#include <functional>
#include <future>
#include "../task_pool.hpp"

namespace async_toolkit::graph {

template<typename T>
class TaskGraph {
public:
    class Node;
    using NodePtr = std::shared_ptr<Node>;
    using NodeWeakPtr = std::weak_ptr<Node>;

    class Node {
    public:
        explicit Node(std::function<T()> task)
            : task_(std::move(task)), executed_(false) {}

        void add_dependency(NodePtr node) {
            dependencies_.push_back(node);
        }

        bool can_execute() const {
            for (const auto& dep : dependencies_) {
                if (!dep->is_executed()) {
                    return false;
                }
            }
            return !executed_;
        }

        T execute() {
            if (!executed_) {
                result_ = task_();
                executed_ = true;
            }
            return result_;
        }

        bool is_executed() const { return executed_; }
        const T& get_result() const { return result_; }

    private:
        std::function<T()> task_;
        std::vector<NodePtr> dependencies_;
        bool executed_;
        T result_;
    };

    NodePtr add_task(std::function<T()> task) {
        auto node = std::make_shared<Node>(std::move(task));
        nodes_.push_back(node);
        return node;
    }

    void add_dependency(NodePtr dependent, NodePtr dependency) {
        dependent->add_dependency(dependency);
    }

    std::vector<T> execute(TaskPool& pool) {
        std::vector<std::future<T>> futures;
        std::unordered_set<NodePtr> completed;
        
        while (completed.size() < nodes_.size()) {
            for (const auto& node : nodes_) {
                if (completed.count(node) == 0 && node->can_execute()) {
                    futures.push_back(pool.submit([node]() {
                        return node->execute();
                    }));
                    completed.insert(node);
                }
            }
        }

        std::vector<T> results;
        results.reserve(futures.size());
        for (auto& future : futures) {
            results.push_back(future.get());
        }
        return results;
    }

private:
    std::vector<NodePtr> nodes_;
};

// Helper function to create a task graph
template<typename T>
auto make_task_graph() {
    return std::make_unique<TaskGraph<T>>();
}

} // namespace async_toolkit::graph
