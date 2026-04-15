#pragma once

#include "resource_control_block.h"

namespace Resource {

template<typename T>
ControlBlock<T>::ControlBlock(std::unique_ptr<T> resource,
                               DestroyFunc destroyFunc,
                               ZeroRefCallback zeroRefCallback)
    : resource_(std::move(resource))
    , destroyFunc_(std::move(destroyFunc))
    , zeroRefCallback_(std::move(zeroRefCallback))
    , refCount_{1}  // 创建时初始引用计数为 1
{}

template<typename T>
size_t ControlBlock<T>::AddRef() {
    return ++refCount_;
}

template<typename T>
size_t ControlBlock<T>::ReleaseRef() {
    size_t newCount = --refCount_;
    if (newCount == 0 && resource_) {
        // 先调用零引用回调（如果存在）
        if (zeroRefCallback_) {
            zeroRefCallback_(resource_.get());
        }
        // 然后释放资源
        // 如果有自定义销毁函数，使用它；否则使用 unique_ptr 的默认删除器
        if (destroyFunc_) {
            T* rawPtr = resource_.get();
            resource_.release();  // 释放所有权，避免双重删除
            destroyFunc_(rawPtr);
        } else {
            resource_.reset();  // 使用默认删除器
        }
    }
    return newCount;
}

template<typename T>
size_t ControlBlock<T>::GetRefCount() const {
    return refCount_.load();
}

template<typename T>
T* ControlBlock<T>::GetResource() const {
    return resource_.get();
}

template<typename T>
bool ControlBlock<T>::IsValid() const {
    return resource_ != nullptr;
}

template<typename T>
void ControlBlock<T>::ForceRelease() {
    if (resource_) {
        if (zeroRefCallback_) {
            zeroRefCallback_(resource_.get());
        }
        if (destroyFunc_) {
            T* rawPtr = resource_.get();
            resource_.release();
            destroyFunc_(rawPtr);
        } else {
            resource_.reset();
        }
        refCount_.store(0);
    }
}

} // namespace Resource
