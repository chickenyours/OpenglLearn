#pragma once

#include <memory>

#include "code/RenderPipe/Pass/pass.h"

namespace Render{
    class ShaderProgram;

    class ImageToBuffer: public Pass{
        public:
            ImageToBuffer();
            void Init(const PassConfig& ctx) override;
            virtual void SetConfig(const PassConfig& cfg) override;
            void Update(const PassRenderContext& ctx, const std::vector<RenderItem>& renderItemList) override;
            void Release() override;
            ~ImageToBuffer();

        private:
            std::unique_ptr<ShaderProgram> screenShader_;
            GLuint framebuffer_ = 0;
            GLuint texture_ = 0;
            GLuint QuadVAO_ = 0;

            unsigned int screenWidth_;
            unsigned int screenHeight_;

    };
}