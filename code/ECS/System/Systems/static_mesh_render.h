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
        public:
            StaticMeshRender():System("StaticMeshRender"){

            }

            virtual bool Init(){
                ctx.outputResolution = Environment::Environment::Instance().windowSize;
                renderPipe.Init(ctx);
                return true;
            }

            int SetCameraObject(EntityID entity, ECS::Core::ComponentRegister& reg){
                ECS::Component::Camera* camera = reg.GetComponent<ECS::Component::Camera>(entity);
                if(camera){
                    ctx.camera = camera;
                    renderPipe.SetConfig(ctx);
                    return 0;
                }
                return 1;
            }



            virtual bool AddEntity(EntityID entity, ECS::Core::ComponentRegister& reg) override {
                ECS::Component::Transform* transform = reg.GetComponent<ECS::Component::Transform>(entity);
                ECS::Component::StaticModel* model = reg.GetComponent<ECS::Component::StaticModel>(entity);
                ECS::Component::MeshRenderer* meshRender  = reg.GetComponent<ECS::Component::MeshRenderer>(entity);
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
                        &transform->worldMatrix
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
                }
                renderPipe.RenderCall();
            }
        private:
            Render::StaicMesh renderPipe;
            Render::RenderPipeContex ctx;
    };
}