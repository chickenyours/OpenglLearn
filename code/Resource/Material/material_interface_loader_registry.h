#pragma once

#include <string>
#include <json/json.h>
#include <functional>
#include <typeindex>
#include "code/DebugTool/ConsoleHelp/color_log.h"

#include "code/Resource/Material/material_interface.h"

namespace Resource{
    using IMaterialLoader = std::function<std::unique_ptr<IMaterial>()>;
    
    // 🔒 内部修改接口（仅限 REGISTER_MATERIAL_INTERFACE 使用）
    inline std::unordered_map<std::string, IMaterialLoader>& _MutableIMaterialLoaderMap() {
        static std::unordered_map<std::string, IMaterialLoader> loaderMap;
        return loaderMap;
    }
    
    // 🔓 外部只读访问接口
    inline const std::unordered_map<std::string, IMaterialLoader>& GetIMaterialLoaderMap() {
        return _MutableIMaterialLoaderMap();
    }

    // 类型映射表
    inline std::unordered_map<std::string, std::type_index>& _MutableIMaterialTypeMap() {
        static std::unordered_map<std::string, std::type_index> typeMap;
        return typeMap;
    }

    // 类型映射表只读访问接口
    inline const std::unordered_map<std::string, std::type_index>& GetIMaterialTypeMap() {
        return _MutableIMaterialTypeMap();
    }
    
    // 更新注册宏
    #define REGISTER_MATERIAL_INTERFACE(key, Type) \
        static_assert(std::is_base_of<IMaterial, Type>::value, \
                #Type " must derive from IMaterial"); \
        Resource::_MutableIMaterialLoaderMap()[key] = []()->std::unique_ptr<IMaterial> { \
            return std::make_unique<Type>(); \
        }; \
        Resource::_MutableIMaterialTypeMap().emplace(std::string(key), std::type_index(typeid(Type)));
    
    // // 注册宏
    // #define REGISTER_MATERIAL_INTERFACE(key, Type) \
    //     static_assert(std::is_base_of<IMaterial, Type>::value, \
    //             #Type " must derive from IMaterial"); \
    //     Resource::_MutableIMaterialLoaderMap()[key] = []()->std::unique_ptr<Type> { \
    //         return std::make_shared<Type>(); \
    //     }

}    

