#include "Brotato/Public/session_module.h"
#include "Brotato/Public/content_catalog.h"

#include <array>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace Brotato;

void Check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
void Near(double actual, double expected, const char* message) {
    if (!std::isfinite(actual) || std::abs(actual - expected) > 1e-5)
        throw std::runtime_error(std::string(message) + ": expected " +
                                 std::to_string(expected) + ", got " + std::to_string(actual));
}
Config QuietConfig() {
    Config config;
    config.minimum = {-100, -100}; config.maximum = {100, 100}; config.playerStart = {0, 0};
    config.enemySpeed = 0; config.waveSeconds = 600;
    config.spawning = false; config.armed = false;
    return config;
}
void Start(SessionModule& session) {
    Check(session.Startup(), "Session Startup: " + session.Error());
    Check(session.IsStarted(), "session is started");
}
void ToMap(SessionModule& session) {
    Check(session.Navigate(Screen::CharacterSelect), "open character selection");
    Check(session.Navigate(Screen::WeaponSelect), "open weapon selection");
    Check(session.SelectWeapon(session.SelectedWeapon()), "confirm starting weapon");
    Check(session.Navigate(Screen::DifficultySelect), "open difficulty selection");
    Check(session.Navigate(Screen::MapSelect), "open map selection");
}
void Run(SessionModule& session) {
    ToMap(session);
    Check(session.StartRun(), "StartRun: " + session.Error());
    Check(session.CurrentScreen() == Screen::Run && session.Game(), "run owns a game");
}
Player& PlayerData(SessionModule& session) {
    return session.Game()->Get<Player>(session.Game()->PlayerEntity());
}

void TestStartupOwnsNoSimulation() {
    auto config = QuietConfig();
    config.character = Characters.size() - 1;
    config.map = Maps.size() - 1;
    SessionModule session(config);
    Check(!session.IsStarted() && !session.Game(), "unstarted session owns no game");
    Check(!session.Navigate(Screen::CharacterSelect) && !session.StartRun() && !session.ReturnHome(),
          "unstarted lifecycle operations are rejected");
    session.FixedTick({1, 1, true, true, true, true, 6});
    session.Advance(100);
    Start(session);
    Check(session.CurrentScreen() == Screen::Home && !session.Game(), "startup only opens home");
    Check(session.SelectedCharacter() == config.character && session.SelectedMap() == config.map,
          "configuration provides initial selections");
    Check(session.Error().empty(), "successful startup clears earlier errors");
    const SessionModule& view = session;
    Check(!view.Game(), "const game access also reports no simulation");
}

void TestNavigationAndCancel() {
    SessionModule session(QuietConfig()); Start(session);
    Check(!session.Navigate(Screen::MapSelect), "home cannot skip character selection");
    Check(!session.Navigate(Screen::Run), "navigation cannot create a run");
    Check(!session.Navigate(static_cast<Screen>(1234)), "invalid enum is rejected");
    Check(session.CurrentScreen() == Screen::Home, "failed navigation leaves screen unchanged");
    Check(session.Navigate(Screen::CharacterSelect), "home opens character selection");
    Check(session.SelectCharacter(Characters.size() - 1), "select character");
    Check(!session.Navigate(Screen::MapSelect), "character selection cannot skip weapon and difficulty");
    Check(session.Navigate(Screen::WeaponSelect), "character selection advances to weapons");
    Check(session.SelectWeapon(WeaponKind::Burst), "choose starting weapon");
    Check(!session.Navigate(Screen::MapSelect), "weapon selection cannot skip difficulty");
    Check(session.Navigate(Screen::DifficultySelect), "weapon selection advances to difficulty");
    Check(session.SelectDifficulty(0), "choose source difficulty");
    Check(session.Navigate(Screen::MapSelect), "difficulty advances to map selection");
    Check(session.SelectMap(Maps.size() - 1), "select map");
    Check(!session.Navigate(Screen::CharacterSelect), "map selection cannot skip backward steps");
    Check(session.Navigate(Screen::DifficultySelect), "map selection can go back to difficulty");
    Check(session.Navigate(Screen::WeaponSelect), "difficulty can go back to weapons");
    Check(session.Navigate(Screen::CharacterSelect), "weapon selection can go back to characters");
    Check(session.Navigate(Screen::Home), "character selection can cancel");
    Check(session.SelectedCharacter() == Characters.size() - 1 && session.SelectedMap() == Maps.size() - 1,
          "cancel preserves choices");
    ToMap(session);
    Check(session.Navigate(Screen::Home) && !session.Game(), "map selection can cancel without a game");
    Check(session.Navigate(Screen::Home) && session.ReturnHome(), "home operations are idempotent");
}

