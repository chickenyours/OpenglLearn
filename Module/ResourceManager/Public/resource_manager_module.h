#pragma once

#include <memory>
#include <string>
#include <functional>
#include <unordered_map>
#include <mutex>
#include <iostream>
#include "module.h"

namespace Resource {

// 可加载接口 - 资源类的根基
class ILoadable {
public:
    virtual ~ILoadable() = default;
    ILoadable() = default;

    inline bool IsLoad() const { return isLoad_; }
    virtual void Release() = 0;

protected:
    bool isLoad_ = false;
};

// 可通过配置文件加载接口
class ILoadFromConfig : public ILoadable {
public:
    virtual bool LoadFromConfigFile(const std::string& configFile) = 0;
};

// 资源句柄 - 管理资源生命周期
template<typename T>
class ResourceHandle {
public:
    ResourceHandle() = default;
    ResourceHandle(std::nullptr_t) : resource_(nullptr), onRelease_(nullptr) {}
    
    ResourceHandle(const std::string& name,
                   T* ptr,
                   std::function<void(const std::string&)> onRelease)
        : name_(name), resource_(ptr), onRelease_(std::move(onRelease)) {}

    ~ResourceHandle();

    ResourceHandle(const ResourceHandle& other);
    ResourceHandle(ResourceHandle&& other) noexcept;

    ResourceHandle& operator=(const ResourceHandle& other);
    ResourceHandle& operator=(ResourceHandle&& other) noexcept;

    T* operator->() { return resource_; }
    const T* operator->() const { return resource_; }
    T& operator*() { return *resource_; }
    T* get() const { return resource_; }

    explicit operator bool() const {
        return resource_ != nullptr;
    }

    const std::string& GetName() const { return name_; }

private:
    std::string name_ = "";
    T* resource_ = nullptr;
    std::function<void(const std::string&)> onRelease_ = nullptr;
};

// 加载选项
template<typename T>
struct LoadOptions {
    std::string key;
    std::function<std::unique_ptr<T>()> generator;
    std::function<void(const std::string&, T*)> onZeroCallFunc = nullptr;
    std::function<void(const std::string&, T*)> onGetRestoreCallFunc = nullptr;

private:
    LoadOptions(const std::string& k,
                std::function<std::unique_ptr<T>()> gen,
                std::function<void(const std::string&, T*)> onZero,
                std::function<void(const std::string&, T*)> onRestore)
        : key(k), generator(std::move(gen)), 
          onZeroCallFunc(std::move(onZero)), onGetRestoreCallFunc(std::move(onRestore)) {}

    template<typename U>
    friend LoadOptions<U> FromConfig(const std::string& fileConfig,
        std::function<void(const std::string&, U*)> onZeroCallFunc,
        std::function<void(const std::string&, U*)> onGetRestoreCallFunc);

    template<typename U>
    friend LoadOptions<U> FromPointer(const std::string& key,
        std::unique_ptr<U> ptr,
        std::function<void(const std::string&, U*)> onZeroCallFunc,
        std::function<void(const std::string&, U*)> onGetRestoreCallFunc);

    template<typename U>
    friend LoadOptions<U> FromGenerator(const std::string& key,
        std::function<std::unique_ptr<U>()> generator,
        std::function<void(const std::string&, U*)> onZeroCallFunc,
        std::function<void(const std::string&, U*)> onGetRestoreCallFunc);

    template<typename U>
    friend LoadOptions<U> FromKey(const std::string& key);
};

// 工厂函数声明
template<typename T>
LoadOptions<T> FromConfig(const std::string& fileConfig,
    std::function<void(const std::string&, T*)> onZeroCallFunc = nullptr,
    std::function<void(const std::string&, T*)> onGetRestoreCallFunc = nullptr);

template<typename T>
LoadOptions<T> FromPointer(const std::string& key,
    std::unique_ptr<T> ptr,
    std::function<void(const std::string&, T*)> onZeroCallFunc = nullptr,
    std::function<void(const std::string&, T*)> onGetRestoreCallFunc = nullptr);

template<typename T>
LoadOptions<T> FromGenerator(const std::string& key,
    std::function<std::unique_ptr<T>()> generator,
    std::function<void(const std::string&, T*)> onZeroCallFunc = nullptr,
    std::function<void(const std::string&, T*)> onGetRestoreCallFunc = nullptr);

template<typename T>
LoadOptions<T> FromKey(const std::string& key);

// 资源管理器模块类
class ResourceManagerModule final : public IModule {
public:
    static ResourceManagerModule& GetInstance();

    ResourceManagerModule() = default;
    ~ResourceManagerModule() override = default;

    ResourceManagerModule(const ResourceManagerModule&) = delete;
    ResourceManagerModule& operator=(const ResourceManagerModule&) = delete;

