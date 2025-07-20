#pragma once

#include <functional>
#include <string>

namespace ECS {
    namespace Core {
        namespace ResourceModule{
            template <typename T>
            class ResourcePool; // 前向声明
            class ResourceManager;
        }
    }
} 

namespace Resource {
 
template<typename T>
class ResourceHandle {
public:
    ResourceHandle() = default;
    ResourceHandle(std::nullptr_t) 
        : resource_(nullptr), _onRelease(nullptr) {}
    ResourceHandle(const std::string& name,
                    T* ptr,
                    std::function<void(const std::string&)> onRelease)
        : name_(name), resource_(ptr), _onRelease(onRelease) {}

    ~ResourceHandle() {
        if (_onRelease) {
            _onRelease(name_);
        }
    }

    ResourceHandle(const ResourceHandle& other);

    ResourceHandle(ResourceHandle&& other){
        name_ = std::move(other.name_);
        resource_ = other.resource_;
        _onRelease = std::move(other._onRelease);
        other.resource_ = nullptr;
        other._onRelease = nullptr;
    }

    T* operator->() { return resource_; }
    const T* operator->() const { return resource_; }
    T& operator*() { return *resource_; }
    T* get() const {return resource_;}

    explicit operator bool() const {
        return resource_ != nullptr;
    }




    ResourceHandle<T>& operator=(const ResourceHandle<T>& other);

    ResourceHandle<T>& operator=(ResourceHandle<T>&& other);

    inline const std::string& GetName() const {return name_;} 

private:

    std::string name_ = "";
    T* resource_ = nullptr;
    std::function<void(const std::string&)> _onRelease = nullptr;
};


} // namespace Resource

// tpp

// 延迟包含
#include "code/ECS/Core/Resource/resource_manager.h"
#include "code/ECS/Core/Resource/resource_load_option.h"

template <typename T>
Resource::ResourceHandle<T>::ResourceHandle(const ResourceHandle& other){
    auto& instance = ECS::Core::ResourceModule::ResourceManager::GetInctance();
        Resource::ResourceHandle<T> tmp = instance.Get<T>(FromKey<T>(other.GetName()));

        this->name_ = tmp.name_;
        this->resource_ = tmp.resource_;
        this->_onRelease = tmp._onRelease;

        // 防止 tmp 析构再次释放
        tmp.resource_ = nullptr;
        tmp._onRelease = nullptr;
}

// 外部实现
template <typename T>
Resource::ResourceHandle<T>& Resource::ResourceHandle<T>::operator=(const Resource::ResourceHandle<T>& other) {
   if (this != &other) {

        // 如果有旧资源，先释放
        if (_onRelease && resource_) {
            _onRelease(name_);
        }

        auto& instance = ECS::Core::ResourceModule::ResourceManager::GetInctance();
        Resource::ResourceHandle<T> tmp = instance.Get<T>(FromKey<T>(other.GetName()));

        this->name_ = tmp.name_;
        this->resource_ = tmp.resource_;
        this->_onRelease = tmp._onRelease;

        // 防止 tmp 析构再次释放
        tmp.resource_ = nullptr;
        tmp._onRelease = nullptr;

        
    }
    return *this;
}

template <typename T>
Resource::ResourceHandle<T>& Resource::ResourceHandle<T>::operator=(Resource::ResourceHandle<T>&& other) {
    if (this != &other) {

        // 如果有旧资源，先释放
        if (_onRelease && resource_) {
            _onRelease(name_);
        }

        name_ = std::move(other.name_);
        resource_ = other.resource_;
        _onRelease = std::move(other._onRelease);
        other.resource_ = nullptr;
        other._onRelease = nullptr;
    }
    return *this;
}