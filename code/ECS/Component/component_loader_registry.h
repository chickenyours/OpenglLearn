#pragma once

#include <unordered_map>
#include <functional>
#include <type_traits>
#include <string>
#include <json/json.h>

#include "code/ECS/Component/component.h"

#include "code/ECS/Component/component_register.h"
#include "code/DebugTool/ConsoleHelp/color_log.h"
#include "code/ECS/data_type.h"  // 包含 EntityID 和 ComponentRegister 声明

namespace ECS::Component {

// 通用 Loader 类型
using ComponentLoader = std::function<bool(
    ECS::Core::ComponentRegister&,
    ECS::EntityID,
    const Json::Value&,
    Log::StackLogErrorHandle
)>;

// 🔒 内部修改接口（仅限 REGISTER_COMPONENT 使用）
inline std::unordered_map<std::string, ComponentLoader>& _MutableComponentLoaderMap() {
    static std::unordered_map<std::string, ComponentLoader> loaderMap;
    return loaderMap;
}

// 🔓 外部只读访问接口
inline const std::unordered_map<std::string, ComponentLoader>& GetComponentLoaderMap() {
    return _MutableComponentLoaderMap();
}

// 注册宏
#define REGISTER_COMPONENT(key, Type) \
    static_assert(std::is_base_of<ECS::Component::Component<Type>, Type>::value, \
            #Type " must derive from Component<" #Type ">"); \
    ECS::Component::_MutableComponentLoaderMap()[key] = [](ECS::Core::ComponentRegister& reg, ECS::EntityID id, const Json::Value& data, Log::StackLogErrorHandle err = nullptr) { \
        Type* component = reg.AddComponent<Type>(id); \
        return component->LoadFromMataData(data, err); \
    }

} // namespace ECS::Component