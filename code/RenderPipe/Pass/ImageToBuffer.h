#pragma once

#include <iostream>
#include <memory>

#include "code/RenderPipe/Pass/pass.h"

namespace Render{
    class ShaderProgram;

    class ImageToBufferPass: public Pass{
        public:
            ImageToBufferPass();
            void Init(const PassConfig& ctx) override;
            virtual void SetConfig(const PassConfig& cfg) override;
            void Update(const PassRenderContext& ctx, const std::vector<RenderItem>& renderItemList) override;
            void Release() override;
            void SetTexture(unsigned int GLTextureType, GLuint textureID);
            inline void SetTextureArrayLayerIndex(int index){textureArraylayerIndex_= index;}
            inline void SetTargetFrameBuffer(GLuint targetFrameBufferID){framebuffer_ = targetFrameBufferID;}
        private:
            std::unique_ptr<ShaderProgram> screenTextureShader_;
            std::unique_ptr<ShaderProgram> screenTextureArrayShader_;
            GLuint framebuffer_ = 0;
            GLuint texture_ = 0;
            GLuint textureArray_ = 0;
            int textureArraylayerIndex_ = 0;
            GLuint QuadVAO_ = 0;

            unsigned int flag = 0;
            unsigned int screenWidth_ = 0;
            unsigned int screenHeight_ = 0;

    };
}