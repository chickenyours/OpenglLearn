#pragma once

#include "resource_manager_module.h"
#include "resource_control_block.h"
#include "resource_handle.h"

// 包含 ResourceHandle 的模板实现
// 注意：必须在这里包含，因为 ResourceHandle 的实现依赖 ControlBlock 的完整定义
#include "resource_handle.inl"
#include "resource_control_block.inl"

#include <iostream>

namespace Resource {

// ============================================================================
// ResourcePool 内部实现
// ============================================================================

template<typename T>
class ResourceManagerModule::ResourcePool {
public:
    ResourceHandle<T> Get(const LoadOptions<T>& options);
    void OnHandleReleaseAll();
    ~ResourcePool();

private:
    struct ResourceEntry {
        std::shared_ptr<ControlBlock<T>> controlBlock = nullptr;
        std::function<void(const std::string&, T*)> onGetRestoreCallFunc = nullptr;
    };

    std::unordered_map<std::string, ResourceEntry> pool_;
    std::mutex mutex_;
};

template<typename T>
ResourceHandle<T> ResourceManagerModule::ResourcePool<T>::Get(const LoadOptions<T>& options) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = pool_.find(options.key);

    if (it == pool_.end()) {
        // 资源不存在，需要创建
        if (!options.generator) {
            std::cerr << "Failed to create resource: generator is null for key: " << options.key << std::endl;
            return ResourceHandle<T>(nullptr);
        }

        auto ptr = options.generator();
        if (!ptr) {
            std::cerr << "Failed to create resource: generator returned null for key: " << options.key << std::endl;
            return ResourceHandle<T>(nullptr);
        }

        // 确定销毁函数
        auto destroyFunc = options.destroyFunc;
        if (!destroyFunc) {
            // 默认使用 default delete
            destroyFunc = std::default_delete<T>();
        }

        // 零引用回调
        auto zeroRefCallback = [key = options.key, onZero = options.onZeroCallFunc](T* p) {
            if (onZero) {
                onZero(key, p);
            }
        };

        // 创建控制块，将 unique_ptr 所有权转移给控制块
        auto controlBlock = std::make_shared<ControlBlock<T>>(
            std::move(ptr),
            std::move(destroyFunc),
            std::move(zeroRefCallback)
        );

        ResourceEntry entry;
        entry.controlBlock = controlBlock;
        entry.onGetRestoreCallFunc = options.onGetRestoreCallFunc;

        pool_[options.key] = std::move(entry);

        std::cout << "Created resource: [key = " << options.key << "]" << std::endl;

        return ResourceHandle<T>(options.key, controlBlock);
    } else {
        // 资源已存在，恢复引用
        auto& entry = it->second;

        if (entry.controlBlock && entry.onGetRestoreCallFunc) {
            entry.onGetRestoreCallFunc(options.key, entry.controlBlock->GetResource());
        }

        // 增加引用计数
        entry.controlBlock->AddRef();

        return ResourceHandle<T>(options.key, entry.controlBlock);
    }
}

template<typename T>
void ResourceManagerModule::ResourcePool<T>::OnHandleReleaseAll() {
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto it = pool_.begin(); it != pool_.end(); ) {
        if (it->second.controlBlock && it->second.controlBlock->GetRefCount() == 0) {
            it->second.controlBlock->ForceRelease();
            std::cout << "Released resource: " << it->first << std::endl;
            it = pool_.erase(it);
        } else {
            ++it;
        }
    }
}

template<typename T>
ResourceManagerModule::ResourcePool<T>::~ResourcePool() {
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto& [key, entry] : pool_) {
        if (entry.controlBlock) {
            entry.controlBlock->ForceRelease();
            if (entry.controlBlock->GetRefCount() != 0) {
                std::cout << "Warning: Resource refCount != 0: " << key << std::endl;
            }
        }
    }
    pool_.clear();
}

// ============================================================================
// ResourceManagerModule 模板方法实现
// ============================================================================

template<typename T>
ResourceHandle<T> ResourceManagerModule::Get(const LoadOptions<T>& options) {
    auto handle = GetPool<T>().Get(options);
    if (!handle) {
        std::cerr << "Failed to load resource: " << options.key << std::endl;
    }
    return handle;
}

template<typename T>
void ResourceManagerModule::TryReleaseAll() {
    GetPool<T>().OnHandleReleaseAll();
}

template<typename T>
typename ResourceManagerModule::ResourcePool<T>& ResourceManagerModule::GetPool() {
    static ResourcePool<T> pool;
    return pool;
}

} // namespace Resource
