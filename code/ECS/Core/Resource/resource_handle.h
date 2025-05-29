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

    ResourceHandle(ResourceHandle& other) = delete;

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

    explicit operator bool() const {
        return resource_ != nullptr;
    }




    ResourceHandle<T>& operator=(const ResourceHandle<T>& other);

    inline const std::string& GetName() const {return name_;} 

private:

    std::string name_;
    T* resource_ = nullptr;
    std::function<void(const std::string&)> _onRelease;
};


} // namespace Resource

// tpp

// 延迟包含
#include "code/ECS/Core/Resource/resource_manager.h"

// 外部实现
template <typename T>
Resource::ResourceHandle<T>& Resource::ResourceHandle<T>::operator=(const Resource::ResourceHandle<T>& other) {
   if (this != &other) {
        auto& instance = ECS::Core::ResourceModule::ResourceManager::GetInctance();
        Resource::ResourceHandle<T> tmp = instance.Get<T>(other.GetName());

        this->name_ = tmp.name_;
        this->resource_ = tmp.resource_;
        this->_onRelease = tmp._onRelease;

        // 防止 tmp 析构再次释放
        tmp.resource_ = nullptr;
        tmp._onRelease = nullptr;

        
    }
    return *this;
}