    const char* GetName() const noexcept override { return "ResourceManager"; }
    bool Startup() override;
    void Shutdown() override;
    bool IsStarted() const noexcept override { return started_; }

    template<typename T>
    ResourceHandle<T> Get(const LoadOptions<T>& options);

    template<typename T>
    void OnHandleRelease(const std::string& key);

    template<typename T>
    void OnZeroRefRelease(const std::string& key);

    template<typename T>
    void TryReleaseAll();

private:
    template<typename T>
    class ResourcePool;

    template<typename T>
    ResourcePool<T>& GetPool();

    bool started_ = false;
};

} // namespace Resource

// 模板实现放在这里（因为需要访问 ResourceManagerModule 内部）
namespace Resource {

// ResourcePool 内部实现
template<typename T>
class ResourceManagerModule::ResourcePool {
public:
    ResourceHandle<T> Get(const LoadOptions<T>& options);
    void OnHandleReleaseAll();
    void OnZeroRefRelease(const std::string& key);
    void OnHandleRelease(const std::string& key);
    ~ResourcePool();

private:
    struct ResourceEntry {
        std::unique_ptr<ILoadable> resource = nullptr;
        size_t refCount = 0;
        std::function<void(const std::string&, T*)> onZeroCallFunc = nullptr;
        std::function<void(const std::string&, T*)> onGetRestoreCallFunc = nullptr;
    };

    std::unordered_map<std::string, ResourceEntry> pool_;
    std::mutex mutex_;
};

template<typename T>
ResourceHandle<T> ResourceManagerModule::ResourcePool<T>::Get(const LoadOptions<T>& options) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto& entry = pool_[options.key];

    if (!entry.resource) {
        if (options.generator) {
            auto ptr = options.generator();
            if (!ptr) {
                std::cerr << "Failed to create resource: generator returned null." << std::endl;
                return nullptr;
            }
            entry.resource = std::move(ptr);
            entry.onZeroCallFunc = options.onZeroCallFunc;
            entry.onGetRestoreCallFunc = options.onGetRestoreCallFunc;
            std::cout << "Create resource: [key = " << options.key << "]" << std::endl;
        } else {
            std::cerr << "Failed to create resource: generator is null." << std::endl;
            return nullptr;
        }
    } else {
        if (entry.onGetRestoreCallFunc) {
            entry.onGetRestoreCallFunc(options.key, static_cast<T*>(entry.resource.get()));
        }
    }

    entry.refCount++;

    return ResourceHandle<T>(
        options.key,
        static_cast<T*>(entry.resource.get()),
        [this](const std::string& key) {
            this->OnHandleRelease(key);
        }
    );
}

template<typename T>
void ResourceManagerModule::ResourcePool<T>::OnHandleReleaseAll() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (auto it = pool_.begin(); it != pool_.end(); ) {
        if (it->second.refCount == 0 && it->second.resource) {
            it->second.resource->Release();
            std::cout << "Released resource: " << it->first << std::endl;
            it = pool_.erase(it);
        } else {
            ++it;
        }
    }
}

template<typename T>
void ResourceManagerModule::ResourcePool<T>::OnZeroRefRelease(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = pool_.find(key);
    if (it != pool_.end()) {
        if (it->second.refCount == 0 && it->second.resource) {
            it->second.resource->Release();
            std::cout << "Released resource: " << it->first << std::endl;
            it = pool_.erase(it);
        }
    }
}

template<typename T>
ResourceManagerModule::ResourcePool<T>::~ResourcePool() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    for (auto& [key, entry] : pool_) {
        if (entry.resource) {
            entry.resource->Release();
            if (entry.refCount != 0) {
                std::cout << "Warning: Resource refCount != 0: " << key << std::endl;
            }
        }
    }
    pool_.clear();
}

template<typename T>
void ResourceManagerModule::ResourcePool<T>::OnHandleRelease(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = pool_.find(key);
    if (it != pool_.end()) {
        if (!it->second.resource) {
            std::cerr << "Resource does not exist: " << key << std::endl;
            return;
        }
        it->second.refCount--;
        if (it->second.refCount == 0) {
            if (it->second.onZeroCallFunc) {
                it->second.onZeroCallFunc(key, static_cast<T*>(it->second.resource.get()));
                std::cout << "Resource released by external strategy: " << key << std::endl;
            } else {
                it->second.resource->Release();
                pool_.erase(it);
                std::cout << "Resource released by default strategy: " << key << std::endl;
            }
        }
    } else {
        if (!key.empty()) {
            std::cout << "Warning: Attempt to release non-existent resource: " << key << std::endl;
        }
    }
}

