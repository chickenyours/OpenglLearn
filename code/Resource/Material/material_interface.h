#pragma once

#include <Json/json.h>
#include <type_traits>
#include <unordered_map>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "code/ECS/Core/Resource/resource_handle.h"
#include "code/ECS/Core/Resource/resource_manager.h"
#include "code/ECS/Core/Resource/resource_load_option.h"

#include "code/Resource/Texture/texture.h"
#include "code/Resource/Shader/shader_type.h"
#include "code/Resource/Shader/shader_description.h"
#include "code/Resource/Shader/shader_program_factory.h"
#include "code/Resource/Shader/shader_program.h"


#include "code/DebugTool/ConsoleHelp/color_log.h"


namespace Resource{

class IMaterial{
    public:
        virtual ~IMaterial() = default;
        virtual bool LoadFromMataData(
            const Json::Value& textures,
            const Json::Value& shaderPrograms,
            const Json::Value& properties,
            const Json::Value& states,
            Log::StackLogErrorHandle errHandle = nullptr
        ) = 0;
};



// 接口内部Property结构体的基类,提供操作数据的基本方法
template <typename T>
class IMaterialProperty{
    public:
        void GetPropertyBlock(void*& addr, int& size){
            addr = (void*)this;
            size = sizeof(T);
        }
};

template <typename T>
class IMaterialState{
    public:
        std::unordered_map<ShaderStage, ShaderDescription> ToShaderDefines() const{
            static_assert(std::is_same<decltype(&T::ToShaderDefinesImpl), std::unordered_map<ShaderStage, ShaderDescription> (T::*)() const>::value,
                "T must implement std::vector<std::string> ToShaderDefinesImpl() const");
            return static_cast<const T*>(this)->ToShaderDefinesImpl();
        }
};

}