void TestInvalidSelectionAndScreenGates() {
    SessionModule session(QuietConfig()); Start(session);
    Check(!session.SelectCharacter(0) && !session.SelectMap(0) && !session.SelectWeapon(WeaponKind::Gun) &&
          !session.SelectDifficulty(0), "home cannot select hidden controls");
    Check(session.Navigate(Screen::CharacterSelect), "open characters");
    Check(!session.SelectCharacter(Characters.size()), "reject past-end character");
    Check(!session.SelectCharacter(std::numeric_limits<std::size_t>::max()), "reject wrapped character index");
    Check(session.SelectedCharacter() == 0, "failed character selection preserves selection");
    Check(!session.SelectMap(0) && !session.StartRun(), "character screen cannot choose a map or start");
    Check(session.Navigate(Screen::WeaponSelect), "open weapons");
    Check(!session.SelectWeapon(WeaponKind::Count) && !session.SelectWeapon(static_cast<WeaponKind>(-1)),
          "weapon selection rejects invalid enums");
    Check(session.SelectedWeapon() == WeaponKind::Wand && !session.Game(), "invalid weapon preserves selection");
    Check(!session.SelectCharacter(0) && !session.SelectDifficulty(0) && !session.SelectMap(0) && !session.StartRun(),
          "weapon screen rejects hidden controls and run start");
    Check(session.SelectWeapon(WeaponKind::Torch), "valid weapon selection clears error");
    Check(session.Navigate(Screen::DifficultySelect), "open difficulty");
    Check(!session.SelectDifficulty(1) && !session.SelectDifficulty(std::numeric_limits<std::size_t>::max()),
          "source scene contains only one actionable difficulty");
    Check(!session.SelectCharacter(0) && !session.SelectWeapon(WeaponKind::Gun) && !session.SelectMap(0) &&
          !session.StartRun(), "difficulty screen rejects hidden controls and start");
    Check(session.SelectedDifficulty() == 0 && session.SelectDifficulty(0), "default source difficulty is selectable");
    Check(session.Navigate(Screen::MapSelect), "open maps");
    Check(!session.SelectMap(RandomMap + 1), "reject map beyond random sentinel");
    Check(!session.SelectMap(std::numeric_limits<std::size_t>::max()), "reject wrapped map index");
    Check(session.SelectedMap() == 0 && !session.Game(), "failed map selection has no simulation side effects");
    Check(!session.SelectCharacter(0), "map screen cannot change hidden character selection");
    Check(session.SelectMap(RandomMap), "random is a valid map choice");
    Check(session.Error().empty(), "successful choice clears errors");
}

