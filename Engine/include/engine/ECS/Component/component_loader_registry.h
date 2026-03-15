#pragma once

#include <unordered_map>
#include <functional>
#include <type_traits>
#include <string>
#include <typeindex>
#include <json/json.h>

#include "engine/ECS/Component/component.h"
#include "engine/DebugTool/ConsoleHelp/color_log.h"
#include "engine/ECS/data_type.h"
#include "engine/ToolAndAlgorithm/DateType/chunk_array.h"

namespace ECS::Component {

    // 现在不再依赖 ECS::Core::ComponentRegister
    // 改成：对一个“已经存在的组件对象地址”执行元数据加载
    using ComponentLoader = std::function<bool(
        void*,
        const Json::Value&,
        Log::StackLogErrorHandle
    )>;

    inline std::unordered_map<std::string, ComponentLoader>& _MutableComponentLoaderMap() {
        static std::unordered_map<std::string, ComponentLoader> loaderMap;
        return loaderMap;
    }

    inline const std::unordered_map<std::string, ComponentLoader>& GetComponentLoaderMap() {
        return _MutableComponentLoaderMap();
    }

    template <typename ComponentT>
    void* ComponentChuckArrayConstructor(size_t sizePerChuck) {
        return new FixedChunkArray<ComponentT>(sizePerChuck);
    }

    template <typename ComponentT>
    void ComponentChuckArrayDestructor(void* componentChuckAddr) {
        delete reinterpret_cast<FixedChunkArray<ComponentT>*>(componentChuckAddr);
    }

    using ComponentChuckArrayConstructorInstance = std::function<void*(size_t)>;

    inline std::unordered_map<std::type_index, ComponentChuckArrayConstructorInstance>&
    _MutableComponentChuckArrayConstructorMap() {
        static std::unordered_map<std::type_index, ComponentChuckArrayConstructorInstance> ctorMap;
        return ctorMap;
    }

    inline const std::unordered_map<std::type_index, ComponentChuckArrayConstructorInstance>&
    GetComponentChuckArrayConstructorMap() {
        return _MutableComponentChuckArrayConstructorMap();
    }

    using ComponentChuckArrayDestructorInstance = std::function<void(void*)>;

    inline std::unordered_map<std::type_index, ComponentChuckArrayDestructorInstance>&
    _MutableComponentChuckArrayDestructorMap() {
        static std::unordered_map<std::type_index, ComponentChuckArrayDestructorInstance> dtorMap;
        return dtorMap;
    }

    inline const std::unordered_map<std::type_index, ComponentChuckArrayDestructorInstance>&
    GetComponentChuckArrayDestructorMap() {
        return _MutableComponentChuckArrayDestructorMap();
    }

    template <typename Type>
    bool RegisterComponentType(const std::string& key) {
        static_assert(std::is_base_of<ECS::Component::Component<Type>, Type>::value,
                      "Type must derive from ECS::Component::Component<Type>");

        // key -> loader
        _MutableComponentLoaderMap()[key] =
            [](void* componentAddr,
               const Json::Value& data,
               Log::StackLogErrorHandle err) -> bool {
                if (componentAddr == nullptr) {
                    LOG_ERROR("RegisterComponentType", "componentAddr is nullptr");
                    return false;
                }

                Type* component = reinterpret_cast<Type*>(componentAddr);
                return component->LoadFromMataData(data, err);
            };

        // type -> storage constructor
        _MutableComponentChuckArrayConstructorMap()[std::type_index(typeid(Type))] =
            [](size_t sizePerChuck) -> void* {
                return ECS::Component::ComponentChuckArrayConstructor<Type>(sizePerChuck);
            };

        // type -> storage destructor
        _MutableComponentChuckArrayDestructorMap()[std::type_index(typeid(Type))] =
            [](void* addr) -> void {
                ECS::Component::ComponentChuckArrayDestructor<Type>(addr);
            };

        return true;
    }

    // 可选：给外部一个统一调用入口，避免到处手写 map.find
    inline bool LoadComponentByKey(
        const std::string& key,
        void* componentAddr,
        const Json::Value& data,
        Log::StackLogErrorHandle err)
    {
        auto& loaderMap = GetComponentLoaderMap();
        auto it = loaderMap.find(key);
        if (it == loaderMap.end()) {
            LOG_ERROR("LoadComponentByKey", "no registered component loader for key: " + key);
            return false;
        }
        return it->second(componentAddr, data, err);
    }

} // namespace ECS::Component

#define REGISTER_COMPONENT(key, Type) \
    ECS::Component::RegisterComponentType<Type>(key)