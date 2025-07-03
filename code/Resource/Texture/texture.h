

// 1D 纹理	GL_TEXTURE_1D	x	稀有，用于曲线、lookup
// 2D 纹理	GL_TEXTURE_2D	x, y	最常见，标准贴图、UI、GBuffer 等
// 3D 纹理	GL_TEXTURE_3D	x, y, z	用于体积纹理（如烟雾、医学成像）
// 2D 纹理数组	GL_TEXTURE_2D_ARRAY	x, y, layer	多层贴图，如动画帧、图集拆分
// 立方体纹理	GL_TEXTURE_CUBE_MAP	6 faces	环境贴图、反射、天空盒
// 多重采样纹理	GL_TEXTURE_2D_MULTISAMPLE	x, y + sample	用于抗锯齿 (MSAA)
// 多重采样数组	GL_TEXTURE_2D_MULTISAMPLE_ARRAY	x, y, layer + sample	用于延迟渲染 MSAA
// 矩阵纹理	GL_TEXTURE_RECTANGLE	类似 2D，但不归一化坐标	用于 FBO 等特殊情况

// {
        //     "configType": "resource",
        //     "resource": {
        //       "resourceType": "texture",
        //       "texture": {
        //         "textureType": "2D",
        //         "args":{
        //           "path": "./images/R.jpg",
        //           "wrapS": "REPEAT",
        //           "wrapT": "REPEAT",
        //           "minFilter": "LINEAR",
        //           "magFilter": "LINEAR",
        //           "needHDR" : false,
        //           "needMipMap": true
        //         }
        //       }
        //     }
        //   }

#pragma once

#include <string>
#include <glad/glad.h>
#include "code/ECS/Core/Resource/resource_interface.h"

namespace Resource {

    constexpr const char* DefualtTexture2DPath = ".json";

    class Texture2D : public ILoadFromConfig {
        public:

            bool LoadFromConfigFile(const std::string& configFile, Log::StackLogErrorHandle errHandle = nullptr) override;
        
            // 从文件加载 2D 纹理
            bool LoadFromFile2D(
                const std::string& imagePath,
                uint32_t wrapS = GL_REPEAT,
                uint32_t wrapT = GL_REPEAT,
                uint32_t minFilter = GL_LINEAR,
                uint32_t magFilter = GL_LINEAR,
                bool needHDR = false,
                bool needMipMap = true
            );
        
            // 创建一个空白 2D 纹理（如 render target）
            bool CreateEmpty2D(
                unsigned int width,
                unsigned int height,
                bool needHDR = false,
                bool needMipMap = false
            );
        
            // 加载 Cube Map（从六个图像路径）
            bool LoadFromFileCube(
                const std::string& posX, const std::string& negX,
                const std::string& posY, const std::string& negY,
                const std::string& posZ, const std::string& negZ,
                uint32_t wrapS = GL_CLAMP_TO_EDGE,
                uint32_t wrapT = GL_CLAMP_TO_EDGE,
                uint32_t wrapR = GL_CLAMP_TO_EDGE,
                uint32_t minFilter = GL_LINEAR,
                uint32_t magFilter = GL_LINEAR,
                bool needHDR = false,
                bool needMipMap = false
            );
        
            void Release() override;

            inline GLuint GetID() const {return id_;}
            inline uint32_t GetTextureType(){return textureType_;}
        
        private:
            GLuint id_ = 0;
            uint32_t textureType_ = GL_TEXTURE_2D;
        };

    class TextureCube{

    };

    class Texture2DArray{
        
    };
}