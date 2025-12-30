#pragma once

#include <unordered_set>
#include <unordered_map>
#include "engine/Resource/RenderPipe/renderPipe.h"

#include "engine/Resource/RenderPipe/RenderItems/model_render_item.h"
#include "engine/Resource/RenderPipe/RenderItems/effect_render_item.h"

#include "engine/Environment/environment.h"
// #include <cstdlib>
#include "engine/Resource/RenderPipe/Passes/camera_pass.h"
#include "engine/Resource/RenderPipe/Passes/scene_pass.h"
#include "engine/Resource/RenderPipe/Passes/screen_pass.h"
#include "engine/Resource/RenderPipe/Passes/environment_pass.h"
#include "engine/Resource/RenderPipe/Passes/pool_pass.h"
#include "engine/Resource/RenderPipe/Passes/marker_pass.h"
#include "engine/Resource/Shader/shader_program.h"
// #include "engine/Resource/RenderPipe/RenderItems/parttical_render_item.h"

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

    inline void ComputeObliqueMatrix(
        const glm::vec3& planePos,
        const glm::vec3& planeNormal,
        const glm::mat4& originCameraProjection,
        const glm::mat4& reflectCameraView,   // world -> camera
        glm::mat4& outProj)
    {
        // -------------------------------------------------------------
        // 1. 世界空间平面 (n, d)，平面方程：n·x + d = 0
        // -------------------------------------------------------------
        glm::vec3 n = glm::normalize(planeNormal);
        float d = -glm::dot(n, planePos);
        glm::vec4 planeWorld(n, d);

        // -------------------------------------------------------------
        // 2. 转换到 camera(eye) 空间（OpenGL：plane_cam = view^-1^T * plane_world）
        // -------------------------------------------------------------
        glm::vec4 planeCam = glm::transpose(glm::inverse(reflectCameraView)) * planeWorld;

        // -------------------------------------------------------------
        // 3. 让平面朝向相机（OpenGL 相机看向 -Z，plane.z 应该为负）
        // -------------------------------------------------------------
        if (planeCam.z > 0.0f)
            planeCam = -planeCam;

        // -------------------------------------------------------------
        // 4. 归一化平面
        // -------------------------------------------------------------
        float len = glm::length(glm::vec3(planeCam));
        if (len < 1e-6f) return;
        planeCam /= len;

        // -------------------------------------------------------------
        // 5. 计算 Q 点：corner = (sign(nx), sign(ny), 1, -1) —— OpenGL NDC！
        // -------------------------------------------------------------
        glm::vec4 corner(
            glm::sign(planeCam.x),
            glm::sign(planeCam.y),
            1.0f,        // 正远裁剪面
            -1.0f        // OpenGL：w = -1
        );

        glm::mat4 invProj = glm::inverse(originCameraProjection);
        glm::vec4 q = invProj * corner;

        float denom = glm::dot(planeCam, q);
        if (glm::abs(denom) < 1e-6f) return;

        // -------------------------------------------------------------
        // 6. 计算 C 向量
        // -------------------------------------------------------------
        glm::vec4 c = planeCam * (2.0f / denom);

        // -------------------------------------------------------------
        // 7. 按照 Gribb–Hartmann 修改投影矩阵：
        //    修改的是「第三行」，而不是第三列
        //
        //    行主序公式：
        //        proj[2][0..3] = C - proj[3][0..3]
        // 
        //    GLM 列主序：
        //        proj[col][2] = C[col] - proj[col][3]
        // -------------------------------------------------------------
        outProj = originCameraProjection;
        for (int col = 0; col < 4; ++col)
        {
            outProj[col][2] = c[col] - originCameraProjection[col][3];
        }
    }



    class StaicMesh: public RenderPipe {
        private: // 参数
            glm::ivec2 currentResolution;
        private: // 镜面参数
            ECS::Component::Transform poolTrans;
            ECS::Component::Camera reflectCamera;
            CameraPass reflectCameraPass; // 反转相机
            ScenePass reflectScenePass;
            ECS::Component::Camera* mainCamera = nullptr;
            GLuint meshVAO = 0; 
            ResourceHandle<Resource::ShaderProgram> shader;
            GLuint Refraction = 0;
            
        public:
            StaicMesh(){
                poolTrans.position = glm::vec3(0.0);
                poolTrans.rotation = glm::vec3(0.0,0.0,0.0);
                poolTrans.scale = glm::vec3(30.0);
            }
            virtual int Init(const RenderPipeContex& cfg) override {

                currentResolution = cfg.outputResolution;

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


                {
                    // mirror mesh
                    float quadVertices[] = {
                        // position           // uv     // normal         // tangent
                        -1.0f,  0.0f, 1.0f,   0.0f,1.0f,  0.0f,1.0f,0.0f,  1.0f,0.0f,0.0f,
                        -1.0f, 0.0f, -1.0f,   0.0f,0.0f,  0.0f,1.0f,0.0f,  1.0f,0.0f,0.0f,
                        1.0f, 0.0f, -1.0f,   1.0f,0.0f,  0.0f,1.0f,0.0f,  1.0f,0.0f,0.0f,
    
                        -1.0f,  0.0f, 1.0f,   0.0f,1.0f,  0.0f,1.0f,0.0f,  1.0f,0.0f,0.0f,
                        1.0f, 0.0f, -1.0f,   1.0f,0.0f,  0.0f,1.0f,0.0f,  1.0f,0.0f,0.0f,
                        1.0f,  0.0f, 1.0f,   1.0f,1.0f,  0.0f,1.0f,0.0f,  1.0f,0.0f,0.0f,
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
                }

                shader = std::move(
                    ECS::Core::ResourceModule::ResourceManager::GetInctance().Get<Resource::ShaderProgramFactory>(
                        FromConfig<Resource::ShaderProgramFactory>("./RenderShader/pool.json"))->GetShaderProgramInstance()
                );
      

                if(!shader){
                    LOG_ERROR("StaicMesh::Init", "Failed to load Shader!");
                    return 7;
                }

               

                if(!marker.Init(ctx)){
                    LOG_ERROR("StaicMesh::Init", "Failed to load Shader!");
                    return 8;
                }

                glGenTextures(1,&Refraction);
                CHECK_GL_ERROR("glGenTextures");
                glBindTexture(GL_TEXTURE_2D, Refraction);
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

                screenPass.AddPassRenderItem(
                    {
                        GL_TEXTURE_2D,
                        Refraction,
                        true,
                        0.0,
                        0.5,
                        0.5,
                        0.5
                    }
                );
               
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

                
                // 粒子计算
                for(auto& item : m_proxy_items){
                    glUseProgram(item.computeShaderProgram->GetID());
                    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, item.particleBuffer->GetID());
                    GLuint numParticles = item.particleBuffer->GetNumElements();
                    GLuint workGroupSize = 256;
                    GLuint numGroups = (numParticles + workGroupSize - 1) / workGroupSize;
                    ShaderU1f(item.computeShaderProgram->GetID(),"deltaTime",item.params.deltaTime);
                    ShaderU1f(item.computeShaderProgram->GetID(),"iTime",item.params.simulationTime);
                    glDispatchCompute(numGroups, 1, 1);
                }
                m_proxy_items.clear();
                // 等待 GPU 写完
                glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

                
                cameraPass.Update();
                // 前向渲染
                scenePass.Update();

                // 复制到折射贴图
                glCopyImageSubData(
                    scenePass.colorBuffer_, GL_TEXTURE_2D, 0, 0, 0, 0,
                    Refraction, GL_TEXTURE_2D, 0, 0, 0, 0,
                    currentResolution.x, currentResolution.y, 1
                );
                CHECK_GL_ERROR("glCopyImageSubData");

                // 反射相机
                glm::vec3 poolforward;
                float yawdeg;
                poolTrans.CalForwardAndYaw(poolforward,yawdeg);
                ComputeReflectCameraView(reflectCamera,*mainCamera,poolTrans.position,poolforward);
                // reflectCamera.projection = HardcodedObliquePerspective();
                ComputeObliqueMatrix(poolTrans.position,poolforward,mainCamera->projection,reflectCamera.view,reflectCamera.projection);
                reflectCameraPass.Update();
                reflectScenePass.Update(); 


                // 绘制
                poolTrans.CalMatrix();
                cameraPass.Update();
                glBindFramebuffer(GL_FRAMEBUFFER, scenePass.FBO_);
                glEnable(GL_DEPTH_TEST);
                glViewport(0,0,scenePass.colorBufferResolution.x,scenePass.colorBufferResolution.y);
                glDepthFunc(GL_LESS);
                glBindVertexArray(meshVAO);
                glUseProgram(shader->GetID());
                ShaderUmatf4(shader->GetID(),"model",poolTrans.localMatrix);
                ShaderUmatf4(shader->GetID(),"uReflectionVP",reflectCamera.projection * reflectCamera.view);
                ShaderUmatf4(shader->GetID(),"uMainCameraVP",mainCamera->projection * mainCamera->view);
                ShaderUvec3(shader->GetID(),"uCameraPos",mainCamera->camPos);
                glActiveTexture(GL_TEXTURE10);
                glBindTexture(GL_TEXTURE_2D,reflectScenePass.GetOutputColorBuffer());
                glActiveTexture(GL_TEXTURE11);
                glBindTexture(GL_TEXTURE_2D,Refraction);
                glDrawArrays(GL_TRIANGLES, 0, 6);
                    


                // 涂鸦标记
                marker.Update();

                screenPass.Update();
            }
            bool RegisterItem(const ModelRenderItem& item) {
                bool a = scenePass.AddItem(item);
                bool b = reflectScenePass.AddItem(item);
                return a && b;
            }
        // 标记组件
        private:
            MarkerPass marker;

        private:
            EnvironmentPass environmentPass;
            CameraPass cameraPass;
            ScenePass scenePass;
            ImageToBufferPass screenPass;
            

            std::unordered_set<Resource::Material*> registeredMaterial_;
            std::unordered_map<Resource::Material*,std::vector<Resource::Mesh*>> cache;
            std::unordered_map<Resource::Model*, std::vector<Resource::Material*>> hh;
        public: // 粒子效果
           

            std::vector<ParticleRenderProxy> m_proxy_items;
            virtual void AddParticalProcessProxyItem(const ParticleRenderProxy* proxy) override {
                m_proxy_items.emplace_back(*proxy);
            }
            
            virtual void AddParticalItem(const BaseParticleDrawItem* drawItem) override {
                scenePass.AddParticalItem(drawItem);
                reflectScenePass.AddParticalItem(drawItem);
            }
            
    };

    
}