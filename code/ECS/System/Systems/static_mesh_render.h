#pragma once

#include <vector>
#include <unordered_map>

#include "code/ECS/System/system.h"
#include "code/Environment/environment.h"

#include "code/ECS/Component/Model/static_model.h"
#include "code/ECS/Component/Render/mesh_renderer.h"
#include "code/ECS/Component/Transform/transform.h"
#include "code/ECS/Component/Camera/camera.h"

#include "code/Resource/RenderPipe/RenderPipes/static_mesh.h"

namespace ECS::System{
    class StaticMeshRender: public System{
        protected:
            virtual bool InitDerive() override{
                ctx.outputResolution = Environment::Environment::Instance().windowSize;
                int code = renderPipe.Init(ctx);
                if(code){
                    LOG_ERROR("StaticMeshRender::InitDerive", "init error : " + std::to_string(code));
                }
                return true;
            }
        public:
            StaticMeshRender():System("StaticMeshRender"){

            }


            int SetCameraObject(EntityID entity){
                ECS::Component::Camera* camera = scene_->registry_->GetComponent<ECS::Component::Camera>(entity);
                if(camera){
                    ctx.camera = camera;
                    renderPipe.SetConfig(ctx);
                    return 0;
                }
                return 1;
            }



            virtual bool AddEntity(EntityID entity) override {
                ECS::Component::Transform* transform = scene_->registry_->GetComponent<ECS::Component::Transform>(entity);
                ECS::Component::StaticModel* model = scene_->registry_->GetComponent<ECS::Component::StaticModel>(entity);
                ECS::Component::MeshRenderer* meshRender  = scene_->registry_->GetComponent<ECS::Component::MeshRenderer>(entity);
                if(transform && model && meshRender){
                    Render::ModelRenderItem item{
                        model->model.get(),
                        [](ECS::Component::MeshRenderer*& meshRender){
                            std::vector<Material*> list;
                            list.reserve(meshRender->materialList.size());
                            for(auto& it : meshRender->materialList){
                                list.push_back(it.get());
                            }
                            return list;
                        }(meshRender),
                        &transform->worldMatrix,
                        meshRender
                    };
                    renderPipe.RegisterItem(item);
                }
                else{
                    // LOG_ERROR("StaticMeshRender","file to register entity because lack of component");
                    return false;
                }
                return true;
            }

            virtual void Update() override {
                if(Environment::Environment::Instance().isWindowSizeChange){
                    ctx.outputResolution = Environment::Environment::Instance().windowSize;
                    renderPipe.SetConfig(ctx);
                    Environment::Environment::Instance().isWindowSizeChange = false;
                }
                renderPipe.RenderCall();
            }
        private:
            Render::StaicMesh renderPipe;
            Render::RenderPipeContex ctx;
    };
}