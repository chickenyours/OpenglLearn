#pragma once

#include "game_module.h"
#include <cstddef>
#include <memory>
#include <random>
#include <string>

namespace Brotato {

enum class Screen { Home, CharacterSelect, WeaponSelect, DifficultySelect, MapSelect, Run };

// Main-thread application flow. Menus own no simulation and never accumulate
// game time. A run is a fresh GameModule, destroyed when returning home.
// Game pointers, component references and entity handles must not cross runs:
// ECS entity generations identify entities within one Scene only.
class SessionModule final : public IModule {
public:
    explicit SessionModule(Config base = {});
    ~SessionModule() override { Shutdown(); }
    const char* GetName() const noexcept override { return "BrotatoSessionModule"; }
    bool Startup() override;
    void Shutdown() override;
    bool IsStarted() const noexcept override { return started_; }

    Screen CurrentScreen() const noexcept { return screen_; }
    std::size_t SelectedCharacter() const noexcept { return character_; }
    WeaponKind SelectedWeapon() const noexcept { return weapon_; }
    bool HasSelectedWeapon() const noexcept { return weaponChosen_; }
    std::size_t SelectedDifficulty() const noexcept { return difficulty_; }
    std::size_t SelectedMap() const noexcept { return map_; }
    GameModule* Game() noexcept { return game_.get(); }
    const GameModule* Game() const noexcept { return game_.get(); }
    const std::string& Error() const noexcept { return error_; }
    // Monotonic identity across destroyed/recreated scenes and Shutdown/Startup.
    std::uint64_t RunSerial() const noexcept { return runSerial_; }

    // Navigate only moves between adjacent menu steps, with Home as a cancel
    // destination. Starting/ending a run requires the explicit operations below.
    bool Navigate(Screen destination);
    bool SelectCharacter(std::size_t index);
    bool SelectWeapon(WeaponKind kind);
    bool SelectDifficulty(std::size_t index);
    bool SelectMap(std::size_t index);
    bool StartRun();
    bool ReturnHome();
    void FixedTick(Input input = {});
    void Advance(double seconds, Input input = {});

private:
    bool Reject(const char* message);
    Config base_;
    std::unique_ptr<GameModule> game_;
    // Map choice has a separate random stream from combat and spawn scheduling.
    std::mt19937 mapRandom_;
    std::size_t character_ = 0, map_ = 0;
    WeaponKind weapon_ = WeaponKind::Wand;
    std::size_t difficulty_ = 0;
    std::uint64_t runSerial_ = 0;
    Screen screen_ = Screen::Home;
    bool started_ = false;
    bool weaponChosen_ = false;
    std::string error_;
};

} // namespace Brotato
