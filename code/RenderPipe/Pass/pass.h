#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>



namespace Render{
    class PassConfig;
    class PassRenderContext;
    class RenderItem;

    class Pass {
        public:
            Pass();
            virtual void Init(const PassConfig& cfg) = 0;
            virtual void SetConfig(const PassConfig& cfg) = 0;
            virtual void Update(const PassRenderContext& ctx, const std::vector<RenderItem>& renderItemList) = 0;
            virtual void Release();
            virtual ~Pass();
    };
}