#pragma once

#include "code/Resource/RenderPipe/UniformBindings.h"

#include "code/Resource/RenderPipe/pass.h"

#include "code/ECS/Component/Camera/camera.h"

#include "code/ToolAndAlgorithm/Opengl/debug.h"

namespace Render{
    class CameraPass : public Pass{
        public:
            virtual bool Init(const PassContex& cfg) override {
                mainCamera_ = cfg.camera;
                glGenBuffers(1, &UBOCamera_);
                if(CHECK_GL_ERROR("glGenBuffers")) return false;
                glBindBuffer(GL_UNIFORM_BUFFER, UBOCamera_);
                if(CHECK_GL_ERROR("glBindBuffer")) return false;
                glBufferData(GL_UNIFORM_BUFFER, sizeof(CameraDataUBOLayout), nullptr, GL_DYNAMIC_DRAW);
                if(CHECK_GL_ERROR("glBufferData")) return false;
                glBindBufferBase(GL_UNIFORM_BUFFER, UBO_BINDING_CAMERA, UBOCamera_);
                if(CHECK_GL_ERROR("glBindBufferBase")) return false;
                glBindBuffer(GL_UNIFORM_BUFFER, 0);
                // std::cout<<"[Camera Pass] : 创建并初始化"<<std::endl;
                // LOG_INFO("Camera Pass","创建并初始化");
                return true;
            }
            virtual void SetConfig(const PassContex& cfg) override {
                mainCamera_ = cfg.camera;
            }
            virtual void Update() override {
                if(mainCamera_){
                    glBindBufferBase(GL_UNIFORM_BUFFER,UBO_BINDING_CAMERA,UBOCamera_);
                    CHECK_GL_ERROR("glBindBufferBase");
                    UBOdata_.projectionMatrix = mainCamera_->projection;
                    UBOdata_.viewMatrix = mainCamera_->view;
                    UBOdata_.viewPos = mainCamera_->camPos;
                    UBOdata_.viewDereict = mainCamera_->camFront;
                    glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(CameraDataUBOLayout), &UBOdata_);
                    CHECK_GL_ERROR("glBufferSubData");
                }
                else{
                    // std::cerr << "[Camera Pass] : Warning! No camera assigned, skipping update." << std::endl;
                    LOG_ERROR("Camera Pass", "No camera assigned, skipping update.");
                }
            }
            // void AddItem(...) // input
            // output
            virtual void ClearCache() override {
                
            }

            ~CameraPass(){
                if (UBOCamera_ != 0) {
                    glDeleteBuffers(1, &UBOCamera_);
                    UBOCamera_ = 0;
                }
            }
            
        private:
            ECS::Component::Camera* mainCamera_ = nullptr;
            GLuint UBOCamera_ = 0;
            CameraDataUBOLayout UBOdata_;
    

    };
}