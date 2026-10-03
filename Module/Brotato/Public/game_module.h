#pragma once

#include "module_base.h"
#include "game_components.h"
#include "engine/ECS/Scene/scene.h"
#include "engine/ECS/System/system_pipeline.h"
#include <memory>
#include <random>
#include <stdexcept>
#include <vector>

namespace Brotato {
class WaveSystem; class MovementSystem; class WeaponSystem;
class ProjectileSystem; class ContactSystem; class PickupSystem;

// Main-thread simulation. Structural changes are committed after the pipeline.
// The renderer only consumes value snapshots; no component pointer crosses a tick.
class GameModule final : public IModule {
public:
    static constexpr double FixedStep = 1.0 / 120.0;
    explicit GameModule(Config config = {});
    ~GameModule() override { Shutdown(); }
    const char* GetName() const noexcept override { return "BrotatoModule"; }
    bool Startup() override;
    void Shutdown() override;
    bool IsStarted() const noexcept override { return started_; }
    const std::string& Error() const { return error_; }
    const Config& Settings() const { return config_; }
    const Statistics& Stats() const { return stats_; }
    State GetState() const { return state_; }
    ECS::Core::Scene* Scene() { return scene_.get(); }
    ECS::EntityHandle PlayerEntity() const { return player_; }
    ECS::EntityHandle WeaponEntity() const { return weapon_; }
    WeaponKind CurrentWeapon() const { return selected_; }
    const WeaponDefinition& Definition(WeaponKind kind) const { return config_.weapons.at(WeaponIndex(kind)); }
    bool EquipWeapon(WeaponKind kind);
    template<class T> T& Get(ECS::EntityHandle entity) {
        if (!scene_ || !scene_->IsAlive(entity)) throw std::logic_error("Brotato: stale entity handle");
        auto* component = scene_->GetActiveComponent<T>(entity.GetID()).Get();
        if (!component) throw std::logic_error("Brotato: missing component");
        return *component;
    }
    void FixedTick(Input input = {});
    void Advance(double seconds, Input input = {});
    void Restart();
    void NextWave();
    void Revive();
    void SetPaused(bool paused);
    std::vector<DrawSprite> Extract();
    // Scenario/editor seams; only call between ticks. Capacity exhaustion returns
    // an invalid EntityHandle, not an unbounded allocation.
    ECS::EntityHandle SpawnEnemy(glm::vec2 position, bool ready = false);
    ECS::EntityHandle SpawnPickup(glm::vec2 position);
    ECS::EntityHandle SpawnProjectile(glm::vec2 position, glm::vec2 velocity);

private:
    friend class WaveSystem; friend class MovementSystem; friend class WeaponSystem;
    friend class ProjectileSystem; friend class ContactSystem; friend class PickupSystem;
    void Validate() const;
    void HandleInput(Input input);
    void Step(Input input);
    void WaveTick();
    void Move();
    void Fire();
    void Projectiles();
    void Contacts();
    void Pickups();
    void Commit();
    void ClearTransient();
    void RefreshCounts();
    void Kill(ECS::EntityHandle entity);
    bool AliveEnemy(ECS::EntityHandle entity);
    ECS::EntityHandle NearestEnemy(float range);
    void ResetWeapon(bool resetCooldowns);
    void ThrustTick();
    void BeamTick();
    void HitCapsule(glm::vec2 start, glm::vec2 end, float angle, float halfLength, float radius);
    ECS::EntityHandle CreateProjectile(glm::vec2 position, glm::vec2 velocity, WeaponKind kind, int path, float heading);
    struct SpawnCommand {
        Image kind;
        glm::vec2 position, velocity{};
        WeaponKind weaponKind = WeaponKind::Wand;
        int path = -1;
        float heading = 0;
    };
    Config config_;
    Statistics stats_;
    std::unique_ptr<ECS::Core::Scene> scene_;
    ObjectWeakPtr<ECS::Core::ArchType> playerType_, weaponType_, enemyType_, projectileType_, pickupType_;
    ECS::EntityHandle player_{0}, weapon_{0};
    WeaponKind selected_ = WeaponKind::Wand;
    std::vector<ECS::EntityHandle> enemies_, projectiles_, pickups_, retired_;
    std::vector<SpawnCommand> pending_;
    ECS::System::Pipeline pipeline_;
    ECS::System::Context context_;
    std::mt19937 random_;
    Input input_;
    State state_ = State::Playing;
    double accumulator_ = 0, spawnTimer_ = 0;
    bool started_ = false;
    std::string error_;
};
} // namespace Brotato