void TestMenusNeverAccumulateTimeOrInput() {
    SessionModule session(QuietConfig()); Start(session);
    const Input stale{1, -1, true, true, true, true, 6};
    for (Screen screen : {Screen::Home, Screen::CharacterSelect, Screen::WeaponSelect, Screen::DifficultySelect, Screen::MapSelect}) {
        Check(session.Navigate(screen), "navigate through menus");
        if (screen == Screen::WeaponSelect) Check(session.SelectWeapon(WeaponKind::Wand), "select initial weapon");
        for (int frame = 0; frame < 20; ++frame) {
            session.Advance(20, stale);
            session.FixedTick(stale);
        }
        Check(!session.Game() && session.CurrentScreen() == screen, "menu input never creates or advances a game");
    }
    Check(session.StartRun(), "start after extended menus");
    Check(session.Game()->Stats().ticks == 0 && session.Game()->GetState() == State::Playing,
          "menu elapsed time and pause input do not carry into run");
    session.Advance(GameModule::FixedStep);
    Check(session.Game()->Stats().ticks == 1 && session.Game()->CurrentWeapon() == WeaponKind::Wand,
          "first frame advances once without buffered weapon input");
    Near(session.Game()->Get<Transform>(session.Game()->PlayerEntity()).position.x, 0,
         "menu movement does not carry into run");
}

void TestWeaponConfirmationLifecycle() {
    auto config = QuietConfig(); config.initialWeapon = WeaponKind::Gun;
    SessionModule session(config); Start(session);
    Check(session.SelectedWeapon() == WeaponKind::Gun && !session.HasSelectedWeapon(),
          "initial configuration supplies a preview without confirming the source selection");
    Check(session.Navigate(Screen::CharacterSelect) && session.Navigate(Screen::WeaponSelect), "open weapons");
    Check(!session.Navigate(Screen::DifficultySelect) && !session.StartRun() &&
          session.CurrentScreen() == Screen::WeaponSelect, "unconfirmed weapon cannot advance");
    Check(!session.SelectWeapon(WeaponKind::Count) && !session.HasSelectedWeapon(),
          "invalid selection cannot enable next");
    Check(session.SelectWeapon(WeaponKind::Burst) && session.HasSelectedWeapon(), "explicit selection enables next");
    Check(session.Navigate(Screen::DifficultySelect) && session.Navigate(Screen::WeaponSelect), "return to weapons");
    Check(session.HasSelectedWeapon() && session.SelectedWeapon() == WeaponKind::Burst &&
          session.Navigate(Screen::DifficultySelect), "back navigation retains confirmed choice");
    Check(session.Navigate(Screen::CharacterSelect) == false, "difficulty cannot skip back to character");
    Check(session.Navigate(Screen::Home) && !session.HasSelectedWeapon() &&
          session.SelectedWeapon() == WeaponKind::Burst, "home resets confirmation while retaining preview");
    Check(session.Navigate(Screen::CharacterSelect) && session.Navigate(Screen::WeaponSelect), "reopen weapons");
    Check(!session.Navigate(Screen::DifficultySelect), "fresh menu visit requires explicit selection again");
    Check(session.SelectWeapon(WeaponKind::Torch), "select torch");
    session.Shutdown(); Start(session);
    Check(!session.HasSelectedWeapon() && session.SelectedWeapon() == WeaponKind::Torch,
          "shutdown preserves preview but resets confirmation");
    Run(session);
    Check(session.ReturnHome() && !session.HasSelectedWeapon(), "returning from run resets confirmation");
}

void TestEveryStartingWeaponIsExclusive() {
    auto config = QuietConfig(); config.armed = true;
    SessionModule session(config); Start(session);
    for (std::size_t index = 0; index < WeaponCount; ++index) {
        const auto kind = static_cast<WeaponKind>(index);
        Check(session.Navigate(Screen::CharacterSelect) && session.Navigate(Screen::WeaponSelect), "open weapon menu");
        Check(session.SelectWeapon(WeaponKind::Wand) && session.SelectWeapon(kind), "replace previous weapon choice");
        Check(session.Navigate(Screen::DifficultySelect) && session.Navigate(Screen::MapSelect) && session.StartRun(),
              "selected weapon reaches a fresh game");
        Check(session.Game()->Settings().initialWeapon == kind && session.Game()->CurrentWeapon() == kind &&
              session.Game()->Get<Weapon>(session.Game()->WeaponEntity()).kind == kind &&
              session.Game()->Stats().weapons == 1, "each source choice equips exactly one matching weapon");
        Check(session.Game()->Stats().ticks == 0 && session.RunSerial() == index + 1,
              "selection commits one fresh run without simulating menu time");
        Check(session.ReturnHome() && session.SelectedWeapon() == kind && !session.HasSelectedWeapon(),
              "home remembers weapon preview and releases confirmation");
    }
}

