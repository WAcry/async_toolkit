#pragma once

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <type_traits>
#include <concepts>

namespace async_toolkit {

class TaskPool {
public:
    explicit TaskPool(size_t thread_count = std::thread::hardware_concurrency())
        : stop_(false) {
        for(size_t i = 0; i < thread_count; ++i) {
            workers_.emplace_back([this] {
                while(true) {
                    std::function<void()> task;
                    {
                        std::unique_lock lock(queue_mutex_);
                        condition_.wait(lock, [this] {
                            return stop_ || !tasks_.empty();
                        });
                        
                        if(stop_ && tasks_.empty()) {
                            return;
                        }
                        
                        task = std::move(tasks_.front());
                        tasks_.pop();
                    }
                    task();
                }
            });
        }
    }

    ~TaskPool() {
        {
            std::unique_lock lock(queue_mutex_);
            stop_ = true;
        }
        condition_.notify_all();
        for(auto& worker : workers_) {
            worker.join();
        }
    }

    template<typename F, typename... Args>
    requires std::invocable<F, Args...>
    auto submit(F&& f, Args&&... args) {
        using return_type = std::invoke_result_t<F, Args...>;
        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        
        std::future<return_type> result = task->get_future();
        {
            std::unique_lock lock(queue_mutex_);
            if(stop_) {
                throw std::runtime_error("Cannot submit task to stopped ThreadPool");
            }
            tasks_.emplace([task]() { (*task)(); });
        }
        condition_.notify_one();
        return result;
    }

    template<typename Pipeline, typename T>
    auto submit(T&& initial_value, Pipeline&& pipeline) {
        return submit([value = std::forward<T>(initial_value), 
                      pipeline = std::forward<Pipeline>(pipeline)]() mutable {
            return pipeline.process(std::move(value));
        });
    }

    size_t thread_count() const noexcept {
        return workers_.size();
    }

    size_t queued_tasks() const {
        std::unique_lock lock(queue_mutex_);
        return tasks_.size();
    }

private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    mutable std::mutex queue_mutex_;
    std::condition_variable condition_;
    bool stop_;
};

} // namespace async_toolkit
