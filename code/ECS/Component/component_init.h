#pragma once
#include "code/ECS/Component/component_loader_registry.h"

// namespace ECS{
//     namespace Component{
//         struct Transform;
//         struct MeshRenderer;
//         struct StaticModel;
//         struct Script;
//     }
// }

#include "code/ECS/Component/Render/mesh_renderer.h"
#include "code/ECS/Component/Transform/transform.h"
#include "code/ECS/Component/Model/static_model.h"
#include "code/ECS/Component/Script/scirpt.h"
#include "code/ECS/Component/Collision/aabb_3D.h"


void RegisterAllComponents() {
    REGISTER_COMPONENT("transform", ECS::Component::Transform);
    REGISTER_COMPONENT("meshRenderer", ECS::Component::MeshRenderer);
    REGISTER_COMPONENT("staticModel", ECS::Component::StaticModel);
    REGISTER_COMPONENT("script", ECS::Component::Script);
    REGISTER_COMPONENT("aabb_3D", ECS::Component::AABB_3D);
    // REGISTER_COMPONENT(ECS::Component::Collider);
}



