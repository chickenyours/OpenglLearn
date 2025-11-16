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
            ECS::Component::Camera* mainCamera;
            GLuint meshVAO = 0; 
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

                if(cfg.camera){
                    mainCamera = cfg.camera;
                    reflectCamera.projection = mainCamera->projection;
                }

                PassContex reflectCtx{
                    cfg.outputResolution,
                    &reflectCamera
                };
                
                if(!environmentPass.Init(ctx)){
                    return 4;
                }
                
                if(!cameraPass.Init(ctx)){
                    return 1;
                }

                if(!scenePass.Init(ctx)){
                    return 2;
                }
                
                if(!reflectScenePass.Init(ctx)){
                    return 2;
                }

                if(!screenPass.Init(ctx)){
                    return 3;
                }

                if(!reflectCameraPass.Init(reflectCtx)){
                    return 5;
                }

                


                screenPass.AddPassRenderItem(
                    {
                        GL_TEXTURE_2D,
                        this->scenePass.GetOutputColorBuffer(),
                        true,
                        0.0,
                        0.0,
                        1.0,
                        1.0
                    }
                );

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
                meshVAO = VAO;
                
                

                return 0;
            }
            virtual void SetConfig(const RenderPipeContex& cfg) override {
                PassContex ctx{
                    cfg.outputResolution,
                    cfg.camera
                };

                if(cfg.camera){
                    mainCamera = cfg.camera;
                    reflectCamera.projection = mainCamera->projection;
                }
                
                environmentPass.SetConfig(ctx);
                cameraPass.SetConfig(ctx);
                scenePass.SetConfig(ctx);
                screenPass.SetConfig(ctx);
            }
            virtual void RenderCall() override {
                environmentPass.UBOdata_.iTime = Environment::Environment::Instance().GetTime();

                environmentPass.Update();
                cameraPass.Update();
                // 反射相机
                ComputeReflectCameraView(reflectCamera,*mainCamera,poolTrans.position,poolTrans.rotation);
                // 获取反射贴图
                reflectCameraPass.Update();
                
                scenePass.Update();
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
            ScenePass reflectScenePass;
            ImageToBufferPass screenPass;

            std::unordered_set<Resource::Material*> registeredMaterial_;
            std::unordered_map<Resource::Material*,std::vector<Resource::Mesh*>> cache;
            std::unordered_map<Resource::Model*, std::vector<Resource::Material*>> hh;

            
    };
}

