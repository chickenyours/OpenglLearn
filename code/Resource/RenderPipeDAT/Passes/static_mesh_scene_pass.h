#pragma once

#include "code/Resource/RenderPipeDAT/pass.h"
#include "code/Resource/Camera/camera.h"
#include "code/Resource/RenderPipeDAT/RenderItems/model_render_item.h"

#include "code/Resource/Material/Interfaces/BPR.h"

namespace Render{
    class StaticMeshScenePass : public Pass{
        public:
            // input
                // camera data
                Resource::Camera* mainCamera;
            // output
                GLint colorBuffer;
            // depend
                std::vector<std::vector<Render::ModelRenderItem>>* models;
        protected:
            virtual int InitSelf() override;
            virtual int UpdateSelf() override;
        private:
            GLuint FBO_ = 0;
            GLuint colorBuffer_ = 0;
            glm::ivec2 colorBufferResolution;
            GLuint depthBuffer_ = 0;
            GLuint UBOComponent_ = 0;

            struct Item{
                const Mesh* mesh;
                glm::mat4* modelMatrix;
                StaticModelComponentDataUBOLayout* ubodata;
            };
            std::unordered_map<IBPR*,std::vector<Item>> bprMaterialCache;
    };
}
