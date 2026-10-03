#pragma once

#include "module_base.h"
#include "forest_components.h"
#include "engine/ECS/Scene/scene.h"
#include "engine/ECS/System/system_pipeline.h"
#include <array>
#include <functional>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace ForestFire {

class EvaluateSystem;
class CommitSystem;

// Main-thread module. Each row-major grid slot owns one stable ECS entity.
class ForestModule final : public IModule {
public:
    explicit ForestModule(SimulationConfig config = {});
    ~ForestModule() override { Shutdown(); }
    const char* GetName() const noexcept override { return "ForestFireModule"; }
    bool Startup() override;
    void Shutdown() override;
    bool IsStarted() const noexcept override { return started_; }

    const SimulationConfig& Config() const noexcept { return config_; }
    const Statistics& Stats() const noexcept { return statistics_; }
    const std::string& Error() const noexcept { return error_; }

    // FixedTick deliberately steps while paused (for a single-step UI).
    void FixedTick();
    // Invalid elapsed time is ignored. At most maxCatchUpSteps execute per call;
    // excess elapsed whole steps are dropped, retaining only the fractional step.
    void Advance(double seconds);
    void SetPaused(bool paused) noexcept;
    bool IsPaused() const noexcept { return paused_; }
    bool SetStepSeconds(double seconds);
    bool Reset();
    bool Reset(std::uint32_t seed);

    // Editing synchronizes both state buffers. Invalid coordinates/state return
    // false. Ignite affects only trees; SetCell can directly create a fire.
    bool SetCell(std::uint32_t x, std::uint32_t y, CellState state);
    bool Ignite(std::uint32_t x, std::uint32_t y);
    std::vector<CellSnapshot> Snapshot() const;

    // Scene is borrowed until Shutdown. Keep grid entities alive and preserve
    // their CellPosition/Cell/NextCell components. Additional entities/components
    // are allowed; the module refreshes borrowed component addresses each phase.
    ECS::Core::Scene* Scene() noexcept { return scene_.get(); }
    const ECS::Core::Scene* Scene() const noexcept { return scene_.get(); }
    ECS::EntityID EntityAt(std::uint32_t x, std::uint32_t y) const;
    std::vector<ECS::EntityID> Neighbors(std::uint32_t x, std::uint32_t y) const;

    // Register/order custom systems while stopped. Context provides Scene and
    // GetService<ForestModule>(). Use NextCell between EvaluateSystem and
    // CommitSystem for rule extensions. Lifecycle/step/edit APIs are not reentrant.
    template<class T, class... Args> T& AddSystem(Args&&... args) {
        RequireConfigurable();
        return pipeline_.Add<T>(std::forward<Args>(args)...);
    }
    template<class Before, class After> bool RunBefore() {
        RequireConfigurable();
        return pipeline_.RunBefore<Before, After>();
    }
    // Add humidity, fuel, terrain, etc. to every grid entity. T follows the
    // engine ECS component contract; its default constructor supplies initial data.
    // Reset changes only built-in cell states, preserving extension components.
    template<class T> void AddCellComponent(const std::string& registrationKey) {
        RequireConfigurable();
        if (registrationKey.empty()) throw std::invalid_argument("A cell component registration key is required");
        ECS::Component::RegisterComponentType<T>(registrationKey);
        cellComponentFactories_.push_back([](ECS::Core::ArchTypeDescription& description) {
            description.AddComponentArray<T>();
        });
    }

private:
    friend class EvaluateSystem;
    friend class CommitSystem;
    struct NeighborIndices {
        std::array<std::size_t, 8> indices{};
        std::size_t count = 0;
    };
    struct CellView { Cell* current; NextCell* next; };

    void RequireConfigurable() const;
    void ValidateConfig() const;
    void BuildNeighbors();
    void RefreshViews();
    void Populate();
    void RefreshStatistics();
    void Evaluate();
    void Commit();
    bool Chance(double probability);
    std::size_t Index(std::uint32_t x, std::uint32_t y) const;

    SimulationConfig config_;
    Statistics statistics_;
    std::unique_ptr<ECS::Core::Scene> scene_;
    ObjectWeakPtr<ECS::Core::ArchType> archetype_;
    ECS::System::Pipeline pipeline_;
    ECS::System::Context context_;
    std::vector<std::function<void(ECS::Core::ArchTypeDescription&)>> cellComponentFactories_;
    std::vector<ECS::EntityID> entities_;
    std::vector<NeighborIndices> neighbors_;
    std::vector<CellView> views_;
    std::mt19937 random_;
    double accumulator_ = 0;
    bool started_ = false;
    bool paused_ = false;
    bool executing_ = false;
    std::string error_;
};

} // namespace ForestFire
