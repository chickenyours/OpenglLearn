#pragma once

#include <iterator>
#include <memory>
#include <utility>

#include "engine/ToolAndAlgorithm/Iterator/seq_iterator_interface.h"

template<class T>
class SeqIterator {
public:
    using iterator_category = std::input_iterator_tag;
    using value_type        = T;
    using difference_type   = std::ptrdiff_t;
    using pointer           = T*;
    using reference         = T&;

    SeqIterator() = default;

    explicit SeqIterator(std::unique_ptr<ISeqCursor<T>> cur)
        : cur_(std::move(cur))
    {
        // 保存“起点”（用于回滚）
        origin_ = cur_ ? cur_->clone() : nullptr;

        // 如果一开始就 done，则视为 end
        if (cur_ && cur_->done()) {
            cur_.reset();
            // origin_ 仍保留也可以，但一般没意义；你也可以选择清空它
            // origin_.reset();
        }
    }

    // 输入迭代器需要可拷贝：clone 两份
    SeqIterator(const SeqIterator& rhs)
        : cur_(rhs.cur_ ? rhs.cur_->clone() : nullptr)
        , origin_(rhs.origin_ ? rhs.origin_->clone() : nullptr)
    {}

    SeqIterator& operator=(const SeqIterator& rhs) {
        if (this == &rhs) return *this;
        cur_    = rhs.cur_ ? rhs.cur_->clone() : nullptr;
        origin_ = rhs.origin_ ? rhs.origin_->clone() : nullptr;
        return *this;
    }

    SeqIterator& operator++() {
        if (cur_) {
            cur_->next();
            if (cur_->done()) cur_.reset(); // 用 nullptr 表示 end
        }
        return *this;
    }
    void operator++(int) { ++(*this); }

    pointer operator->() const { return cur_ ? cur_->get() : nullptr; }

    // 推荐：约定 next() 会跳过无效元素，保证 get()!=nullptr
    reference operator*() const { return *cur_->get(); }

    // === 新增：回滚到第一个元素 ===
    // 返回 false 表示没有可回滚的起点（比如 default-constructed end iterator）
    bool Rewind() {
        if (!origin_) {
            cur_.reset();
            return false;
        }
        cur_ = origin_->clone();
        if (cur_ && cur_->done()) cur_.reset();
        return true;
    }

    // 可选：回到 end（有些 system 想手动“耗尽”）
    void ToEnd() { cur_.reset(); }

    friend bool operator==(const SeqIterator& a, const SeqIterator& b) {
        // 这个实现只保证 end 比较正确（输入迭代器最常见的做法）
        return a.cur_.get() == b.cur_.get(); // end 都是 nullptr
    }
    friend bool operator!=(const SeqIterator& a, const SeqIterator& b) { return !(a == b); }

private:
    std::unique_ptr<ISeqCursor<T>> cur_;
    std::unique_ptr<ISeqCursor<T>> origin_; // 保存“起点”
};

template<class T>
class SeqRange {
public:
    explicit SeqRange(std::unique_ptr<ISeqCursor<T>> begin) : begin_(std::move(begin)) {}

    SeqIterator<T> begin() const { return SeqIterator<T>(begin_ ? begin_->clone() : nullptr); }
    SeqIterator<T> end()   const { return SeqIterator<T>(); }

private:
    std::unique_ptr<ISeqCursor<T>> begin_;
};