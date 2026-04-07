// query 测试
#include <iostream>
#include <vector>
#include <chrono>
#include <iomanip>
#include <glm/vec3.hpp>

#include "engine/ECS/Component/component_init.h"
#include "engine/ECS/Scene/scene.h"

#include "engine/ECS/Component/Transform/transform.h"
#include "engine/ECS/Component/Collision/aabb_3D.h"

#include "engine/ECS/Query/query.h"
#include "engine/ECS/Core/Kernel/kernel.h"

#define G 10
#define B 10

void test1(){
    ECS::Core::Scene scene;
    auto description = scene.CreateArchTypeDescription();
    description->AddComponentArray<ECS::Component::Transform>();
    description->AddComponentArray<ECS::Component::AABB_3D>();
    auto preload = scene.CreateArchTypePreloadInstance(description,1024);
    auto archtype = scene.CreateArchType(description,32);
    auto archtype2 = scene.CreateArchType(description,64);
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

    for(int i = 0; i < B; i++){
        std::vector<ECS::EntityHandle> group;
        scene.RegisterPreloadToArchType(preload,archtype2,group);
        entities.push_back(group);
    }

    ECS::Core::ChunkQuery<
        ECS::Core::Require<ECS::Component::Transform,ECS::Component::AABB_3D>,
        ECS::Core::Optional<>,
        ECS::Core::Exclude<>
        > query;

    query.RegisterArchType(archtype.Get());
    query.RegisterArchType(archtype2.Get());
    for(auto chunk : query){
        auto transFormChunk = chunk.Get<ECS::Component::Transform>();
        auto AABBChunk = chunk.Get<ECS::Component::AABB_3D>();
        for(size_t i = 0; i < chunk.count; ++i){
            transFormChunk[i].position.x += 666.0f;
            AABBChunk[i].width += 10;
        }
    }
    

    for(auto group : entities){
        for(auto& handle : group){
            ECS::Core::EntityComponentHandle<ECS::Component::Transform> transform = scene.GetActiveComponent<ECS::Component::Transform>(handle.GetID());
            auto aabb = scene.GetActiveComponent<ECS::Component::AABB_3D>(handle.GetID());
            std::cout << "entityID: " << std::to_string(handle.GetID()) << ", position.x :" << transform.Get()->position.x << " aabb.width: " << aabb.Get()->width << "\n";
        }
    }
}

int main() {
    RegisterAllComponents();
    ECS::Core::ECSKernel kernel;
    kernel.Init();
    test1();
}