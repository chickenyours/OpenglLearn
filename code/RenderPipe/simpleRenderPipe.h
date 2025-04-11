// 这是一个渲染管线的头文件，定义了渲染管线的基本结构和接口
#pragma once

#include <iostream>
#include <vector>
#include <memory>

#include <glad/glad.h>

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "code/RenderPipe/RenderPipe.h"

namespace Render{
    class Mesh;
    class Material;
    class Camera;
    class RenderPipeConfig;
    class RenderPipeRenderContext;
    class RenderItem;

    class CSMPass; 
    class ImageToBufferPass;

    class SimpleRenderPipe : public RenderPipe{
        public:
            SimpleRenderPipe();
            virtual bool Init(const RenderPipeConfig& cfg) override;
            virtual void SetConfig(const RenderPipeConfig& cfg) override;
            void SetCamera(Camera* camera);
            ~SimpleRenderPipe();
        protected:
            virtual void Update(const std::vector<RenderItem>& renderItemList) override;
            virtual void Release() override;
        private:
            // CSM Pass
            std::unique_ptr<CSMPass> pCSMPass;
            std::unique_ptr<ImageToBufferPass> pImageToScreenPass;
            // 主相机(注意,并不是每个渲染管线都这样做,比如多相机渲染,但是RenderPipe和Pass解耦的话,你可以随便做,只需要给Pass提供正确的数据(渲染上下文))
            Camera* mainCamera_;
            // 视口(窗口)的长宽
            unsigned int viewWidth_;
            unsigned int viewHeight_;
    };
}