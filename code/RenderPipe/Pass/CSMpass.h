#pragma once
#include "code/RenderPipe/Pass/pass.h"

namespace Render{
    class CSMPass:public Pass{
        public:
        void Init(const InitRenderContext& ctx) override;
        void Update(const RenderContext& ctx) override;
        void Release() override;
        private:
        glm::vec3 lightDir;
        glm::mat4 GetLightSpaceMatrix(const float nearPlane, const float farPlane,const Camera &camera);
        std::vector<glm::mat4> GetLightSpaceMatrices(const Camera &camera,std::vector<float> shadowCascadeLevels);
        std::vector<glm::vec4> GetFrustumCornersWorldSpace(const glm::mat4& projview);
        std::vector<glm::vec4> GetFrustumCornersWorldSpace(const glm::mat4& proj, const glm::mat4& view);
 
    };
}