#pragma once
#include "engine/ECS/Component/component_loader_registry.h"

// namespace ECS{
//     namespace Component{
//         struct Transform;
//         struct MeshRenderer;
//         struct StaticModel;
//         struct Script;
//     }
// }

#include "engine/ECS/Component/Render/mesh_renderer.h"
#include "engine/ECS/Component/Transform/transform.h"
#include "engine/ECS/Component/Model/static_model.h"
#include "engine/ECS/Component/Script/scirpt.h"
#include "engine/ECS/Component/Collision/aabb_3D.h"
#include "engine/ECS/Component/Render/2d_renderer.h"


void RegisterAllComponents() {
    REGISTER_COMPONENT("transform", ECS::Component::Transform);
    REGISTER_COMPONENT("meshRenderer", ECS::Component::MeshRenderer);
    REGISTER_COMPONENT("staticModel", ECS::Component::StaticModel);
    REGISTER_COMPONENT("script", ECS::Component::Script);
    REGISTER_COMPONENT("aabb_3D", ECS::Component::AABB_3D);
    REGISTER_COMPONENT("renderer2D", ECS::Component::Renderer2D);
    // REGISTER_COMPONENT(ECS::Component::Collider);
}