void TestSelectedContentAndBaseRules() {
    auto config = QuietConfig(); config.initialWeapon = WeaponKind::Gun;
    config.initialHealth = 77; config.playerSpeed = 6;
    SessionModule session(config); Start(session);
    Check(session.Navigate(Screen::CharacterSelect), "open character selection");
    Check(session.SelectCharacter(Characters.size() - 1), "select last character");
    Check(session.Navigate(Screen::WeaponSelect) && session.SelectWeapon(WeaponKind::Burst), "select starting shotgun");
    Check(session.Navigate(Screen::DifficultySelect) && session.SelectDifficulty(0), "select source difficulty");
    Check(session.Navigate(Screen::MapSelect), "open map selection");
    Check(session.SelectMap(Maps.size() - 1) && session.StartRun(), "select last map and begin");
    const auto& settings = session.Game()->Settings();
    Check(settings.character == Characters.size() - 1 && settings.map == Maps.size() - 1,
          "new game receives selected content");
    Check(settings.initialHealth == 77 && settings.playerSpeed == 6 && !settings.spawning && !settings.armed,
          "selection preserves gameplay rules without invented character bonuses");
    Check(session.Game()->Get<Health>(session.Game()->PlayerEntity()).current == 77 && session.Game()->CurrentWeapon() == WeaponKind::Burst &&
          settings.initialWeapon == WeaponKind::Burst, "base health and selected weapon are applied");
    const auto& sprite = session.Game()->Get<Sprite>(session.Game()->PlayerEntity());
    Near(sprite.size.x, Characters.back().size.x, "selected character width reaches ECS sprite");
    Near(sprite.size.y, Characters.back().size.y, "selected character height reaches ECS sprite");
    const SessionModule& view = session;
    Check(view.Game()->Settings().character == settings.character, "const access exposes the running selection");
}

void TestRunCannotBeReenteredOrEdited() {
    SessionModule session(QuietConfig()); Start(session); Run(session);
    session.FixedTick({1, 0});
    auto* game = session.Game();
    const auto ticks = game->Stats().ticks;
    Check(session.Startup(), "repeated startup is harmless");
    Check(!session.StartRun() && !session.Navigate(Screen::Home) && !session.Navigate(Screen::Run),
          "running game cannot be replaced through navigation or start");
    Check(!session.SelectCharacter(0) && !session.SelectMap(0) && !session.SelectWeapon(WeaponKind::Burst) &&
          !session.SelectDifficulty(0), "run cannot edit menu selections");
    Check(session.Game() == game && game->Stats().ticks == ticks && session.CurrentScreen() == Screen::Run,
          "rejected run operations preserve simulation");
    Check(session.ReturnHome() && !session.Game(), "explicit return destroys simulation");
}

