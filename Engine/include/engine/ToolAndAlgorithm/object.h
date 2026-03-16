#pragma once

#include <mutex>
#include <cstddef>
#include <cassert>
#include <utility>
#include <type_traits>

// 前置声明
template <class T>
class ObjectPtr;

template <class T>
class ObjectWeakPtr;

// 控制块：管理 weak 计数 + 对象指针 + owner 是否还存在
template <class T>
class ObjectControlBlock {
public:
    explicit ObjectControlBlock(T* p) : ptr_(p) {}

    ObjectControlBlock(const ObjectControlBlock&) = delete;
    ObjectControlBlock& operator=(const ObjectControlBlock&) = delete;

    void IncWeak() {
        std::lock_guard<std::mutex> lk(mtx_);
        ++weakCount_;
    }

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

    // 这里只返回一个快照指针，不提供保活
    T* Lock() const {
        std::lock_guard<std::mutex> lk(mtx_);
        return ownerGone_ ? nullptr : ptr_;
    }

    bool HasAlive() const {
        std::lock_guard<std::mutex> lk(mtx_);
        return !ownerGone_;
    }

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
    ~ObjectControlBlock() = default;

private:
    mutable std::mutex mtx_;
    T* ptr_ = nullptr;
    std::size_t weakCount_ = 0;
    bool ownerGone_ = false;

    template <class U>
    friend class ObjectPtr;

    template <class U>
    friend class ObjectWeakPtr;
};

template <class T>
class ObjectPtr {
public:
    ObjectPtr() = default;

    template <class... Args,
              class = std::enable_if_t<std::is_constructible_v<T, Args...>>>
    explicit ObjectPtr(Args&&... args) {
        T* obj = new T(std::forward<Args>(args)...);
        block_ = new ObjectControlBlock<T>(obj);
    }

    explicit ObjectPtr(T* ptr) {
        block_ = new ObjectControlBlock<T>(ptr);
    }

    ObjectPtr(const ObjectPtr&) = delete;
    ObjectPtr& operator=(const ObjectPtr&) = delete;

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

    ~ObjectPtr() {
        Reset();
    }

    void Reset() {
        if (block_) {
            block_->OwnerReleaseAndMaybeDelete();
            block_ = nullptr;
        }
    }

    T* Get() const {
        return block_ ? block_->Lock() : nullptr;
    }

    T& operator*() const {
        T* p = Get();
        assert(p != nullptr && "Dereferencing null ObjectPtr");
        return *p;
    }

    T* operator->() const {
        T* p = Get();
        assert(p != nullptr && "Accessing null ObjectPtr");
        return p;
    }

    explicit operator bool() const {
        return Get() != nullptr;
    }

    ObjectWeakPtr<T> GenWeakPtr() const;

private:
    ObjectControlBlock<T>* block_ = nullptr;

    template <class U>
    friend class ObjectPtr;

    template <class U>
    friend class ObjectWeakPtr;

    template <class U>
    friend bool operator==(const ObjectPtr<U>& lhs, const ObjectPtr<U>& rhs);

    template <class U>
    friend bool operator!=(const ObjectPtr<U>& lhs, const ObjectPtr<U>& rhs);

    template <class U>
    friend bool operator==(const ObjectWeakPtr<U>& lhs, const ObjectWeakPtr<U>& rhs);

    template <class U>
    friend bool operator!=(const ObjectWeakPtr<U>& lhs, const ObjectWeakPtr<U>& rhs);

    template <class U>
    friend bool operator==(const ObjectPtr<U>& lhs, const ObjectWeakPtr<U>& rhs);

    template <class U>
    friend bool operator!=(const ObjectPtr<U>& lhs, const ObjectWeakPtr<U>& rhs);

    template <class U>
    friend bool operator==(const ObjectWeakPtr<U>& lhs, const ObjectPtr<U>& rhs);

    template <class U>
    friend bool operator!=(const ObjectWeakPtr<U>& lhs, const ObjectPtr<U>& rhs);

    template <class U>
    friend bool operator==(const ObjectPtr<U>& lhs, std::nullptr_t);

    template <class U>
    friend bool operator!=(const ObjectPtr<U>& lhs, std::nullptr_t);

    template <class U>
    friend bool operator==(std::nullptr_t, const ObjectPtr<U>& rhs);

    template <class U>
    friend bool operator!=(std::nullptr_t, const ObjectPtr<U>& rhs);
};

template <class T>
class ObjectWeakPtr {
public:
    ObjectWeakPtr() = default;

    explicit ObjectWeakPtr(ObjectControlBlock<T>* b) : block_(b) {
        if (block_) {
            block_->IncWeak();
        }
    }

    ObjectWeakPtr(const ObjectWeakPtr& o) : block_(o.block_) {
        if (block_) {
            block_->IncWeak();
        }
    }

    ObjectWeakPtr& operator=(const ObjectWeakPtr& o) {
        if (this != &o) {
            Release();
            block_ = o.block_;
            if (block_) {
                block_->IncWeak();
            }
        }
        return *this;
    }

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

    ~ObjectWeakPtr() {
        Release();
    }

    void Release() {
        if (block_) {
            block_->DecWeak();
            block_ = nullptr;
        }
    }

    T* lock() const {
        return block_ ? block_->Lock() : nullptr;
    }

