#pragma once

#include <functional>
#include <json/json.h>

namespace ECS {
    namespace Core {
        namespace ResourceSystem{
            template <typename T>
            class ResourcePool; // 前向声明
        }
    }

} // namespace ECS
namespace Resource {

    class AbstractResource  {
    public:
        virtual ~AbstractResource () = default;
        AbstractResource () = default;
    protected:
        virtual bool LoadFromConfigFile(const std::string& configFile) = 0;
        virtual void Release() = 0;
        template <typename T>
        friend class ECS::Core::ResourceSystem::ResourcePool;
    };

    template<typename T>
    class ResourceHandle {
    public:
        ResourceHandle() = default;
        ResourceHandle(const std::string& name,
                        T* ptr,
                        std::function<void(const std::string&)> onRelease)
            : _name(name), _resource(ptr), _onRelease(onRelease) {}
    
        ~ResourceHandle() {
            if (_onRelease) {
                _onRelease(_name);
            }
        }
    
        T* operator->() { return _resource; }
        const T* operator->() const { return _resource; }
        T& operator*() { return *_resource; }
    
    private:
    
        std::string _name;
        T* _resource = nullptr;
        std::function<void(const std::string&)> _onRelease;
    };

} // namespace Resource



