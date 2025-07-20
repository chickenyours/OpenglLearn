#pragma once

#include "code/Resource/RenderPipe/pass.h"

#include "code/ECS/Core/Resource/resource_handle.h"
#include "code/ECS/Core/Resource/resource_load_option.h"
#include "code/ECS/Core/Resource/resource_manager.h"

#include "code/Resource/Shader/shader_program_factory.h"

namespace Render
{

    struct ImageToBufferRenderItem{
        unsigned int GLTextureType;
        GLuint textureID;
        
        bool scaleViewPortflag = 0.0;
        float viewPortScaleOffsetX = 0.0;
        float viewPortScaleOffsetY = 0.0;
        float viewPortScaleWidth = 0.0;
        float viewPortScaleHeight = 0.0;
        
        int textureArraylayerIndex = 0;
        unsigned int viewPortOffsetX = 0;
        unsigned int viewPortOffsetY = 0;
        unsigned int viewPortWidth = 0;
        unsigned int viewPortHeight = 0;

    };

    class ImageToBufferPass : public Pass{
        public:
            virtual bool Init(const PassContex& cfg) override {
                simpleShader = std::move(
                    ECS::Core::ResourceModule::ResourceManager::GetInctance().Get<Resource::ShaderProgramFactory>(
                        FromConfig<Resource::ShaderProgramFactory>("./RenderShader/screen.json"))->GetShaderProgramInstance()
                );

                if(!simpleShader){
                    LOG_ERROR("Pass:ImageToBufferPass", "Failed to load simpleShader!");
                    return false;
                }

                if (cfg.outputResolution.x <= 0 || cfg.outputResolution.y <= 0 || cfg.outputResolution.x >= 3000 || cfg.outputResolution.y >= 3000) {
                    LOG_WARNING("Pass:ImageToBufferPass", "Invalid output resolution!");
                }
                screenTargetResolution_ = cfg.outputResolution;
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

                return true;
            }
            virtual void SetConfig(const PassContex& cfg) override {
                if (cfg.outputResolution.x <= 0 || cfg.outputResolution.y <= 0 || cfg.outputResolution.x >= 3000 || cfg.outputResolution.y >= 3000) {
                    LOG_WARNING("Pass:ImageToBufferRenderItem", "Invalid output resolution!");
                }
                if(screenTargetResolution_ == cfg.outputResolution) return;
                screenTargetResolution_ = cfg.outputResolution;
                for(auto& passRenderItem : views_){
                    if(passRenderItem.scaleViewPortflag){
                        passRenderItem.viewPortOffsetX = static_cast<int>(screenTargetResolution_.x * passRenderItem.viewPortScaleOffsetX);
                        passRenderItem.viewPortOffsetY = static_cast<int>(screenTargetResolution_.y * passRenderItem.viewPortScaleOffsetY);
                        passRenderItem.viewPortWidth = static_cast<int>(screenTargetResolution_.x * passRenderItem.viewPortScaleWidth);
                        passRenderItem.viewPortHeight = static_cast<int>(screenTargetResolution_.y * passRenderItem.viewPortScaleHeight);
                    }
                }
            }

            int AddPassRenderItem(ImageToBufferRenderItem passRenderItem){
                if(passRenderItem.scaleViewPortflag){
                        passRenderItem.viewPortOffsetX = static_cast<int>(screenTargetResolution_.x * passRenderItem.viewPortScaleOffsetX);
                        passRenderItem.viewPortOffsetY = static_cast<int>(screenTargetResolution_.y * passRenderItem.viewPortScaleOffsetY);
                        passRenderItem.viewPortWidth = static_cast<int>(screenTargetResolution_.x * passRenderItem.viewPortScaleWidth);
                        passRenderItem.viewPortHeight = static_cast<int>(screenTargetResolution_.y * passRenderItem.viewPortScaleHeight);
                }
                views_.push_back(passRenderItem);
                return static_cast<int>(views_.size()) - 1;
            }

            virtual void Update() override {
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                glClearColor(0.0f, 1.00f, 0.00f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT);
                glDisable(GL_DEPTH_TEST);
                glBindVertexArray(QuadVAO_);
                glActiveTexture(GL_TEXTURE0);
                for (const auto& item : views_) {
                    glViewport(item.viewPortOffsetX,item.viewPortOffsetY,item.viewPortWidth,item.viewPortHeight);
                    if (item.GLTextureType == GL_TEXTURE_2D) {
                        glUseProgram(simpleShader->GetID());
                        // std::cout << simpleShader->GetID() << std::endl;
                        glBindTexture(GL_TEXTURE_2D, item.textureID);
                        glDrawArrays(GL_TRIANGLES, 0, 6);
                    } else if (item.GLTextureType == GL_TEXTURE_2D_ARRAY) {
                        // screenTextureArrayShader_->Use();
                        // ShaderU1i(*screenTextureArrayShader_, "layerIndex", item.textureArraylayerIndex);
                        // glBindTexture(GL_TEXTURE_2D_ARRAY, item.textureID);
                    } else {
                        LOG_WARNING("Pass:ImageToBufferRenderItem", "Unknown GLTextureType");
                        continue;
                    }
                }
                glEnable(GL_DEPTH_TEST);
            }
            virtual void ClearCache() override {
                views_.clear();
            }
            ~ImageToBufferPass(){
                if(QuadVAO_){
                    glDeleteVertexArrays(1, &QuadVAO_);
                    QuadVAO_ = 0;
                }
            }

            
        private:
            glm::ivec2 screenTargetResolution_;
            Resource::ResourceHandle<Resource::ShaderProgram> simpleShader;
            std::vector<ImageToBufferRenderItem> views_;
            GLuint QuadVAO_ = 0;
    };
} // namespace Render
