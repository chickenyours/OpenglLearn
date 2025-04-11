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
            void Release() override;
            void SetTexture(unsigned int GLTextureType, GLuint textureID);
        private:
            unsigned int targetBufferWidth_ = 0;
            unsigned int targetBufferHeight_ = 0;
            GLuint FBO_;
    };
}