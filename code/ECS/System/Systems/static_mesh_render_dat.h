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
    class StaticMeshRenderDAT: public System{
        protected:
            virtual bool InitDerive() override{

            }
        public:
            StaticMeshRenderDAT():System("StaticMeshRenderDAT"){

            }
            virtual bool AddEntity(EntityID entity) override {
                return true;
            }
            virtual void Update() override {

            }
    };
}
