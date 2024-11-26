# C++ Async Toolkit

A modern C++ high-performance asynchronous stream processing and concurrency toolkit that provides a rich set of concurrent programming tools and abstractions.

## Features

- 💪 High-performance async task processor
- 🔄 Stream processing with chainable API
- 🎯 Thread pool and task scheduler
- 🚀 Lock-free data structures
- 📦 Async Future/Promise implementation
- 🛠 Concurrency toolkit
- 🎭 Coroutine support
- ⚡ Zero-copy design
- 🔒 RAII resource management
- 📊 Task dependency graph
- ⏰ Priority scheduling
- 🎯 Task cancellation support

## Components

### 1. Lock-free Data Structures

```cpp
#include <async_toolkit/lockfree/queue.hpp>

async_toolkit::lockfree::Queue<int> queue;
queue.push(42);
auto value = queue.pop(); // std::optional<int>
```

### 2. Coroutine Support

```cpp
#include <async_toolkit/coroutine/task.hpp>

async_toolkit::coroutine::Task<int> async_task() {
    co_return 42;
}

auto task = async_task();
int result = task.get(); // 42
```

### 3. Priority Scheduler

```cpp
#include <async_toolkit/scheduler/priority_scheduler.hpp>

async_toolkit::scheduler::PriorityScheduler scheduler;
auto task_id = scheduler.schedule([]{ std::cout << "High priority task\n"; }, 10);
scheduler.schedule_after([]{ std::cout << "Delayed task\n"; }, 
                        std::chrono::seconds(5));
scheduler.cancel(task_id); // Cancel task
```

### 4. Task Dependency Graph

```cpp
#include <async_toolkit/graph/task_graph.hpp>

auto graph = async_toolkit::graph::make_task_graph<int>();
auto task1 = graph->add_task([]{ return 1; });
auto task2 = graph->add_task([]{ return 2; });
auto task3 = graph->add_task([]{ return 3; });

graph->add_dependency(task3, task1); // task3 depends on task1
graph->add_dependency(task3, task2); // task3 depends on task2

async_toolkit::TaskPool pool;
auto results = graph->execute(pool); // [1, 2, 3]
```

### 5. High-Performance Memory Pool

```cpp
#include <async_toolkit/memory/memory_pool.hpp>

// Create memory pool
async_toolkit::memory::MemoryPool<MyClass> pool;

// Allocate object
auto ptr = pool.allocate(arg1, arg2);

// Use RAII for memory management
auto smart_ptr = async_toolkit::memory::PoolPtr<MyClass>::make(pool, arg1, arg2);
```

### 6. High-Performance Memory Allocator

```cpp
#include <async_toolkit/memory/allocator.hpp>

// Use global allocator
void* ptr = async_toolkit::memory::Allocator::instance().allocate(1024);
async_toolkit::memory::Allocator::instance().deallocate(ptr, 1024);

// Use STL allocator wrapper
std::vector<int, async_toolkit::memory::StlAllocator<int>> vec;
vec.push_back(42);

// Get memory usage statistics
auto stats = async_toolkit::memory::Allocator::instance().get_stats();
std::cout << "Total allocations: " << stats.total_allocations << "\n"
          << "Active allocations: " << stats.active_allocations << "\n"
          << "Allocated bytes: " << stats.allocated_bytes << "\n"
          << "Freed bytes: " << stats.freed_bytes << "\n";

// Trigger memory defragmentation
async_toolkit::memory::Allocator::instance().collect_garbage();
```

### 7. Coroutine Scheduler

The Coroutine Scheduler provides efficient scheduling and management of C++20 coroutines. It supports features like:
- Cooperative multitasking
- Priority-based coroutine scheduling
- Coroutine synchronization primitives
- Automatic stack management
- Integration with the event loop

### 8. High-Performance RPC Framework

The RPC Framework offers:
- High-performance client-server communication
- Automatic serialization/deserialization
- Load balancing
- Service discovery
- Connection pooling
- Request timeout and retry mechanisms
- Bi-directional streaming support
- Protocol buffer integration

### 9. Pipeline

```cpp
#include <async_toolkit/task_pool.hpp>
#include <async_toolkit/pipeline.hpp>

// Create task pool
async_toolkit::TaskPool pool(4); // 4 worker threads

// Create processing pipeline
auto pipeline = async_toolkit::Pipeline::create()
    .then([](int x) { return x * 2; })
    .then([](int x) { return std::to_string(x); })
    .then([](std::string x) { return "Result: " + x; });

// Submit task
auto future = pool.submit(42, pipeline);
auto result = future.get(); // "Result: 84"
```

