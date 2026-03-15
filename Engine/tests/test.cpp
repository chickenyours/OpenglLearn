#include "engine/ECS/Component/component_init.h"
#include "engine/ECS/Scene/scene.h"



int main(){
    RegisterAllComponents();
    ECS::Core::Scene scene;
    ECS::Core::ArchTypeDescription* description = scene.CreateArchTypeDescription();
    description->AddComponentArray<ECS::Component::Transform>();
    description->AddComponentArray<ECS::Component::AABB_3D>();
}