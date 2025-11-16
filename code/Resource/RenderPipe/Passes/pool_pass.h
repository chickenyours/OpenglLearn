#pragma once

#include "code/Resource/RenderPipe/pass.h"
#include "code/Resource/RenderPipe/Passes/camera_pass.h"
#include "code/Resource/RenderPipe/RenderItems/model_render_item.h"


namespace Render{
    class PoolPass : public Pass{
        private:
            GLuint reflectionTexture = 0;
            GLuint refractionTexture = 0;
            GLuint frontTexture = 0; 
        public:
            ECS::Component::Camera* mainCam = nullptr;
            ECS::Component::Transform pooltrans;
            CameraPass camPass;

            

            virtual bool Init(const PassContex& cfg) override{
                glGenTextures(1,&frontTexture);
                glBindTexture(GL_TEXTURE_2D,frontTexture);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, cfg.outputResolution.x, cfg.outputResolution.y, 0, GL_RGBA, GL_FLOAT, nullptr);

                if(cfg.camera){
                    mainCam = cfg.camera;
                    camPass.Init(cfg);
                    return true;
                }
                return false;
            }
            virtual void SetConfig(const PassContex& cfg) override{
                if(cfg.camera){
                    mainCam = cfg.camera;
                    camPass.SetConfig(cfg);
                }
            }
            virtual void Update() override {

            }
            virtual void ClearCache() override {

            }

          
        private:
            Resource::ResourceHandle<Resource::ShaderProgram> poolShader;
    };
}