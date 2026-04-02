#pragma once
#include <vector>
#include <cstddef>
#include <utility>
#include <new>
#include <memory>
#include <type_traits>
#include <algorithm>
#include "engine/DebugTool/ConsoleHelp/color_log.h"

enum class ChunkHeadState{
    READ,
    WRITE,
    CHANGE,
    IDLE
};

template <typename T>
class FixedChunkArray {
private:
    size_t size_ = 0;
    size_t capacity_ = 0;
    size_t sizePerChuck_ = 0;
    std::vector<T*> chunks_;

    static T& at_chunks(std::vector<T*>& chunks, size_t sizePerChunk, size_t index) {
        return chunks[index / sizePerChunk][index % sizePerChunk];
    }
    static const T& at_chunks(const std::vector<T*>& chunks, size_t sizePerChunk, size_t index) {
        return chunks[index / sizePerChunk][index % sizePerChunk];
    }

    static T* alloc_chunk(size_t count) {
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

        chunks_.push_back(raw);
        capacity_ += n;
    }

    void destroy_constructed_range() noexcept {
        for (size_t i = 0; i < size_; ++i) {
            std::destroy_at(&at_chunks(chunks_, sizePerChuck_, i));
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
        for (T* p : chunks_) {
            free_chunk(p);
        }
    }

    FixedChunkArray(const FixedChunkArray&) = delete;
    FixedChunkArray& operator=(const FixedChunkArray&) = delete;

    FixedChunkArray(FixedChunkArray&& other) noexcept
        : size_(other.size_),
          capacity_(other.capacity_),
          sizePerChuck_(other.sizePerChuck_),
          chunks_(std::move(other.chunks_)) {
        other.size_ = 0;
        other.capacity_ = 0;
        other.sizePerChuck_ = 0;
    }

    FixedChunkArray& operator=(FixedChunkArray&& other) noexcept {
        if (this == &other) return *this;

        destroy_constructed_range();
        for (T* p : chunks_) free_chunk(p);

        size_ = other.size_;
        capacity_ = other.capacity_;
        sizePerChuck_ = other.sizePerChuck_;
        chunks_ = std::move(other.chunks_);

        other.size_ = 0;
        other.capacity_ = 0;
        other.sizePerChuck_ = 0;
        return *this;
    }

    size_t size() const { return size_; }
    size_t capacity() const { return capacity_; }
    bool empty() const { return size_ == 0; }
    size_t ChunkCount() const { return chunks_.size(); }
    size_t SizePerChunk() const { return sizePerChuck_; }

    T* GetChunkData(size_t chunkIndex) {
        return chunkIndex < chunks_.size() ? chunks_[chunkIndex] : nullptr;
    }

    const T* GetChunkData(size_t chunkIndex) const {
        return chunkIndex < chunks_.size() ? chunks_[chunkIndex] : nullptr;
    }

    T& operator[](size_t index) {
        return at_chunks(chunks_, sizePerChuck_, index);
    }

    const T& operator[](size_t index) const {
        return at_chunks(chunks_, sizePerChuck_, index);
    }

    template <class... Args>
    T& emplace_back(Args&&... args) {
        ensure_capacity_for_one_more();
        T& slot = at_chunks(chunks_, sizePerChuck_, size_);
        std::construct_at(&slot, std::forward<Args>(args)...);
        ++size_;
        return slot;
    }

    void push_back(const T& other) { emplace_back(other); }
    void push_back(T&& other) { emplace_back(std::move(other)); }

    void swap(size_t a, size_t b) {
        if (a >= size_ || b >= size_) return;
        using std::swap;
        swap(at_chunks(chunks_, sizePerChuck_, a), at_chunks(chunks_, sizePerChuck_, b));
    }

    void remove(size_t index) {
        if (index >= size_) return;

        const size_t last = size_ - 1;
        if (index != last) {
            T& dst = at_chunks(chunks_, sizePerChuck_, index);
            T& src = at_chunks(chunks_, sizePerChuck_, last);
            dst = std::move(src);
        }

        std::destroy_at(&at_chunks(chunks_, sizePerChuck_, last));
        --size_;
    }

    void clear() noexcept {
        destroy_constructed_range();
    }
};
