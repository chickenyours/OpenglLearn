#pragma once

#include <cstddef>
#include <memory>

template<class T>
struct ISeqCursor {
    virtual ~ISeqCursor() = default;

    // 移动到下一个“可见元素”
    virtual void next() = 0;

    // 是否结束
    virtual bool done() const = 0;

    // 当前元素地址；无效/不存在时可返回 nullptr（通常你会在 next() 里跳过无效，所以这里尽量不返回 nullptr）
    virtual T* get() const = 0;

    // 克隆：用于迭代器拷贝（满足输入迭代器/前向迭代器语义）
    virtual std::unique_ptr<ISeqCursor<T>> clone() const = 0;
};