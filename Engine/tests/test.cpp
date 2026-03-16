#include "engine/ECS/Component/component_init.h"
#include "engine/ECS/Scene/scene.h"



int main(){
    RegisterAllComponents();
    ECS::Core::Scene scene;
    auto description = scene.CreateArchTypeDescription();
    description->AddComponentArray<ECS::Component::Transform>();
    description->AddComponentArray<ECS::Component::AABB_3D>();
    auto archtype = scene.CreateArchType(description,256);
    ECS::EntityHandle entity1 = scene.CreateEntity(archtype);
    auto transformHandle = scene.GetActiveComponent<ECS::Component::Transform>(entity1.GetID());
    ECS::Component::Transform* transform = transformHandle.Get();
    transform->position = glm::vec3(1.0);
}