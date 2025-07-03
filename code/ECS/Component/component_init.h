#pragma once
#include "code/ECS/Component/component_loader_registry.h"

#include "code/ECS/Component/Render/mesh_renderer.h"
#include "code/ECS/Component/Transform/transform.h"
#include "code/ECS/Component/Model/static_model.h"

void RegisterAllComponents() {
    REGISTER_COMPONENT("transform", ECS::Component::Transform);
    REGISTER_COMPONENT("meshRenderer", ECS::Component::MeshRenderer);
    REGISTER_COMPONENT("staticModel", ECS::Component::StaticModel);
    // REGISTER_COMPONENT(ECS::Component::Collider);
}
