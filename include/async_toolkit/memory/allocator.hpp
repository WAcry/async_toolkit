#pragma once

#include <atomic>
#include <array>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>
#include "../lockfree/mpmc_queue.hpp"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

namespace async_toolkit::memory {

// 内存块大小类别
struct SizeClass {
    static constexpr size_t TINY_MAX = 256;
    static constexpr size_t SMALL_MAX = 4096;
    static constexpr size_t MEDIUM_MAX = 65536;
    
    static size_t get_size_class(size_t size) {
        if (size <= TINY_MAX) return (size + 15) & ~15;
        if (size <= SMALL_MAX) return (size + 127) & ~127;
        if (size <= MEDIUM_MAX) return (size + 4095) & ~4095;
        return size;
    }
};

// 内存统计信息快照
struct MemoryStatsSnapshot {
    size_t allocated_bytes;
    size_t freed_bytes;
    size_t active_allocations;
    size_t total_allocations;
    size_t fragmentation_bytes;
};

// 内存统计信息
struct MemoryStats {
    std::atomic<size_t> allocated_bytes{0};
    std::atomic<size_t> freed_bytes{0};
    std::atomic<size_t> active_allocations{0};
    std::atomic<size_t> total_allocations{0};
    std::atomic<size_t> fragmentation_bytes{0};
    
    void record_allocation(size_t size) {
        allocated_bytes += size;
        active_allocations++;
        total_allocations++;
    }
    
    void record_deallocation(size_t size) {
        freed_bytes += size;
        active_allocations--;
    }

    MemoryStatsSnapshot get_snapshot() const {
        return MemoryStatsSnapshot{
            allocated_bytes.load(),
            freed_bytes.load(),
            active_allocations.load(),
            total_allocations.load(),
            fragmentation_bytes.load()
        };
    }
};

// 内存块头部信息
struct BlockHeader {
    size_t size;
    size_t original_size;
    bool is_huge_page;
    void* next;
};

// 线程本地缓存
class ThreadCache {
public:
    static constexpr size_t NUM_SIZE_CLASSES = 32;
    static constexpr size_t CACHE_SIZE = 1024;

    void* allocate(size_t size) {
        size_t size_class = SizeClass::get_size_class(size);
        auto& free_list = free_lists_[get_index(size_class)];
        
        if (!free_list.empty()) {
            void* ptr = free_list.back();
            free_list.pop_back();
            return ptr;
        }
        
        return fetch_from_central_cache(size_class);
    }
    
    void deallocate(void* ptr, size_t size) {
        size_t size_class = SizeClass::get_size_class(size);
        auto& free_list = free_lists_[get_index(size_class)];
        
        if (free_list.size() < CACHE_SIZE) {
            free_list.push_back(ptr);
        } else {
            return_to_central_cache(ptr, size_class);
        }
    }

private:
    static size_t get_index(size_t size_class) {
        return (size_class / 16) - 1;
    }
    
    void* fetch_from_central_cache(size_t size_class);
    void return_to_central_cache(void* ptr, size_t size_class);
    
    std::array<std::vector<void*>, NUM_SIZE_CLASSES> free_lists_;
};

// 大页内存管理
class HugePageAllocator {
public:
    static void* allocate(size_t size) {
#ifdef _WIN32
        void* ptr = VirtualAlloc(nullptr, size,
                                MEM_LARGE_PAGES | MEM_COMMIT | MEM_RESERVE,
                                PAGE_READWRITE);
#else
        void* ptr = mmap(nullptr, size,
                        PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB,
                        -1, 0);
#endif
        return ptr;
    }
    
    static void deallocate(void* ptr, size_t size) {
#ifdef _WIN32
        VirtualFree(ptr, 0, MEM_RELEASE);
#else
        munmap(ptr, size);
#endif
    }
};

// 中央缓存
class CentralCache {
public:
    void* allocate(size_t size_class) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& span_list = spans_[size_class];
        
        if (span_list.empty()) {
            allocate_new_span(size_class);
        }
        
        void* ptr = span_list.back();
        span_list.pop_back();
        stats_.record_allocation(size_class);
        return ptr;
    }
    
    void deallocate(void* ptr, size_t size_class) {
        std::lock_guard<std::mutex> lock(mutex_);
        spans_[size_class].push_back(ptr);
        stats_.record_deallocation(size_class);
    }
    
    MemoryStatsSnapshot get_stats() const {
        return stats_.get_snapshot();
    }

private:
    void allocate_new_span(size_t size_class);
    
    std::mutex mutex_;
    std::unordered_map<size_t, std::vector<void*>> spans_;
    MemoryStats stats_;
};

// 主分配器
class Allocator {
public:
    static Allocator& instance() {
        static Allocator inst;
        return inst;
    }
    
    void* allocate(size_t size) {
        if (size > SizeClass::MEDIUM_MAX) {
            return allocate_huge(size);
        }
        
        size_t size_class = SizeClass::get_size_class(size);
        return thread_cache().allocate(size_class);
    }
    
    void deallocate(void* ptr, size_t size) {
        if (!ptr) return;
        
        BlockHeader* header = static_cast<BlockHeader*>(ptr) - 1;
        if (header->is_huge_page) {
            deallocate_huge(ptr);
            return;
        }
        
        thread_cache().deallocate(ptr, header->size);
    }
    
    void collect_garbage() {
        // 触发内存碎片整理
        compact_memory();
    }
    
    MemoryStatsSnapshot get_stats() const {
        return central_cache_.get_stats();
    }

private:
    Allocator() = default;
    
    void* allocate_huge(size_t size) {
        size_t total_size = size + sizeof(BlockHeader);
        void* ptr = HugePageAllocator::allocate(total_size);
        
        if (ptr) {
            BlockHeader* header = static_cast<BlockHeader*>(ptr);
            header->size = size;
            header->original_size = total_size;
            header->is_huge_page = true;
            header->next = nullptr;
            
            return header + 1;
        }
        return nullptr;
    }
    
    void deallocate_huge(void* ptr) {
        BlockHeader* header = static_cast<BlockHeader*>(ptr) - 1;
        HugePageAllocator::deallocate(header, header->original_size);
    }
    
    void compact_memory() {
        // 实现内存碎片整理逻辑
        // 1. 扫描空闲块
        // 2. 合并相邻的空闲块
        // 3. 重新组织内存布局
    }
    
    static ThreadCache& thread_cache() {
        thread_local ThreadCache cache;
        return cache;
    }
    
    CentralCache central_cache_;
};

// 分配器包装器
template<typename T>
class StlAllocator {
public:
    using value_type = T;
    
    T* allocate(size_t n) {
        return static_cast<T*>(Allocator::instance().allocate(n * sizeof(T)));
    }
    
    void deallocate(T* ptr, size_t n) {
        Allocator::instance().deallocate(ptr, n * sizeof(T));
    }
};

} // namespace async_toolkit::memory
