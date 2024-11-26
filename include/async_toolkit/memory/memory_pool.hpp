#pragma once

#include <cstddef>
#include <memory>
#include <vector>
#include <array>
#include <mutex>
#include <cassert>
#include <new>

namespace async_toolkit::memory {

template<typename T, size_t BlockSize = 4096>
class MemoryPool {
    static_assert(BlockSize >= sizeof(T), "BlockSize must be at least sizeof(T)");
    static_assert(BlockSize >= sizeof(void*), "BlockSize must be at least sizeof(void*)");
    static_assert(BlockSize && ((BlockSize & (BlockSize - 1)) == 0), "BlockSize must be a power of 2");

    struct Block {
        alignas(std::max_align_t) std::array<std::byte, BlockSize> data;
        Block* next;
    };

    struct Chunk {
        static constexpr size_t chunk_size = 64 * 1024; // 64KB
        std::array<Block, chunk_size / sizeof(Block)> blocks;
    };

public:
    MemoryPool() noexcept : free_list_(nullptr) {}

    ~MemoryPool() {
        for (auto& chunk : chunks_) {
            delete chunk;
        }
    }

    template<typename... Args>
    T* allocate(Args&&... args) {
        void* ptr = allocate_block();
        return new(ptr) T(std::forward<Args>(args)...);
    }

    void deallocate(T* ptr) noexcept {
        if (!ptr) return;
        
        ptr->~T();
        auto block = reinterpret_cast<Block*>(ptr);
        
        std::lock_guard<std::mutex> lock(mutex_);
        block->next = free_list_;
        free_list_ = block;
    }

    size_t allocated_size() const noexcept {
        return chunks_.size() * Chunk::chunk_size;
    }

private:
    void* allocate_block() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (!free_list_) {
            allocate_chunk();
        }
        
        Block* block = free_list_;
        free_list_ = block->next;
        return block;
    }

    void allocate_chunk() {
        auto chunk = new Chunk();
        chunks_.push_back(chunk);
        
        // Initialize free list with new blocks
        for (size_t i = 0; i < chunk->blocks.size() - 1; ++i) {
            chunk->blocks[i].next = &chunk->blocks[i + 1];
        }
        chunk->blocks.back().next = free_list_;
        free_list_ = &chunk->blocks[0];
    }

    Block* free_list_;
    std::vector<Chunk*> chunks_;
    std::mutex mutex_;
};

// RAII Packed Memory Pool
template<typename T, size_t BlockSize = 4096>
class PoolPtr {
public:
    PoolPtr() : ptr_(nullptr), pool_(nullptr) {}
    
    template<typename... Args>
    static PoolPtr make(MemoryPool<T, BlockSize>& pool, Args&&... args) {
        return PoolPtr(pool.allocate(std::forward<Args>(args)...), &pool);
    }

    ~PoolPtr() {
        if (ptr_ && pool_) {
            pool_->deallocate(ptr_);
        }
    }

    PoolPtr(PoolPtr&& other) noexcept
        : ptr_(other.ptr_), pool_(other.pool_) {
        other.ptr_ = nullptr;
        other.pool_ = nullptr;
    }

    PoolPtr& operator=(PoolPtr&& other) noexcept {
        if (this != &other) {
            if (ptr_ && pool_) {
                pool_->deallocate(ptr_);
            }
            ptr_ = other.ptr_;
            pool_ = other.pool_;
            other.ptr_ = nullptr;
            other.pool_ = nullptr;
        }
        return *this;
    }

    T* get() const noexcept { return ptr_; }
    T* operator->() const noexcept { return ptr_; }
    T& operator*() const noexcept { return *ptr_; }
    explicit operator bool() const noexcept { return ptr_ != nullptr; }

private:
    PoolPtr(T* ptr, MemoryPool<T, BlockSize>* pool)
        : ptr_(ptr), pool_(pool) {}

    T* ptr_;
    MemoryPool<T, BlockSize>* pool_;
};

} // namespace async_toolkit::memory
