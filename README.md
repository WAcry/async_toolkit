# C++ Async Toolkit

一个现代C++高性能异步流式处理和并发工具库，提供了丰富的并发编程工具和抽象。

## 特性

- 💪 高性能异步任务处理器
- 🔄 流式处理链式API
- 🎯 线程池和任务调度器
- 🚀 无锁数据结构
- 📦 异步Future/Promise实现
- 🛠 并发工具集合
- 🎭 协程支持
- ⚡ 零拷贝设计
- 🔒 RAII资源管理

## 要求

- C++20 兼容的编译器
- CMake 3.14 或更高版本
- 支持的编译器:
  - GCC 10+
  - Clang 10+
  - MSVC 2019+

## 构建

```bash
mkdir build
cd build
cmake ..
cmake --build .
```

## 使用示例

```cpp
#include <async_toolkit/task_pool.hpp>
#include <async_toolkit/pipeline.hpp>

// 创建任务池
async_toolkit::TaskPool pool(4); // 4个工作线程

// 创建处理管道
auto pipeline = async_toolkit::Pipeline::create()
    .then([](int x) { return x * 2; })
    .then([](int x) { return std::to_string(x); })
    .then([](std::string x) { return "Result: " + x; });

// 提交任务
auto future = pool.submit(42, pipeline);
auto result = future.get(); // "Result: 84"
```

## 许可

MIT License
