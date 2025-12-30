#pragma once

#include "code/ToolAndAlgorithm/DAT/target.h"
#include "code/Resource/RenderPipeDAT/pass.h"
#include "code/ECS/Core/Resource/resource_handle.h"
#include "code/ECS/Core/Resource/resource_load_option.h"
#include "code/ECS/Core/Resource/resource_manager.h"
#include "code/Resource/Shader/shader_program_factory.h"

namespace Render{
    struct ScreenNodeInput{
        
    };
    struct ScreenNodeOutput{
        
    };
    struct ScreenNodeDepend{
        
    };

    struct ImageToBufferRenderItem{
        unsigned int GLTextureType;
        GLuint textureID;
        
        bool scaleViewPortflag = false;
        float viewPortScaleOffsetX = 0.0f;
        float viewPortScaleOffsetY = 0.0f;
        float viewPortScaleWidth = 0.0f;
        float viewPortScaleHeight = 0.0f;
        
        int textureArraylayerIndex = 0;
        unsigned int viewPortOffsetX = 0;
        unsigned int viewPortOffsetY = 0;
        unsigned int viewPortWidth = 0;
        unsigned int viewPortHeight = 0;

    };

    class ScreenPass : public Pass<ScreenNodeInput,ScreenNodeOutput,ScreenNodeDepend>{
        private:
            glm::ivec2 screenTargetResolution_;
            std::vector<ImageToBufferRenderItem> views_;
            GLuint QuadVAO_ = 0;
            Resource::ResourceHandle<Resource::ShaderProgram> simpleShader;
        protected:
            virtual int Init(const NodeContext& ctx){
                simpleShader = std::move(
                    ECS::Core::ResourceModule::ResourceManager::GetInctance().Get<Resource::ShaderProgramFactory>(
                        FromConfig<Resource::ShaderProgramFactory>("./RenderShader/screen.json"))->GetShaderProgramInstance()
                );
                if(!simpleShader){
                    LOG_ERROR("Pass:ImageToBufferPass", "Failed to load simpleShader!");
                    return 1;
                }

                if (ctx.RenderPipeOutputResolution.x <= 0 || ctx.RenderPipeOutputResolution.y <= 0 || ctx.RenderPipeOutputResolution.x >= 3000 || ctx.RenderPipeOutputResolution.y >= 3000) {
                    LOG_WARNING("Pass:ImageToBufferPass", "Invalid output resolution!");
                }
                screenTargetResolution_ = ctx.RenderPipeOutputResolution;
                float quadVertices[] = {
                    // positions   // texCoords
                    -1.0f,  1.0f,  0.0f, 1.0f,
                    -1.0f, -1.0f,  0.0f, 0.0f,
                    1.0f, -1.0f,  1.0f, 0.0f,

                    -1.0f,  1.0f,  0.0f, 1.0f,
                    1.0f, -1.0f,  1.0f, 0.0f,
                    1.0f,  1.0f,  1.0f, 1.0f
                };
                GLuint VAO, VBO;
                glGenVertexArrays(1, &VAO);
                glGenBuffers(1, &VBO);
                glBindVertexArray(VAO);
                glBindBuffer(GL_ARRAY_BUFFER, VBO);
                glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
                glEnableVertexAttribArray(0);
                glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
                glEnableVertexAttribArray(1);
                glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
                glBindVertexArray(0);

                QuadVAO_ = VAO;
                return 0;
            }
            virtual int Set(const NodeContext& cxt){
                return 0;
            }
            virtual int Update(){
                return 0;
            }
        public:
            ~ScreenPass(){
                if(QuadVAO_){
                    glDeleteVertexArrays(1, &QuadVAO_);
                    QuadVAO_ = 0;
                }
            }
    };
}