    explicit operator bool() const {
        return block_ && block_->HasAlive();
    }

    T& operator*() const {
        T* p = lock();
        assert(p != nullptr && "Dereferencing expired/null ObjectWeakPtr");
        return *p;
    }

    T* Get() const {
        return lock();
    }

    const T* operator->() const {
        T* p = lock();
        assert(p != nullptr && "Accessing expired/null ObjectWeakPtr");
        return p;
    }

    T* operator->() {
        T* p = lock();
        assert(p != nullptr && "Accessing expired/null ObjectWeakPtr");
        return p;
    }

    bool expired() const {
        return lock() == nullptr;
    }

    void SetNull() {
        Release();
    }

private:
    ObjectControlBlock<T>* block_ = nullptr;

    template <class U>
    friend class ObjectPtr;

    template <class U>
    friend class ObjectWeakPtr;

    template <class U>
    friend bool operator==(const ObjectPtr<U>& lhs, const ObjectPtr<U>& rhs);

    template <class U>
    friend bool operator!=(const ObjectPtr<U>& lhs, const ObjectPtr<U>& rhs);

    template <class U>
    friend bool operator==(const ObjectWeakPtr<U>& lhs, const ObjectWeakPtr<U>& rhs);

    template <class U>
    friend bool operator!=(const ObjectWeakPtr<U>& lhs, const ObjectWeakPtr<U>& rhs);

    template <class U>
    friend bool operator==(const ObjectPtr<U>& lhs, const ObjectWeakPtr<U>& rhs);

    template <class U>
    friend bool operator!=(const ObjectPtr<U>& lhs, const ObjectWeakPtr<U>& rhs);

    template <class U>
    friend bool operator==(const ObjectWeakPtr<U>& lhs, const ObjectPtr<U>& rhs);

    template <class U>
    friend bool operator!=(const ObjectWeakPtr<U>& lhs, const ObjectPtr<U>& rhs);

    template <class U>
    friend bool operator==(const ObjectWeakPtr<U>& lhs, std::nullptr_t);

    template <class U>
    friend bool operator!=(const ObjectWeakPtr<U>& lhs, std::nullptr_t);

    template <class U>
    friend bool operator==(std::nullptr_t, const ObjectWeakPtr<U>& rhs);

    template <class U>
    friend bool operator!=(std::nullptr_t, const ObjectWeakPtr<U>& rhs);
};

template <class T>
ObjectWeakPtr<T> ObjectPtr<T>::GenWeakPtr() const {
    return ObjectWeakPtr<T>(block_);
}

// --------------------------------------------------
// 比较运算符：按控制块身份比较，而不是按 lock()/Get() 返回值比较
// --------------------------------------------------

template <class T>
bool operator==(const ObjectPtr<T>& lhs, const ObjectPtr<T>& rhs) {
    return lhs.block_ == rhs.block_;
}

template <class T>
bool operator!=(const ObjectPtr<T>& lhs, const ObjectPtr<T>& rhs) {
    return !(lhs == rhs);
}

template <class T>
bool operator==(const ObjectWeakPtr<T>& lhs, const ObjectWeakPtr<T>& rhs) {
    return lhs.block_ == rhs.block_;
}

template <class T>
bool operator!=(const ObjectWeakPtr<T>& lhs, const ObjectWeakPtr<T>& rhs) {
    return !(lhs == rhs);
}

template <class T>
bool operator==(const ObjectPtr<T>& lhs, const ObjectWeakPtr<T>& rhs) {
    return lhs.block_ == rhs.block_;
}

template <class T>
bool operator!=(const ObjectPtr<T>& lhs, const ObjectWeakPtr<T>& rhs) {
    return !(lhs == rhs);
}

template <class T>
bool operator==(const ObjectWeakPtr<T>& lhs, const ObjectPtr<T>& rhs) {
    return lhs.block_ == rhs.block_;
}

template <class T>
bool operator!=(const ObjectWeakPtr<T>& lhs, const ObjectPtr<T>& rhs) {
    return !(lhs == rhs);
}

// --------------------------------------------------
// 与 nullptr 比较：按“当前是否还能拿到活对象”判断
// --------------------------------------------------

template <class T>
bool operator==(const ObjectPtr<T>& lhs, std::nullptr_t) {
    return lhs.Get() == nullptr;
}

template <class T>
bool operator!=(const ObjectPtr<T>& lhs, std::nullptr_t) {
    return !(lhs == nullptr);
}

template <class T>
bool operator==(std::nullptr_t, const ObjectPtr<T>& rhs) {
    return rhs.Get() == nullptr;
}

template <class T>
bool operator!=(std::nullptr_t, const ObjectPtr<T>& rhs) {
    return !(nullptr == rhs);
}

template <class T>
bool operator==(const ObjectWeakPtr<T>& lhs, std::nullptr_t) {
    return lhs.lock() == nullptr;
}

template <class T>
bool operator!=(const ObjectWeakPtr<T>& lhs, std::nullptr_t) {
    return !(lhs == nullptr);
}

template <class T>
bool operator==(std::nullptr_t, const ObjectWeakPtr<T>& rhs) {
    return rhs.lock() == nullptr;
}

template <class T>
bool operator!=(std::nullptr_t, const ObjectWeakPtr<T>& rhs) {
    return !(nullptr == rhs);
}