// benchmark_preload_vs_direct.cpp

#include <iostream>
#include <vector>
#include <chrono>
#include <numeric>
#include <string>
#include <iomanip>

#include <glm/vec3.hpp>

#include "engine/ECS/Component/component_init.h"
#include "engine/ECS/Scene/scene.h"
#include "engine/ECS/Component/Transform/transform.h"
#include "engine/ECS/Component/Collision/aabb_3D.h"

#ifndef G
#define G 100
#endif

#ifndef B
#define B 100
#endif

#ifndef TEST_ROUNDS
#define TEST_ROUNDS 5
#endif

namespace Bench {

using Clock = std::chrono::steady_clock;
using Microseconds = std::chrono::microseconds;

struct BenchmarkResult {
    double preloadPrepareUs = 0.0;   // 预载区创建+写入模板数据
    double preloadRegisterUs = 0.0;  // 批量登记到 archtype
    double preloadTotalUs = 0.0;     // 上面两者合计

    double directCreateUs = 0.0;     // 直接逐个创建+写入组件

    size_t preloadEntityCount = 0;
    size_t directEntityCount = 0;

    bool preloadCheckPassed = false;
    bool directCheckPassed = false;
};

static inline double ToUs(const Clock::duration& d) {
    return std::chrono::duration_cast<Microseconds>(d).count();
}

bool CheckOneEntityData(
    ECS::Core::Scene& scene,
    const ECS::EntityHandle& handle,
    int expectedValue)
{
    auto transform = scene.GetActiveComponent<ECS::Component::Transform>(handle.GetID());
    auto aabb = scene.GetActiveComponent<ECS::Component::AABB_3D>(handle.GetID());

    if (!transform.Get() || !aabb.Get()) {
        return false;
    }

    const auto& pos = transform.Get()->position;
    const auto* box = aabb.Get();

    if (pos.x != static_cast<float>(expectedValue) ||
        pos.y != static_cast<float>(expectedValue) ||
        pos.z != static_cast<float>(expectedValue)) {
        return false;
    }

    if (box->height != expectedValue ||
        box->width  != expectedValue * 2 ||
        box->length != expectedValue * 3) {
        return false;
    }

    return true;
}

bool CheckPreloadBatchResult(
    ECS::Core::Scene& scene,
    const std::vector<std::vector<ECS::EntityHandle>>& entities)
{
    if (entities.empty()) {
        return false;
    }

    // 检查第一批
    for (int i = 0; i < static_cast<int>(entities[0].size()); ++i) {
        if (!CheckOneEntityData(scene, entities[0][i], i)) {
            std::cerr << "[CHECK] preload first group failed at i = " << i << "\n";
            return false;
        }
    }

    // 检查最后一批
    const auto& lastGroup = entities.back();
    for (int i = 0; i < static_cast<int>(lastGroup.size()); ++i) {
        if (!CheckOneEntityData(scene, lastGroup[i], i)) {
            std::cerr << "[CHECK] preload last group failed at i = " << i << "\n";
            return false;
        }
    }

    return true;
}

bool CheckDirectResult(
    ECS::Core::Scene& scene,
    const std::vector<ECS::EntityHandle>& entities)
{
    if (entities.size() != static_cast<size_t>(B * G)) {
        return false;
    }

    // 检查头一段
    for (int i = 0; i < G; ++i) {
        if (!CheckOneEntityData(scene, entities[i], i % G)) {
            std::cerr << "[CHECK] direct head failed at i = " << i << "\n";
            return false;
        }
    }

    // 检查尾一段
    for (int i = static_cast<int>(entities.size()) - G; i < static_cast<int>(entities.size()); ++i) {
        if (!CheckOneEntityData(scene, entities[i], i % G)) {
            std::cerr << "[CHECK] direct tail failed at i = " << i << "\n";
            return false;
        }
    }

    return true;
}

BenchmarkResult RunOnce()
{
    BenchmarkResult result;

    // -----------------------------
    // 方案1：预载区 + 批量登记
    // -----------------------------
    {
        ECS::Core::Scene scene;
        auto description = scene.CreateArchTypeDescription();
        description->AddComponentArray<ECS::Component::Transform>();
        description->AddComponentArray<ECS::Component::AABB_3D>();

        auto preload = scene.CreateArchTypePreloadInstance(description, 1024);
        auto archtype = scene.CreateArchType(description, 1024);

        auto t0 = Clock::now();

        size_t startIndex = preload->CreateUnit(G);
        (void)startIndex;

        auto arrayTransform = preload->TryCastComponentArray<ECS::Component::Transform>();
        auto arrayAABB      = preload->TryCastComponentArray<ECS::Component::AABB_3D>();

        if (!arrayTransform || !arrayAABB) {
            std::cerr << "[ERROR] preload component array cast failed.\n";
            return result;
        }

        for (int i = 0; i < G; ++i) {
            (*arrayTransform)[i].position = glm::vec3(static_cast<float>(i));
            (*arrayAABB)[i].height = i;
            (*arrayAABB)[i].width  = i * 2;
            (*arrayAABB)[i].length = i * 3;
        }

        auto t1 = Clock::now();

        std::vector<std::vector<ECS::EntityHandle>> entities;
        entities.reserve(B);

        for (int i = 0; i < B; ++i) {
            entities.emplace_back();
            entities.back().reserve(G);
            scene.RegisterPreloadToArchType(preload, archtype, entities.back());
        }

        auto t2 = Clock::now();

        result.preloadPrepareUs  = ToUs(t1 - t0);
        result.preloadRegisterUs = ToUs(t2 - t1);
        result.preloadTotalUs    = ToUs(t2 - t0);
        result.preloadEntityCount = archtype->ActiveCount();
        result.preloadCheckPassed = CheckPreloadBatchResult(scene, entities);

        std::cout << "[Preload] ActiveCount = " << archtype->ActiveCount() << "\n";
    }

    // -----------------------------
    // 方案2：逐个创建实体
    // -----------------------------
    {
        ECS::Core::Scene scene;
        auto description = scene.CreateArchTypeDescription();
        description->AddComponentArray<ECS::Component::Transform>();
        description->AddComponentArray<ECS::Component::AABB_3D>();

        auto archtype = scene.CreateArchType(description, 1024);

        std::vector<ECS::EntityHandle> entities;
        entities.reserve(B * G);

        auto t0 = Clock::now();

        for (int i = 0; i < B * G; ++i) {
            auto entityHandle = scene.CreateEntity(archtype);
            entities.push_back(entityHandle);

            auto transform = scene.GetActiveComponent<ECS::Component::Transform>(entityHandle.GetID());
            auto aabb      = scene.GetActiveComponent<ECS::Component::AABB_3D>(entityHandle.GetID());

            if (!transform.Get() || !aabb.Get()) {
                std::cerr << "[ERROR] direct component fetch failed at i = " << i << "\n";
                return result;
            }

            int v = i % G;
            transform.Get()->position = glm::vec3(static_cast<float>(v));
            aabb.Get()->height = v;
            aabb.Get()->width  = v * 2;
            aabb.Get()->length = v * 3;
        }

        auto t1 = Clock::now();

        result.directCreateUs = ToUs(t1 - t0);
        result.directEntityCount = archtype->ActiveCount();
        result.directCheckPassed = CheckDirectResult(scene, entities);

        std::cout << "[Direct ] ActiveCount = " << archtype->ActiveCount() << "\n";
    }

    return result;
}

void PrintSummary(const std::vector<BenchmarkResult>& results)
{
    auto avg = [&](auto getter) {
        double sum = 0.0;
        for (const auto& r : results) sum += getter(r);
        return sum / static_cast<double>(results.size());
    };

    double avgPreloadPrepare = avg([](const BenchmarkResult& r){ return r.preloadPrepareUs;  });
    double avgPreloadRegister= avg([](const BenchmarkResult& r){ return r.preloadRegisterUs; });
    double avgPreloadTotal   = avg([](const BenchmarkResult& r){ return r.preloadTotalUs;    });
    double avgDirect         = avg([](const BenchmarkResult& r){ return r.directCreateUs;    });

    std::cout << "\n================ Benchmark Summary ================\n";
    std::cout << "Rounds                  : " << results.size() << "\n";
    std::cout << "Batches(B)              : " << B << "\n";
    std::cout << "Group Size(G)           : " << G << "\n";
    std::cout << "Total Entities          : " << (B * G) << "\n\n";

    std::cout << std::fixed << std::setprecision(3);
    std::cout << "Preload prepare avg (us): " << avgPreloadPrepare << "\n";
    std::cout << "Preload register avg(us): " << avgPreloadRegister << "\n";
    std::cout << "Preload total avg   (us): " << avgPreloadTotal << "\n";
    std::cout << "Direct create avg   (us): " << avgDirect << "\n\n";

    if (avgPreloadTotal > 0.0) {
        std::cout << "Direct / PreloadTotal   : " << (avgDirect / avgPreloadTotal) << " x\n";
    }
    if (avgPreloadRegister > 0.0) {
        std::cout << "Direct / PreloadRegister: " << (avgDirect / avgPreloadRegister) << " x\n";
    }

    bool allPreloadCheck = true;
    bool allDirectCheck = true;
    for (const auto& r : results) {
        allPreloadCheck &= r.preloadCheckPassed;
        allDirectCheck  &= r.directCheckPassed;
    }

    std::cout << "\nPreload check passed    : " << (allPreloadCheck ? "YES" : "NO") << "\n";
    std::cout << "Direct check passed     : " << (allDirectCheck ? "YES" : "NO") << "\n";
    std::cout << "===================================================\n";
}

} // namespace Bench

int main()
{
    RegisterAllComponents();

    std::vector<Bench::BenchmarkResult> results;
    results.reserve(TEST_ROUNDS);

    for (int round = 0; round < TEST_ROUNDS; ++round) {
        std::cout << "\n********** Round " << (round + 1) << " **********\n";
        auto result = Bench::RunOnce();

        std::cout << std::fixed << std::setprecision(3);
        std::cout << "[Round " << (round + 1) << "] "
                  << "preload_prepare=" << result.preloadPrepareUs  << " us, "
                  << "preload_register=" << result.preloadRegisterUs << " us, "
                  << "preload_total="    << result.preloadTotalUs    << " us, "
                  << "direct="           << result.directCreateUs    << " us\n";

        results.push_back(result);
    }

    Bench::PrintSummary(results);
    return 0;
}