void TestReturnCreatesFreshRun() {
    auto config = QuietConfig(); config.initialWeapon = WeaponKind::Torch;
    config.character = Characters.size() - 1; config.map = Maps.size() - 1;
    SessionModule session(config); Start(session); Run(session);
    session.Game()->Get<Health>(session.Game()->PlayerEntity()).current = 7; PlayerData(session).materials = 91;
    PlayerData(session).level = 4; PlayerData(session).experience = 66;
    auto* game = session.Game();
    game->SpawnEnemy({3, 0}, true); game->SpawnPickup({4, 0}); game->SpawnProjectile({5, 0}, {1, 0});
    game->EquipWeapon(WeaponKind::Burst); session.FixedTick({1, 0});
    Check(session.ReturnHome() && !session.Game(), "return destroys a populated game");
    session.Advance(10, {1, 0}); session.FixedTick({1, 0});
    Run(session);
    const auto& stats = session.Game()->Stats();
    Check(stats.ticks == 0 && stats.wave == 1 && stats.kills == 0 && stats.shots == 0 &&
          stats.enemies == 0 && stats.projectiles == 0 && stats.pickups == 0 && stats.weapons == 1,
          "new run starts with only the player and equipped weapon");
    Check(session.Game()->Get<Health>(session.Game()->PlayerEntity()).current == config.initialHealth && PlayerData(session).materials == 0 &&
          PlayerData(session).level == 0 && PlayerData(session).experience == 0,
          "new run resets progression and health");
    Check(session.Game()->CurrentWeapon() == WeaponKind::Torch &&
          session.Game()->Settings().character == config.character && session.Game()->Settings().map == config.map,
          "menu choices persist while in-run weapon changes do not become starting configuration");
    Near(session.Game()->Get<Transform>(session.Game()->PlayerEntity()).position.x, 0, "fresh player position");
}

void TestReturnFromPausedDeadAndWaveComplete() {
    for (State state : {State::Paused, State::Dead, State::WaveComplete}) {
        auto config = QuietConfig(); config.initialHealth = config.contactDamage = 10;
        if (state == State::WaveComplete) config.waveSeconds = float(GameModule::FixedStep * 2);
        SessionModule session(config); Start(session); Run(session);
        if (state == State::Paused) session.Game()->SetPaused(true);
        else if (state == State::Dead) {
            session.Game()->SpawnEnemy(config.playerStart, true);
            session.FixedTick();
        } else for (int tick = 0; tick < 4; ++tick) session.FixedTick();
        Check(session.Game()->GetState() == state, "scenario reaches requested terminal or paused state");
        Check(session.ReturnHome() && !session.Game(), "all gameplay states can return home");
        Run(session);
        Check(session.Game()->GetState() == State::Playing && session.Game()->Stats().ticks == 0,
              "new run does not inherit paused, dead or completed state");
    }
}

void TestFailedStartIsTransactional() {
    auto config = QuietConfig(); config.initialHealth = 0;
    SessionModule session(config); Start(session); ToMap(session);
    Check(session.SelectMap(RandomMap), "choose random map before failed start");
    for (int attempt = 0; attempt < 3; ++attempt) {
        Check(!session.StartRun() && !session.Error().empty(), "invalid game configuration reports failure");
        Check(session.IsStarted() && session.CurrentScreen() == Screen::MapSelect && !session.Game(),
              "failed startup leaves the menu usable and owns no partial game");
        Check(session.SelectedMap() == RandomMap, "failed run preserves random selection");
    }
    Check(session.ReturnHome() && session.Error().empty(), "failed run can be cancelled");
}

void TestInitialSelectionValidation() {
    auto config = QuietConfig(); config.character = Characters.size();
    SessionModule badCharacter(config);
    Check(!badCharacter.Startup() && !badCharacter.Game() && !badCharacter.IsStarted(),
          "invalid initial character prevents session startup");
    config.character = 0; config.map = RandomMap + 1;
    SessionModule badMap(config);
    Check(!badMap.Startup() && !badMap.Game() && !badMap.IsStarted(),
          "invalid initial map prevents session startup");
    config.map = RandomMap; config.initialWeapon = WeaponKind::Count;
    SessionModule badWeapon(config);
    Check(!badWeapon.Startup() && !badWeapon.Game() && !badWeapon.IsStarted(), "invalid initial weapon prevents session startup");
    config.initialWeapon = WeaponKind::Wand;
    SessionModule randomMap(config); Start(randomMap);
    Check(randomMap.SelectedMap() == RandomMap, "random initial selection is accepted");
}

