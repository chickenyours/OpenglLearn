// 这是一个渲染管线的头文件，定义了渲染管线的基本结构和接口
#pragma once

#include <iostream>
#include <vector>
#include <memory>

#include <glad/glad.h>

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Render{
    class RenderItem;
    class RenderPipeRenderContext;
    class RenderPipeConfig;

    class CSMPass; 

    class RenderPipe{
        public:
            virtual bool Init(const RenderPipeConfig& cfg) = 0;
            virtual void SetConfig(const RenderPipeConfig& cfg) = 0;
            void RenderCall();
            void Push(const RenderItem& renderItem);
        protected:
            virtual void Update(const std::vector<RenderItem>& renderItemList) = 0;
            virtual void Release() = 0;
            // 渲染队列
        private:
            std::vector<RenderItem> renderItemList_;
    };
}