#pragma once
#include <cstdint>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>



namespace Render{
    class PassConfig;
    class PassRenderContext;
    class RenderItem;
    enum class RenderPassFlag : uint64_t;

    class Pass {
        public:
            Pass();
            virtual void Init(const PassConfig& cfg) = 0;
            virtual void SetConfig(const PassConfig& cfg) = 0;
            virtual void Update(const PassRenderContext& ctx, const std::vector<RenderItem>& renderItemList) = 0;
            virtual void Release();
            virtual ~Pass();
        protected:
            inline void SetDefaultPass(bool flag){defaultPassFlag = flag;}
            bool CheckPass(RenderPassFlag flag, uint64_t renderEnablePassFlag, uint64_t renderDisablePassFlag);
        private:
            bool defaultPassFlag = true;
    };
}