void TestShutdownRestartAndRepeatedRuns() {
    auto config = QuietConfig();
    SessionModule session(config); Start(session); ToMap(session);
    Check(session.SelectMap(Maps.size() - 1), "select map to retain");
    Check(session.StartRun(), "start before shutdown");
    session.Shutdown(); session.Shutdown();
    Check(!session.IsStarted() && !session.Game() && session.CurrentScreen() == Screen::Home,
          "shutdown is idempotent and releases simulation");
    Check(!session.StartRun() && !session.ReturnHome(), "shutdown session rejects run transitions");
    Start(session);
    Check(session.SelectedMap() == Maps.size() - 1 && !session.Game(), "startup retains selection but opens home");
    for (int run = 0; run < 24; ++run) {
        Run(session);
        session.Game()->SpawnEnemy({3, 0}, true);
        session.Game()->SpawnPickup({4, 0});
        session.Game()->SpawnProjectile({5, 0}, {1, 0});
        session.FixedTick();
        Check(session.ReturnHome() && session.ReturnHome() && !session.Game(), "repeated cleanup remains safe");
    }
}

void TestRandomMapsAreDeterministicAndReachable() {
    auto config = QuietConfig(); config.map = RandomMap;
    SessionModule first(config), second(config); Start(first); Start(second);
    std::vector<bool> seen(Maps.size(), false);
    for (int run = 0; run < 64; ++run) {
        Run(first); Run(second);
        const auto map = first.Game()->Settings().map;
        Check(map < Maps.size() && second.Game()->Settings().map == map,
              "same seed gives same valid random-map sequence");
        Check(first.SelectedMap() == RandomMap, "resolved run does not replace random menu choice");
        seen[map] = true;
        Check(!first.StartRun(), "rejected repeated start does not consume map randomness");
        Check(first.ReturnHome() && second.ReturnHome(), "return both random sessions");
        Check(first.Navigate(Screen::CharacterSelect) && first.Navigate(Screen::WeaponSelect) &&
              first.SelectWeapon(first.SelectedWeapon()) && first.Navigate(Screen::DifficultySelect) &&
              first.Navigate(Screen::MapSelect) && first.ReturnHome(),
              "extra menu visits do not consume map randomness");
    }
    for (bool reached : seen) Check(reached, "every source map is reachable by random selection");
}

void TestExplicitMapsDoNotConsumeRandomSequence() {
    auto config = QuietConfig(); config.map = RandomMap;
    SessionModule randomOnly(config), mixed(config); Start(randomOnly); Start(mixed);
    ToMap(mixed); Check(mixed.SelectMap(Maps.size() - 1) && mixed.StartRun(), "run one explicit map");
    Check(mixed.Game()->Settings().map == Maps.size() - 1, "explicit selection is exact");
    Check(mixed.ReturnHome(), "return from explicit map");
    ToMap(mixed); Check(mixed.SelectMap(RandomMap) && mixed.StartRun(), "switch to random selection");
    Run(randomOnly);
    Check(randomOnly.Game()->Settings().map == mixed.Game()->Settings().map,
          "explicit run does not consume the random-map stream");
}

void TestMapChoiceDoesNotPerturbCombatRandomness() {
    auto config = QuietConfig(); config.map = RandomMap; config.spawning = true;
    SessionModule session(config); Start(session); Run(session);
    config.map = session.Game()->Settings().map;
    GameModule direct(config);
    Check(direct.Startup(), "start direct comparison game");
    for (int tick = 0; tick < 240; ++tick) { session.FixedTick(); direct.FixedTick(); }
    const auto a = session.Game()->Extract(), b = direct.Extract();
    Check(a.size() == b.size() && session.Game()->Stats().enemies > 0,
          "comparison includes matching spawned enemies");
    for (std::size_t index = 0; index < a.size(); ++index) {
        Check(a[index].image == b[index].image, "random map choice preserves draw order and spawn phases");
        Near(a[index].position.x, b[index].position.x, "map choice preserves random spawn x");
        Near(a[index].position.y, b[index].position.y, "map choice preserves random spawn y");
    }
}

