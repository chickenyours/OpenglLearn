#pragma once

#include "code/RenderPipe/UniformBindings.h"
#include "code/RenderPipe/Pass/pass.h"

namespace Render{
    class Camera;
    // 你可能觉得奇怪,连最简单的CameraUBO(写个函数都可以)还要写个Pass,杂鱼~
    class CameraPass: public Pass{
        public:
            virtual  void Init(const PassConfig& cfg) override;
            virtual void SetConfig(const PassConfig& cfg) override;
            virtual void Update(const PassRenderContext& ctx, const std::vector<RenderItem>& renderItemList) override;
            virtual void Release() override;
            // void SetCamera(Camera* camera); 
        private:
            GLuint UBOCamera_;
            CameraDataUBOLayout UBOdata_;
    };
}