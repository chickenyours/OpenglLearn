#pragma once

#include "resource_handle.h"
#include "resource_control_block.h"

namespace Resource {

template<typename T>
ResourceHandle<T>::ResourceHandle(std::nullptr_t)
    : name_("")
    , controlBlock_(nullptr) {}

template<typename T>
ResourceHandle<T>::ResourceHandle(const std::string& name, 
                                   std::shared_ptr<ControlBlock<T>> controlBlock)
    : name_(name)
    , controlBlock_(controlBlock) {}

template<typename T>
ResourceHandle<T>::~ResourceHandle() {
    // 析构时自动减少引用计数
    if (controlBlock_) {
        controlBlock_->ReleaseRef();
    }
}

template<typename T>
ResourceHandle<T>::ResourceHandle(const ResourceHandle& other)
    : name_(other.name_)
    , controlBlock_(other.controlBlock_) {
    // 拷贝构造时增加引用计数
    if (controlBlock_) {
        controlBlock_->AddRef();
    }
}

template<typename T>
ResourceHandle<T>::ResourceHandle(ResourceHandle&& other) noexcept
    : name_(std::move(other.name_))
    , controlBlock_(std::move(other.controlBlock_)) {
    other.controlBlock_ = nullptr;
}

template<typename T>
ResourceHandle<T>& ResourceHandle<T>::operator=(const ResourceHandle& other) {
    if (this != &other) {
        // 释放当前资源
        if (controlBlock_) {
            controlBlock_->ReleaseRef();
        }

        // 拷贝新资源
        name_ = other.name_;
        controlBlock_ = other.controlBlock_;

        // 增加新资源的引用计数
        if (controlBlock_) {
            controlBlock_->AddRef();
        }
    }
    return *this;
}

template<typename T>
ResourceHandle<T>& ResourceHandle<T>::operator=(ResourceHandle&& other) noexcept {
    if (this != &other) {
        // 释放当前资源
        if (controlBlock_) {
            controlBlock_->ReleaseRef();
        }

        name_ = std::move(other.name_);
        controlBlock_ = std::move(other.controlBlock_);
        other.controlBlock_ = nullptr;
    }
    return *this;
}

template<typename T>
T* ResourceHandle<T>::operator->() {
    return controlBlock_ ? controlBlock_->GetResource() : nullptr;
}

template<typename T>
const T* ResourceHandle<T>::operator->() const {
    return controlBlock_ ? controlBlock_->GetResource() : nullptr;
}

template<typename T>
T& ResourceHandle<T>::operator*() {
    return *controlBlock_->GetResource();
}

template<typename T>
const T& ResourceHandle<T>::operator*() const {
    return *controlBlock_->GetResource();
}

template<typename T>
T* ResourceHandle<T>::get() const {
    return controlBlock_ ? controlBlock_->GetResource() : nullptr;
}

template<typename T>
ResourceHandle<T>::operator bool() const {
    return controlBlock_ && controlBlock_->IsValid();
}

template<typename T>
const std::string& ResourceHandle<T>::GetName() const {
    return name_;
}

template<typename T>
size_t ResourceHandle<T>::GetRefCount() const {
    return controlBlock_ ? controlBlock_->GetRefCount() : 0;
}

} // namespace Resource
