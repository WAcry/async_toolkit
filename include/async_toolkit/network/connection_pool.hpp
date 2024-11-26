#pragma once

#include <memory>
#include <vector>
#include <queue>
#include <mutex>
#include <chrono>
#include <functional>
#include "../reactor/event_loop.hpp"

namespace async_toolkit::network {

class Connection {
public:
    using Ptr = std::shared_ptr<Connection>;
    using EventCallback = std::function<void()>;

    Connection(reactor::FileDescriptor fd, reactor::EventLoop& loop)
        : fd_(fd), loop_(loop), connected_(true) {}

    ~Connection() {
        disconnect();
    }

    bool is_connected() const { return connected_; }

    void disconnect() {
        if (connected_) {
            connected_ = false;
#ifdef _WIN32
            closesocket(fd_);
#else
            close(fd_);
#endif
        }
    }

    void set_read_callback(EventCallback cb) {
        read_callback_ = std::move(cb);
    }

    void set_write_callback(EventCallback cb) {
        write_callback_ = std::move(cb);
    }

    reactor::FileDescriptor fd() const { return fd_; }

private:
    reactor::FileDescriptor fd_;
    reactor::EventLoop& loop_;
    bool connected_;
    EventCallback read_callback_;
    EventCallback write_callback_;
};

class ConnectionPool {
public:
    explicit ConnectionPool(size_t max_size = 100)
        : max_size_(max_size) {}

    Connection::Ptr acquire() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!idle_connections_.empty()) {
            auto conn = idle_connections_.front();
            idle_connections_.pop();
            return conn;
        }
        return nullptr;
    }

    void release(Connection::Ptr conn) {
        if (!conn || !conn->is_connected()) return;

        std::lock_guard<std::mutex> lock(mutex_);
        if (idle_connections_.size() < max_size_) {
            idle_connections_.push(conn);
        }
    }

    void add_connection(Connection::Ptr conn) {
        if (!conn || !conn->is_connected()) return;

        std::lock_guard<std::mutex> lock(mutex_);
        if (idle_connections_.size() < max_size_) {
            idle_connections_.push(conn);
        }
    }

private:
    size_t max_size_;
    std::mutex mutex_;
    std::queue<Connection::Ptr> idle_connections_;
};

} // namespace async_toolkit::network
