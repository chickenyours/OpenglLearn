#pragma once

#include "code/Resource/RenderPipeDAT/pass.h"

#include "code/Resource/RenderPipe/RenderItems/model_render_item.h"
#include "code/Resource/Material/Interfaces/BPR.h"
#include "code/ECS/Component/Render/mesh_renderer.h"
#include "code/Resource/RenderPipeDAT/data_interface.h"

namespace Render{

    struct SceneNodeInput{
        
    };

    struct SceneNodeOutput{
        TextureBuffer colorBuffer;
        DepthBuffer depthBuffer;
    };

    struct SceneNodeDepend{
        VersionTargetGroupViewer<Render::ModelRenderItem> renderItems;
    };

    class ScenePass : public Node<SceneNodeInput,SceneNodeOutput,SceneNodeDepend>{
        private:
            struct MeshSimpleItem{
                const Mesh* mesh;
                const glm::mat4* modelMatrix;
                const ECS::Component::MeshRenderer* com;
            };
            std::unordered_map<Resource::Material*, IBPR*> cache;
            GLuint FBO_ = 0;
            GLuint colorBuffer_ = 0;
            GLuint depthBuffer_ = 0;
            GLuint UBOComponent_ = 0;
            glm::ivec2 colorBufferResolution;
        public:
            virtual int Init(const NodeContext& cxt) override {
                if (cxt.RenderPipeOutputResolution.x > 3000 || cxt.RenderPipeOutputResolution.y > 3000 || cxt.RenderPipeOutputResolution.x <= 15 || cxt.RenderPipeOutputResolution.y <= 15) {
                    LOG_ERROR("RenderPass:ScenePass->SetConfig", "Invalid output resolution: x=" + std::to_string(cxt.RenderPipeOutputResolution.x) + ", y=" + std::to_string(cxt.RenderPipeOutputResolution.y));
                    return 1;
                }
                colorBufferResolution = cxt.RenderPipeOutputResolution;
                glGenFramebuffers(1,&FBO_);
                CHECK_GL_ERROR("glGenFramebuffers");
                glGenTextures(1,&colorBuffer_);
                CHECK_GL_ERROR("glGenTextures");
                glGenRenderbuffers(1, &depthBuffer_);
                CHECK_GL_ERROR("glGenRenderbuffers");

                glBindTexture(GL_TEXTURE_2D, colorBuffer_);
                CHECK_GL_ERROR("glBindTexture");

                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, cxt.RenderPipeOutputResolution.x, cxt.RenderPipeOutputResolution.y, 0, GL_RGBA, GL_FLOAT, nullptr);
                CHECK_GL_ERROR("glTexImage2D");

                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                CHECK_GL_ERROR("glTexParameteri(GL_TEXTURE_MIN_FILTER)");

                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                CHECK_GL_ERROR("glTexParameteri(GL_TEXTURE_MAG_FILTER)");

                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                CHECK_GL_ERROR("glTexParameteri(GL_TEXTURE_WRAP_S)");

                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                CHECK_GL_ERROR("glTexParameteri(GL_TEXTURE_WRAP_T)");

                glBindFramebuffer(GL_FRAMEBUFFER, FBO_);
                CHECK_GL_ERROR("glBindFramebuffer");

                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorBuffer_, 0);
                CHECK_GL_ERROR("glFramebufferTexture2D");

                glBindRenderbuffer(GL_RENDERBUFFER, depthBuffer_);
                CHECK_GL_ERROR("glBindRenderbuffer");

                glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, cxt.RenderPipeOutputResolution.x, cxt.RenderPipeOutputResolution.y);
                CHECK_GL_ERROR("glRenderbufferStorage");

                glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depthBuffer_);
                CHECK_GL_ERROR("glFramebufferRenderbuffer");
                

                GLint colorAttachmentType;
                glGetFramebufferAttachmentParameteriv(
                    GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &colorAttachmentType
                );
                if (colorAttachmentType == GL_NONE) {
                    LOG_ERROR("RenderPass:ScenePass->Init", "Color attachment 0 is missing!");
                    return 1;
                }
                glGetFramebufferAttachmentParameteriv(
                    GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &colorAttachmentType
                );
                if (colorAttachmentType == GL_NONE) {
                    LOG_ERROR("RenderPass:ScenePass->Init", "Depth attachment is missing!");
                    return 1;
                }

                if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                    LOG_ERROR("RenderPass:ScenePass->Init", "Framebuffer not complete!");
                    return 1;
                }
                
                glBindFramebuffer(GL_FRAMEBUFFER, 0);

                // UBO

                glGenBuffers(1, &UBOComponent_);
                if(CHECK_GL_ERROR("glGenBuffers")) return 1;
                glBindBuffer(GL_UNIFORM_BUFFER, UBOComponent_);
                if(CHECK_GL_ERROR("glBindBuffer")) return 1;
                glBufferData(GL_UNIFORM_BUFFER, sizeof(StaticModelComponentDataUBOLayout), nullptr, GL_DYNAMIC_DRAW);
                if(CHECK_GL_ERROR("glBufferData")) return 1;
                glBindBufferBase(GL_UNIFORM_BUFFER, UBO_STATIC_MODEL_COMPONENT_DATA, UBOComponent_);
                if(CHECK_GL_ERROR("glBindBufferBase")) return 1;
                glBindBuffer(GL_UNIFORM_BUFFER, 0);

                return 0;
            }
            virtual int Set(const NodeContext& cxt) override {
                if(colorBufferResolution == cxt.RenderPipeOutputResolution) return 0;
                if (cxt.RenderPipeOutputResolution.x > 3000 || cxt.RenderPipeOutputResolution.y > 3000 || cxt.RenderPipeOutputResolution.x <= 15 || cxt.RenderPipeOutputResolution.y <= 15) {
                    LOG_ERROR("RenderPass:ScenePass->SetConfig", "Invalid output resolution: x=" + std::to_string(cxt.RenderPipeOutputResolution.x) + ", y=" + std::to_string(cxt.RenderPipeOutputResolution.y));
                    return 1;
                }
                colorBufferResolution = cxt.RenderPipeOutputResolution;
                glBindTexture(GL_TEXTURE_2D, colorBuffer_);
                CHECK_GL_ERROR("glBindTexture");
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, colorBufferResolution.x, colorBufferResolution.y, 0, GL_RGBA, GL_FLOAT, nullptr);
                CHECK_GL_ERROR("glTexImage2D");
                glBindRenderbuffer(GL_RENDERBUFFER, depthBuffer_);
                CHECK_GL_ERROR("glBindRenderbuffer");
                glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, colorBufferResolution.x, colorBufferResolution.y);
                CHECK_GL_ERROR("glRenderbufferStorage");
                glBindFramebuffer(GL_FRAMEBUFFER, FBO_);
                CHECK_GL_ERROR("glBindFramebuffer");
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colorBuffer_, 0);
                CHECK_GL_ERROR("glFramebufferTexture2D");
                glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depthBuffer_);
                CHECK_GL_ERROR("glFramebufferRenderbuffer");
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                return 0;
            }
            virtual int Update() override {
                // auto flag = depend.renderItems.Update();
                // if(flag != VersionTargetGroupViewerFlag::SUCCESS &&
                //    flag == VersionTargetGroupViewerFlag::CHANGE_ALL){
                //     for(const auto& it : depend.renderItems.addArray){
                        
                //     }

                // }
                auto group = depend.renderItems.Get();
                if(group){
                    for(auto& array : group->Get()){
                        for(auto& item : array->Get()){
                            for(auto& it : item->model->GetMeshes()){
                                if(item->materialList.size() < it.GetMaterialIndex()){
                                    auto material = item->materialList[it.GetMaterialIndex()];
                                    auto bprIt = cache.find(material);
                                    if(bprIt != cache.end()){
                                        IBPR* bpr = bprIt->second;
                                        glActiveTexture(GL_TEXTURE0);
                                        CHECK_GL_ERROR("glActiveTexture(GL_TEXTURE0)");
                                        glBindTexture(GL_TEXTURE_2D, bpr->albedoMap_->GetID());
                                        CHECK_GL_ERROR("glBindTexture(albedoMap)");
                                        glActiveTexture(GL_TEXTURE1);
                                        CHECK_GL_ERROR("glActiveTexture(GL_TEXTURE1)");
                                        glBindTexture(GL_TEXTURE_2D, bpr->normalMap_->GetID());
                                        CHECK_GL_ERROR("glBindTexture(normalMap)");
                                        if(bpr->state_.useMetallicMap){
                                            glActiveTexture(GL_TEXTURE2);
                                            CHECK_GL_ERROR("glActiveTexture(GL_TEXTURE2)");
                                            glBindTexture(GL_TEXTURE_2D, bpr->metallicMap_->GetID());
                                            CHECK_GL_ERROR("glBindTexture(metallicMap)");
                                        }
                                        if(bpr->state_.useRoughnessMap){
                                            glActiveTexture(GL_TEXTURE3);
                                            CHECK_GL_ERROR("glActiveTexture(GL_TEXTURE3)");
                                            glBindTexture(GL_TEXTURE_2D, bpr->roughnessMap_->GetID());
                                            CHECK_GL_ERROR("glBindTexture(roughnessMap)");
                                        }
                                        if(bpr->state_.useAoMap){
                                            glActiveTexture(GL_TEXTURE4);
                                            CHECK_GL_ERROR("glActiveTexture(GL_TEXTURE4)");
                                            glBindTexture(GL_TEXTURE_2D, bpr->aoMap_->GetID());
                                            CHECK_GL_ERROR("glBindTexture(aoMap)");
                                        }
                                        bpr->UpLoadProperty();
                                        glUseProgram(bpr->mainShader_->GetID());
                                        CHECK_GL_ERROR("glUseProgram");
                                        glBindBuffer(GL_UNIFORM_BUFFER,UBOComponent_);
                                        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(StaticModelComponentDataUBOLayout), &item->com->uboData);
                                        CHECK_GL_ERROR("glBufferSubData");
                                        ShaderUmatf4(bpr->mainShader_->GetID(), "model", *item->modelMatrix);
                                        glBindVertexArray(it.GetVAO());
                                        CHECK_GL_ERROR("glBindVertexArray");
                                        glDrawElements(GL_TRIANGLES, it.GetIndicesSize(), GL_UNSIGNED_INT, 0);
                                        CHECK_GL_ERROR("glDrawElements");
                                    }
                                }
                            }
                        }
                    }
                }

                
    //     if(item && cache4.count(item->Get())){
            //         auto& sub = cache3.emplace_back();
            //         item->Get(&sub);
            //         const Render::ModelRenderItem* ptr = sub.Get();
            //         cache4[ptr] = *ptr;
            //         std::vector<IBPR*> keys;
            //         keys.reserve(ptr->materialList.size());
            //         for(auto material : ptr->materialList){
            //             IBPR* bprFeature = material->TryGetFeature<IBPR>();
            //             keys.push_back(bprFeature);
            //             if(bprFeature && !cache.count(bprFeature)){
            //                 cache[bprFeature] = {};
            //             }                    
            //         }
            //         for(auto& mesh : ptr->model->GetMeshes()){
            //             if(mesh.GetMaterialIndex() >= ptr->materialList.size()){
            //                 LOG_ERROR("RenderPass:ScenePass->AddItem", "Mesh material index out of range");
            //             }
            //             auto material = keys[mesh.GetMaterialIndex()];
            //             if(material){ // 实际上可以分配到默认(显式错误)的材质
            //                 cache[material].push_back(MeshSimpleItem{&mesh,ptr->modelMatrix,ptr->com});
            //                 cache2[&mesh] = {material,cache[material].size() - 1};
            //             }
            //         }
            //     }
                
                
                return 0;
            }

        private:
            // void AddItem(ModifierHandle<ModelRenderItem>* item){
            //     if(item && cache4.count(item->Get())){
            //         auto& sub = cache3.emplace_back();
            //         item->Get(&sub);
            //         const Render::ModelRenderItem* ptr = sub.Get();
            //         cache4[ptr] = *ptr;
            //         std::vector<IBPR*> keys;
            //         keys.reserve(ptr->materialList.size());
            //         for(auto material : ptr->materialList){
            //             IBPR* bprFeature = material->TryGetFeature<IBPR>();
            //             keys.push_back(bprFeature);
            //             if(bprFeature && !cache.count(bprFeature)){
            //                 cache[bprFeature] = {};
            //             }                    
            //         }
            //         for(auto& mesh : ptr->model->GetMeshes()){
            //             if(mesh.GetMaterialIndex() >= ptr->materialList.size()){
            //                 LOG_ERROR("RenderPass:ScenePass->AddItem", "Mesh material index out of range");
            //             }
            //             auto material = keys[mesh.GetMaterialIndex()];
            //             if(material){ // 实际上可以分配到默认(显式错误)的材质
            //                 cache[material].push_back(MeshSimpleItem{&mesh,ptr->modelMatrix,ptr->com});
            //                 cache2[&mesh] = {material,cache[material].size() - 1};
            //             }
            //         }
            //     }
            // }
            // void Remove(ModifierHandle<ModelRenderItem>* item){
            //     if(cache)
            // }
    };
}