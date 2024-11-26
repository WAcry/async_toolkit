#pragma once

#include <string>
#include <vector>
#include <random>
#include <mutex>
#include <atomic>
#include <unordered_map>
#include <map>
#include <chrono>

namespace async_toolkit::rpc {

// Load balancing strategies
enum class LoadBalanceStrategy {
    RANDOM,         // Random selection
    ROUND_ROBIN,    // Round-robin
    LEAST_CONN,     // Least connections
    CONSISTENT_HASH // Consistent hashing
};

// Base load balancer class
class LoadBalancer {
public:
    virtual ~LoadBalancer() = default;
    virtual std::string select_server(const std::vector<std::string>& servers) = 0;
    virtual void update_server_stats(const std::string& server, 
                                   size_t active_connections,
                                   double response_time) = 0;
};

// Random load balancer
class RandomLoadBalancer : public LoadBalancer {
public:
    RandomLoadBalancer() : rd_(), gen_(rd_()), dist_(0.0, 1.0) {}

    std::string select_server(const std::vector<std::string>& servers) override {
        if (servers.empty()) return "";
        size_t index = static_cast<size_t>(dist_(gen_) * servers.size());
        return servers[index];
    }

    void update_server_stats(const std::string&, size_t, double) override {}

private:
    std::random_device rd_;
    std::mt19937 gen_;
    std::uniform_real_distribution<> dist_;
};

// Round-robin load balancer
class RoundRobinLoadBalancer : public LoadBalancer {
public:
    std::string select_server(const std::vector<std::string>& servers) override {
        if (servers.empty()) return "";
        size_t index = current_index_++ % servers.size();
        return servers[index];
    }

    void update_server_stats(const std::string&, size_t, double) override {}

private:
    std::atomic<size_t> current_index_{0};
};

// Least connections load balancer
class LeastConnLoadBalancer : public LoadBalancer {
public:
    std::string select_server(const std::vector<std::string>& servers) override {
        if (servers.empty()) return "";

        std::lock_guard<std::mutex> lock(mutex_);
        std::string selected_server = servers[0];
        size_t min_connections = get_connection_count(selected_server);

        for (size_t i = 1; i < servers.size(); ++i) {
            size_t conn_count = get_connection_count(servers[i]);
            if (conn_count < min_connections) {
                min_connections = conn_count;
                selected_server = servers[i];
            }
        }

        return selected_server;
    }

    void update_server_stats(const std::string& server, 
                           size_t active_connections,
                           double) override {
        std::lock_guard<std::mutex> lock(mutex_);
        connection_counts_[server] = active_connections;
    }

private:
    size_t get_connection_count(const std::string& server) {
        auto it = connection_counts_.find(server);
        return it != connection_counts_.end() ? it->second : 0;
    }

    std::mutex mutex_;
    std::unordered_map<std::string, size_t> connection_counts_;
};

// Consistent hash load balancer
class ConsistentHashLoadBalancer : public LoadBalancer {
public:
    ConsistentHashLoadBalancer(size_t virtual_nodes = 100) 
        : virtual_nodes_(virtual_nodes) {}

    std::string select_server(const std::vector<std::string>& servers) override {
        if (servers.empty()) return "";

        std::lock_guard<std::mutex> lock(mutex_);
        rebuild_if_needed(servers);

        // Use current time as hash key
        size_t hash = std::hash<size_t>{}(
            std::chrono::system_clock::now().time_since_epoch().count()
        );

        auto it = hash_ring_.lower_bound(hash);
        if (it == hash_ring_.end()) {
            it = hash_ring_.begin();
        }

        return it->second;
    }

    void update_server_stats(const std::string&, size_t, double) override {}

private:
    std::mutex mutex_;
    size_t virtual_nodes_;
    std::vector<std::string> current_servers_;
    std::map<size_t, std::string> hash_ring_;

    void rebuild_if_needed(const std::vector<std::string>& servers) {
        bool need_rebuild = servers.size() != current_servers_.size();
        if (!need_rebuild) {
            for (size_t i = 0; i < servers.size(); ++i) {
                if (servers[i] != current_servers_[i]) {
                    need_rebuild = true;
                    break;
                }
            }
        }

        if (need_rebuild) {
            rebuild_hash_ring(servers);
            current_servers_ = servers;
        }
    }

    void rebuild_hash_ring(const std::vector<std::string>& servers) {
        hash_ring_.clear();
        for (const auto& server : servers) {
            for (size_t i = 0; i < virtual_nodes_; ++i) {
                std::string virtual_node = server + "#" + std::to_string(i);
                size_t hash = std::hash<std::string>{}(virtual_node);
                hash_ring_[hash] = server;
            }
        }
    }
};

// Load balancer factory function
inline std::unique_ptr<LoadBalancer> 
create_load_balancer(LoadBalanceStrategy strategy) {
    switch (strategy) {
        case LoadBalanceStrategy::RANDOM:
            return std::make_unique<RandomLoadBalancer>();
        case LoadBalanceStrategy::ROUND_ROBIN:
            return std::make_unique<RoundRobinLoadBalancer>();
        case LoadBalanceStrategy::LEAST_CONN:
            return std::make_unique<LeastConnLoadBalancer>();
        case LoadBalanceStrategy::CONSISTENT_HASH:
            return std::make_unique<ConsistentHashLoadBalancer>();
        default:
            return std::make_unique<RoundRobinLoadBalancer>();
    }
}

} // namespace async_toolkit::rpc
