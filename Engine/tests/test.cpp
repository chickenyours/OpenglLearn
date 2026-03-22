// 批量创建

#include <iostream>
#include <vector>

#include <glm/vec3.hpp>

#include "engine/ECS/Component/component_init.h"
#include "engine/ECS/Scene/scene.h"

#include "engine/ECS/Component/Transform/transform.h"
#include "engine/ECS/Component/Collision/aabb_3D.h"

#define G 10
#define B 10

void preload_test1(){
    ECS::Core::Scene scene;
    auto description = scene.CreateArchTypeDescription();
    description->AddComponentArray<ECS::Component::Transform>();
    description->AddComponentArray<ECS::Component::AABB_3D>();
    auto preload = scene.CreateArchTypePreloadInstance(description,1024);
    auto archtype = scene.CreateArchType(description,1024);
    size_t startIndex = preload->CreateUnit(G);

    auto arrayTransform = preload->TryCastComponentArray<ECS::Component::Transform>();
    auto array = preload->TryCastComponentArray<ECS::Component::AABB_3D>();
    for(int i = 0; i < G; i++){
        (*arrayTransform)[i].position = glm::vec3(i);
        (*array)[i].height = i;
        (*array)[i].width = i * 2;
        (*array)[i].length = i * 3;
    }


    std::vector<std::vector<ECS::EntityHandle>> entities;
    for(int i = 0; i < B; i++){
        std::vector<ECS::EntityHandle> group;
        scene.RegisterPreloadToArchType(preload,archtype,group);
        entities.push_back(group);
    }

    LOG_INFO("archtype","size" + std::to_string(archtype->ActiveCount()));

    // for(auto group : entities){
    //     for(auto& handle : group){
    //         ECS::Core::EntityComponentHandle<ECS::Component::Transform> transform = scene.GetActiveComponent<ECS::Component::Transform>(handle.GetID());
    //         auto aabb = scene.GetActiveComponent<ECS::Component::AABB_3D>(handle.GetID());
    //         std::cout << "entityID: " << std::to_string(handle.GetID()) << ", position.x :" << transform.Get()->position.x << " aabb.width: " << aabb.Get()->width << "\n";
    //     }
    // }
}

void common_create_test(){
    ECS::Core::Scene scene;
    auto description = scene.CreateArchTypeDescription();
    description->AddComponentArray<ECS::Component::Transform>();
    description->AddComponentArray<ECS::Component::AABB_3D>();
    auto archtype = scene.CreateArchType(description,1024);
    for(int i = 0; i < B * G; i++){
        auto entityhandle = scene.CreateEntity(archtype);
        auto transform = scene.GetActiveComponent<ECS::Component::Transform>(entityhandle.GetID());
        auto aabb = scene.GetActiveComponent<ECS::Component::AABB_3D>(entityhandle.GetID());
        transform.Get()->position = glm::vec3(i);
        aabb.Get()->height = (i % G);
        aabb.Get()->width = (i % G) * 2;
        aabb.Get()->length = (i % G) * 3;
    }

    LOG_INFO("archtype", "Active entity count: " + std::to_string(archtype->ActiveCount()));

    auto arrayTransform = archtype->TryCastComponentArray<ECS::Component::Transform>();
    auto arrayAABB = archtype->TryCastComponentArray<ECS::Component::AABB_3D>();
    
    for(int i = 0; i < B * G; i++){
        LOG_INFO("entity_" + std::to_string(i), 
            "position: (" + std::to_string((*arrayTransform)[i].position.x) + "), "
            "width: " + std::to_string((*arrayAABB)[i].width));
    }

    LOG_INFO("archtype", "All components initialized successfully!");

    
    for(int i = 0; i < B * G; i++){
        if(std::fmod((*arrayAABB)[i].width,10.0f) < 5.0f){
            ECS::EntityID targetID = archtype->GetIndexEntities()[i];
            scene.DeleteEntity();
        }
            
    }

    
}


int main() {
    RegisterAllComponents();


}