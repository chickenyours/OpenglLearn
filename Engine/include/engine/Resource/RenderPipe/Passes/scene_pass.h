#pragma once

#include <vector>

#include "engine/ToolAndAlgorithm/Opengl/api.h"
#include "engine/Resource/RenderPipe/pass.h"
#include "engine/Resource/RenderPipe/RenderItems/effect_render_item.h"
#include "engine/Resource/RenderPipe/RenderItems/model_render_item.h"
#include "engine/Resource/Material/Interfaces/BPR.h"
#include "engine/ToolAndAlgorithm/Opengl/debug.h"



namespace Render{

    struct Vertexs : public ILoadable{
       
            GLuint vao = 0;
            GLuint vbo = 0;

            virtual void Release(){
                glDeleteVertexArrays(1,&vao);
                glDeleteBuffers(1, &vbo);
            }
    };

    class ScenePass : public Pass{
        public:
            virtual bool Init(const PassContex& cfg) override {

                // 检测代码
                if(cfg.outputResolution.x > 3000 || cfg.outputResolution.y > 3000 || cfg.outputResolution.x <= 15 || cfg.outputResolution.y <= 15){
                    LOG_ERROR("RenderPass:ScenePass->Init", "Invalid output resolution: x=" + std::to_string(cfg.outputResolution.x) + ", y=" + std::to_string(cfg.outputResolution.y));
                    return false;
                }

                colorBufferResolution = cfg.outputResolution;

                glGenFramebuffers(1,&FBO_);
                CHECK_GL_ERROR("glGenFramebuffers");
                glGenTextures(1,&colorBuffer_);
                CHECK_GL_ERROR("glGenTextures");
                glGenRenderbuffers(1, &depthBuffer_);
                CHECK_GL_ERROR("glGenRenderbuffers");

                glBindTexture(GL_TEXTURE_2D, colorBuffer_);
                CHECK_GL_ERROR("glBindTexture");

                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, cfg.outputResolution.x, cfg.outputResolution.y, 0, GL_RGBA, GL_FLOAT, nullptr);
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

                glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, cfg.outputResolution.x, cfg.outputResolution.y);
                CHECK_GL_ERROR("glRenderbufferStorage");

                glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depthBuffer_);
                CHECK_GL_ERROR("glFramebufferRenderbuffer");
                

                GLint colorAttachmentType;
                glGetFramebufferAttachmentParameteriv(
                    GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &colorAttachmentType
                );
                if (colorAttachmentType == GL_NONE) {
                    LOG_ERROR("RenderPass:ScenePass->Init", "Color attachment 0 is missing!");
                }
                glGetFramebufferAttachmentParameteriv(
                    GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &colorAttachmentType
                );
                if (colorAttachmentType == GL_NONE) {
                    LOG_ERROR("RenderPass:ScenePass->Init", "Depth attachment is missing!");
                }

                if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                    LOG_ERROR("RenderPass:ScenePass->Init", "Framebuffer not complete!");
                }
                
                glBindFramebuffer(GL_FRAMEBUFFER, 0);

                // UBO

                glGenBuffers(1, &UBOComponent_);
                if(CHECK_GL_ERROR("glGenBuffers")) return false;
                glBindBuffer(GL_UNIFORM_BUFFER, UBOComponent_);
                if(CHECK_GL_ERROR("glBindBuffer")) return false;
                glBufferData(GL_UNIFORM_BUFFER, sizeof(StaticModelComponentDataUBOLayout), nullptr, GL_DYNAMIC_DRAW);
                if(CHECK_GL_ERROR("glBufferData")) return false;
                glBindBufferBase(GL_UNIFORM_BUFFER, UBO_STATIC_MODEL_COMPONENT_DATA, UBOComponent_);
                if(CHECK_GL_ERROR("glBindBufferBase")) return false;
                glBindBuffer(GL_UNIFORM_BUFFER, 0);


                if(isSkyBoxNeeded){
                    // 获取天空盒数据
                    skyBoxVAOHandle = ECS::Core::ResourceModule::ResourceManager::GetInctance().Get<Vertexs>
                        (FromGenerator<Vertexs>(
                            "ScenePassVAO", [&](Log::StackLogErrorHandle errHandle)->std::unique_ptr<Vertexs> {

                                float skyboxVertices[] = {
                                    // positions
                                    -1.0f,  1.0f, -1.0f,
                                    -1.0f, -1.0f, -1.0f,
                                    1.0f, -1.0f, -1.0f,
                                    1.0f, -1.0f, -1.0f,
                                    1.0f,  1.0f, -1.0f,
                                    -1.0f,  1.0f, -1.0f,
                
                                    -1.0f, -1.0f,  1.0f,
                                    -1.0f, -1.0f, -1.0f,
                                    -1.0f,  1.0f, -1.0f,
                                    -1.0f,  1.0f, -1.0f,
                                    -1.0f,  1.0f,  1.0f,
                                    -1.0f, -1.0f,  1.0f,
                
                                    1.0f, -1.0f, -1.0f,
                                    1.0f, -1.0f,  1.0f,
                                    1.0f,  1.0f,  1.0f,
                                    1.0f,  1.0f,  1.0f,
                                    1.0f,  1.0f, -1.0f,
                                    1.0f, -1.0f, -1.0f,
                
                                    -1.0f, -1.0f,  1.0f,
                                    -1.0f,  1.0f,  1.0f,
                                    1.0f,  1.0f,  1.0f,
                                    1.0f,  1.0f,  1.0f,
                                    1.0f, -1.0f,  1.0f,
                                    -1.0f, -1.0f,  1.0f,
                
                                    -1.0f,  1.0f, -1.0f,
                                    1.0f,  1.0f, -1.0f,
                                    1.0f,  1.0f,  1.0f,
                                    1.0f,  1.0f,  1.0f,
                                    -1.0f,  1.0f,  1.0f,
                                    -1.0f,  1.0f, -1.0f,
                
                                    -1.0f, -1.0f, -1.0f,
                                    -1.0f, -1.0f,  1.0f,
                                    1.0f, -1.0f, -1.0f,
                                    1.0f, -1.0f, -1.0f,
                                    -1.0f, -1.0f,  1.0f,
                                    1.0f, -1.0f,  1.0f
                                };
                                GLuint VAO, VBO;
                                glGenVertexArrays(1, &VAO);
                                glGenBuffers(1, &VBO);

                                glBindVertexArray(VAO);
                                glBindBuffer(GL_ARRAY_BUFFER, VBO);
                
                                glBufferData(GL_ARRAY_BUFFER, sizeof(skyboxVertices), skyboxVertices, GL_STATIC_DRAW);
                
                                GLsizei stride = 3 * sizeof(float);
                
                                // aPos
                                glEnableVertexAttribArray(0);
                                glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);

                                glBindVertexArray(0);
                                glBindBuffer(GL_ARRAY_BUFFER, 0);

                                std::unique_ptr<Vertexs> vaoOut = std::make_unique<Vertexs>();
                                vaoOut->vao = VAO;
                                vaoOut->vbo = VBO;

                       
                                
                                return vaoOut;
                            },
                            [](const std::string& key, Vertexs* pair){
                                glDeleteVertexArrays(1,&pair->vao);
                                glDeleteBuffers(1,&pair->vbo);
                            },
                            nullptr
                        ));

                    if(!skyBoxVAOHandle){
                        LOG_ERROR("RenderPass:ScenePass->Init", "Framebuffer not complete!");
                        return false;
                    }

                    skyBoxVAO = skyBoxVAOHandle->vao;
                    

                    skyBoxShader = std::move(
                        ECS::Core::ResourceModule::ResourceManager::GetInctance().Get<Resource::ShaderProgramFactory>(
                            FromConfig<Resource::ShaderProgramFactory>("./RenderShader/sky_box.json"))->GetShaderProgramInstance()
                    );

                    if(!skyBoxShader){
                        LOG_ERROR("StaicMesh::Init", "Failed to load Shader!");
                        return false;
                    }

                    cube = std::move(
                        ECS::Core::ResourceModule::ResourceManager::GetInctance().Get<TextureCube>(
                            FromConfig<TextureCube>("./images/skybox/sky_box.json"))
                    );

                    if(!cube){
                        LOG_ERROR("StaicMesh::Init", "Failed to load cube!");
                        return false;
                    }

                    isSkyBoxInit = true;

                }

                

                return true;
            }

            virtual void SetConfig(const PassContex& cfg) override {
                if(cfg.outputResolution == colorBufferResolution) return;
                if (cfg.outputResolution.x > 3000 || cfg.outputResolution.y > 3000 || cfg.outputResolution.x <= 15 || cfg.outputResolution.y <= 15) {
                    LOG_ERROR("RenderPass:ScenePass->SetConfig", "Invalid output resolution: x=" + std::to_string(cfg.outputResolution.x) + ", y=" + std::to_string(cfg.outputResolution.y));
                    return;
                }
                colorBufferResolution = cfg.outputResolution;
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
            }

            std::vector<BaseParticleDrawItem> m_partical_items;
            void AddParticalItem(const BaseParticleDrawItem* proxy){
                m_partical_items.push_back(*proxy);
            }

            bool AddItem(const ModelRenderItem& item){
                std::vector<IBPR*> keys;
                keys.reserve(item.materialList.size());
                for(auto material : item.materialList){
                    IBPR* bprFeature = material->TryGetFeature<IBPR>();
                    keys.push_back(bprFeature);
                    if(bprFeature && !cache.count(bprFeature)){
                        // cache.emplace(bprFeature, std::vector<std::pair<Mesh*, glm::mat4*>>{});
                        cache[bprFeature] = {};
                    }                    
                }

                for(auto& mesh : item.model->GetMeshes()){
                    if(mesh.GetMaterialIndex() >= item.materialList.size()){
                        LOG_ERROR("RenderPass:ScenePass->AddItem", "Mesh material index out of range");
                        // return false;
                    }
                    auto material = keys[mesh.GetMaterialIndex()];
                    if(material){
                        cache[material].push_back(Item{&mesh,item.modelMatrix,item.com});
                    }
                }
                return true;
            }

            virtual void ClearCache() override{
                cache.clear();
            }
            

            GLuint inline GetOutputColorBuffer(){return colorBuffer_;}

            virtual void Update() override {
                
                glBindFramebuffer(GL_FRAMEBUFFER, FBO_);
                CHECK_GL_ERROR("glBindFramebuffer");
                glEnable(GL_DEPTH_TEST);
                CHECK_GL_ERROR("glEnable(GL_DEPTH_TEST)");
                glClearColor(0.2f, 0.2f, 0.6f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT| GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
                glViewport(0,0,colorBufferResolution.x,colorBufferResolution.y);

                if(isSkyBoxNeeded && isSkyBoxInit){
                    // 天空盒渲染
                    glDisable(GL_DEPTH_TEST);
                    glUseProgram(skyBoxShader->GetID());
                    glBindVertexArray(skyBoxVAO);
                    glActiveTexture(GL_TEXTURE7);
                    glBindTexture(GL_TEXTURE_CUBE_MAP,cube->GetID());
                    glDrawArrays(GL_TRIANGLES,0,36);
                }

                glEnable(GL_DEPTH_TEST);
                glDepthFunc(GL_LESS);
                CHECK_GL_ERROR("glDepthFunc(GL_LESS)");
                for(auto& [key,v] : cache){
                    glActiveTexture(GL_TEXTURE0);
                    CHECK_GL_ERROR("glActiveTexture(GL_TEXTURE0)");
                    glBindTexture(GL_TEXTURE_2D, key->albedoMap_->GetID());
                    CHECK_GL_ERROR("glBindTexture(albedoMap)");
                    glActiveTexture(GL_TEXTURE1);
                    CHECK_GL_ERROR("glActiveTexture(GL_TEXTURE1)");
                    glBindTexture(GL_TEXTURE_2D, key->normalMap_->GetID());
                    CHECK_GL_ERROR("glBindTexture(normalMap)");
                    if(key->state_.useMetallicMap){
                        glActiveTexture(GL_TEXTURE2);
                        CHECK_GL_ERROR("glActiveTexture(GL_TEXTURE2)");
                        glBindTexture(GL_TEXTURE_2D, key->metallicMap_->GetID());
                        CHECK_GL_ERROR("glBindTexture(metallicMap)");
                    }
                    if(key->state_.useRoughnessMap){
                        glActiveTexture(GL_TEXTURE3);
                        CHECK_GL_ERROR("glActiveTexture(GL_TEXTURE3)");
                        glBindTexture(GL_TEXTURE_2D, key->roughnessMap_->GetID());
                        CHECK_GL_ERROR("glBindTexture(roughnessMap)");
                    }
                    if(key->state_.useAoMap){
                        glActiveTexture(GL_TEXTURE4);
                        CHECK_GL_ERROR("glActiveTexture(GL_TEXTURE4)");
                        glBindTexture(GL_TEXTURE_2D, key->aoMap_->GetID());
                        CHECK_GL_ERROR("glBindTexture(aoMap)");
                    }
                    key->UpLoadProperty();
                    glUseProgram(key->mainShader_->GetID());
                    // std::cout << key->mainShader_->GetID() << std::endl;
                    CHECK_GL_ERROR("glUseProgram");
                    for(auto& item : v){
                        glBindBuffer(GL_UNIFORM_BUFFER,UBOComponent_);
                        glBindBufferBase(GL_UNIFORM_BUFFER, UBO_STATIC_MODEL_COMPONENT_DATA, UBOComponent_);
                        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(StaticModelComponentDataUBOLayout), &item.com->uboData);
                        CHECK_GL_ERROR("glBufferSubData");
                        ShaderUmatf4(key->mainShader_->GetID(), "model", *item.modelMatrix);
                        glBindVertexArray(item.mesh->GetVAO());
                        CHECK_GL_ERROR("glBindVertexArray");
                        glDrawElements(GL_TRIANGLES, item.mesh->GetIndicesSize(), GL_UNSIGNED_INT, 0);
                        CHECK_GL_ERROR("glDrawElements");
                    }

                    // 粒子渲染
                    for(auto& item : m_partical_items){
                        auto material = item.material->TryGetFeature<IBaseParticle>();
                        if(material){
                            auto shaderProgram = material->mainShader.get();
                            if(shaderProgram){
                                glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, item.particleBuffer->GetID());
                                CHECK_GL_ERROR("glDrawElements");
                                glUseProgram(shaderProgram->GetID());
                                CHECK_GL_ERROR("glDrawElements");
                                glDrawArrays(GL_POINTS, 0, item.particleBuffer->GetNumElements());
                                CHECK_GL_ERROR("glDrawElements");
                            }
                            else{
                                LOG_ERROR("ScenePass::Update","item shaderProgram is nullptr");
                            }
                        }
                    }
                    m_partical_items.clear();

                }


            }

            ~ScenePass(){
                if (FBO_ != 0) {
                    glDeleteFramebuffers(1, &FBO_);
                }
                if (colorBuffer_ != 0) {
                    glDeleteTextures(1, &colorBuffer_);
                }
                if (depthBuffer_ != 0) {
                    glDeleteRenderbuffers(1, &depthBuffer_);
                }
            }
        private:
            // std::vector<std::pair<Mesh*,IBPR*>> cache;

            struct Item{
                const Resource::Mesh* mesh;
                glm::mat4* modelMatrix;
                ECS::Component::MeshRenderer* com;
            };
            
            std::unordered_map<IBPR*,std::vector<Item>> cache;
        public:
            glm::ivec2 colorBufferResolution;
            GLuint FBO_ = 0;
            GLuint colorBuffer_ = 0;
            GLuint depthBuffer_ = 0;
            GLuint UBOComponent_ = 0;

        // 天空盒
        private:
            
            ResourceHandle<TextureCube> cube;
            ResourceHandle<Vertexs> skyBoxVAOHandle;
            GLuint skyBoxVAO = 0;
            ResourceHandle<Resource::ShaderProgram> skyBoxShader;
            bool isSkyBoxInit = false;
        public:
            bool isSkyBoxNeeded = true;
    };
}