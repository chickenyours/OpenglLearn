#pragma once

#include <unordered_set>
#include <unordered_map>
#include "code/Resource/RenderPipe/renderPipe.h"

#include "code/Resource/RenderPipe/RenderItems/model_render_item.h"

#include "code/Environment/environment.h"
// #include <cstdlib>
#include "code/Resource/RenderPipe/Passes/camera_pass.h"
#include "code/Resource/RenderPipe/Passes/scene_pass.h"
#include "code/Resource/RenderPipe/Passes/screen_pass.h"
#include "code/Resource/RenderPipe/Passes/environment_pass.h"
#include "code/Resource/RenderPipe/Passes/pool_pass.h"

namespace Render{

        inline void ComputeReflectCameraView(ECS::Component::Camera& reflectCamera, const ECS::Component::Camera& mainCamera,
        const glm::vec3& p0, const glm::vec3& planeNormal){
            glm::vec3 n = glm::normalize(planeNormal);

            auto reflectPoint = [&](const glm::vec3& P){
                float d = glm::dot(P - p0, n);
                return P - 2.0f * d * n;
            };

            auto reflectDir = [&](const glm::vec3& D){
                return glm::normalize(D - 2.0f * glm::dot(D, n) * n);
            };

            // position
            reflectCamera.camPos   = reflectPoint(mainCamera.camPos);
            // directions
            reflectCamera.camFront = reflectDir(mainCamera.camFront);
            reflectCamera.camUp    = reflectDir(mainCamera.camUp);

            // view
            reflectCamera.view = glm::lookAt(
                reflectCamera.camPos,
                reflectCamera.camPos + reflectCamera.camFront,
                reflectCamera.camUp
            );

            

    }


    class StaicMesh: public RenderPipe {
        private: // 镜面参数
            ECS::Component::Transform poolTrans;
            ECS::Component::Camera reflectCamera;
            CameraPass reflectCameraPass; // 反转相机
            ScenePass reflectScenePass;
            ECS::Component::Camera* mainCamera = nullptr;
            GLuint meshVAO = 0; 
            ResourceHandle<ShaderProgram> shader;
        public:
            StaicMesh(){
                poolTrans.position = glm::vec3(0.0);
                poolTrans.rotation = glm::vec3(0.0,1.0,0.0);
                poolTrans.scale = glm::vec3(1.0);
            }
            virtual int Init(const RenderPipeContex& cfg) override {
                PassContex ctx{
                    cfg.outputResolution,
                    cfg.camera
                };

                PassContex reflectCtx{
                    cfg.outputResolution,
                    &reflectCamera
                };

                // if(cfg.camera){
                //     mainCamera->projection = cfg.camera->projection;
                // }
                
                if(!environmentPass.Init(ctx)){
                    return 4;
                }
                
                if(!cameraPass.Init(ctx)){
                    return 1;
                }

                if(!scenePass.Init(ctx)){
                    return 2;
                }

                if(!reflectCameraPass.Init(reflectCtx)){
                    return 5;
                }
                
                if(!reflectScenePass.Init(reflectCtx)){
                    return 6;
                }

                if(!screenPass.Init(ctx)){
                    return 3;
                }

                screenPass.AddPassRenderItem(
                    {
                        GL_TEXTURE_2D,
                        this->reflectScenePass.GetOutputColorBuffer(),
                        true,
                        0.0,
                        0.0,
                        0.5,
                        0.5
                    }
                );

                screenPass.AddPassRenderItem(
                    {
                        GL_TEXTURE_2D,
                        this->scenePass.GetOutputColorBuffer(),
                        true,
                        0.5,
                        0.5,
                        0.5,
                        0.5
                    }
                );

                // mirror mesh
                float quadVertices[] = {
                    // position           // uv     // normal         // tangent
                    -1.0f,  0.0f, 1.0f,   0.0f,1.0f,  0.0f,0.0f,1.0f,  1.0f,0.0f,0.0f,
                    -1.0f, 0.0f, -1.0f,   0.0f,0.0f,  0.0f,0.0f,1.0f,  1.0f,0.0f,0.0f,
                    1.0f, 0.0f, -1.0f,   1.0f,0.0f,  0.0f,0.0f,1.0f,  1.0f,0.0f,0.0f,

                    -1.0f,  0.0f, 1.0f,   0.0f,1.0f,  0.0f,0.0f,1.0f,  1.0f,0.0f,0.0f,
                    1.0f, 0.0f, -1.0f,   1.0f,0.0f,  0.0f,0.0f,1.0f,  1.0f,0.0f,0.0f,
                    1.0f,  0.0f, 1.0f,   1.0f,1.0f,  0.0f,0.0f,1.0f,  1.0f,0.0f,0.0f,
                };
                GLuint VAO, VBO;
                glGenVertexArrays(1, &VAO);
                glGenBuffers(1, &VBO);

                glBindVertexArray(VAO);
                glBindBuffer(GL_ARRAY_BUFFER, VBO);

                glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);

