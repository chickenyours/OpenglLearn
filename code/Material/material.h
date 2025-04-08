#pragma once
#include <string>
#include <vector>
#include <map>
#include <memory>

#include <json/json.h>

#include <glad/glad.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <assimp/material.h>

#include "code/shader.h"
#include "code/Texture/texture.h"

using namespace std;

#define MAX_OPENGL_TEXTURE_CHANNEL 32

/**
 * @brief Starting from texture channel 16, all texture channels are custom textures.
 */
#define CUSTOM_TEXTURE_START 16

namespace Render{

    enum MaterialPropertyFlag{
        MATERIAL_SHADOW_OPAQUE = 1     ,
        MATERIAL_SHADOW_CAST   = 1 <<1 
    };

    struct UniformParameter{
        static UniformParameter globalUniformParameter;
        map<string, float> floatParameterMap;
        map<string, int> intParameterMap;
        map<string, bool> boolParameterMap;
        map<string, glm::vec3> vec3ParameterMap;
        map<string, glm::vec4> vec4ParameterMap;
        map<string, glm::mat4> mat4ParameterMap;
    };
    
    class Material{
        public:
            static map<string, float> GlobalFloatParameterMap;
            static map<string, int> GlobalIntParameterMap;
            static map<string, bool> GlobalBoolParameterMap;
            static map<string, glm::vec3> GlobalVec3ParameterMap;
            static map<string, glm::vec4> GlobalVec4ParameterMap;
            static map<string, glm::mat4> GlobalMat4ParameterMap;
            // 材质的名称
            string name;
            // 材质的ID(目前没有开发)
            int id;
            // 材质持有的纹理资源
            vector<Texture> textures;
            // 纹理通道,和m_TextureMap对齐,值为通道值
            vector<GLuint> textureChannel;
            // 材质的参数,在Json文件中的类型标记分别是: float int vec3 vec4 mat4
            map<string, float> floatParameterMap;
            map<string, int> intParameterMap;
            map<string, bool> boolParameterMap;
            map<string, glm::vec3> vec3ParameterMap;
            map<string, glm::vec4> vec4ParameterMap;
            map<string, glm::mat4> mat4ParameterMap;
            // 材质属性掩码
            int propertyFlag;
            // 材质持有的着色器
            unique_ptr<ShaderProgram> shaderProgram;
            // 材质的构造函数
            Material(const aiMaterial& material, const Json::Value& materialJson);
            Material(Material&& other);
            ~Material();
            // 设置材质的着色器
            void SetMaterialPropertiesToShader();
            // 加载材质的参数
            void LoadParameterFromModelAiMaterial(const aiMaterial& material);
            // 加载材质的参数
            void LoadParameterFromConfigFile(const Json::Value& materialJson);
            // 尝试加载所有的纹理
            void LoadAllTexture();
            // 绑定所有纹理
            void BindAllTexture();

            inline ShaderProgram* GetShaderProgram(){ return shaderProgram.get(); }

            void Print(int tabs = 0);

    }; 
}
