#pragma once
#include <vector>
#include <cstddef>
#include <utility>
#include <new>        // operator new/delete, std::align_val_t
#include <memory>     // std::construct_at, std::destroy_at
#include <type_traits>
#include <algorithm>  // std::min
#include <atomic>
#include <mutex>
#include "engine/DebugTool/ConsoleHelp/color_log.h"

enum class ChunkHeadState{
    READ,
    WRITE,
    CHANGE,
    IDLE
};

struct ChunkHead{
    void* data;
    bool isOccupied;
    bool isWaited;
    std::mutex requestMutex;
    ChunkHeadState state = ChunkHeadState::IDLE;
};


template <typename T>
class FixedChunkArray {
    friend class ChunkSchedule;
private:
    size_t size_ = 0;          // 已构造元素个数
    size_t capacity_ = 0;      // 可容纳元素总数（chunk * sizePerChuck）
    size_t sizePerChuck_ = 0;
    size_t lastChunkCount_ = 0;
    std::vector<ChunkHead> chunkHeads_; // 指向 raw storage（未必构造）

    static T& at_chunks(std::vector<T*>& chunks, size_t sizePerChunk, size_t index) {
        return chunks[index / sizePerChunk][index % sizePerChunk];
    }
    // static T& at_chunks(const std::vector<T*>& chunks, size_t sizePerChunk, size_t index) const {
    //     return chunks[index / sizePerChunk][index % sizePerChunk];
    // }

    static T* alloc_chunk(size_t count) {
        // raw storage，按 T 的对齐分配
        void* mem = ::operator new(sizeof(T) * count, std::align_val_t(alignof(T)));
        return reinterpret_cast<T*>(mem);
    }

    static void free_chunk(T* p) noexcept {
        ::operator delete(p, std::align_val_t(alignof(T)));
    }

    void ensure_capacity_for_one_more() {
        if (size_ < capacity_) return;

        const size_t n = sizePerChuck_;
        T* raw = nullptr;
        try {
            raw = alloc_chunk(n);
        } catch (...) {
            LOG_WARNING("FixedChuckArray", "Chunk raw allocation failed.");
            throw;
        }

        chunkHeads_.push_back(raw);
        capacity_ += n;
    }

    // 只 destroy 已构造区间 [0, size_)
    void destroy_constructed_range() noexcept {
        for (size_t i = 0; i < size_; ++i) {
            std::destroy_at(&at_chunks(chunkHeads_, sizePerChuck_, i));
        }
        size_ = 0;
    }

public:
    explicit FixedChunkArray(size_t sizePerChuck) {
        if (sizePerChuck > static_cast<size_t>(2048)) {
            LOG_WARNING("FixedChuckArray",
                        "sizePerChuck too large, clamped to 2048: " + std::to_string(sizePerChuck));
        }
        sizePerChuck_ = std::min<size_t>(sizePerChuck, 2048);
        if (sizePerChuck_ == 0) {
            sizePerChuck_ = 1;
            LOG_WARNING("FixedChuckArray", "sizePerChuck was 0, clamped to 1.");
        }
    }

    ~FixedChunkArray() noexcept {
        destroy_constructed_range();
        for (T* p : chunkHeads_) {
            free_chunk(p);
        }
    }

    FixedChunkArray(const FixedChunkArray&) = delete;
    FixedChunkArray& operator=(const FixedChunkArray&) = delete;

    FixedChunkArray(FixedChunkArray&& other) noexcept
        : size_(other.size_),
          capacity_(other.capacity_),
          sizePerChuck_(other.sizePerChuck_),
          chunkHeads_(std::move(other.chunkHeads_)) {
        other.size_ = 0;
        other.capacity_ = 0;
        other.sizePerChuck_ = 0;
    }

    FixedChunkArray& operator=(FixedChunkArray&& other) noexcept {
        if (this == &other) return *this;

        // 释放自身
        destroy_constructed_range();
        for (T* p : chunkHeads_) free_chunk(p);

        size_ = other.size_;
        capacity_ = other.capacity_;
        sizePerChuck_ = other.sizePerChuck_;
        chunkHeads_ = std::move(other.chunkHeads_);

        other.size_ = 0;
        other.capacity_ = 0;
        other.sizePerChuck_ = 0;
        return *this;
    }

    size_t size() const { return size_; }
    size_t capacity() const { return capacity_; }
    bool empty() const { return size_ == 0; }

    // 访问已构造元素；你也可以加 assert(index < size_)
    T& operator[](size_t index) {
        return at_chunks(chunkHeads_, sizePerChuck_, index);
    }

    T& operator[](size_t index) const {
        return at_chunks(chunkHeads_, sizePerChuck_, index);
    }

    template <class... Args>
    T& emplace_back(Args&&... args) {
        ensure_capacity_for_one_more();
        T& slot = at_chunks(chunkHeads_, sizePerChuck_, size_);
        std::construct_at(&slot, std::forward<Args>(args)...);
        ++size_;
        return slot;
    }

    void push_back(const T& other) { emplace_back(other); }
    void push_back(T&& other) { emplace_back(std::move(other)); }

    // swap 两个“已构造”元素
    void swap(size_t a, size_t b) {
        if (a >= size_ || b >= size_) return;
        using std::swap;
        swap(at_chunks(chunkHeads_, sizePerChuck_, a),
             at_chunks(chunkHeads_, sizePerChuck_, b));
    }

    // O(1) remove：用 last 覆盖 index，然后 destroy last
    void remove(size_t index) { 
        if (index >= size_) return;

        const size_t last = size_ - 1;
        if (index != last) {
            // 用 move 覆盖，避免 swap 可能对未定义状态敏感（可选）
            T& dst = at_chunks(chunkHeads_, sizePerChuck_, index);
            T& src = at_chunks(chunkHeads_, sizePerChuck_, last);
            dst = std::move(src);
        }

        // 销毁最后一个已构造元素
        std::destroy_at(&at_chunks(chunkHeads_, sizePerChuck_, last));
        --size_;
    }

    void clear() noexcept {
        destroy_constructed_range(); // destroy [0,size_)
    }
};