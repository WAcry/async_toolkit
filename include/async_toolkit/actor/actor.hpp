#pragma once

#include <memory>
#include <queue>
#include <functional>
#include <unordered_map>
#include <typeindex>
#include <any>
#include "../channel/mpmc_channel.hpp"
#include "../executor/thread_pool_executor.hpp"

namespace async_toolkit::actor {

class Actor;
using ActorRef = std::shared_ptr<Actor>;

class Actor : public std::enable_shared_from_this<Actor> {
public:
    struct Message {
        std::type_index type;
        std::any content;
        ActorRef sender;

        Message() : type(typeid(void)), sender(nullptr) {}

        template<typename T>
        Message(const T& msg, ActorRef from)
            : type(typeid(T)), content(msg), sender(from) {}
    };

    Actor(size_t mailbox_size = 1024)
        : mailbox_(mailbox_size),
          running_(true) {
        process_thread_ = std::thread([this] { process_messages(); });
    }

    virtual ~Actor() {
        running_ = false;
        if (process_thread_.joinable()) {
            process_thread_.join();
        }
    }

    template<typename T>
    void tell(const T& message, ActorRef sender = nullptr) {
        mailbox_.try_send(Message(message, sender));
    }

    template<typename T>
    void register_handler(std::function<void(const T&, ActorRef)> handler) {
        handlers_[typeid(T)] = [handler, this](const Message& msg) {
            handler(std::any_cast<T>(msg.content), msg.sender);
        };
    }

    virtual void on_start() {}
    virtual void on_stop() {}

protected:
    ActorRef self() {
        return shared_from_this();
    }

private:
    void process_messages() {
        on_start();

        while (running_) {
            auto msg = mailbox_.try_receive();
            if (msg) {
                auto it = handlers_.find(msg->type);
                if (it != handlers_.end()) {
                    try {
                        it->second(*msg);
                    } catch (...) {
                        // Handle exception
                    }
                }
            }
        }

        on_stop();
    }

    channel::MPMCChannel<Message> mailbox_;
    std::atomic<bool> running_;
    std::thread process_thread_;
    std::unordered_map<std::type_index, std::function<void(const Message&)>> handlers_;
};

class ActorSystem {
public:
    explicit ActorSystem(size_t thread_count = std::thread::hardware_concurrency())
        : executor_(thread_count) {}

    template<typename ActorType, typename... Args>
    ActorRef spawn_actor(Args&&... args) {
        auto actor = std::make_shared<ActorType>(std::forward<Args>(args)...);
        actors_.push_back(actor);
        return actor;
    }

    void shutdown() {
        actors_.clear();
    }

private:
    executor::ThreadPoolExecutor executor_;
    std::vector<ActorRef> actors_;
};

// Common Actor Patterns
class RoundRobinRouter : public Actor {
public:
    RoundRobinRouter(std::vector<ActorRef> routees)
        : routees_(std::move(routees)), current_(0) {}

    template<typename T>
    void route(const T& message, ActorRef sender = nullptr) {
        size_t index = (current_++) % routees_.size();
        routees_[index]->tell(message, sender);
    }

private:
    std::vector<ActorRef> routees_;
    std::atomic<size_t> current_;
};

class Supervisor : public Actor {
public:
    void supervise(ActorRef actor, 
                  std::function<void(const std::exception&)> strategy) {
        supervised_[actor] = strategy;
    }

    void handle_failure(ActorRef actor, const std::exception& e) {
        auto it = supervised_.find(actor);
        if (it != supervised_.end()) {
            it->second(e);
        }
    }

private:
    std::unordered_map<ActorRef, std::function<void(const std::exception&)>> supervised_;
};

} // namespace async_toolkit::actor
