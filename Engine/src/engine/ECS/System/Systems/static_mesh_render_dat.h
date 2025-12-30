#pragma once

#include <vector>
#include <unordered_map>

#include "engine/ECS/System/system.h"
#include "engine/Environment/environment.h"

#include "engine/ECS/Component/Model/static_model.h"
#include "engine/ECS/Component/Render/mesh_renderer.h"
#include "engine/ECS/Component/Transform/transform.h"
#include "engine/ECS/Component/Camera/camera.h"

#include "engine/Resource/RenderPipe/RenderPipes/static_mesh.h"

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

