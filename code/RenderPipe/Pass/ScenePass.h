#pragma once

#include <iostream>
#include <memory>

#include "code/RenderPipe/Pass/pass.h"

namespace Render{
    class ShaderProgram;

    class ScenePass: public Pass{
        public:
            ScenePass();
            void Init(const PassConfig& cfg) override;
            virtual void SetConfig(const PassConfig& cfg) override;
            void Update(const PassRenderContext& ctx, const std::vector<RenderItem>& renderItemList) override;
            GLuint inline GetColorBufferTexture(){return colorBufferTexture_;}
            void Release() override;
        private:
            unsigned int targetBufferWidth_ = 0;
            unsigned int targetBufferHeight_ = 0;
            GLuint FBO_;
            GLuint colorBufferTexture_;
            GLuint depthBuffer_;
    };
}