#include "Brotato/Public/session_module.h"
#include "Brotato/Public/content_catalog.h"
#include "Brotato/Public/selection_catalog.h"
#include <exception>
#include <utility>

namespace Brotato {

SessionModule::SessionModule(Config base)
    : base_(std::move(base)), mapRandom_(base_.seed ^ 0x9e3779b9u),
      character_(base_.character), map_(base_.map), weapon_(base_.initialWeapon), difficulty_(base_.difficulty) {}

bool SessionModule::Reject(const char* message) {
    error_ = message;
    return false;
}

bool SessionModule::Startup() {
    if (started_) return true;
    if (character_ >= Characters.size() || map_ > RandomMap || !ValidWeapon(weapon_) || difficulty_>=DifficultyCount())
        return Reject("Brotato session: invalid initial character, weapon or map");
    screen_ = Screen::Home;
    weaponChosen_ = false;
    started_ = true;
    error_.clear();
    return true;
}

void SessionModule::Shutdown() {
    game_.reset();
    screen_ = Screen::Home;
    weaponChosen_ = false;
    started_ = false;
}

bool SessionModule::Navigate(Screen destination) {
    if (!started_) return Reject("Brotato session: not started");
    if (screen_ == Screen::Run || destination == Screen::Run)
        return Reject("Brotato session: use StartRun or ReturnHome to change runs");
    if (screen_ == Screen::WeaponSelect && destination == Screen::DifficultySelect && !weaponChosen_)
        return Reject("Brotato session: choose a starting weapon before continuing");
    const bool valid = destination == Screen::Home || destination == screen_ ||
        (screen_ == Screen::Home && destination == Screen::CharacterSelect) ||
        (screen_ == Screen::CharacterSelect && destination == Screen::WeaponSelect) ||
        (screen_ == Screen::WeaponSelect &&
         (destination == Screen::CharacterSelect || destination == Screen::DifficultySelect)) ||
        (screen_ == Screen::DifficultySelect &&
         (destination == Screen::WeaponSelect || destination == Screen::MapSelect)) ||
        (screen_ == Screen::MapSelect && destination == Screen::DifficultySelect);
    if (!valid) return Reject("Brotato session: invalid menu transition");
    screen_ = destination;
    if (screen_ == Screen::Home) weaponChosen_ = false;
    error_.clear();
    return true;
}

bool SessionModule::SelectCharacter(std::size_t index) {
    if (!started_ || screen_ != Screen::CharacterSelect)
        return Reject("Brotato session: character selection is not open");
    if (index >= Characters.size()) return Reject("Brotato session: invalid character");
    character_ = index;
    error_.clear();
    return true;
}

bool SessionModule::SelectMap(std::size_t index) {
    if (!started_ || screen_ != Screen::MapSelect)
        return Reject("Brotato session: map selection is not open");
    if (index > RandomMap) return Reject("Brotato session: invalid map");
    map_ = index;
    error_.clear();
    return true;
}

bool SessionModule::SelectWeapon(WeaponKind kind) {
    if (!started_ || screen_ != Screen::WeaponSelect)
        return Reject("Brotato session: weapon selection is not open");
    if (!ValidWeapon(kind)) return Reject("Brotato session: invalid weapon");
    weapon_ = kind;
    weaponChosen_ = true;
    error_.clear();
    return true;
}

bool SessionModule::SelectDifficulty(std::size_t index) {
    if (!started_ || screen_ != Screen::DifficultySelect)
        return Reject("Brotato session: difficulty selection is not open");
    // Generated source choices stay separate from the new rule profiles.
    if (index >= DifficultyCount()) return Reject("Brotato session: invalid difficulty");
    difficulty_ = index;
    error_.clear();
    return true;
}

bool SessionModule::StartRun() {
    if (!started_ || screen_ != Screen::MapSelect || !weaponChosen_)
        return Reject("Brotato session: finish map selection before starting");
    try {
        Config settings = base_;
        settings.character = character_;
        settings.difficulty = difficulty_;
        settings.initialWeapon = weapon_;
        auto candidateRandom = mapRandom_;
        settings.map = map_ == RandomMap ? candidateRandom() % Maps.size() : map_;
        auto candidate = std::make_unique<GameModule>(std::move(settings));
        if (!candidate->Startup()) {
            error_ = candidate->Error();
            return false;
        }
        game_ = std::move(candidate);
        ++runSerial_;
        mapRandom_ = std::move(candidateRandom);
        screen_ = Screen::Run;
        error_.clear();
        return true;
    } catch (const std::exception& error) {
        error_ = error.what();
        return false;
    }
}

bool SessionModule::ReturnHome() {
    if (!started_) return Reject("Brotato session: not started");
    game_.reset();
    screen_ = Screen::Home;
    weaponChosen_ = false;
    error_.clear();
    return true;
}

void SessionModule::FixedTick(Input input) {
    if (started_ && screen_ == Screen::Run && game_) game_->FixedTick(input);
}

void SessionModule::Advance(double seconds, Input input) {
    if (started_ && screen_ == Screen::Run && game_) game_->Advance(seconds, input);
}

} // namespace Brotato
