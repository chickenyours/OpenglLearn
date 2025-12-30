#pragma once

#include <iostream>
#include <memory>

#include "code/RenderPipe/Pass/pass.h"

namespace Render{
    class ShaderProgram;
    struct ImageToBufferRenderItem{
        unsigned int GLTextureType;
        GLuint textureID;
        int textureArraylayerIndex;

        unsigned int viewPortOffsetX;
        unsigned int viewPortOffsetY;
        unsigned int viewPortWidth;
        unsigned int viewPortHeight;

        bool scaleViewPortflag;
        float viewPortScaleOffsetX;
        float viewPortScaleOffsetY;
        float viewPortScaleWidth;
        float viewPortScaleHeight;
    };

    class ImageToBufferPass: public Pass{
        public:
            ImageToBufferPass();
            void Init(const PassConfig& ctx) override;
            virtual void SetConfig(const PassConfig& cfg) override;
            void Update(const PassRenderContext& ctx, const std::vector<RenderItem>& renderItemList) override;
            void Release() override;
            // void SetTexture(unsigned int GLTextureType, GLuint textureID);
            // inline void SetTextureArrayLayerIndex(int index){textureArraylayerIndex_= index;}
            inline void SetTargetFrameBuffer(GLuint targetFrameBufferID){framebuffer_ = targetFrameBufferID;}
            int AddPassRenderItem(ImageToBufferRenderItem& passRenderItem);
            bool SetPassRenderItem(int index, ImageToBufferRenderItem& passRenderItem);
            void ClearPassRenderItem();


        private:
            std::unique_ptr<ShaderProgram> screenTextureShader_;
            std::unique_ptr<ShaderProgram> screenTextureArrayShader_;

            GLuint QuadVAO_ = 0;
            unsigned int screenWidth_ = 0;
            unsigned int screenHeight_ = 0;

            std::vector<ImageToBufferRenderItem> passRenderItemList_;


            GLuint framebuffer_ = 0;
            // GLuint texture_ = 0;
            // GLuint textureArray_ = 0;
            // int textureArraylayerIndex_ = 0;
            // unsigned int flag = 0;

    };
}