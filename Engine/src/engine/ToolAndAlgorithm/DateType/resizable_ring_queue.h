// ResizableRingQueue.h
#pragma once
#include <vector>
#include <utility>
#include <stdexcept>
#include <type_traits>

template<class T>
class ResizableRingQueue {
public:
    ResizableRingQueue() = default;
    explicit ResizableRingQueue(size_t cap) { reserve(cap); }

    // 基本查询
    size_t size()     const noexcept { return count_; }
    size_t capacity() const noexcept { return buf_.size(); }
    bool   empty()    const noexcept { return count_ == 0; }
    bool   full()     const noexcept { return count_ == buf_.size() && !buf_.empty(); }

    // 访问（有/无 const）
    T& front() {
        if (empty()) throw std::out_of_range("front on empty queue");
        return buf_[head_];
    }
    const T& front() const {
        if (empty()) throw std::out_of_range("front on empty queue");
        return buf_[head_];
    }
    T& back() {
        if (empty()) throw std::out_of_range("back on empty queue");
        return buf_[wrap_(head_ + count_ - 1)];
    }
    const T& back() const {
        if (empty()) throw std::out_of_range("back on empty queue");
        return buf_[wrap_(head_ + count_ - 1)];
    }

    // 入队：push / emplace（满了就扩容）
    void push(const T& v)  { ensure_can_grow_(); buf_[wrap_(head_ + count_)] = v; ++count_; }
    void push(T&& v)       { ensure_can_grow_(); buf_[wrap_(head_ + count_)] = std::move(v); ++count_; }

    template<class... Args>
    T& emplace(Args&&... args) {
        ensure_can_grow_();
        auto idx = wrap_(head_ + count_);
        buf_[idx] = T(std::forward<Args>(args)...); // 先构造成值再赋给槽位（简单稳妥）
        ++count_;
        return buf_[idx];
    }

    // 出队：不返回值，避免额外拷贝/移动
    void pop() {
        if (empty()) throw std::out_of_range("pop on empty queue");
        // 如需及时析构，可以显式重置该槽位：
        // buf_[head_] = T();
        head_ = wrap_(head_ + 1);
        --count_;
    }

    // 尝试弹出到 out，失败返回 false（无异常）
    bool try_pop(T& out) {
        if (empty()) return false;
        out = std::move(buf_[head_]);
        head_ = wrap_(head_ + 1);
        --count_;
        return true;
    }

    // 预留容量：会把现有元素线性化到新缓冲区开头
    void reserve(size_t new_cap) {
        if (new_cap <= capacity()) return;
        reallocate_(new_cap);
    }

    // 清空但保留容量
    void clear() noexcept {
        head_ = 0; count_ = 0;
        // 如需强制析构，可用 buf_.assign(buf_.size(), T());
    }

    // 按逻辑索引访问（0..size-1）
    T& operator[](size_t i)             { return buf_[wrap_(head_ + i)]; }
    const T& operator[](size_t i) const { return buf_[wrap_(head_ + i)]; }

private:
    std::vector<T> buf_;
    size_t head_  = 0;   // 队首在缓冲区里的位置
    size_t count_ = 0;   // 当前元素数

    size_t wrap_(size_t x) const noexcept {
        size_t cap = buf_.size();
        return cap ? (x % cap) : 0;
    }

    void ensure_can_grow_() {
        if (capacity() == 0) {
            buf_.resize(1);
        } else if (full()) {
            reallocate_(capacity() * 2);
        }
    }

    void reallocate_(size_t new_cap) {
        std::vector<T> nb;
        nb.resize(new_cap);
        // 线性搬运：保持逻辑顺序到 [0..count_-1]
        for (size_t i = 0; i < count_; ++i) {
            nb[i] = std::move(buf_[wrap_(head_ + i)]);
        }
        buf_.swap(nb);
        head_ = 0;
        // tail = count_（隐式），保持逻辑不变
    }
};
