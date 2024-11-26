#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>
#include <sstream>
#include <format>
#include "../lockfree/mpmc_queue.hpp"

namespace async_toolkit::logging {

enum class LogLevel {
    TRACE,
    DEBUG,
    INFO,
    WARN,
    ERROR,
    FATAL
};

struct LogMessage {
    LogLevel level;
    std::chrono::system_clock::time_point timestamp;
    std::string message;
    std::string file;
    int line;
    std::string function;
    std::thread::id thread_id;
};

class AsyncLogger {
public:
    static constexpr size_t DEFAULT_QUEUE_SIZE = 8192;
    static constexpr size_t MAX_FILE_SIZE = 100 * 1024 * 1024; // 100MB

    AsyncLogger(const std::string& log_dir, 
               const std::string& prefix = "app",
               size_t queue_size = DEFAULT_QUEUE_SIZE)
        : log_dir_(log_dir),
          file_prefix_(prefix),
          queue_(queue_size),
          running_(true),
          current_file_size_(0) {
        std::filesystem::create_directories(log_dir_);
        rotate_log_file();
        worker_ = std::thread(&AsyncLogger::process_logs, this);
    }

    ~AsyncLogger() {
        running_ = false;
        flush();
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    template<typename... Args>
    void log(LogLevel level, 
             const char* file,
             int line,
             const char* function,
             const std::string& format,
             Args&&... args) {
        auto now = std::chrono::system_clock::now();
        std::string formatted_message;
        try {
            formatted_message = std::vformat(format, std::make_format_args(args...));
        } catch (const std::format_error&) {
            formatted_message = format;
        }

        LogMessage msg{
            level,
            now,
            std::move(formatted_message),
            file,
            line,
            function,
            std::this_thread::get_id()
        };

        while (!queue_.try_enqueue(std::move(msg))) {
            std::this_thread::yield();
        }
    }

    void flush() {
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [this] { return queue_.empty(); });
    }

private:
    void process_logs() {
        std::vector<LogMessage> batch;
        batch.reserve(100);

        while (running_ || !queue_.empty()) {
            LogMessage msg;
            if (queue_.try_dequeue(msg)) {
                batch.push_back(std::move(msg));

                // Write to file when batch is full or queue is empty
                if (batch.size() >= 100 || queue_.empty()) {
                    write_batch(batch);
                    batch.clear();
                    cv_.notify_all();
                }
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }

        // Final batch
        if (!batch.empty()) {
            write_batch(batch);
            cv_.notify_all();
        }
    }

    void write_batch(const std::vector<LogMessage>& batch) {
        if (!log_file_.is_open()) {
            rotate_log_file();
        }

        for (const auto& msg : batch) {
            std::string log_entry = format_log_entry(msg);
            log_file_ << log_entry;
            current_file_size_ += log_entry.size();

            if (current_file_size_ >= MAX_FILE_SIZE) {
                rotate_log_file();
            }
        }

        log_file_.flush();
    }

    std::string format_log_entry(const LogMessage& msg) {
        static const char* level_strings[] = {
            "TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL"
        };

        auto time = std::chrono::system_clock::to_time_t(msg.timestamp);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            msg.timestamp.time_since_epoch()).count() % 1000;

        std::stringstream ss;
        ss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S")
           << '.' << std::setfill('0') << std::setw(3) << ms
           << " [" << level_strings[static_cast<int>(msg.level)] << "] "
           << "[" << msg.thread_id << "] "
           << msg.file << ":" << msg.line << " "
           << msg.function << " - "
           << msg.message << "\n";

        return ss.str();
    }

    void rotate_log_file() {
        if (log_file_.is_open()) {
            log_file_.close();
        }

        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::stringstream ss;
        ss << log_dir_ << "/"
           << file_prefix_ << "_"
           << std::put_time(std::localtime(&time), "%Y%m%d_%H%M%S")
           << ".log";

        log_file_.open(ss.str(), std::ios::app);
        current_file_size_ = 0;
    }

private:
    std::string log_dir_;
    std::string file_prefix_;
    lockfree::MPMCQueue<LogMessage> queue_;
    std::atomic<bool> running_;
    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::ofstream log_file_;
    size_t current_file_size_;
};

// Global logger instance
inline std::unique_ptr<AsyncLogger> g_logger;

// Initialize logging system
inline void init_logging(const std::string& log_dir, 
                        const std::string& prefix = "app",
                        size_t queue_size = AsyncLogger::DEFAULT_QUEUE_SIZE) {
    g_logger = std::make_unique<AsyncLogger>(log_dir, prefix, queue_size);
}

// Log macros
#define LOG_TRACE(...) \
    if (g_logger) g_logger->log(LogLevel::TRACE, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define LOG_DEBUG(...) \
    if (g_logger) g_logger->log(LogLevel::DEBUG, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define LOG_INFO(...) \
    if (g_logger) g_logger->log(LogLevel::INFO, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define LOG_WARN(...) \
    if (g_logger) g_logger->log(LogLevel::WARN, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define LOG_ERROR(...) \
    if (g_logger) g_logger->log(LogLevel::ERROR, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define LOG_FATAL(...) \
    if (g_logger) g_logger->log(LogLevel::FATAL, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)

} // namespace async_toolkit::logging
