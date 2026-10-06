#pragma once

#include "module_base.h"
#include "game_world.h"
#include "engine/ECS/System/system_pipeline.h"

namespace Brotato {
// Application facade: lifecycle, fixed-step scheduling and value snapshots.
// Simulation systems receive GameWorld, never this module or its callbacks.
class GameModule final : public IModule {
public:
    static constexpr double FixedStep = SimulationStep;
    static constexpr std::size_t MaxPendingEvents = MaxPendingGameEvents;
    explicit GameModule(Config config = {});
    ~GameModule() override { Shutdown(); }
    const char* GetName() const noexcept override { return "BrotatoModule"; }
    bool Startup() override;
    void Shutdown() override;
    bool IsStarted() const noexcept override { return started_; }
    const std::string& Error() const { return error_; }
    const Config& Settings() const { return world_.config; }
    const Statistics& Stats() const { return world_.stats; }
    State GetState() const { return world_.state; }
    ECS::Core::Scene* Scene() { return world_.scene.get(); }
    ECS::EntityHandle PlayerEntity() const { return world_.player; }
    ECS::EntityHandle WeaponEntity() const { return world_.weapon; }
    WeaponKind CurrentWeapon() const { return world_.selected; }
    std::vector<GameEvent> DrainEvents();
    std::uint64_t PresentationEpoch() const { return world_.presentationEpoch; }
    std::uint64_t DroppedEventCount() const { return world_.droppedEvents; }
    const WeaponDefinition& Definition(WeaponKind kind) const { return world_.Definition(kind); }
    bool EquipWeapon(WeaponKind kind);
    bool SelectUpgrade(UpgradeKind kind);
    ShopResult BuyShopItem(std::size_t slot);
    ShopResult BuyShopOffer(std::size_t slot);
    ShopResult MergeWeapon(std::size_t slot);
    ShopResult SellWeapon(std::size_t slot);
    EquipmentSnapshot ExtractEquipment();
    bool SelectWeaponSlot(std::size_t slot);
    ShopResult RerollShop();
    ShopResult ToggleShopLock(std::size_t slot);
    ShopSnapshot ExtractShop();
    BuildSnapshot ExtractBuild();
    RunResult ExtractResult();
    RunSetupSnapshot ExtractRunSetup();
    EncounterSnapshot ExtractEncounter();
    std::vector<DrawBossStatus> ExtractBossStatus();
    template<class T> T& Get(ECS::EntityHandle entity) { return world_.Get<T>(entity); }
    void FixedTick(Input input = {});
    void Advance(double seconds, Input input = {});
    void Restart();
    void NextWave();
    void Revive();
    void SetPaused(bool paused);
    std::vector<DrawSprite> Extract();
    std::vector<DrawDamageText> ExtractDamageText();
    std::vector<DrawEnemyStatus> ExtractEnemyStatus();
    std::vector<DrawCombatCue> ExtractCombatCues();
    // Scenario/editor seams; only call between ticks.
    ECS::EntityHandle SpawnEnemy(glm::vec2 position, bool ready = false, EnemyKind = EnemyKind::Normal, ChampionKind = ChampionKind::None);
    ECS::EntityHandle SpawnPickup(glm::vec2 position);
    ECS::EntityHandle SpawnProjectile(glm::vec2 position, glm::vec2 velocity);
    ECS::EntityHandle SpawnHostileProjectile(glm::vec2 position, glm::vec2 velocity, int damage);
    ECS::EntityHandle SpawnBoss(glm::vec2 position, bool ready = false, bool finalBoss = false);

private:
    void Validate() const;
    void HandleInput(Input input);
    void Step(Input input);
    GameWorld world_;
    ECS::System::Pipeline pipeline_;
    ECS::System::Context context_;
    double accumulator_ = 0;
    bool started_ = false;
    std::string error_;
};
} // namespace Brotato
