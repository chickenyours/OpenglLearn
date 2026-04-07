// 批量创建

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

#define G 16
#define B 256

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

    for(auto group : entities){
        for(auto& handle : group){
            ECS::Core::EntityComponentHandle<ECS::Component::Transform> transform = scene.GetActiveComponent<ECS::Component::Transform>(handle.GetID());
            auto aabb = scene.GetActiveComponent<ECS::Component::AABB_3D>(handle.GetID());
            std::cout << "entityID: " << std::to_string(handle.GetID()) << ", position.x :" << transform.Get()->position.x << " aabb.width: " << aabb.Get()->width << "\n";
        }
    }
}

void common_create_test(){
    ECS::Core::Scene scene;
    auto description = scene.CreateArchTypeDescription();
    description->AddComponentArray<ECS::Component::Transform>();
    description->AddComponentArray<ECS::Component::AABB_3D>();
    auto archtype = scene.CreateArchType(description,1024);
    std::vector<ECS::EntityHandle> handle;
    for(int i = 0; i < B * G; i++){
        auto entityhandle = scene.CreateEntity(archtype);
        handle.push_back(entityhandle);
        auto transform = scene.GetActiveComponent<ECS::Component::Transform>(entityhandle.GetID());
        auto aabb = scene.GetActiveComponent<ECS::Component::AABB_3D>(entityhandle.GetID());
        transform.Get()->position = glm::vec3(i);
        aabb.Get()->height = (i % G);
        aabb.Get()->width = (i % G) * 2;
        aabb.Get()->length = (i % G) * 3;
    }

    

    LOG_INFO("archtype", "Active entity count: " + std::to_string(archtype->ActiveCount()));

    for(int i = 0; i < B * G; i+=2){
        scene.DeleteEntity(handle[i]);
    }

    auto arrayTransform = archtype->TryCastComponentArray<ECS::Component::Transform>();
    auto arrayAABB = archtype->TryCastComponentArray<ECS::Component::AABB_3D>();
    for(int i = 0; i < B * G / 2; i++){
        LOG_INFO("entity_" + std::to_string(archtype->GetIndexEntities()[i]), 
            "position: (" + std::to_string((*arrayTransform)[i].position.x) + "), "
            "width: " + std::to_string((*arrayAABB)[i].width));
    }

    LOG_INFO("archtype", "All components initialized successfully!");

    
    // for(int i = 0; i < B * G; i++){
    //     if(std::fmod((*arrayAABB)[i].width,10.0f) < 5.0f){
    //         ECS::EntityID targetID = archtype->GetIndexEntities()[i];
    //         scene.DeleteEntity();
    //     }
            
    // }

    
}

void query_check_test(){
    ECS::Core::Scene scene;
    auto description = scene.CreateArchTypeDescription();
    description->AddComponentArray<ECS::Component::Transform>();
    description->AddComponentArray<ECS::Component::AABB_3D>();
    auto archtype = scene.CreateArchType(description,2);
    ECS::Core::ChunkQuery<
        ECS::Core::Require<ECS::Component::Transform>,
        ECS::Core::Optional<>,
        ECS::Core::Exclude<ECS::Component::AABB_3D>
        > query;
    if(query.CheckArchType(archtype.Get())){
        LOG_INFO("query_check_test","yes");
    }
    else{
        LOG_INFO("query_check_test","no");
    }
}

void chunk_schedule_test(){
    ECS::Core::Scene scene;
    auto description = scene.CreateArchTypeDescription();
    description->AddComponentArray<ECS::Component::Transform>();
    description->AddComponentArray<ECS::Component::AABB_3D>();
    auto preload = scene.CreateArchTypePreloadInstance(description,32);
    auto archtype = scene.CreateArchType(description,256);
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

    auto task1 = [&]()->void {
        LOG_INFO("task1","666");
        ECS::Core::ChunkExecuteHandle<ECS::Component::Transform> handle;
        scene.GetChunkSchedule()->GetChunk<ECS::Component::Transform>(
            ECS::Core::FailOption::WAIT,
            archtype.Get(),
            0,
            ChunkHeadState::READ,
            handle
        );
        LOG_INFO("task1","start work");
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        LOG_INFO("task1","finish work");
    };

    auto task2 = [&]()->void {
        LOG_INFO("task1","777");
        ECS::Core::ChunkExecuteHandle<ECS::Component::Transform> handle;
        scene.GetChunkSchedule()->GetChunk<ECS::Component::Transform>(
            ECS::Core::FailOption::WAIT,
            archtype.Get(),
            0,
            ChunkHeadState::WRITE,
            handle
        );
        LOG_INFO("task2","start work");
        std::this_thread::sleep_for(std::chrono::milliseconds(800));
        LOG_INFO("task2","finish work");
    };

    ECS::Core::JobSystem* jobSystem = scene.GetJobSystem();

    jobSystem->Submit(ECS::Core::Task(task1));
    jobSystem->Submit(ECS::Core::Task(task2));

    jobSystem->DispatchAll();

    jobSystem->WaitIdle();

    
    

    // handle.ref
    

}