template<typename T>
ResourceHandle<T> ResourceManagerModule::Get(const LoadOptions<T>& options) {
    auto handle = GetPool<T>().Get(options);
    if (!handle) {
        std::cerr << "Failed to load resource" << std::endl;
    }
    return handle;
}

template<typename T>
void ResourceManagerModule::OnHandleRelease(const std::string& key) {
    GetPool<T>().OnHandleRelease(key);
}

template<typename T>
void ResourceManagerModule::OnZeroRefRelease(const std::string& key) {
    GetPool<T>().OnZeroRefRelease(key);
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

// 工厂函数实现
template<typename T>
LoadOptions<T> FromConfig(const std::string& fileConfig,
    std::function<void(const std::string&, T*)> onZeroCallFunc,
    std::function<void(const std::string&, T*)> onGetRestoreCallFunc) {
    
    static_assert(std::is_base_of_v<ILoadFromConfig, T>,
                  "FromConfig<T> requires T to implement ILoadFromConfig");

    return LoadOptions<T>(
        fileConfig,
        [fileConfig]() -> std::unique_ptr<T> {
            auto ptr = new T();
            if (!ptr->LoadFromConfigFile(fileConfig)) {
                std::cerr << "Failed to load configuration file: " << fileConfig << std::endl;
                delete ptr;
                return nullptr;
            }
            return std::unique_ptr<T>(ptr);
        },
        std::move(onZeroCallFunc),
        std::move(onGetRestoreCallFunc)
    );
}

template<typename T>
LoadOptions<T> FromPointer(const std::string& key,
    std::unique_ptr<T> ptr,
    std::function<void(const std::string&, T*)> onZeroCallFunc,
    std::function<void(const std::string&, T*)> onGetRestoreCallFunc) {
    
    return LoadOptions<T>(
        key,
        [ptr = std::move(ptr), key]() mutable {
            if (!ptr) {
                std::cerr << "Pointer is null for key: " << key << std::endl;
                return nullptr;
            }
            return std::move(ptr);
        },
        std::move(onZeroCallFunc),
        std::move(onGetRestoreCallFunc)
    );
}

template<typename T>
LoadOptions<T> FromGenerator(const std::string& key,
    std::function<std::unique_ptr<T>()> generator,
    std::function<void(const std::string&, T*)> onZeroCallFunc,
    std::function<void(const std::string&, T*)> onGetRestoreCallFunc) {
    
    return LoadOptions<T>(
        key,
        std::move(generator),
        std::move(onZeroCallFunc),
        std::move(onGetRestoreCallFunc)
    );
}

template<typename T>
LoadOptions<T> FromKey(const std::string& key) {
    return LoadOptions<T>(
        key,
        nullptr,
        nullptr,
        nullptr
    );
}

// ResourceHandle 析构函数和成员函数
template<typename T>
ResourceHandle<T>::~ResourceHandle() {
    if (onRelease_) {
        onRelease_(name_);
    }
}

template<typename T>
ResourceHandle<T>::ResourceHandle(const ResourceHandle& other) {
    auto& instance = ResourceManagerModule::GetInstance();
    ResourceHandle<T> tmp = instance.Get<T>(FromKey<T>(other.GetName()));

    name_ = tmp.name_;
    resource_ = tmp.resource_;
    onRelease_ = std::move(tmp.onRelease_);

    tmp.resource_ = nullptr;
    tmp.onRelease_ = nullptr;
}

template<typename T>
ResourceHandle<T>::ResourceHandle(ResourceHandle&& other) noexcept
    : name_(std::move(other.name_)), 
      resource_(other.resource_), 
      onRelease_(std::move(other.onRelease_)) {
    other.resource_ = nullptr;
    other.onRelease_ = nullptr;
}

template<typename T>
ResourceHandle<T>& ResourceHandle<T>::operator=(const ResourceHandle<T>& other) {
    if (this != &other) {
        if (onRelease_ && resource_) {
            onRelease_(name_);
        }

        auto& instance = ResourceManagerModule::GetInstance();
        ResourceHandle<T> tmp = instance.Get<T>(FromKey<T>(other.GetName()));

        name_ = tmp.name_;
        resource_ = tmp.resource_;
        onRelease_ = std::move(tmp.onRelease_);

        tmp.resource_ = nullptr;
        tmp.onRelease_ = nullptr;
    }
    return *this;
}

template<typename T>
ResourceHandle<T>& ResourceHandle<T>::operator=(ResourceHandle<T>&& other) noexcept {
    if (this != &other) {
        if (onRelease_ && resource_) {
            onRelease_(name_);
        }

        name_ = std::move(other.name_);
        resource_ = other.resource_;
        onRelease_ = std::move(other.onRelease_);
        other.resource_ = nullptr;
        other.onRelease_ = nullptr;
    }
    return *this;
}

} // namespace Resource
