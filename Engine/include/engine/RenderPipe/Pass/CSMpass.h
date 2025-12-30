#pragma once

#include <memory>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
// 继承类必须包含
#include "code/RenderPipe/Pass/pass.h"
#include "code/RenderPipe/UniformBindings.h"

namespace Render{
    class PassConfig;
    class PassRenderContext;
    class RenderItem;
    class Camera;
    class ShaderProgram;
    
    class CSMPass:public Pass{
        public:
            CSMPass();
            void Init(const PassConfig& ctx) override;
            virtual void SetConfig(const PassConfig& cfg) override;
            void Update(const PassRenderContext& ctx, const std::vector<RenderItem>& renderItemList) override;
            void Release() override;
            void SetCamera(Camera* camera);
            inline unsigned int GetLightDepthMapNumLayout(){return DistanceLayers_.size();}
            inline GLuint GetlightDepthMaps(){return lightDepthMaps_;}
        private:
            glm::vec3 lightDir_;
            int distanceLayersCount_; 
            std::vector<float> DistanceLayers_;
            glm::mat4 GetLightSpaceMatrix(const float nearPlane, const float farPlane,const Camera &camera);
            std::vector<glm::mat4> GetLightSpaceMatrices(const Camera &camera,const std::vector<float>& shadowCascadeLevels);
            std::vector<glm::vec4> GetFrustumCornersWorldSpace(const glm::mat4& projview);
            std::vector<glm::vec4> GetFrustumCornersWorldSpace(const glm::mat4& proj, const glm::mat4& view);

            GLuint matricesUBO_;
            GLuint CSMInfoUBO_;
            GLuint lightFBO_;
            GLuint lightDepthMaps_;
            int depthMapResolution_ = 1024;

            std::unique_ptr<ShaderProgram> depthShader_;
            Camera* camera_;

            CSMInfoUBOLayout CSMUBOdata_;
        
        public:
            //调试数据
            GLuint visualMap1;
            GLuint visualMap2;
            GLuint visualMap3;
            GLuint visualMap4;
            bool isVisual = false;
        private:
            GLuint visualFBO_;
            GLuint visualRBO_;
            std::unique_ptr<ShaderProgram> visualMap1Shader_;
            // std::unique_ptr<ShaderProgram> visualMap2Shader_;
            // std::unique_ptr<ShaderProgram> visualMap3Shader_;
    };
}