                GLsizei stride = 11 * sizeof(float);

                // aPos
                glEnableVertexAttribArray(0);
                glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0);

                // aTex
                glEnableVertexAttribArray(1);
                glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, (void*)(3 * sizeof(float)));

                // aNormal
                glEnableVertexAttribArray(2);
                glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, stride, (void*)(5 * sizeof(float)));

                // aTangent
                glEnableVertexAttribArray(3);
                glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, stride, (void*)(8 * sizeof(float)));

                glBindVertexArray(0);

                meshVAO = VAO;

                shader = std::move(
                    ECS::Core::ResourceModule::ResourceManager::GetInctance().Get<Resource::ShaderProgramFactory>(
                        FromConfig<Resource::ShaderProgramFactory>("./RenderShader/mirror.json"))->GetShaderProgramInstance()
                );
                if(!shader){
                    LOG_ERROR("StaicMesh::Init", "Failed to load Shader!");
                    return 7;
                }
               
                return 0;
            }
            virtual void SetConfig(const RenderPipeContex& cfg) override {
                PassContex ctx{
                    cfg.outputResolution,
                    cfg.camera
                };

                PassContex reflectCtx{
                    cfg.outputResolution,
                    &reflectCamera
                };

                if(cfg.camera){
                    mainCamera= cfg.camera;
                    reflectCamera.projection = cfg.camera->projection;
                }

                environmentPass.SetConfig(ctx);
                cameraPass.SetConfig(ctx);
                scenePass.SetConfig(ctx);
                reflectCameraPass.SetConfig(reflectCtx);
                reflectScenePass.SetConfig(reflectCtx);
                screenPass.SetConfig(ctx);
            }
            virtual void RenderCall() override {
                environmentPass.UBOdata_.iTime = Environment::Environment::Instance().GetTime();
                environmentPass.Update();
                cameraPass.Update();
                scenePass.Update();

                // 反射相机
                ComputeReflectCameraView(reflectCamera,*mainCamera,poolTrans.position,poolTrans.rotation);
                reflectCamera.projection = mainCamera->projection;
                // 获取反射贴图
                reflectCameraPass.Update();
                reflectScenePass.Update(); 
                poolTrans.CalMatrix();
                

                // 绘制
                cameraPass.Update();
                glBindFramebuffer(GL_FRAMEBUFFER, scenePass.FBO_);
                glEnable(GL_DEPTH_TEST);
                glViewport(0,0,scenePass.colorBufferResolution.x,scenePass.colorBufferResolution.y);
                glDepthFunc(GL_LESS);
                glBindVertexArray(meshVAO);
                glUseProgram(shader->GetID());
                ShaderUmatf4(shader->GetID(),"model",poolTrans.localMatrix);
                ShaderUmatf4(shader->GetID(),"uReflectionVP",reflectCamera.projection * reflectCamera.view);
                ShaderUvec3(shader->GetID(),"uCameraPos",mainCamera->camPos);
                glActiveTexture(GL_TEXTURE10);
                glBindTexture(GL_TEXTURE_2D,reflectScenePass.GetOutputColorBuffer());
                glDrawArrays(GL_TRIANGLES, 0, 6);

                screenPass.Update();
            }
            bool RegisterItem(const ModelRenderItem& item) {
                bool a = scenePass.AddItem(item);
                bool b = reflectScenePass.AddItem(item);
                return a && b;
            }
            
        private:
            EnvironmentPass environmentPass;
            CameraPass cameraPass;
            ScenePass scenePass;
            ImageToBufferPass screenPass;
            

            std::unordered_set<Resource::Material*> registeredMaterial_;
            std::unordered_map<Resource::Material*,std::vector<Resource::Mesh*>> cache;
            std::unordered_map<Resource::Model*, std::vector<Resource::Material*>> hh;

            
    };
}