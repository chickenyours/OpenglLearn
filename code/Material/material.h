#pragma once
#include <iostream>
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <memory>
#include <json/json.h>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <assimp/material.h>



#define MAX_OPENGL_TEXTURE_CHANNEL 32

/**
 * @brief Starting from texture channel 16, all texture channels are custom textures.
 */
#define CUSTOM_TEXTURE_START 16

namespace Render{
    class ShaderProgram;
    class Texture;
    enum class RenderPassFlag : uint64_t;

    enum MaterialPropertyFlag{
        MATERIAL_SHADOW_OPAQUE = 1     ,
        MATERIAL_SHADOW_CAST   = 1 <<1 
    };

    struct UniformParameter{
        static UniformParameter globalUniformParameter;
        std::map<std::string, float> floatParameterMap;
        std::map<std::string, int> intParameterMap;
        std::map<std::string, bool> boolParameterMap;
        std::map<std::string, glm::vec3> vec3ParameterMap;
        std::map<std::string, glm::vec4> vec4ParameterMap;
        std::map<std::string, glm::mat4> mat4ParameterMap;
    };
    
    class Material{
        public:
            static std::map<std::string, float> GlobalFloatParameterMap;
            static std::map<std::string, int> GlobalIntParameterMap;
            static std::map<std::string, bool> GlobalBoolParameterMap;
            static std::map<std::string, glm::vec3> GlobalVec3ParameterMap;
            static std::map<std::string, glm::vec4> GlobalVec4ParameterMap;
            static std::map<std::string, glm::mat4> GlobalMat4ParameterMap;
            // 材质的名称
            std::string name;
            // 材质的ID(目前没有开发)
            int id = 0;
            // 材质持有的纹理资源
            std::vector<Texture> textures;
            // 纹理通道,和m_TextureMap对齐,值为通道值
            std::vector<GLuint> textureChannel;
            // Pass掩码
            uint64_t renderEnablePassFlag_ = 0;
            uint64_t renderDisablePassFlag_ = 0;
            // 材质的参数,在Json文件中的类型标记分别是: float int vec3 vec4 mat4
            std::map<std::string, float> floatParameterMap;
            std::map<std::string, int> intParameterMap;
            std::map<std::string, bool> boolParameterMap;
            std::map<std::string, glm::vec3> vec3ParameterMap;
            std::map<std::string, glm::vec4> vec4ParameterMap;
            std::map<std::string, glm::mat4> mat4ParameterMap;
            // 材质属性掩码
            // int propertyFlag;
            // 材质持有的着色器
            std::unique_ptr<ShaderProgram> shaderProgram;
            // 材质的构造函数
            Material(const aiMaterial& material, const Json::Value& materialJson);
            Material(Material&& other);
            ~Material();
            // 设置材质的着色器
            void SetMaterialPropertiesToShader();
            // 加载材质的参数
            void LoadParameterFromModelAiMaterial(const aiMaterial& material);
            // 加载材质的参数
            void LoadParameterFromMaterialJson(const Json::Value& materialJson);
            // 尝试加载所有的纹理
            void LoadAllTexture();
            // 绑定所有纹理
            void BindAllTexture();

            // 加载材质配置中的文件里的渲染模式(pass)
            void LoadRenderPassFlagFromMaterialJson(const Json::Value& materialJsonRenderMode);

            inline ShaderProgram* GetShaderProgram(){ return shaderProgram.get(); }

            void Print(int tabs = 0);

    }; 
}
