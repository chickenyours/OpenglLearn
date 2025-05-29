#pragma once

#include <string>
#include <json/json.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "code/Resource/Material/material.h"

#include "code/DebugTool/ConsoleHelp/color_log.h"

namespace Resource{

// 蓝图类,和资源系统,子类实现接口来创建和管理具体Material类
// 使用静态映射多态,通过模板继承
template <typename Derived>
class MaterialBlueprint{
    public:
        static bool LoadMaterialFromConfigFile(
            Material& material,
            const Json::Value* textures,
            const Json::Value* properties,
            const Json::Value* shaders
        ){
            if(material.isLoaded_){
                LOG_WARNING_TEMPLATE("Resource MaterialBlueprint", "Material object has be loaded");
            }

            return Derived::LoadMaterialFromConfigFile(material, textures, properties, shaders);
        }
    protected:
        // 访问 Material 内部数据的访问器
        static void UploadProperty(Material& material, size_t start, size_t size, const void* data) {
            if(start + size > material.propertySize_){
                LOG_ERROR_TEMPLATE("Resource MaterialBlueprint", "Property buffer over size");
                return;
            }
            material.UploadPropertyData(start, size, data);
        }

        static void BindTexture(Material& material, GLuint textureID, size_t index) {
            if(index > material.textureSize_){
               LOG_ERROR_TEMPLATE("Resource MaterialBlueprint", "Texture over size"); 
               return;
            }
            material.BindTexture(textureID, index);
        }

        static void BindShader(Material& material, GLuint shaderID, size_t index) {
            if(index > material.shaderProgramSize_){
               LOG_ERROR_TEMPLATE("Resource MaterialBlueprint", "Texture over size"); 
               return;
            }
            material.BindShader(shaderID, index);
        }
};

}