void TestRunIdentitySurvivesSceneRecreation() {
    SessionModule session(QuietConfig()); Start(session);
    Check(session.RunSerial() == 0 && !session.StartRun() && session.RunSerial() == 0,
          "menu and rejected starts must not allocate a run identity");
    Run(session);
    const auto first = session.RunSerial();
    Check(first > 0 && !session.StartRun() && session.RunSerial() == first, "reentry changed run identity");
    session.Game()->Restart();
    Check(session.RunSerial() == first, "in-scene restart changed session identity");
    Check(session.ReturnHome() && session.RunSerial() == first, "home must preserve monotonic identity");
    Run(session);
    Check(session.RunSerial() > first, "recreated game reused identity");
    const auto second = session.RunSerial();
    session.Shutdown(); Start(session); Run(session);
    Check(session.RunSerial() > second, "shutdown reset identity and could retain stale audio");
}

void TestIndependentSessions() {
    SessionModule first(QuietConfig()), second(QuietConfig()); Start(first); Start(second);
    Run(first); Run(second);
    first.FixedTick({1, 0}); first.Game()->SetPaused(true);
    Check(second.Game()->Stats().ticks == 0 && second.Game()->GetState() == State::Playing,
          "sessions own independent time and state");
    Check(first.ReturnHome() && !first.Game(), "first session releases its game");
    second.FixedTick({-1, 0});
    Check(second.Game()->Stats().ticks == 1 &&
          second.Game()->Get<Transform>(second.Game()->PlayerEntity()).position.x < 0,
          "destroying one session leaves the other ECS scene usable");
}

} // namespace

int main() {
    const std::vector<std::pair<const char*, std::function<void()>>> tests{
        {"startup without simulation", TestStartupOwnsNoSimulation},
        {"navigation and cancel", TestNavigationAndCancel},
        {"selection validation and screen gates", TestInvalidSelectionAndScreenGates},
        {"menus do not accumulate time or input", TestMenusNeverAccumulateTimeOrInput},
        {"explicit weapon confirmation lifecycle", TestWeaponConfirmationLifecycle},
        {"exclusive source starting weapons", TestEveryStartingWeaponIsExclusive},
        {"selected content and base rules", TestSelectedContentAndBaseRules},
        {"run reentry and selection guards", TestRunCannotBeReenteredOrEdited},
        {"fresh run after return", TestReturnCreatesFreshRun},
        {"return from paused dead and wave complete", TestReturnFromPausedDeadAndWaveComplete},
        {"transactional failed startup", TestFailedStartIsTransactional},
        {"initial selection validation", TestInitialSelectionValidation},
        {"shutdown restart and repeated runs", TestShutdownRestartAndRepeatedRuns},
        {"deterministic reachable random maps", TestRandomMapsAreDeterministicAndReachable},
        {"explicit maps preserve random stream", TestExplicitMapsDoNotConsumeRandomSequence},
        {"map choice preserves combat random stream", TestMapChoiceDoesNotPerturbCombatRandomness},
        {"independent sessions", TestIndependentSessions},
        {"run identity across scene recreation", TestRunIdentitySurvivesSceneRecreation},
    };
    int failed = 0;
    for (const auto& [name, test] : tests) {
        try { test(); std::cout << "[PASS] " << name << '\n'; }
        catch (const std::exception& error) { ++failed; std::cerr << "[FAIL] " << name << ": " << error.what() << '\n'; }
    }
    std::cout << (tests.size() - failed) << '/' << tests.size() << " Brotato session tests passed\n";
    return failed == 0 ? 0 : 1;
}