void chunk_schedule_preform_test1(){
    ECS::Core::Scene scene;
    auto description = scene.CreateArchTypeDescription();
    description->AddComponentArray<ECS::Component::Transform>();
    description->AddComponentArray<ECS::Component::AABB_3D>();
    auto preload = scene.CreateArchTypePreloadInstance(description, 32);
    auto archtype = scene.CreateArchType(description, 256);
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
        scene.RegisterPreloadToArchType(preload, archtype, group);
        entities.push_back(group);
    }

    // 预热，减少首次调用带来的偶然影响
    for(int i = 0; i < 1000; ++i){
        ECS::Core::ChunkExecuteHandle<ECS::Component::Transform> warmupHandle;
        scene.GetChunkSchedule()->GetChunk<ECS::Component::Transform>(
            ECS::Core::FailOption::WAIT,
            archtype.Get(),
            0,
            ChunkHeadState::WRITE,
            warmupHandle
        );
    }

    // 测试句柄获取开销
    const int testTime = 100000;

    // 防止编译器过度优化
    volatile std::size_t guard = 0;

    auto begin = std::chrono::steady_clock::now();

    for(int i = 0; i < testTime; i++){
        ECS::Core::ChunkExecuteHandle<ECS::Component::Transform> handle;
        scene.GetChunkSchedule()->GetChunk<ECS::Component::Transform>(
            ECS::Core::FailOption::WAIT,
            archtype.Get(),
            0,
            ChunkHeadState::WRITE,
            handle
        );

        // 轻微使用结果，避免循环被完全视作无意义
        guard += static_cast<std::size_t>(i & 1);
    }

    auto end = std::chrono::steady_clock::now();

    const auto totalNs =
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count();

    const double avgNsPerCall =
        static_cast<double>(totalNs) / static_cast<double>(testTime);

    std::cout << "chunk_schedule_preform_test result\n";
    std::cout << "test count              : " << testTime << "\n";
    std::cout << "total time (ns)         : " << totalNs << "\n";
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "avg cost per call (ns)  : " << avgNsPerCall << "\n";
    std::cout << "guard                   : " << guard << "\n";
}

void chunk_schedule_preform_test2(){
    ECS::Core::Scene scene;
    auto description = scene.CreateArchTypeDescription();
    description->AddComponentArray<ECS::Component::Transform>();
    description->AddComponentArray<ECS::Component::AABB_3D>();
    auto preload = scene.CreateArchTypePreloadInstance(description,32);
    auto archtype = scene.CreateArchType(description,256);
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

    const int testTime = 100;
    const int workers = 4;
    const int timesPerWorker = testTime / workers;

    volatile std::size_t guard = 0;

    auto task1 = [&]()->void {
        float sum = 0.0f;
        for(int i = 0; i < timesPerWorker; i++){
            ECS::Core::ChunkExecuteHandle<ECS::Component::Transform> handle;
            scene.GetChunkSchedule()->GetChunk<ECS::Component::Transform>(
                ECS::Core::FailOption::WAIT,
                archtype.Get(),
                0,
                ChunkHeadState::READ,
                handle
            );

            sum += handle.GetChunk()[0].position.x;

            // handle.GetChunk()[0].position.x += 1.0f;

            // guard += static_cast<std::size_t>(i & 1);
        }

        LOG_INFO("task",std::to_string(sum));

    };

    ECS::Core::JobSystem* jobSystem = scene.GetJobSystem();

    auto begin = std::chrono::steady_clock::now();

    for(int i = 0; i < workers; i++){
        jobSystem->Submit(ECS::Core::Task(task1));
    }

    jobSystem->DispatchAll();
    jobSystem->WaitIdle();

    auto end = std::chrono::steady_clock::now();

    const long long totalNs =
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count();

    const int actualTotalCalls = timesPerWorker * workers;

    const double avgNsPerCall =
        actualTotalCalls > 0
            ? static_cast<double>(totalNs) / static_cast<double>(actualTotalCalls)
            : 0.0;

    ECS::Core::ChunkExecuteHandle<ECS::Component::Transform> handle;
    scene.GetChunkSchedule()->GetChunk<ECS::Component::Transform>(
        ECS::Core::FailOption::WAIT,
        archtype.Get(),
        0,
        ChunkHeadState::WRITE,
        handle
    );

    std::cout << "x: " << handle.GetChunk()[0].position.x;

    std::cout << "chunk_schedule_preform_test2 result\n";
    std::cout << "workers                  : " << workers << "\n";
    std::cout << "times per worker         : " << timesPerWorker << "\n";
    std::cout << "total handle calls       : " << actualTotalCalls << "\n";
    std::cout << "total time (ns)          : " << totalNs << "\n";
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "avg cost per handle (ns) : " << avgNsPerCall << "\n";
    std::cout << "guard                    : " << guard << "\n";
}


int main() {
    RegisterAllComponents();
    ECS::Core::ECSKernel kernel;
    kernel.Init();
    // preload_test1();
    // common_create_test();
    // query_check_test();
    chunk_schedule_preform_test2();

}