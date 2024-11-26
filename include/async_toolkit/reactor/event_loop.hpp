#pragma once

#include <memory>
#include <functional>
#include <unordered_map>
#include <vector>
#include <chrono>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include "../executor/thread_pool_executor.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>
#endif

namespace async_toolkit::reactor {

using FileDescriptor = 
#ifdef _WIN32
    SOCKET
#else
    int
#endif
;

class EventLoop {
public:
    using EventCallback = std::function<void()>;
    using TimerCallback = std::function<void()>;

    EventLoop()
        : running_(false) {
#ifdef _WIN32
        WSADATA wsa_data;
        WSAStartup(MAKEWORD(2, 2), &wsa_data);
        event_handle_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
#else
        epoll_fd_ = epoll_create1(0);
#endif
    }

    ~EventLoop() {
#ifdef _WIN32
        CloseHandle(event_handle_);
        WSACleanup();
#else
        close(epoll_fd_);
#endif
    }

    void run() {
        running_ = true;
        while (running_) {
            process_timers();
            process_events();
        }
    }

    void stop() {
        running_ = false;
    }

    // 注册IO事件处理器
    void register_handler(FileDescriptor fd, EventCallback callback) {
#ifdef _WIN32
        CreateIoCompletionPort((HANDLE)fd, event_handle_, 
                             (ULONG_PTR)new EventCallback(callback), 0);
#else
        epoll_event event;
        event.events = EPOLLIN | EPOLLET;
        event.data.ptr = new EventCallback(callback);
        epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &event);
#endif
        handlers_[fd] = callback;
    }

    // 注册定时器
    void register_timer(std::chrono::milliseconds delay, TimerCallback callback,
                       bool periodic = false) {
        auto deadline = std::chrono::steady_clock::now() + delay;
        std::lock_guard<std::mutex> lock(timer_mutex_);
        timers_.push({deadline, callback, delay, periodic});
        timer_cv_.notify_one();
    }

    // 取消定时器
    void cancel_timer(const TimerCallback& callback) {
        std::lock_guard<std::mutex> lock(timer_mutex_);
        // 实现定时器取消逻辑
    }

private:
    struct Timer {
        std::chrono::steady_clock::time_point deadline;
        TimerCallback callback;
        std::chrono::milliseconds period;
        bool periodic;

        bool operator>(const Timer& other) const {
            return deadline > other.deadline;
        }
    };

    void process_events() {
#ifdef _WIN32
        DWORD bytes_transferred;
        ULONG_PTR completion_key;
        LPOVERLAPPED overlapped;
        
        if (GetQueuedCompletionStatus(event_handle_, &bytes_transferred,
                                    &completion_key, &overlapped, 100)) {
            auto callback = (EventCallback*)completion_key;
            (*callback)();
        }
#else
        epoll_event events[64];
        int nfds = epoll_wait(epoll_fd_, events, 64, 100);
        
        for (int i = 0; i < nfds; ++i) {
            auto callback = (EventCallback*)events[i].data.ptr;
            (*callback)();
        }
#endif
    }

    void process_timers() {
        std::unique_lock<std::mutex> lock(timer_mutex_);
        auto now = std::chrono::steady_clock::now();

        while (!timers_.empty() && timers_.top().deadline <= now) {
            auto timer = timers_.top();
            timers_.pop();

            lock.unlock();
            timer.callback();
            lock.lock();

            if (timer.periodic) {
                timer.deadline += timer.period;
                timers_.push(timer);
            }
        }
    }

#ifdef _WIN32
    HANDLE event_handle_;
#else
    int epoll_fd_;
#endif
    std::atomic<bool> running_;
    std::unordered_map<FileDescriptor, EventCallback> handlers_;
    std::priority_queue<Timer, std::vector<Timer>, std::greater<Timer>> timers_;
    std::mutex timer_mutex_;
    std::condition_variable timer_cv_;
};

// 便捷的TCP服务器包装
class TcpServer {
public:
    TcpServer(EventLoop& loop, uint16_t port)
        : loop_(loop) {
        // 初始化服务器socket
#ifdef _WIN32
        server_socket_ = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, 
                                 WSA_FLAG_OVERLAPPED);
#else
        server_socket_ = socket(AF_INET, SOCK_STREAM, 0);
        fcntl(server_socket_, F_SETFL, O_NONBLOCK);
#endif

        sockaddr_in addr;
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);
        bind(server_socket_, (sockaddr*)&addr, sizeof(addr));
        listen(server_socket_, SOMAXCONN);

        // 注册接受连接的处理器
        loop_.register_handler(server_socket_, [this] { accept_connection(); });
    }

    void set_connection_callback(std::function<void(FileDescriptor)> callback) {
        on_connection_ = std::move(callback);
    }

private:
    void accept_connection() {
        sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);
#ifdef _WIN32
        auto client_socket = WSAAccept(server_socket_, (sockaddr*)&client_addr,
                                     &addr_len, NULL, 0);
#else
        auto client_socket = accept(server_socket_, (sockaddr*)&client_addr,
                                  &addr_len);
#endif

        if (on_connection_) {
            on_connection_(client_socket);
        }
    }

    EventLoop& loop_;
    FileDescriptor server_socket_;
    std::function<void(FileDescriptor)> on_connection_;
};

} // namespace async_toolkit::reactor
