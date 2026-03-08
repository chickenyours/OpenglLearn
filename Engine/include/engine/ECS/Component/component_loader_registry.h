#pragma once

#include <unordered_map>
#include <functional>
#include <type_traits>
#include <string>
#include <typeindex>          // ✅ 需要这个
#include <json/json.h>

#include "engine/ECS/Component/component.h"
#include "engine/ECS/Component/component_register.h"
#include "engine/DebugTool/ConsoleHelp/color_log.h"
#include "engine/ECS/data_type.h"
#include "engine/ToolAndAlgorithm/DateType/chuck_array.h"

namespace ECS::Component {

using ComponentLoader = std::function<bool(
    ECS::Core::ComponentRegister&,
    ECS::EntityID,
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
    return new FixedChuckArray<ComponentT>(sizePerChuck);
}

template <typename ComponentT>
void ComponentChuckArrayDistructor(void* ComponentChuckAddr) {
    delete reinterpret_cast<FixedChuckArray<ComponentT>*>(ComponentChuckAddr);
}

using ComponentChuckArrayConstructorInstance = std::function<void*(size_t)>;

inline std::unordered_map<std::type_index, ComponentChuckArrayConstructorInstance>& _MutableComponentChuckArrayConstructorMap() {
    static std::unordered_map<std::type_index, ComponentChuckArrayConstructorInstance> ctorMap;
    return ctorMap;
}

inline const std::unordered_map<std::type_index, ComponentChuckArrayConstructorInstance>& GetComponentChuckArrayConstructorMap() {
    return _MutableComponentChuckArrayConstructorMap();
}

using ComponentChuckArrayDestructorInstance = std::function<void(void*)>;

inline std::unordered_map<std::type_index, ComponentChuckArrayDestructorInstance>& _MutableComponentChuckArrayDestructorMap() {
    static std::unordered_map<std::type_index, ComponentChuckArrayDestructorInstance> ctorMap;
    return ctorMap;
}

inline const std::unordered_map<std::type_index, ComponentChuckArrayDestructorInstance>& GetComponentChuckArrayDestructorMap() {
    return _MutableComponentChuckArrayDestructorMap();
}

} // namespace ECS::Component


// ✅ 推荐：注册宏用“静态变量 + lambda”触发一次初始化
#define REGISTER_COMPONENT(key, Type)                                                        \
    static_assert(std::is_base_of<ECS::Component::Component<Type>, Type>::value,             \
                  #Type " must derive from Component<" #Type ">");                           \
    namespace {                                                                              \
        const bool _ecs_component_registered_##Type = []() -> bool {                          \
            ECS::Component::_MutableComponentLoaderMap()[key] =                               \
                [](ECS::Core::ComponentRegister& reg, ECS::EntityID id,                      \
                   const Json::Value& data, Log::StackLogErrorHandle err) -> bool {          \
                    Type* component = reg.AddComponent<Type>(id);                            \
                    if (!component) return false;                                            \
                    return component->LoadFromMataData(data, err);                           \
                };                                                                            \
            ECS::Component::_MutableComponentChuckArrayConstructorMap()[std::type_index(typeid(Type))] = \
                [](size_t sizePerChuck) -> void* {                                           \
                    return ECS::Component::ComponentChuckArrayConstructor<Type>(sizePerChuck); \
                };                                                                            \
            ECS::Component::_MutableComponentChuckArrayDistructorMap()[std::type_index(typeid(Type))] = \
                [](void* addr) -> void {                                           \
                    return ECS::Component::ComponentChuckArrayDestructor<Type>(addr); \
                };                                                                            \
            return true;                                                                      \
        }();                                                                                  \
    }