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
void* ComponentChunkArrayConstructor(size_t sizePerChunk) {
    return new FixedChunkArray<ComponentT>(sizePerChunk);
}

template <typename ComponentT>
void ComponentChunkArrayDestructor(void* componentChunkAddr) {
    delete reinterpret_cast<FixedChunkArray<ComponentT>*>(componentChunkAddr);
}

using ComponentChunkArrayConstructorInstance = std::function<void*(size_t)>;

inline std::unordered_map<std::type_index, ComponentChunkArrayConstructorInstance>& _MutableComponentChunkArrayConstructorMap() {
    static std::unordered_map<std::type_index, ComponentChunkArrayConstructorInstance> ctorMap;
    return ctorMap;
}

inline const std::unordered_map<std::type_index, ComponentChunkArrayConstructorInstance>& GetComponentChunkArrayConstructorMap() {
    return _MutableComponentChunkArrayConstructorMap();
}

using ComponentChunkArrayDestructorInstance = std::function<void(void*)>;

inline std::unordered_map<std::type_index, ComponentChunkArrayDestructorInstance>& _MutableComponentChunkArrayDestructorMap() {
    static std::unordered_map<std::type_index, ComponentChunkArrayDestructorInstance> dtorMap;
    return dtorMap;
}

inline const std::unordered_map<std::type_index, ComponentChunkArrayDestructorInstance>& GetComponentChunkArrayDestructorMap() {
    return _MutableComponentChunkArrayDestructorMap();
}

template <typename Type>
bool RegisterComponentType(const std::string& key) {
    static_assert(std::is_base_of<ECS::Component::Component<Type>, Type>::value,
                  "Type must derive from ECS::Component::Component<Type>");

    _MutableComponentLoaderMap()[key] =
        [](ECS::Core::ComponentRegister& reg,
           ECS::EntityID id,
           const Json::Value& data,
           Log::StackLogErrorHandle err) -> bool {
            Type* component = reg.AddComponent<Type>(id);
            if (!component) {
                return false;
            }
            return component->LoadFromMataData(data, err);
        };

    _MutableComponentChunkArrayConstructorMap()[std::type_index(typeid(Type))] =
        [](size_t sizePerChunk) -> void* {
            return ECS::Component::ComponentChunkArrayConstructor<Type>(sizePerChunk);
        };

    _MutableComponentChunkArrayDestructorMap()[std::type_index(typeid(Type))] =
        [](void* addr) -> void {
            ECS::Component::ComponentChunkArrayDestructor<Type>(addr);
        };

    return true;
}

} // namespace ECS::Component

#define REGISTER_COMPONENT(key, Type) \
    ECS::Component::RegisterComponentType<Type>(key)