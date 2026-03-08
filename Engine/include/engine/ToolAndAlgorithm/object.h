#pragma once

#include <mutex>
#include <cstddef>
#include <cassert>

// 控制块：管理 weak 计数 + 对象指针 + owner 是否还存在
template <class T>
class ObjectControlBlock {
public:
    explicit ObjectControlBlock(T* p) : ptr_(p) {}

    // 禁止拷贝
    ObjectControlBlock(const ObjectControlBlock&) = delete;
    ObjectControlBlock& operator=(const ObjectControlBlock&) = delete;

    // weak +1：如果 owner 已销毁，仍可让 weak 存在（观察“已销毁”状态）
    void IncWeak() {
        std::lock_guard<std::mutex> lk(mtx_);
        ++weakCount_;
    }

    // weak -1：如果 owner 已销毁且 weak 归零 -> 删控制块
    void DecWeak() {
        bool shouldDelete = false;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            assert(weakCount_ > 0 && "DecWeak underflow");
            --weakCount_;
            shouldDelete = ownerGone_ && weakCount_ == 0;
        }
        if (shouldDelete) {
            delete this;
        }
    }

    // weak 观察：返回当前对象指针；若 owner 已销毁则返回 nullptr
    // 注意：这只是“拿到一个快照指针”，不提供使用期保活（和你之前的帧末延迟释放配合才安全）
    T* Lock() const {
        return ownerGone_ ? nullptr : ptr_;
    }

    bool HasAlive(){
        return !ownerGone_;
    }

    // owner 析构时调用：先 delete 对象，再标记 ownerGone；最后如果无 weak 则删控制块
    void OwnerReleaseAndMaybeDelete() {
        T* toDeleteObj = nullptr;
        bool deleteBlock = false;

        {
            std::lock_guard<std::mutex> lk(mtx_);
            if (!ownerGone_) {
                ownerGone_ = true;
                toDeleteObj = ptr_;
                ptr_ = nullptr;
                deleteBlock = (weakCount_ == 0);
            }
        }

        delete toDeleteObj;

        if (deleteBlock) {
            delete this;
        }
    }

private:
    ~ObjectControlBlock() = default; // 只能通过 OwnerRelease/DecWeak 触发 delete

    mutable std::mutex mtx_;
    T* ptr_ = nullptr;
    std::size_t weakCount_ = 0;
    bool ownerGone_ = false;
};

// 前置声明
template <class T>
class ObjectWeakPtr;

// 唯一拥有指针（像 unique_ptr）：只有一个 owner
template <class T>
class ObjectPtr {
public:
    ObjectPtr() = default;

    template <class... Args>
    explicit ObjectPtr(Args&&... args) {
        T* obj = new T(std::forward<Args>(args)...);
        block_ = new ObjectControlBlock<T>(obj);
    }

    explicit ObjectPtr(T* ptr) {
        block_ = new ObjectControlBlock<T>(ptr);
    }

    // 禁止拷贝（唯一拥有）
    ObjectPtr(const ObjectPtr&) = delete;
    ObjectPtr& operator=(const ObjectPtr&) = delete;

    // 允许移动
    ObjectPtr(ObjectPtr&& other) noexcept : block_(other.block_) {
        other.block_ = nullptr;
    }
    ObjectPtr& operator=(ObjectPtr&& other) noexcept {
        if (this != &other) {
            Reset();
            block_ = other.block_;
            other.block_ = nullptr;
        }
        return *this;
    }

    ~ObjectPtr() { Reset(); }

    void Reset() {
        if (block_) {
            block_->OwnerReleaseAndMaybeDelete();
            block_ = nullptr;
        }
    }

    // 直接访问（owner 才能用）
    T* Get() const {
        return block_ ? block_->Lock() : nullptr;
    }
    T& operator*() const { return *Get(); }
    T* operator->() const { return Get(); }

    explicit operator bool() const { return Get() != nullptr; }

    ObjectWeakPtr<T> GenWeakPtr() const;

private:
    ObjectControlBlock<T>* block_ = nullptr;

    friend class ObjectWeakPtr<T>;
};

template <class T>
class ObjectWeakPtr {
public:
    ObjectWeakPtr() = default;
    explicit ObjectWeakPtr(ObjectControlBlock<T>* b) : block_(b) {
        if (block_) block_->IncWeak();
    }

    // 拷贝：计数+1
    ObjectWeakPtr(const ObjectWeakPtr& o) : block_(o.block_) {
        if (block_) block_->IncWeak();
    }
    ObjectWeakPtr& operator=(const ObjectWeakPtr& o) {
        if (this != &o) {
            Release();
            block_ = o.block_;
            if (block_) block_->IncWeak();
        }
        return *this;
    }

    // 移动：转移指针不改计数
    ObjectWeakPtr(ObjectWeakPtr&& o) noexcept : block_(o.block_) {
        o.block_ = nullptr;
    }
    ObjectWeakPtr& operator=(ObjectWeakPtr&& o) noexcept {
        if (this != &o) {
            Release();
            block_ = o.block_;
            o.block_ = nullptr;
        }
        return *this;
    }

    ~ObjectWeakPtr() { Release(); }

    void Release() {
        if (block_) {
            block_->DecWeak();
            block_ = nullptr;
        }
    }

    // 观察对象是否还活着
    T* lock() const {
        return block_ ? block_->Lock() : nullptr;
    }

    explicit operator bool() {
        return block_ && block_->HasAlive();
    }

    T& operator*() { 
        return *block_->ptr_; 
    }

    T* get() const {
        return block_->ptr_;
    }

    const T* operator->() const { 
        return block_->ptr_; 
    }

    T* operator->() { 
        return block_->ptr_; 
    }

    bool expired() const { return lock() == nullptr; }

private:
    ObjectControlBlock<T>* block_ = nullptr;
};

template <class T>
ObjectWeakPtr<T> ObjectPtr<T>::GenWeakPtr() const {
    return ObjectWeakPtr<T>(block_);
}