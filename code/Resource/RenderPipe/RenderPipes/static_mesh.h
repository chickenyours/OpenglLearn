#pragma once

#include <unordered_set>
#include <unordered_map>
#include "code/Resource/RenderPipe/renderPipe.h"

#include "code/Resource/RenderPipe/RenderItems/model_render_item.h"

#include "code/Environment/environment.h"

#include "code/Resource/RenderPipe/Passes/camera_pass.h"
#include "code/Resource/RenderPipe/Passes/scene_pass.h"
#include "code/Resource/RenderPipe/Passes/screen_pass.h"
#include "code/Resource/RenderPipe/Passes/environment_pass.h"

namespace Render{
    class StaicMesh: public RenderPipe {
        public:
            virtual int Init(const RenderPipeContex& cfg) override {
                PassContex ctx{
                    cfg.outputResolution,
                    cfg.camera
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

                if(!screenPass.Init(ctx)){
                    return 3;
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
               
                return 0;
            }
            virtual void SetConfig(const RenderPipeContex& cfg) override {
                PassContex ctx{
                    cfg.outputResolution,
                    cfg.camera
                };
                environmentPass.SetConfig(ctx);
                cameraPass.SetConfig(ctx);
                scenePass.SetConfig(ctx);
                screenPass.SetConfig(ctx);
            }
            virtual void RenderCall() override {
                environmentPass.UBOdata_.iTime = Environment::Environment::Instance().GetTime();
                environmentPass.Update();
                cameraPass.Update();
                scenePass.Update();
                screenPass.Update();
            }
            bool RegisterItem(const ModelRenderItem& item) {
                return scenePass.AddItem(item);
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