### 10. SIMD Vectorization

The SIMD module provides:
- Vectorized operations for common algorithms
- SIMD-aware memory allocator
- Platform-specific optimizations
- Automatic fallback for unsupported architectures
- Template library for vector operations

### 11. High-Performance Channels

```cpp
#include <async_toolkit/channel/mpmc_channel.hpp>

// Create multi-producer multi-consumer channel with capacity 1024
async_toolkit::channel::MPMCChannel<std::string> channel(1024);

// Send message
channel.try_send("Hello", std::chrono::milliseconds(100));

// Receive message
auto msg = channel.try_receive(std::chrono::milliseconds(100));
```

### 12. Lock-free Skip List

```cpp
#include <async_toolkit/lockfree/skiplist.hpp>

// Create skip list
async_toolkit::lockfree::SkipList<int, std::string> list;

// Insert data
list.insert(1, "one");
list.insert(2, "two");

// Find data
auto value = list.find(1); // std::optional<std::string>
```

### 13. Actor System

```cpp
#include <async_toolkit/actor/actor.hpp>

// Define an Actor
class MyActor : public async_toolkit::actor::Actor {
public:
    MyActor() {
        register_handler<std::string>([this](const std::string& msg, ActorRef sender) {
            std::cout << "Received: " << msg << std::endl;
        });
    }
};

// Create Actor system
async_toolkit::actor::ActorSystem system;
auto actor = system.spawn_actor<MyActor>();

// Send message
actor->tell("Hello, Actor!");
```

### 14. Event Loop

```cpp
#include <async_toolkit/reactor/event_loop.hpp>

// Create event loop
async_toolkit::reactor::EventLoop loop;

// Create TCP server
async_toolkit::net::TcpServer server(&loop, "127.0.0.1", 8080);

// Register connection callback
server.set_connection_callback([](auto fd) {
    std::cout << "New connection\n";
});

// Register timer
loop.run_after(std::chrono::seconds(5), []() {
    std::cout << "Timer triggered\n";
});

// Start event loop
loop.run();
```

### 15. Work-Stealing Scheduler

```cpp
#include <async_toolkit/scheduler/work_stealing_scheduler.hpp>

// Create scheduler
async_toolkit::scheduler::WorkStealingScheduler scheduler(4); // 4 worker threads

// Submit single task
scheduler.submit([]{ std::cout << "Task executed\n"; });

// Submit task with priority
scheduler.submit_with_priority(
    []{ std::cout << "High priority task\n"; }, 
    10
);

// Submit batch of tasks
std::vector<std::function<void()>> tasks = {
    []{ std::cout << "Task 1\n"; },
    []{ std::cout << "Task 2\n"; },
    []{ std::cout << "Task 3\n"; }
};
scheduler.submit_batch(tasks.begin(), tasks.end());
```

### 16. Lock-free B+ Tree

```cpp
#include <async_toolkit/lockfree/bptree.hpp>

// Create B+ tree instance
async_toolkit::lockfree::BPlusTree<int, std::string> tree;

// Insert key-value pair
tree.insert(1, "one");
tree.insert(2, "two");
tree.insert(3, "three");

// Find value
auto value = tree.find(2);  // Returns "two"

// Range query
std::vector<std::pair<int, std::string>> result;
tree.range_query(1, 3, std::back_inserter(result));

// Remove key-value pair
tree.remove(2);
```

### 17. Asynchronous Logging System

```cpp
#include <async_toolkit/logging/async_logger.hpp>

// Initialize logging system
async_toolkit::logging::init_logging("logs", "myapp");

// Use logging macros for different log levels
LOG_TRACE("Starting application...");
LOG_DEBUG("Debug information: {}", debug_info);
LOG_INFO("Processing file: {}", filename);
LOG_WARN("Resource usage high: {}%", usage);
LOG_ERROR("Failed to open file: {}", error);
LOG_FATAL("System crash: {}", crash_reason);

// Flush logs before program exit
g_logger->flush();
```

## Requirements

- C++20 compatible compiler
- CMake 3.14 or higher
- Supported compilers:
    - GCC 10+
    - Clang 10+
    - MSVC 2019+

## Building

```bash
mkdir build
cd build
cmake ..
cmake --build .
```

## License

MIT License
