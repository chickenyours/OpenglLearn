#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <glad/glad.h>
#include "ApplicationWindow/public/window.h"
#include "Brotato/Public/game_module.h"
#include "Brotato/Public/combat_geometry.h"
#include "Brotato/Public/expanded_gameplay.h"
#include "Brotato/Public/item_art_catalog.h"
#include "Brotato/Public/session_module.h"
#include "Brotato/Public/content_catalog.h"
#include "Brotato/Public/selection_catalog.h"
#include "Brotato/Public/menu_layout.h"
#include "Brotato/Public/map_presentation.h"
#include "Brotato/Public/game_audio.h"
#include "Render/Private/rhi_device.h"
#include "Render/Public/Sprite/sprite_batch.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace {
using namespace Brotato;
constexpr float ViewHalfHeight = MenuLayout::HalfHeight;
constexpr float ViewHalfWidth = MenuLayout::HalfWidth;
constexpr glm::vec4 White{.96f, .94f, .87f, 1};
constexpr glm::vec4 Muted{.65f, .69f, .65f, 1};
constexpr glm::vec4 Green{.62f, .84f, .30f, 1};
constexpr glm::vec4 Panel{.065f, .075f, .062f, .96f};
constexpr glm::vec4 ClearColor{.032f, .04f, .032f, 1};

void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

struct Options {
    std::filesystem::path assets, capture, captureAudio;
    bool smoke = false, smokeArena = false, mute = false, sourceRules = false, autoUpgrade = false;
    int smokeTicks = 720;
    int weapon = 1, character = 1, difficulty = 0;
    std::size_t map = RandomMap;
    std::string smokeMenu, smokePresentation, smokeGameplay;
};

template<class Character> Options Parse(int argc, Character** argv) {
    Options options;
    const std::filesystem::path executable = std::filesystem::absolute(argv[0]).parent_path();
    for (int i = 1; i < argc; ++i) {
        const std::string argument = std::filesystem::path(argv[i]).string();
        if (argument == "--smoke-test") options.smoke = true;
        else if (argument == "--mute") options.mute = true;
        else if (argument == "--source-rules") options.sourceRules = true;
        else if (argument == "--auto-upgrade") options.autoUpgrade = true;
        else if (argument == "--smoke-arena") options.smoke = options.smokeArena = true;
        else if (argument == "--assets" || argument == "--capture" || argument == "--smoke-ticks" || argument == "--weapon" ||
                 argument == "--character" || argument == "--difficulty" || argument == "--map" || argument == "--smoke-menu" ||
                 argument == "--smoke-presentation" || argument == "--capture-audio" || argument == "--smoke-gameplay") {
            Require(i + 1 < argc, "Missing value for " + argument);
            if (argument == "--assets") options.assets = std::filesystem::path(argv[++i]);
            else if (argument == "--capture") options.capture = std::filesystem::path(argv[++i]);
            else if (argument == "--capture-audio") options.captureAudio = std::filesystem::path(argv[++i]);
            else if (argument == "--smoke-gameplay") {
                options.smokeGameplay = std::filesystem::path(argv[++i]).string();
                constexpr std::array scenes{"enemies", "upgrade", "growth-cycle", "armored-hit", "ranged", "ranged-fire", "charge", "charge-dodge", "elite", "blast", "knockback", "shop", "shop-buy", "shop-lock", "shop-cycle", "build", "weapon-shop", "weapon-buy", "weapon-merge", "weapon-sell", "arsenal", "traits-shop", "traits-build", "families", "burn", "slow", "pierce", "lifesteal", "boss-fan", "boss-ring", "boss-charge", "boss-enrage", "boss-volley", "run-victory", "run-defeat", "boss-timeout", "run-restart", "run-home", "campaign", "campaign-focus", "encounter-swarm", "encounter-crossfire", "encounter-stampede", "encounter-reinforcements", "healer", "healer-pulse", "summoner", "summoner-pack", "champions"};
                Require(std::find(scenes.begin(), scenes.end(), options.smokeGameplay) != scenes.end(), "Unknown --smoke-gameplay scene");
                options.smoke = true;
            }
            else if (argument == "--smoke-presentation") {
                options.smokePresentation = std::filesystem::path(argv[++i]).string();
                constexpr std::array scenes{"idle", "move", "spawn", "death", "muzzle", "pause"};
                Require(std::find(scenes.begin(), scenes.end(), options.smokePresentation) != scenes.end(), "Unknown --smoke-presentation scene");
                options.smoke = true;
            }
            else if (argument == "--smoke-menu") {
                options.smokeMenu = std::filesystem::path(argv[++i]).string();
                constexpr std::array scenes{"home", "characters", "weapons", "weapon-unselected", "difficulty", "maps", "pause", "dead", "wave-complete", "return-home", "cycle"};
                Require(std::find(scenes.begin(), scenes.end(), options.smokeMenu) != scenes.end(), "Unknown --smoke-menu scene");
                options.smoke = true;
            }
            else {
                const std::string value = std::filesystem::path(argv[++i]).string();
                if (argument == "--map" && value == "random") { options.map = RandomMap; continue; }
                size_t consumed = 0;
                const int parsed = std::stoi(value, &consumed);
                if (argument == "--weapon") {
                    Require(consumed == value.size() && parsed >= 1 && parsed <= int(WeaponCount),
                        "--weapon must be an integer between 1 and 6");
                    options.weapon = parsed;
                } else if (argument == "--difficulty") {
                    Require(consumed == value.size() && parsed >= 0 && parsed < int(RunDifficulties.size()), "--difficulty must be 0, 1 or 2");
                    options.difficulty = parsed;
                } else if (argument == "--character" || argument == "--map") {
                    const auto count = argument == "--character" ? Characters.size() : Maps.size();
                    Require(consumed == value.size() && parsed >= 1 && std::size_t(parsed) <= count,
                        argument + " must be between 1 and " + std::to_string(count));
                    if (argument == "--character") options.character = parsed;
                    else options.map = std::size_t(parsed - 1);
                } else {
                    Require(consumed == value.size() && parsed >= 0 && parsed <= 120000,
                        "--smoke-ticks must be an integer between 0 and 120000");
                    options.smokeTicks = parsed;
                }
            }
        } else throw std::runtime_error("Usage: brotato_game [--source-rules] [--assets DIR] [--weapon 1..6] [--character 1..5] [--difficulty 0..2] [--map 1..5|random] [--mute] [--smoke-test | --smoke-arena | --smoke-menu SCENE | --smoke-presentation SCENE | --smoke-gameplay SCENE] [--auto-upgrade] [--smoke-ticks N] [--capture FILE.png] [--capture-audio FILE.wav]");
    }
    Require(options.smokeMenu.empty() || !options.smokeArena, "--smoke-menu and --smoke-arena cannot be combined");
    Require(options.smokePresentation.empty() || (options.smokeMenu.empty() && !options.smokeArena), "Choose only one smoke scenario");
    Require(options.captureAudio.empty() || options.smoke, "--capture-audio requires a smoke scenario");
    Require(!options.autoUpgrade || options.smoke, "--auto-upgrade requires a smoke scenario");
    Require(!options.autoUpgrade || (options.smokeGameplay.empty() && options.smokeMenu.empty() && options.smokePresentation.empty()), "--auto-upgrade requires a simulated wave or arena");
    Require(options.smokeGameplay.empty() || (!options.sourceRules && options.smokeMenu.empty() && options.smokePresentation.empty() && !options.smokeArena), "Gameplay scenes require expanded rules and no other fixed scenario");
    Require(!options.sourceRules || options.difficulty==0, "Source rules only support difficulty 0");
    if (options.assets.empty()) {
        for (const auto& candidate : {executable / "Brotato", executable.parent_path() / "Asset" / "Brotato",
                                     std::filesystem::current_path() / "Asset" / "Brotato"}) {
            if (std::filesystem::is_regular_file(candidate / "player.png")) { options.assets = candidate; break; }
        }
    }
    Require(!options.assets.empty(), "Brotato assets not found; supply --assets Asset/Brotato");
    options.assets = std::filesystem::absolute(options.assets);
    return options;
}

Config GameConfig(const Options& options) {
    Config config = options.sourceRules ? Config{} : ExpandedGameplay();
    config.initialWeapon = static_cast<WeaponKind>(options.weapon - 1);
    config.character = std::size_t(options.character - 1);
    config.difficulty = std::size_t(options.difficulty);
    config.map = options.map;
    // The complete campaign smoke uses the ordinary game configuration.
    if(options.smokeGameplay=="campaign" || options.smokeGameplay=="campaign-focus")return config;
    if(!options.smokeMenu.empty() || !options.smokePresentation.empty() || !options.smokeGameplay.empty()){config.campaign=false;config.encounters=false;}
    if (options.smokeArena || !options.smokeMenu.empty() || !options.smokePresentation.empty() || !options.smokeGameplay.empty()) {
        config.spawning = false;
        config.enemySpeed = 0;
    }
    if (!options.smokeMenu.empty()) config.armed = false;
    if (!options.smokePresentation.empty()) {
        config.armed = options.smokePresentation == "muzzle";
        if (config.armed) config.initialWeapon = WeaponKind::Gun;
    }
    if (!options.smokeGameplay.empty()) { config.armed = false; config.dropChance = 0; config.deathDelay = .05f; }
    if(options.smokeGameplay=="growth-cycle") config.builds=false;
    if(options.smokeGameplay.starts_with("shop") || options.smokeGameplay=="build")config.arsenal=false;
    if(options.smokeGameplay.starts_with("weapon-") || options.smokeGameplay=="arsenal"){config.waveSeconds=.05f;config.waveIncrement=0;config.traits=false;}
    if(options.smokeGameplay.starts_with("traits-") || options.smokeGameplay=="families"){config.waveSeconds=.05f;config.waveIncrement=0;}
    if(options.smokeGameplay=="arsenal"){config.armed=true;config.waveSeconds=.5f;}
    if(options.smokeGameplay.starts_with("shop") || options.smokeGameplay=="build") {config.waveSeconds=.05f;config.waveIncrement=0;}
    if(options.smokeGameplay.starts_with("run-") || options.smokeGameplay=="boss-timeout") {
        config.campaign=true;config.campaignWaves=1;config.waveSeconds=.1f;config.waveIncrement=0;config.spawning=true;config.spawnWarning=0;config.deathDelay=.025f;
    }
    if(options.smokeGameplay.starts_with("boss-") || options.smokeGameplay.starts_with("run-"))config.contactDamage=0;
    if(options.smokeGameplay.starts_with("encounter-")){config.encounters=true;config.spawning=true;config.contactDamage=0;config.enemySpeed=2.3f;}
    if(options.smokeGameplay=="champions")config.encounters=true;
    return config;
}

const char* ImageName(Image image, std::size_t character) {
    switch (image) {
    case Image::Player: return Characters.at(character).image;
    case Image::Enemy: return "enemy";
    case Image::EnemyFast: return "enemy_fast";
    case Image::EnemyArmored: return "enemy_armored";
    case Image::EnemyRanged: return "enemy_ranged";
    case Image::EnemyCharger: return "enemy_charger";
    case Image::EnemyElite: return "enemy_elite";
    case Image::EnemyBoss: return "enemy_boss";
    case Image::EnemyHealer: return "enemy_healer";
    case Image::EnemySummoner: return "enemy_summoner";
    case Image::EnemyProjectile: return "bullet";
    case Image::Projectile: return "bullet";
    case Image::Material: return "material";
    case Image::Spawn: return "spawn";
    case Image::Weapon: return "weapon";
    case Image::Torch: return "weapon_torch";
    case Image::LaserWeapon: return "weapon_laser";
    case Image::Knife: return "weapon_knife";
    case Image::Gun: return "weapon_gun";
    case Image::BurstWeapon: return "weapon_burst";
    case Image::GunProjectile: return "projectile_gun";
    case Image::BurstProjectile: return "projectile_burst";
    case Image::LaserSegment: return "laser_segment";
    case Image::Muzzle: return "muzzle_flash";
    case Image::PlayerBody: return "player_shadow";
    case Image::PlayerLegLeft: return "player_leg_left";
    case Image::PlayerLegRight: return "player_leg_right";
    case Image::PlayerShadow: return "player_shadow";
    case Image::PlayerMark: return "player_mark";
    case Image::HitParticle: return "hit_particle";
    }
    throw std::runtime_error("Unknown extracted sprite image");
}

const char* StateName(State state) {
    switch (state) {
    case State::Playing: return "SURVIVE";
    case State::Paused: return "PAUSED";
    case State::WaveComplete: return "WAVE CLEARED";
    case State::Dead: return "RUN ENDED";
    case State::LevelUp: return "LEVEL UP";
    case State::Shop: return "WAVE SHOP";
    case State::Victory: return "VICTORY";
    case State::Defeat: return "DEFEAT";
    }
    return "";
}

int DisplaySeconds(double remaining) {
    // Fixed-step subtraction may leave 14.00000000000034 after six seconds.
    return int(std::ceil(std::max(0.0, remaining - 1e-7)));
}
glm::vec4 DifficultyColor(std::size_t difficulty) {
    return difficulty==0?Green:difficulty==1?glm::vec4(1,.75f,.24f,1):glm::vec4(1,.35f,.25f,1);
}

struct Completion { bool done = false; };
struct Pixels : Completion {
    int width = 0, height = 0;
    std::vector<unsigned char> rgba;
    GLenum error = GL_NO_ERROR;
};

enum class Action { ChooseCharacter, ChooseWeapon, ChooseDifficulty, ChooseMap, Back, Forward, PreviousPage, NextPage, Resume, Restart, NextWave, Revive, Home, Exit, ChooseUpgrade, BuyItem, LockItem, RerollShop, ShowStock, ShowEquipment, FocusEquipment, MergeWeapon, SellWeapon };
struct MenuButton {
    MenuLayout::Rect rect;
    std::string label;
    Action action;
    std::size_t value = 0;
    bool selected = false;
    bool enabled = true;
};

class Application {
public:
    Application(ApplicationWindow::Window& window, const Options& options) : window_(window), device_(new Render::RHIDevice),
        batch_(device_.GenWeakPtr()), session_(GameConfig(options)), gameAudio_(audio_.GetMixer()) {}
    ~Application() {
        // Every asynchronous lambda captures owned shared state. Join the render
        // thread while the window and any in-progress readback are still alive.
        batch_.Shutdown();
        device_->StopAndRelease();
        while (device_->returnSystem.DrainCallbacks(4096)) {}
        session_.Shutdown();
        gameAudio_.Shutdown();
        audio_.Shutdown();
    }

    void Start(const Options& options) {
        const auto context = window_.GetRenderContextAsOpengl();
        Require(context.has_value(), "Missing OpenGL render context");
        device_->Run(Render::BackendType::Opengl, *context);
        std::vector<Render::SpriteImage> images;
        std::set<std::string> imageNames;
        const auto addImage = [&](const char* name) {
            if (imageNames.insert(name).second) images.push_back({name, options.assets / (std::string(name) + ".png")});
        };
        for (const char* name : {"player", "enemy", "weapon", "bullet", "material", "floor", "spawn",
            "weapon_torch", "weapon_laser", "weapon_knife", "weapon_gun", "weapon_burst",
            "projectile_gun", "projectile_burst", "laser_segment", "muzzle_flash",
            "player_leg_left", "player_leg_right", "player_shadow", "player_mark", "hit_particle", "enemy_fast", "enemy_armored", "enemy_ranged", "enemy_charger", "enemy_elite", "enemy_boss", "enemy_healer", "enemy_summoner"})
            addImage(name);
        for (const auto& character : Characters) addImage(character.image);
        for(const auto& item:ItemDefinitions) addImage(item.image);
        for (const auto& weapon : WeaponSelections) addImage(weapon.image);
        for (const auto& difficulty : Difficulties) addImage(difficulty.image);
        AppendMapImages(images, options.assets);
        Require(batch_.Initialize(images), batch_.Error());
        Wait([&] { return batch_.Ready() || !batch_.Error().empty(); }, "sprite initialization");
        Require(batch_.Ready(), batch_.Error());
        Require(session_.Startup(), session_.Error());
        Require(gameAudio_.Load(options.assets), gameAudio_.Error());
        muted_ = options.mute;
        autoUpgrade_ = options.autoUpgrade;
        expanded_ = !options.sourceRules;
        audio_.GetMixer().SetVolume(muted_ ? 0.f : 1.f);
        if (!options.smoke && !audio_.Startup())
            std::cerr << "Audio output unavailable; continuing without device: " << audio_.Error() << '\n';
        if (!options.captureAudio.empty()) {
            if (!options.captureAudio.parent_path().empty()) std::filesystem::create_directories(options.captureAudio.parent_path());
            audioCapture_.open(options.captureAudio, std::ios::binary);
            Require(bool(audioCapture_), "Cannot open audio capture output");
            std::array<char, 44> header{};
            audioCapture_.write(header.data(), header.size());
        }
        if (options.smoke && options.smokeMenu.empty()) BeginRun(options);
        if (options.smokeArena) {
            // An explicitly requested deterministic weapon showcase. Ordinary
            // --smoke-test still uses the complete seeded survival simulation.
            const auto target = Game().SpawnEnemy(Game().Settings().playerStart + glm::vec2(2.5f, 0), true);
            Require(Game().Scene()->IsAlive(target), "Weapon smoke arena could not spawn its target");
        }
    }

    void Smoke(const Options& options) {
        if (!options.smokeMenu.empty()) PrepareMenuSmoke(options);
        else if (!options.smokePresentation.empty()) PreparePresentationSmoke(options.smokePresentation);
        else if (!options.smokeGameplay.empty()) PrepareGameplaySmoke(options.smokeGameplay);
        else for (int tick = 0; tick < options.smokeTicks; ++tick) SmokeTick();
        if (!options.smokeMenu.empty() || options.smokeTicks == 0)
            for (int tick = 0; tick < 60; ++tick) PumpOfflineAudio();
        FinishAudioCapture();
        int width = 0, height = 0;
        glfwGetFramebufferSize(window_.GetNativeWindow(), &width, &height);
        Require(width > 0 && height > 0, "Smoke framebuffer has no pixels");
        const auto sprites = RenderFrame(width, height, false);
        auto pixels = Capture(width, height);
        if (session_.Game()) Require(sprites > 0, "Smoke extraction produced no sprites");
        size_t visible = 0;
        // Dark maps under a modal overlay are still valid content. Measure
        // distance from our clear color instead of requiring bright pixels.
        for (size_t i = 0; i < pixels->rgba.size(); i += 4) {
            bool foreground = false;
            for (int channel = 0; channel < 3; ++channel)
                foreground |= std::abs(int(pixels->rgba[i + channel]) - int(std::lround(ClearColor[channel] * 255))) > 12;
            if (foreground) ++visible;
        }
        Save(*pixels, options.capture);
        const bool playing = session_.Game() && Game().GetState() == State::Playing;
        Require(visible > size_t(width) * height / (playing ? 5 : 12),
            "Smoke framebuffer is blank: visiblePixels=" + std::to_string(visible));
        std::cout << "Brotato OpenGL smoke passed: scenario=" << (!options.smokeGameplay.empty() ? options.smokeGameplay : !options.smokePresentation.empty() ? options.smokePresentation : !options.smokeMenu.empty() ? options.smokeMenu : options.smokeArena ? "weapon-arena" : "seeded-survival");
        if (session_.Game()) {
            const auto& stats = Game().Stats();
            const auto& player = Game().Get<Player>(Game().PlayerEntity());
            Require(Game().DroppedEventCount() == 0, "Application failed to drain gameplay presentation events");
            std::cout << " weapon=" << WeaponIndex(Game().CurrentWeapon()) + 1 << " character=" << Game().Settings().character + 1
                      << " map=" << Game().Settings().map + 1 << " difficulty=" << Game().ExtractRunSetup().difficulty << " ticks=" << stats.ticks
                      << " entities=" << 1 + stats.weapons + stats.enemies + stats.projectiles + stats.pickups + stats.effects + stats.hostileProjectiles + stats.ownedItems + stats.statuses
                      << " wave=" << stats.wave << " enemies=" << stats.enemies
                      << " shots=" << stats.shots << " kills=" << stats.kills
                      << " effects=" << stats.effects
                      << " materials=" << player.materials << " hp=" << Game().Get<Health>(Game().PlayerEntity()).current
                      << " level=" << player.level << " xp=" << player.experience
                      << " hits=" << stats.hits << " mode=" << (Game().Settings().builds ? "build" : Game().Settings().expanded ? "growth" : "source")
                      << " itemStacks=" << stats.ownedItems
                      << " statuses=" << stats.statuses << " burnHits=" << stats.burnHits << " piercedHits=" << stats.piercedHits << " crit=" << stats.criticalAttacks << " stolenHP=" << stats.stolenHealth
                      << " bosses=" << stats.bossSpawns << " bossKills=" << stats.bossKills << " bossAttacks=" << stats.bossAttacks
                      << " champions=" << stats.championSpawns << " healed=" << stats.healedEnemies << " summoned=" << stats.summonedEnemies
                      << " spawned=" << stats.spawned[0] << ',' << stats.spawned[1] << ',' << stats.spawned[2] << ',' << stats.spawned[3] << ',' << stats.spawned[4] << ',' << stats.spawned[5] << ',' << stats.spawned[6] << ',' << stats.spawned[7]
                      << " hostiles=" << stats.hostileProjectiles << " volleys=" << stats.enemyVolleys << " charges=" << stats.charges << " splashHits=" << stats.splashHits
                      << " pending=" << Game().Get<Growth>(Game().PlayerEntity()).pending
                      << " state=" << StateName(Game().GetState());
        } else std::cout << " activeGame=0";
        if (session_.Game()) std::cout << " drawSprites=" << sprites;
        else std::cout << " menuButtons=" << Buttons().size();
        std::cout << " batchDraws=1 visiblePixels=" << visible << " audioPeak=" << audioPeak_ << " GL errors=0\n";
    }

    void Interactive(const Options& options) {
        std::cout << "Brotato: click or arrows + Enter to choose | Esc back\n"
                     "In game: WASD/arrows move | 1-6 weapon | P/Esc pause | R restart\n"
                     "Enter next wave | V revive after defeat | H return home from an overlay | M mute\n";
        auto* native = window_.GetNativeWindow();
        std::array<bool, GLFW_KEY_LAST + 1> previous{}, keys{}, pressed{};
        bool previousMouse = false, captured = false;
        double lastTime = glfwGetTime(), titleTime = -1;
        while (!window_.ShouldClose() && !exitRequested_) {
            window_.PollEvents();
            const bool focused = glfwGetWindowAttrib(native, GLFW_FOCUSED) != 0;
            for (int key = GLFW_KEY_SPACE; key <= GLFW_KEY_LAST; ++key) {
                keys[key] = focused && glfwGetKey(native, key) == GLFW_PRESS;
                pressed[key] = keys[key] && !previous[key];
                previous[key] = keys[key];
            }
            int width = 0, height = 0, windowWidth = 0, windowHeight = 0;
            glfwGetFramebufferSize(native, &width, &height);
            glfwGetWindowSize(native, &windowWidth, &windowHeight);
            double cursorX = 0, cursorY = 0;
            glfwGetCursorPos(native, &cursorX, &cursorY);
            pointer_ = focused ? MenuLayout::CursorToCanvas(cursorX, cursorY, windowWidth, windowHeight, width, height) : std::nullopt;
            const bool mouse = focused && glfwGetMouseButton(native, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
            const bool click = mouse && !previousMouse;
            previousMouse = mouse;
            const double now = glfwGetTime(), elapsed = std::clamp(now - lastTime, 0.0, .1);
            lastTime = now;
            if (pressed[GLFW_KEY_M]) {
                muted_ = !muted_;
                audio_.GetMixer().SetVolume(muted_ ? 0.f : 1.f);
            }
            const auto screenAtFrameStart = session_.CurrentScreen();
            const bool frozenAtFrameStart=session_.Game() && Game().GetState()!=State::Playing;
            const bool handledPointerAction = click && pointer_ && ActivateAt(*pointer_);
            Input input;
            if (!handledPointerAction && session_.CurrentScreen() == Screen::Run) {
                if (pressed[GLFW_KEY_H] && Game().GetState() != State::Playing) session_.ReturnHome();
                else if (pressed[GLFW_KEY_ESCAPE] && Game().GetState() != State::LevelUp) {
                    if (Game().GetState() == State::Playing || Game().GetState() == State::Paused) input.pause = true;
                    else session_.ReturnHome();
                }
                if (session_.Game()) {
                    input.horizontal = float(keys[GLFW_KEY_D] || keys[GLFW_KEY_RIGHT]) - float(keys[GLFW_KEY_A] || keys[GLFW_KEY_LEFT]);
                    input.vertical = float(keys[GLFW_KEY_W] || keys[GLFW_KEY_UP]) - float(keys[GLFW_KEY_S] || keys[GLFW_KEY_DOWN]);
                    input.pause = input.pause || pressed[GLFW_KEY_P];
                    input.restart = pressed[GLFW_KEY_R];
                    input.nextWave = pressed[GLFW_KEY_ENTER];
                    if(Game().GetState()==State::Victory || Game().GetState()==State::Defeat){input.restart=input.restart || input.nextWave;input.nextWave=false;}
                    input.revive = pressed[GLFW_KEY_V];
                    if (Game().GetState() == State::LevelUp) {
                        if (pressed[GLFW_KEY_LEFT] || pressed[GLFW_KEY_A]) upgradeFocus_ = std::max(0, upgradeFocus_ - 1);
                        if (pressed[GLFW_KEY_RIGHT] || pressed[GLFW_KEY_D]) upgradeFocus_ = std::min(2, upgradeFocus_ + 1);
                        if (pressed[GLFW_KEY_ENTER]) input.chooseUpgrade = upgradeFocus_ + 1;
                        for (int index = 0; index < 3; ++index)
                            if (pressed[GLFW_KEY_1 + index]) input.chooseUpgrade = index + 1;
                    } else if(Game().GetState()==State::Shop) {
                        if(pressed[GLFW_KEY_LEFT] || pressed[GLFW_KEY_A]) shopFocus_=std::max(0,shopFocus_-1);
                        if(pressed[GLFW_KEY_RIGHT] || pressed[GLFW_KEY_D]) shopFocus_=std::min(int(ShopSlots)-1,shopFocus_+1);
                        if(pressed[GLFW_KEY_SPACE]) input.buySlot=shopFocus_+1;
                        for(int index=0;index<int(ShopSlots);++index) if(pressed[GLFW_KEY_1+index]) {
                            if(keys[GLFW_KEY_LEFT_SHIFT] || keys[GLFW_KEY_RIGHT_SHIFT]) input.lockSlot=index+1;
                            else input.buySlot=index+1;
                        }
                        input.rerollShop=pressed[GLFW_KEY_F];
                        if(Game().Settings().arsenal && Game().Settings().builds) {
                            if(pressed[GLFW_KEY_TAB])equipmentView_=!equipmentView_;
                            if(equipmentView_) {
                                input.buySlot=input.lockSlot=0;input.rerollShop=false;
                                if(pressed[GLFW_KEY_LEFT] || pressed[GLFW_KEY_A])equipmentFocus_=std::max(0,equipmentFocus_-1);
                                if(pressed[GLFW_KEY_RIGHT] || pressed[GLFW_KEY_D])equipmentFocus_=std::min(int(EquipmentSlots)-1,equipmentFocus_+1);
                                for(int i=0;i<int(EquipmentSlots);++i)if(pressed[GLFW_KEY_1+i])equipmentFocus_=i;
                                if(pressed[GLFW_KEY_U])input.mergeSlot=equipmentFocus_+1;
                                if(pressed[GLFW_KEY_X])input.sellSlot=equipmentFocus_+1;
                            }
                        }
                    } else {
                        for (int index = 0; index < int(WeaponCount); ++index)
                            if (pressed[GLFW_KEY_1 + index]) input.selectWeapon = index + 1;
                    }
                }
            } else if (!handledPointerAction) {
                if (pressed[GLFW_KEY_ESCAPE]) Activate({{}, {}, Action::Back});
                else if (pressed[GLFW_KEY_ENTER]) Activate({{}, {}, Action::Forward});
                else if (pressed[GLFW_KEY_SPACE] && session_.CurrentScreen() == Screen::WeaponSelect)
                    Activate({{}, {}, Action::ChooseWeapon, WeaponIndex(session_.SelectedWeapon())});
                else {
                    if (pressed[GLFW_KEY_LEFT] || pressed[GLFW_KEY_A]) MoveSelection(-1);
                    if (pressed[GLFW_KEY_RIGHT] || pressed[GLFW_KEY_D]) MoveSelection(1);
                    if (pressed[GLFW_KEY_UP] || pressed[GLFW_KEY_W]) MoveSelection(-int(MenuLayout::Columns));
                    if (pressed[GLFW_KEY_DOWN] || pressed[GLFW_KEY_S]) MoveSelection(int(MenuLayout::Columns));
                    if (pressed[GLFW_KEY_PAGE_UP]) MoveSelection(-int(MenuLayout::PageSize));
                    if (pressed[GLFW_KEY_PAGE_DOWN]) MoveSelection(int(MenuLayout::PageSize));
                }
            }
            // Menu time belongs to the old screen; a newly started run begins
            // at tick zero even if loading or a slow frame preceded its click.
            session_.Advance(session_.CurrentScreen() == screenAtFrameStart && !frozenAtFrameStart ? elapsed : 0.0, input);
            gameAudio_.Sync(session_.Game(), session_.RunSerial());
            if (!audio_.IsStarted()) {
                // Keep voice cursors tied to elapsed time even without a device.
                fallbackAudioFrames_ += elapsed * 48000;
                const auto frames = static_cast<std::size_t>(fallbackAudioFrames_);
                fallbackAudioFrames_ -= double(frames);
                std::array<float, 9600> discarded{};
                audio_.GetMixer().Render(std::span<float>(discarded.data(), frames * 2));
            }
            if (width < 1 || height < 1) { glfwWaitEventsTimeout(.05); continue; }
            if (!captured && !options.capture.empty()) {
                RenderFrame(width, height, false);
                Save(*Capture(width, height), options.capture);
                captured = true;
            }
            RenderFrame(width, height, true);
            if (now - titleTime > .25) {
                std::ostringstream title;
                title << "Brotato | " << (expanded_ ? "Survival + Growth" : "Source rules") << (muted_ ? " | MUTED" : "");
                if (session_.Game()) title << " | " << StateName(Game().GetState())
                    << " | " << Characters.at(Game().Settings().character).name
                    << " | " << Maps.at(Game().Settings().map).name
                    << " | Wave " << Game().Stats().wave << " | " << DisplaySeconds(Game().Stats().remaining) << "s";
                else title << " | " << MenuTitle(session_.CurrentScreen());
                glfwSetWindowTitle(native, title.str().c_str());
                titleTime = now;
            }
        }
    }

private:
    ApplicationWindow::Window& window_;
    ObjectPtr<Render::RHIDevice> device_;
    Render::SpriteBatch2D batch_;
    SessionModule session_;
    Audio::AudioModule audio_;
    GameAudio gameAudio_;
    std::ofstream audioCapture_;
    std::uint32_t audioBytes_ = 0;
    float audioPeak_ = 0;
    double fallbackAudioFrames_ = 0;
    bool muted_ = false;
    bool autoUpgrade_ = false, expanded_ = true;
    int upgradeFocus_ = 0;
    int shopFocus_ = 0;
    int equipmentFocus_ = 0;
    bool equipmentView_ = false;
    std::optional<glm::vec2> pointer_;
    bool exitRequested_ = false;
    uint64_t frameIndex_ = 0;

    static const char* MenuTitle(Screen screen) {
        switch (screen) {
        case Screen::Home: return "HOME";
        case Screen::CharacterSelect: return "CHOOSE YOUR CHARACTER";
        case Screen::WeaponSelect: return "CHOOSE YOUR WEAPON";
        case Screen::DifficultySelect: return "CHOOSE YOUR DIFFICULTY";
        case Screen::MapSelect: return "CHOOSE YOUR MAP";
        case Screen::Run: return "RUN";
        }
        return "";
    }

    std::size_t ChoiceCount(Screen screen) const {
        switch (screen) {
        case Screen::CharacterSelect: return Characters.size();
        case Screen::WeaponSelect: return WeaponCount;
        case Screen::DifficultySelect: return session_.DifficultyCount();
        case Screen::MapSelect: return Maps.size() + 1;
        default: return 0;
        }
    }

    std::size_t SelectedChoice(Screen screen) const {
        switch (screen) {
        case Screen::CharacterSelect: return session_.SelectedCharacter();
        case Screen::WeaponSelect: return WeaponIndex(session_.SelectedWeapon());
        case Screen::DifficultySelect: return session_.SelectedDifficulty();
        case Screen::MapSelect: return session_.SelectedMap();
        default: return 0;
        }
    }

    void SelectChoice(Screen screen, std::size_t choice) {
        switch (screen) {
        case Screen::CharacterSelect: Require(session_.SelectCharacter(choice), session_.Error()); break;
        case Screen::WeaponSelect: Require(session_.SelectWeapon(WeaponKind(choice)), session_.Error()); break;
        case Screen::DifficultySelect: Require(session_.SelectDifficulty(choice), session_.Error()); break;
        case Screen::MapSelect: Require(session_.SelectMap(choice), session_.Error()); break;
        default: break;
        }
    }

    GameModule& Game() {
        Require(session_.Game() != nullptr, "No active Brotato run");
        return *session_.Game();
    }

    void PumpOfflineAudio() {
        gameAudio_.Sync(session_.Game(), session_.RunSerial());
        std::array<float, 800> samples{}; // 400 stereo frames = one 120 Hz tick.
        audio_.GetMixer().Render(samples);
        std::array<char, 1600> pcm{};
        for (std::size_t index = 0; index < samples.size(); ++index) {
            Require(std::isfinite(samples[index]), "Mixer produced nonfinite audio");
            audioPeak_ = std::max(audioPeak_, std::abs(samples[index]));
            const auto value = static_cast<std::uint16_t>(static_cast<std::int16_t>(
                std::lround(std::clamp(samples[index], -1.f, 1.f) * 32767.f)));
            pcm[index * 2] = char(value & 255);
            pcm[index * 2 + 1] = char(value >> 8);
        }
        if (audioCapture_.is_open()) {
            audioCapture_.write(pcm.data(), pcm.size());
            audioBytes_ += std::uint32_t(pcm.size());
            Require(bool(audioCapture_), "Failed to write audio capture");
        }
    }

    void FinishAudioCapture() {
        if (!audioCapture_.is_open()) return;
        audioCapture_.seekp(0);
        const auto integer = [&](std::uint32_t value, int bytes) {
            for (int byte = 0; byte < bytes; ++byte) audioCapture_.put(char((value >> (byte * 8)) & 255));
        };
        audioCapture_.write("RIFF", 4); integer(audioBytes_ + 36, 4);
        audioCapture_.write("WAVEfmt ", 8); integer(16, 4);
        integer(1, 2); integer(2, 2); integer(48000, 4); integer(192000, 4);
        integer(4, 2); integer(16, 2); audioCapture_.write("data", 4); integer(audioBytes_, 4);
        audioCapture_.flush();
        Require(bool(audioCapture_), "Failed to finish audio capture");
        audioCapture_.close();
    }

    void SmokeTick(Input input = {}) {
        if (autoUpgrade_ && session_.Game() && Game().GetState() == State::LevelUp)
            Require(Game().SelectUpgrade(Game().Get<Growth>(Game().PlayerEntity()).choices[std::size_t((Game().Get<Player>(Game().PlayerEntity()).level-1)%3)]), "Automatic smoke upgrade failed");
        session_.FixedTick(input); PumpOfflineAudio();
    }

    void PrepareGameplaySmoke(const std::string& scene) {
        const auto origin = Game().Settings().playerStart;
        if(scene.starts_with("encounter-")){
            auto& director=Game().Get<SpawnDirector>(Game().PlayerEntity());
            director.wave=scene=="encounter-swarm"?1:scene=="encounter-crossfire"?2:scene=="encounter-stampede"?3:4;
            for(int i=0;i<1020;++i)SmokeTick();
            Require(Game().ExtractEncounter().events==1 && Game().ExtractEncounter().event!=EncounterEvent::None,"Encounter event missing");
            Require(Game().Stats().enemies>5 && Game().Stats().kills==0,"Encounter showcase did not spawn packs");return;
        }
        if(scene=="healer" || scene=="healer-pulse" || scene=="summoner" || scene=="summoner-pack"){
            const bool healer=scene.starts_with("healer");
            const auto support=Game().SpawnEnemy(origin+glm::vec2(5,0),true,healer?EnemyKind::Healer:EnemyKind::Summoner);
            Game().Get<EnemyBrain>(support).cooldown=0;
            if(healer){auto ally=Game().SpawnEnemy(origin+glm::vec2(5,1.5f),true,EnemyKind::Armored);Game().Get<Health>(ally).maximum=20;Game().Get<Health>(ally).current=5;}
            for(int i=0;i<(scene=="healer-pulse"?100:scene=="summoner-pack"?150:20);++i)SmokeTick();
            if(scene=="healer-pulse")Require(Game().Stats().healedEnemies==1,"Support did not restore ally health");
            else if(scene=="summoner-pack")Require(Game().Stats().summonedEnemies==2,"Summoner did not create two minions");
            else Require(!Game().ExtractCombatCues().empty(),"Support warning missing");return;
        }
        if(scene=="champions"){
            Game().SpawnEnemy(origin+glm::vec2(-3,0),true,EnemyKind::Fast,ChampionKind::Swift);
            Game().SpawnEnemy(origin+glm::vec2(3,0),true,EnemyKind::Armored,ChampionKind::Bulwark);
            for(int i=0;i<12;++i)SmokeTick();Require(Game().Stats().championSpawns==2,"Champion modifiers missing");return;
        }
        if(scene=="campaign" || scene=="campaign-focus") {
            const auto& config=Game().Settings();
            const glm::vec2 route[]{{config.maximum.x-2,config.maximum.y-2},{config.minimum.x+2,config.maximum.y-2},{config.minimum.x+2,config.minimum.y+2},{config.maximum.x-2,config.minimum.y+2}};
            std::size_t waypoint=0;
            for(int wave=1;wave<=config.campaignWaves;++wave) {
                int limit=14000;
                while(limit-->0 && (Game().GetState()==State::Playing || Game().GetState()==State::LevelUp)) {
                    if(Game().GetState()==State::LevelUp) {
                        const auto choices=Game().Get<Growth>(Game().PlayerEntity()).choices;auto chosen=choices[0];
                        for(auto preferred:{UpgradeKind::Damage,UpgradeKind::Armor,UpgradeKind::MaxHealth,UpgradeKind::AttackSpeed,UpgradeKind::MoveSpeed,UpgradeKind::PickupRange})
                            if(std::find(choices.begin(),choices.end(),preferred)!=choices.end()){chosen=preferred;break;}
                        Require(Game().SelectUpgrade(chosen),"Campaign offered upgrade failed");continue;
                    }
                    auto delta=route[waypoint]-Game().Get<Transform>(Game().PlayerEntity()).position;
                    if(glm::length(delta)<.35f){waypoint=(waypoint+1)%std::size(route);delta=route[waypoint]-Game().Get<Transform>(Game().PlayerEntity()).position;}
                    auto direction=glm::normalize(delta);
                    if(scene=="campaign-focus") {
                        glm::vec2 bossPosition{};bool found=false;
                        {
                            Query<BossBrain,Enemy,Transform> bosses;bosses.Refresh(*Game().Scene());
                            for(auto chunk:bosses)for(std::size_t row=0;row<chunk.count;++row)
                                if(chunk.Get<BossBrain>()[row].objective && chunk.Get<Enemy>()[row].phase==EnemyPhase::Alive){bossPosition=chunk.Get<Transform>()[row].position;found=true;}
                        }
                        if(found) {
                            const auto position=Game().Get<Transform>(Game().PlayerEntity()).position;
                            const auto radial=position-bossPosition;const float distance=glm::length(radial);
                            const auto outward=distance>1e-6f?radial/distance:glm::vec2(1,0);
                            const auto tangent=glm::vec2(-outward.y,outward.x);
                            const auto goal=glm::clamp(bossPosition+outward*3.8f+tangent*1.2f,config.minimum+glm::vec2(.6f),config.maximum-glm::vec2(.6f));
                            const auto toward=goal-position;if(glm::length(toward)>1e-6f)direction=glm::normalize(toward);
                        }
                    }
                    SmokeTick({direction.x,direction.y});
                }
                std::cout<<"Campaign wave="<<wave<<" state="<<StateName(Game().GetState())<<" hp="<<Game().ExtractBuild().health<<" kills="<<Game().Stats().kills<<" bossKills="<<Game().Stats().bossKills<<'\n';
                if(wave==config.campaignWaves){Require(Game().GetState()==State::Victory && Game().Stats().bossKills==2,"Ordinary six-wave route did not win");break;}
                Require(Game().GetState()==State::Shop,"Campaign route did not reach shop");
                for(int roll=0;roll<4;++roll) {
                    const auto stock=Game().ExtractShop().shop.offers;bool bought=false;
                    if(Game().ExtractEquipment().count<EquipmentSlots)
                        for(auto preferred:{WeaponKind::Gun,WeaponKind::Burst,WeaponKind::Wand,WeaponKind::Laser,WeaponKind::Torch,WeaponKind::Knife})
                            for(std::size_t i=0;i<ShopSlots;++i)if(stock[i].type==OfferType::Weapon && stock[i].weapon==preferred && Game().BuyShopOffer(i)==ShopResult::Bought)bought=true;
                    for(auto preferred:{ItemKind::Plant,ItemKind::Vest,ItemKind::Cake,ItemKind::Lens,ItemKind::Coffee,ItemKind::Sausage,ItemKind::Bat,ItemKind::Sunglasses,ItemKind::Bandana,ItemKind::Beanie})
                        for(std::size_t i=0;i<ShopSlots;++i)if(stock[i].type==OfferType::Item && stock[i].kind==preferred && Game().BuyShopOffer(i)==ShopResult::Bought)bought=true;
                    if(!bought || Game().ExtractBuild().materials<12 || Game().RerollShop()!=ShopResult::Rerolled)break;
                }
                Game().NextWave();Require(Game().GetState()==State::Playing,"Campaign next wave failed");
            }
            return;
        }
        if(scene.starts_with("run-") || scene=="boss-timeout") {
            SmokeTick();ECS::EntityHandle boss(0);Query<BossBrain> query;query.Refresh(*Game().Scene());for(auto chunk:query)if(chunk.count)boss=chunk.Entity(0,*Game().Scene());
            Require(Game().Scene()->IsAlive(boss),"Campaign did not spawn objective boss");
            const bool winning=scene=="run-victory" || scene=="run-restart" || scene=="run-home";
            if(winning){
                const auto bullet=Game().SpawnProjectile(Game().Get<Transform>(boss).position,{});Game().Get<Damage>(bullet).amount=100000;
            } else if(scene=="run-defeat")Game().SpawnHostileProjectile(origin,{},100000);
            for(int i=0;i<100 && (Game().GetState()==State::Playing || Game().GetState()==State::LevelUp);++i){
                if(Game().GetState()==State::LevelUp)Require(Game().SelectUpgrade(Game().Get<Growth>(Game().PlayerEntity()).choices[0]),"Result fixture upgrade failed");else SmokeTick();
            }
            const auto result=Game().ExtractResult();Require(result.outcome==(winning?RunOutcome::Victory:RunOutcome::Defeat),"Campaign outcome mismatch");
            Require(Game().Stats().enemies==0 && Game().Stats().hostileProjectiles==0 && Game().Stats().statuses==0,"Result retained combat entities");
            if(scene=="boss-timeout")Require(result.reason==RunEndReason::BossEscaped && result.health>0,"Boss deadline did not require kill");
            if(scene=="run-restart" || scene=="run-home"){
                const auto buttons=Buttons();const auto action=scene=="run-restart"?Action::Restart:Action::Home;
                const auto button=std::find_if(buttons.begin(),buttons.end(),[&](const auto& b){return b.action==action;});
                Require(button!=buttons.end()&&ActivateAt(button->rect.center),"Result button did not activate");
                if(scene=="run-restart")Require(Game().Stats().wave==1&&Game().Stats().bossSpawns==0&&Game().ExtractResult().outcome==RunOutcome::None,"Result restart retained progress");
                else Require(session_.CurrentScreen()==Screen::Home&&!session_.Game(),"Result home retained game");
            }
            return;
        }
        if(scene.starts_with("boss-")){
            const auto boss=Game().SpawnBoss(origin+glm::vec2(4,0),true,scene=="boss-enrage");Require(Game().Scene()->IsAlive(boss),"Boss showcase spawn failed");
            auto& brain=Game().Get<BossBrain>(boss);brain.cooldown=0;brain.sequence=scene=="boss-ring"?1:scene=="boss-charge"?2:0;
            if(scene=="boss-enrage")Game().Get<Health>(boss).current=Game().Get<Health>(boss).maximum/2;
            for(int i=0;i<(scene=="boss-volley"?90:15);++i)SmokeTick();
            Require(Game().ExtractBossStatus().size()==1,"Boss HUD extraction missing");
            if(scene=="boss-volley")Require(Game().Stats().enemyVolleys==1&&Game().Stats().hostileProjectiles==5,"Boss fan did not create complete volley");
            else Require(!Game().ExtractCombatCues().empty(),"Boss attack warning missing");return;
        }
        if(scene.starts_with("traits-") || scene=="families") {
            for(int i=0;i<100 && Game().GetState()==State::Playing;++i)SmokeTick();
            Require(Game().GetState()==State::Shop,"Trait fixture did not enter shop");
            const auto click=[&](Action action,std::size_t slot=0) {
                const auto buttons=Buttons();const auto b=std::find_if(buttons.begin(),buttons.end(),[&](const auto& value){return value.action==action&&value.value==slot;});
                Require(b!=buttons.end()&&ActivateAt(b->rect.center),"Trait shop button missed");
            };
            Game().Get<Player>(Game().PlayerEntity()).materials=500;
            const auto stock=[&](std::size_t card,WeaponKind kind,WeaponAffix affix) {
                ShopOffer offer;offer.type=OfferType::Weapon;offer.weapon=kind;offer.affix=affix;offer.price=WeaponPrice(kind,0)+3;offer.sold=false;
                Game().Get<Shop>(Game().PlayerEntity()).offers[card]=offer;
            };
            stock(0,WeaponKind::Torch,WeaponAffix::Burning);stock(1,WeaponKind::Gun,WeaponAffix::Piercing);
            Game().Get<Shop>(Game().PlayerEntity()).offers[2]={ItemKind::Bat,10,false,false};
            Game().Get<Shop>(Game().PlayerEntity()).offers[3]={ItemKind::Sausage,12,false,false};
            if(scene=="traits-shop")return;
            for(std::size_t item=0;item<ItemCount;++item) {
                Game().Get<Shop>(Game().PlayerEntity()).offers[2]={ItemKind(item),ItemDefinitions[item].price,false,false};click(Action::BuyItem,2);
            }
            Require(Game().ExtractBuild().stats.pierce==1&&Game().ExtractBuild().stats.burning==1,"Trait items did not change build");
            if(scene=="traits-build")return;
            for(int i=1;i<6;++i){stock(0,WeaponKind(i),WeaponAffix(i%4+1));if(!ValidAffix(WeaponKind(i),Game().Get<Shop>(Game().PlayerEntity()).offers[0].affix))Game().Get<Shop>(Game().PlayerEntity()).offers[0].affix=WeaponAffix::Chilling;click(Action::BuyItem,0);}
            Require(Game().Get<CombatStats>(Game().PlayerEntity()).families==std::array<int,3>{2,2,2},"Family pairs not derived from equipment");
            click(Action::ShowEquipment);return;
        }
        if(scene=="burn" || scene=="slow" || scene=="pierce" || scene=="lifesteal") {
            const int count=scene=="pierce"?4:1;std::vector<ECS::EntityHandle> targets;
            for(int i=0;i<count;++i){auto e=Game().SpawnEnemy(origin+glm::vec2(2.f+i*1.8f,0),true,EnemyKind::Armored);Game().Get<Health>(e).current=Game().Get<Health>(e).maximum=100;targets.push_back(e);}
            const auto projectile=Game().SpawnProjectile(origin+glm::vec2(-2,0),{120,0});
            auto& damage=Game().Get<Damage>(projectile);damage=Damage{};damage.amount=20;damage.owner=Game().PlayerEntity();
            if(scene=="burn")damage.burning=2;
            if(scene=="slow")damage.slow=.5f;
            if(scene=="pierce")Game().Get<Projectile>(projectile).pierces=2;
            if(scene=="lifesteal"){damage.lifeSteal=.1f;Game().Get<Health>(Game().PlayerEntity()).current=80;}
            for(int i=0;i<(scene=="burn"?70:scene=="slow"?20:10);++i)SmokeTick();
            if(scene=="burn")Require(Game().Stats().burnHits==1&&Game().ExtractEnemyStatus()[0].burning,"Burn did not pulse or render");
            if(scene=="slow")Require(Game().ExtractEnemyStatus()[0].slow<1,"Slow did not render");
            if(scene=="pierce")Require(Game().Stats().piercedHits==2&&Game().Get<Health>(targets[3]).current==100,"Piercing did not stop at its target budget");
            if(scene=="lifesteal")Require(Game().Stats().stolenHealth==2&&Game().Get<Health>(Game().PlayerEntity()).current==82,"Life steal did not heal direct damage");
            return;
        }
        if(scene.starts_with("weapon-") || scene=="arsenal") {
            for(int i=0;i<100 && Game().GetState()==State::Playing;++i)SmokeTick();
            Require(Game().GetState()==State::Shop,"Weapon fixture did not enter shop");
            const auto click=[&](Action action,std::size_t slot=0) {
                const auto buttons=Buttons();const auto button=std::find_if(buttons.begin(),buttons.end(),[&](const auto& b){return b.action==action&&b.value==slot;});
                Require(button!=buttons.end() && ActivateAt(button->rect.center),"Equipment button click missed");
            };
            Game().Get<Player>(Game().PlayerEntity()).materials=500;
            const auto stock=[&](std::size_t card,WeaponKind kind,int tier=0) {
                ShopOffer offer;offer.type=OfferType::Weapon;offer.weapon=kind;offer.tier=tier;offer.price=WeaponPrice(kind,tier);offer.sold=false;
                Game().Get<Shop>(Game().PlayerEntity()).offers[card]=offer;
            };
            stock(0,WeaponKind::Burst,3);stock(1,WeaponKind::Gun,1);
            Game().Get<Shop>(Game().PlayerEntity()).offers[2]={ItemKind::Lens,6,false,false};
            Game().Get<Shop>(Game().PlayerEntity()).offers[3]={ItemKind::Coffee,7,false,false};
            if(scene=="weapon-shop")return;
            const auto ticks=Game().Stats().ticks;
            for(int i=1;i<6;++i) {
                stock(0,scene=="arsenal"?WeaponKind(i):i==1?WeaponKind::Wand:WeaponKind(i),scene=="arsenal"?i%4:0);
                click(Action::BuyItem,0);
            }
            Require(Game().ExtractEquipment().count==6 && Game().Stats().weapons==6,"Six equipment purchases did not create six entities");
            click(Action::ShowEquipment);
            if(scene=="weapon-buy")return;
            if(scene=="arsenal") {
                click(Action::NextWave);
                const auto enemy=Game().SpawnEnemy(origin+glm::vec2(5,0),true,EnemyKind::Armored);
                Game().Get<Health>(enemy).current=Game().Get<Health>(enemy).maximum=1000;
                for(int i=0;i<12;++i)SmokeTick();
                Require(Game().Stats().shots>=6&&Game().Stats().weapons==6,"Equipped weapons did not attack concurrently");
                return;
            }
            click(Action::MergeWeapon);
            Require(Game().ExtractEquipment().count==5&&Game().ExtractEquipment().slots[0].tier==1&&!Game().ExtractEquipment().slots[1].present,"Merge button failed to upgrade and free one slot");
            if(scene=="weapon-sell") {
                const auto before=Game().ExtractBuild().materials;const auto value=Game().ExtractEquipment().slots[0].sellPrice;
                click(Action::SellWeapon);
                Require(Game().ExtractEquipment().count==4&&Game().ExtractBuild().materials==before+value&&Game().Scene()->IsAlive(Game().WeaponEntity()),"Sale button failed to pay or repair focus");
                click(Action::FocusEquipment,2);
            }
            Require(Game().Stats().ticks==ticks,"Equipment transactions advanced simulation");
            return;
        }
        if(scene.starts_with("shop") || scene=="build") {
            for(int i=0;i<8;++i) SmokeTick();
            Require(Game().GetState()==State::Shop,"Wave did not open a shop");
            const auto click=[&](Action action,std::size_t slot=0) {
                const auto buttons=Buttons();
                const auto button=std::find_if(buttons.begin(),buttons.end(),[&](const auto& b){return b.action==action && b.value==slot;});
                Require(button!=buttons.end() && ActivateAt(button->rect.center),"Shop button click missed");
            };
            const auto ticks=Game().Stats().ticks;
            for(int i=0;i<20;++i) session_.Advance(.25,{1,1});
            Require(Game().Stats().ticks==ticks,"Shop advanced combat time");
            if(scene=="shop") return;
            if(scene=="shop-lock" || scene=="shop-cycle") {
                const auto offer=Game().ExtractShop().shop.offers[3];
                click(Action::LockItem,3); click(Action::RerollShop);
                const auto after=Game().ExtractShop().shop.offers[3];
                Require(after.locked && after.kind==offer.kind && after.price==offer.price,"Reroll changed a locked offer");
                if(scene=="shop-lock") return;
            }
            if(scene=="build") {
                // Purchase all six real item types through the ordinary button
                // transaction; only stock/wallet are controlled by the fixture.
                Game().Get<Player>(Game().PlayerEntity()).materials=200;
                Game().Get<Health>(Game().PlayerEntity()).current=100;
                for(std::size_t item=0;item<ItemCount;++item) {
                    Game().Get<Shop>(Game().PlayerEntity()).offers[0]={ItemKind(item),ItemDefinitions[item].price,false,false};
                    click(Action::BuyItem,0);
                }
                const auto acquired=Game().ExtractBuild();
                Require(std::all_of(acquired.items.begin(),acquired.items.end(),[](int n){return n==1;}),"Build did not acquire all six items");
                return;
            }
            const auto before=Game().ExtractShop();
            click(Action::BuyItem,0);
            const auto after=Game().ExtractShop();
            Require(after.shop.offers[0].sold && after.build.materials==before.build.materials-before.shop.offers[0].price &&
                after.build.items[std::size_t(before.shop.offers[0].kind)]==1,"Purchase did not debit and acquire exactly once");
            Require(Game().BuyShopItem(0)==ShopResult::Sold && Game().ExtractBuild().materials==after.build.materials,"Sold offer was charged twice");
            if(scene=="shop-buy") return;
            const auto locked=after.shop.offers[3];
            click(Action::NextWave);
            Require(Game().GetState()==State::Playing && Game().ExtractBuild().items==after.build.items,"Next wave lost purchased items");
            for(int i=0;i<8;++i) SmokeTick();
            Require(Game().GetState()==State::Shop && Game().Stats().wave==2,"Second wave did not return to shop");
            const auto second=Game().ExtractShop();
            Require(second.shop.offers[3].locked && second.shop.offers[3].kind==locked.kind && second.shop.offers[3].price==locked.price,
                    "Locked stock did not survive the wave");
            return;
        }
        if (scene == "enemies") {
            for (int i=0; i<int(EnemyCount); ++i) Game().SpawnEnemy(origin + glm::vec2(-5.f + (i%4)*3.f,-1.5f+(i/4)*3.f),true,EnemyKind(i));
            for (int i=0; i<12; ++i) SmokeTick();
            Require(Game().ExtractEnemyStatus().size() == EnemyCount, "Enemy showcase lost a type");
            return;
        }
        if (scene == "ranged" || scene == "ranged-fire" || scene == "charge" || scene == "charge-dodge" || scene == "elite") {
            const auto kind = scene == "elite" ? EnemyKind::Elite : scene == "ranged" || scene == "ranged-fire" ? EnemyKind::Ranged : EnemyKind::Charger;
            const auto entity = Game().SpawnEnemy(origin + glm::vec2(5,0),true,kind);
            Game().Get<EnemyBrain>(entity).cooldown = 0;
            if (scene == "ranged" || scene == "charge") {
                for (int i=0; i<30; ++i) SmokeTick();
                Require(Game().Get<EnemyBrain>(entity).action == EnemyAction::Windup && !Game().ExtractCombatCues().empty(), "Attack windup did not show a telegraph");
            } else {
                for (int i=0; i<200; ++i) {
                    SmokeTick(scene == "charge-dodge" ? Input{0,1} : Input{});
                    if (Game().Get<EnemyBrain>(entity).action == EnemyAction::Charging || Game().Stats().enemyVolleys > 0) break;
                }
                if (scene == "charge-dodge") {
                    for (int i=0; i<65; ++i) SmokeTick();
                    Require(Game().Stats().charges == 1 && Game().Get<Health>(Game().PlayerEntity()).current == Game().Settings().initialHealth, "Perpendicular dodge did not avoid the dash");
                } else {
                    Require(Game().Stats().hostileProjectiles == (scene == "elite" ? 3 : 1), "Enemy volley was not committed");
                    for (int i=0; i<16; ++i) SmokeTick();
                }
            }
            return;
        }
        if (scene == "blast" || scene == "knockback") {
            const auto target = Game().SpawnEnemy(origin + glm::vec2(3,0),true,EnemyKind::Armored);
            if (scene == "blast") Game().SpawnEnemy(origin + glm::vec2(4,0),true);
            Game().SpawnProjectile(origin + glm::vec2(3,0),{1,0}); SmokeTick();
            for (int i=0; i<8; ++i) SmokeTick();
            Require(Game().Get<Health>(target).current == 4 && Game().Get<Transform>(target).position.x > origin.x+3, "Weapon knockback did not push the survivor");
            if (scene == "blast") Require(Game().Stats().splashHits == 1 && Game().Stats().kills == 1, "Blast failed to damage a neighbor once");
            return;
        }
        if (scene == "armored-hit") {
            const auto enemy = Game().SpawnEnemy(origin + glm::vec2(2,0),true,EnemyKind::Armored);
            Game().SpawnProjectile(origin + glm::vec2(2,0),{}); SmokeTick();
            Require(Game().Get<Health>(enemy).current == 4 && Game().Stats().kills == 0, "Armored enemy did not survive its first hit");
            for (int i=0; i<12; ++i) SmokeTick();
            return;
        }
        const auto earn = [&](int count) {
            for (int i=0; i<count; ++i) {
                const auto position = origin + glm::vec2(-3.f + (i%4)*2.f,-1.f - (i/4)*1.5f);
                Game().SpawnEnemy(position,true);
                Game().SpawnProjectile(position,{});
            }
            for (int i=0; i<40 && Game().GetState() == State::Playing; ++i) SmokeTick();
            Require(Game().GetState() == State::LevelUp, "Kills did not open the upgrade choice");
            const auto ticks = Game().Stats().ticks; const auto remaining = Game().Stats().remaining;
            for (int i=0; i<20; ++i) session_.Advance(.25,{1,1});
            Require(Game().Stats().ticks == ticks && Game().Stats().remaining == remaining, "Upgrade choice advanced the world");
        };
        earn(4);
        if (scene == "upgrade") return;
        for (int choice=0; choice<3; ++choice) {
            const auto buttons = Buttons();
            const auto selected = std::find_if(buttons.begin(),buttons.end(),[=](const auto& b) { return b.action == Action::ChooseUpgrade && b.value == std::size_t(choice); });
            Require(selected != buttons.end() && ActivateAt(selected->rect.center), "Upgrade card click missed");
            Require(Game().GetState() == State::Playing, "Choosing a card failed to resume");
            if (choice < 2) earn(choice == 0 ? 6 : 8);
        }
        const auto& growth = Game().Get<Growth>(Game().PlayerEntity());
        Require(growth.upgrades == std::array<int,UpgradeCount>{1,1,1}, "Growth cycle missed an upgrade type");
        Game().SpawnEnemy(origin + glm::vec2(2,0),true,EnemyKind::Armored);
        Game().SpawnProjectile(origin + glm::vec2(2,0),{}); SmokeTick();
        Input move; move.horizontal = 1; SmokeTick(move);
        const auto& stats = Game().Get<CombatStats>(Game().PlayerEntity());
        Require(stats.bonusDamage == 1 && stats.attackSpeed > 1 && stats.moveSpeed > 1, "Growth choices did not affect ECS stats");
    }

    void PreparePresentationSmoke(const std::string& scene) {
        const auto origin = Game().Get<Transform>(Game().PlayerEntity()).position;
        if (scene == "spawn") {
            Game().SpawnEnemy(origin + glm::vec2(2.5f, 0));
            for (int tick = 0; tick < 90; ++tick) SmokeTick();
            Require(Game().Stats().enemies == 1, "Spawn animation scenario lost its enemy");
        } else if (scene == "death") {
            const auto position = origin + glm::vec2(2.5f, 0);
            Game().SpawnEnemy(position, true);
            Game().SpawnProjectile(position, {});
            for (int tick = 0; tick < 20; ++tick) SmokeTick();
            Require(Game().Stats().kills == 1 && Game().Stats().effects > 0, "Death scenario did not produce particles");
        } else if (scene == "muzzle") {
            Game().SpawnEnemy(origin + glm::vec2(2.5f, 0), true);
            SmokeTick();
            Require(Game().Stats().shots == 1, "Muzzle scenario did not fire");
        } else {
            Game().SpawnEnemy(origin + glm::vec2(2.5f, 0), true);
            for (int tick = 0; tick < 30; ++tick) SmokeTick(scene == "idle" ? Input{} : Input{1, 0});
            if (scene == "pause") {
                const auto ticks = Game().Stats().ticks;
                Game().SetPaused(true);
                for (int tick = 0; tick < 30; ++tick) SmokeTick();
                Require(Game().Stats().ticks == ticks, "Pause animation advanced simulation");
            }
        }
    }

    void BeginRun(const Options& options) {
        Activate({{}, {}, Action::Forward});
        Activate({{}, {}, Action::ChooseCharacter, std::size_t(options.character - 1)});
        Activate({{}, {}, Action::Forward});
        Activate({{}, {}, Action::ChooseWeapon, WeaponIndex(GameConfig(options).initialWeapon)});
        Activate({{}, {}, Action::Forward});
        Activate({{}, {}, Action::ChooseDifficulty, std::size_t(options.difficulty)});
        Activate({{}, {}, Action::Forward});
        Activate({{}, {}, Action::ChooseMap, options.map});
        Activate({{}, {}, Action::Forward});
        Require(session_.CurrentScreen() == Screen::Run && session_.Game(), "Menu actions did not start a run");
        const auto setup=Game().ExtractRunSetup();Require(setup.character==std::size_t(options.character-1) && setup.difficulty==std::size_t(options.difficulty),"Menu did not pass selected character/difficulty to ECS");
    }

    void PrepareMenuSmoke(const Options& options) {
        const auto& scene = options.smokeMenu;
        Require(session_.CurrentScreen() == Screen::Home && !session_.Game(), "Home created a game prematurely");
        if (scene == "characters" || scene == "weapons" || scene == "weapon-unselected" || scene == "difficulty" || scene == "maps") {
            Require(session_.Navigate(Screen::CharacterSelect), session_.Error());
            Require(session_.SelectCharacter(std::size_t(options.character - 1)), session_.Error());
            if (scene != "characters") {
                Require(session_.Navigate(Screen::WeaponSelect), session_.Error());
                if (scene == "weapon-unselected") {
                    Require(!session_.HasSelectedWeapon(), "Weapon page was confirmed without a choice");
                    Require(!ActivateAt(MenuLayout::Forward.center), "Disabled next button accepted a pointer click");
                    Activate({{}, {}, Action::Forward});
                    Require(session_.CurrentScreen() == Screen::WeaponSelect, "Enter bypassed required weapon choice");
                } else Require(session_.SelectWeapon(WeaponKind(options.weapon - 1)), session_.Error());
            }
            if (scene == "difficulty" || scene == "maps") {
                Require(session_.Navigate(Screen::DifficultySelect), session_.Error());
                Require(session_.SelectDifficulty(std::size_t(options.difficulty)), session_.Error());
            }
            if (scene == "maps") {
                Require(session_.Navigate(Screen::MapSelect), session_.Error());
                Require(session_.SelectMap(options.map), session_.Error());
            }
        } else if (scene == "cycle") {
            const auto clickButton = [&](Action action, std::size_t value = 0) {
                const auto buttons = Buttons();
                const auto button = std::find_if(buttons.begin(), buttons.end(), [&](const auto& item) {
                    return item.action == action && item.value == value;
                });
                Require(button != buttons.end(), "Cycle smoke could not find its rendered button");
                const auto center = button->rect.center;
                const double cursorX = (center.x / ViewHalfWidth + 1) * 640.0;
                const double cursorY = (1 - center.y / ViewHalfHeight) * 360.0;
                const auto point = MenuLayout::CursorToCanvas(cursorX, cursorY, 1280, 720, 1280, 720);
                Require(point && ActivateAt(*point), "Cycle smoke button click missed its rendered region");
            };
            for (int cycle = 0; cycle < 3; ++cycle) {
                Require(!ActivateAt({ViewHalfWidth + 1, 0}), "Outside click unexpectedly activated a menu button");
                clickButton(Action::Forward);
                clickButton(Action::ChooseCharacter, std::size_t(options.character - 1));
                clickButton(Action::Forward);
                clickButton(Action::ChooseWeapon, std::size_t(options.weapon - 1));
                clickButton(Action::Forward);
                clickButton(Action::Back);
                Require(session_.CurrentScreen() == Screen::WeaponSelect && session_.HasSelectedWeapon(),
                    "Back from difficulty lost the confirmed weapon");
                clickButton(Action::Back);
                Require(session_.CurrentScreen() == Screen::CharacterSelect, "Weapon back button skipped the character page");
                clickButton(Action::Forward);
                clickButton(Action::Forward);
                clickButton(Action::ChooseDifficulty, std::size_t(options.difficulty));
                clickButton(Action::Forward);
                clickButton(Action::Back);
                Require(session_.CurrentScreen() == Screen::DifficultySelect, "Map back button skipped difficulty");
                clickButton(Action::Forward);
                clickButton(Action::ChooseMap, options.map);
                clickButton(Action::Forward);
                Require(session_.CurrentScreen() == Screen::Run && session_.Game(), "Cycle clicks did not start a run");
                Require(Game().Stats().ticks == 0 && Game().Stats().wave == 1 && Game().Stats().kills == 0,
                    "New run retained previous game state");
                Require(Game().CurrentWeapon() == WeaponKind(options.weapon - 1) && Game().Stats().weapons == 1,
                    "Weapon selection must activate exactly one chosen weapon");
                session_.FixedTick();
                Require(Game().Stats().ticks == 1, "Cycle run did not advance");
                Input pause;
                pause.pause = true;
                session_.Advance(0, pause);
                Require(Game().GetState() == State::Paused && Game().Stats().ticks == 1, "Cycle pause advanced the game");
                clickButton(Action::Home);
                Require(session_.CurrentScreen() == Screen::Home && !session_.Game(), "Returning home retained a game");
            }
        } else if (scene != "home") {
            BeginRun(options);
            if (scene == "pause") {
                session_.FixedTick();
                Game().SetPaused(true);
                Require(Game().GetState() == State::Paused, "Pause smoke did not pause");
            } else if (scene == "dead") {
                Game().Get<Health>(Game().PlayerEntity()).current = Game().Settings().contactDamage;
                Game().SpawnEnemy(Game().Settings().playerStart, true);
                session_.FixedTick();
                Require(Game().GetState() == State::Dead, "Defeat smoke did not take contact damage");
            } else if (scene == "wave-complete") {
                for (int tick = 0; tick < 120000 && Game().GetState() == State::Playing; ++tick) session_.FixedTick();
                Require(Game().GetState() == State::WaveComplete || Game().GetState()==State::Shop, "Wave smoke did not complete");
            } else if (scene == "return-home") {
                session_.FixedTick();
                Require(session_.ReturnHome() && !session_.Game(), "Return-home retained a game");
            }
        }
        if (session_.Game()) {
            const auto ticks = Game().Stats().ticks;
            session_.Advance(.2);
            Require(Game().Stats().ticks == ticks, "Menu overlay advanced frozen simulation");
        } else {
            session_.Advance(.2);
            Require(!session_.Game(), "Menu idle created a game");
        }
    }

    void MoveSelection(int delta) {
        const auto screen = session_.CurrentScreen();
        const auto count = ChoiceCount(screen);
        if (count == 0) return;
        const auto selected = SelectedChoice(screen);
        const auto next = std::size_t(std::clamp(int(selected) + delta, 0, int(count - 1)));
        SelectChoice(screen, next);
    }

    std::vector<MenuButton> Buttons() {
        const auto screen = session_.CurrentScreen();
        if (screen == Screen::Home) return {
            {{{0, -1.15f}, {6.4f, .95f}}, "START A RUN", Action::Forward},
            {{{0, -2.4f}, {6.4f, .75f}}, "EXIT", Action::Exit}};
        std::vector<MenuButton> buttons;
        if (ChoiceCount(screen) > 0) {
            const auto count = ChoiceCount(screen);
            const auto selected = SelectedChoice(screen);
            const auto& definitions = session_.Settings().weapons;
            const auto page = selected / MenuLayout::PageSize;
            for (std::size_t index = page * MenuLayout::PageSize; index < std::min(count, (page + 1) * MenuLayout::PageSize); ++index) {
                std::string name;
                Action action = Action::ChooseMap;
                if (screen == Screen::CharacterSelect) { name = Characters[index].name; action = Action::ChooseCharacter; }
                else if (screen == Screen::WeaponSelect) { name = definitions[index].name; action = Action::ChooseWeapon; }
                else if (screen == Screen::DifficultySelect) { name = ProfileRulesEnabled(session_.Settings())?RunDifficulties[index].name:Difficulties[0].name; action = Action::ChooseDifficulty; }
                else name = index == RandomMap ? "RANDOM" : Maps[index].name;
                const bool confirmed = screen != Screen::WeaponSelect || session_.HasSelectedWeapon();
                buttons.push_back({MenuLayout::Card(index % MenuLayout::PageSize), name, action, index, index == selected && confirmed});
            }
            buttons.push_back({MenuLayout::Back, "BACK", Action::Back});
            const auto next = screen == Screen::CharacterSelect ? "CHOOSE WEAPON" : screen == Screen::WeaponSelect ? "DIFFICULTY" :
                screen == Screen::DifficultySelect ? "CHOOSE MAP" : "START RUN";
            buttons.push_back({MenuLayout::Forward, next, Action::Forward, 0, false,
                screen != Screen::WeaponSelect || session_.HasSelectedWeapon()});
            if (count > MenuLayout::PageSize) {
                if (page > 0) buttons.push_back({MenuLayout::PreviousPage, "<", Action::PreviousPage});
                if ((page + 1) * MenuLayout::PageSize < count) buttons.push_back({MenuLayout::NextPage, ">", Action::NextPage});
            }
        } else if (screen == Screen::Run && Game().GetState() == State::LevelUp) {
            const auto choices=Game().Get<Growth>(Game().PlayerEntity()).choices;
            for (std::size_t i=0; i<3; ++i) buttons.push_back({MenuLayout::UpgradeCard(i),std::to_string(i+1)+"  "+UpgradeDefinitions[std::size_t(choices[i])].name,Action::ChooseUpgrade,i,int(i)==upgradeFocus_});
            buttons.push_back({{{-2.8f,-2.7f},{5.f,.65f}},"R  RESTART RUN",Action::Restart});
            buttons.push_back({{{2.8f,-2.7f},{5.f,.65f}},"H  RETURN HOME",Action::Home});
        } else if(screen==Screen::Run && Game().GetState()==State::Shop) {
            const auto snapshot=Game().ExtractShop();
            const bool armory=Game().Settings().arsenal && Game().Settings().builds;
            if(armory) {
                buttons.push_back({MenuLayout::ShopStockTab,"TAB  MARKET",Action::ShowStock,0,!equipmentView_});
                buttons.push_back({MenuLayout::ShopEquipmentTab,"TAB  EQUIPMENT  "+std::to_string(snapshot.equipment.count)+"/6",Action::ShowEquipment,0,equipmentView_});
            }
            if(armory && equipmentView_) {
                for(std::size_t i=0;i<EquipmentSlots;++i)buttons.push_back({MenuLayout::EquipmentCard(i),"",Action::FocusEquipment,i,int(i)==equipmentFocus_});
                const auto& selected=snapshot.equipment.slots[std::size_t(equipmentFocus_)];
                buttons.push_back({MenuLayout::ShopReroll,"U  MERGE",Action::MergeWeapon,0,false,selected.present&&selected.canMerge});
                buttons.push_back({MenuLayout::ShopSell,"X  SELL +"+std::to_string(selected.sellPrice),Action::SellWeapon,0,false,selected.present&&snapshot.equipment.count>1});
            } else {
            for(std::size_t i=0;i<ShopSlots;++i) {
                const auto& offer=snapshot.shop.offers[i];
                const bool weapon=offer.type==OfferType::Weapon;
                if(weapon?!ValidWeapon(offer.weapon):std::size_t(offer.kind)>=ItemCount)continue;
                const bool capacity=weapon?snapshot.equipment.count<EquipmentSlots && Game().Stats().weapons<Game().Settings().maxWeapons:
                    snapshot.build.items[std::size_t(offer.kind)]<MaxItemStack;
                buttons.push_back({MenuLayout::ShopCard(i),weapon?Game().Definition(offer.weapon).name:ItemDefinitions[std::size_t(offer.kind)].name,Action::BuyItem,i,int(i)==shopFocus_,
                    !offer.sold && snapshot.build.materials>=offer.price && capacity});
                buttons.push_back({MenuLayout::ShopLock(i),offer.locked?"LOCKED / UNLOCK":"LOCK FOR NEXT WAVE",Action::LockItem,i,offer.locked,!offer.sold});
            }
            const bool allLocked=std::all_of(snapshot.shop.offers.begin(),snapshot.shop.offers.end(),[](const auto& o){return o.locked&&!o.sold;});
            buttons.push_back({MenuLayout::ShopReroll,"F  REROLL  "+std::to_string(snapshot.rerollCost),Action::RerollShop,0,false,snapshot.build.materials>=snapshot.rerollCost&&!allLocked});
            }
            buttons.push_back({MenuLayout::ShopNext,"ENTER  NEXT WAVE",Action::NextWave});
            buttons.push_back({{{-3.0f,-4.72f},{5.2f,.55f}},"R  RESTART RUN",Action::Restart});
            buttons.push_back({{{3.0f,-4.72f},{5.2f,.55f}},"H  RETURN HOME",Action::Home});
        } else if (screen == Screen::Run && Game().GetState() != State::Playing) {
            const auto state = Game().GetState();
            if(state==State::Victory || state==State::Defeat){
                buttons.push_back({{{0,-.65f},{7.8f,.7f}},"ENTER  NEW RUN",Action::Restart});
                buttons.push_back({{{0,-1.60f},{7.8f,.7f}},"H  RETURN HOME",Action::Home});
                return buttons;
            }
            buttons.push_back({{{0, -.48f}, {7.8f, .7f}}, state == State::Paused ? "P  RESUME" : state == State::Dead ? "V  REVIVE" : "ENTER  NEXT WAVE",
                state == State::Paused ? Action::Resume : state == State::Dead ? Action::Revive : Action::NextWave});
            buttons.push_back({{{0, -1.43f}, {7.8f, .7f}}, "R  RESTART RUN", Action::Restart});
            buttons.push_back({{{0, -2.38f}, {7.8f, .7f}}, "H  RETURN HOME", Action::Home});
        }
        return buttons;
    }

    bool ActivateAt(glm::vec2 canvasPoint) {
        const auto buttons = Buttons();
        for (const auto& button : buttons) if (button.enabled && button.rect.Contains(canvasPoint)) {
            Activate(button);
            return true;
        }
        return false;
    }

    void Activate(const MenuButton& button) {
        if (!button.enabled || (button.action == Action::Forward && session_.CurrentScreen() == Screen::WeaponSelect && !session_.HasSelectedWeapon())) return;
        switch (button.action) {
        case Action::ChooseCharacter: Require(session_.SelectCharacter(button.value), session_.Error()); break;
        case Action::ChooseWeapon: Require(session_.SelectWeapon(WeaponKind(button.value)), session_.Error()); break;
        case Action::ChooseDifficulty: Require(session_.SelectDifficulty(button.value), session_.Error()); break;
        case Action::ChooseMap: Require(session_.SelectMap(button.value), session_.Error()); break;
        case Action::PreviousPage: MoveSelection(-int(MenuLayout::PageSize)); break;
        case Action::NextPage: MoveSelection(int(MenuLayout::PageSize)); break;
        case Action::Back:
            if (session_.CurrentScreen() == Screen::Home) exitRequested_ = true;
            else if (session_.CurrentScreen() == Screen::MapSelect) Require(session_.Navigate(Screen::DifficultySelect), session_.Error());
            else if (session_.CurrentScreen() == Screen::DifficultySelect) Require(session_.Navigate(Screen::WeaponSelect), session_.Error());
            else if (session_.CurrentScreen() == Screen::WeaponSelect) Require(session_.Navigate(Screen::CharacterSelect), session_.Error());
            else Require(session_.ReturnHome(), session_.Error());
            break;
        case Action::Forward:
            if (session_.CurrentScreen() == Screen::Home) Require(session_.Navigate(Screen::CharacterSelect), session_.Error());
            else if (session_.CurrentScreen() == Screen::CharacterSelect) Require(session_.Navigate(Screen::WeaponSelect), session_.Error());
            else if (session_.CurrentScreen() == Screen::WeaponSelect) Require(session_.Navigate(Screen::DifficultySelect), session_.Error());
            else if (session_.CurrentScreen() == Screen::DifficultySelect) Require(session_.Navigate(Screen::MapSelect), session_.Error());
            else if (session_.CurrentScreen() == Screen::MapSelect) Require(session_.StartRun(), session_.Error());
            break;
        case Action::Resume: Game().SetPaused(false); break;
        case Action::Restart: Game().Restart(); break;
        case Action::NextWave: Game().NextWave(); break;
        case Action::Revive: Game().Revive(); break;
        case Action::Home: Require(session_.ReturnHome(), session_.Error()); break;
        case Action::Exit: exitRequested_ = true; break;
        case Action::ChooseUpgrade:
            if (!Game().SelectUpgrade(Game().Get<Growth>(Game().PlayerEntity()).choices.at(button.value))) return;
            upgradeFocus_ = 0; break;
        case Action::BuyItem: Game().BuyShopOffer(button.value); break;
        case Action::ShowStock: equipmentView_=false; break;
        case Action::ShowEquipment: equipmentView_=true; break;
        case Action::FocusEquipment: equipmentFocus_=int(button.value); break;
        case Action::MergeWeapon: Game().MergeWeapon(std::size_t(equipmentFocus_)); break;
        case Action::SellWeapon: Game().SellWeapon(std::size_t(equipmentFocus_)); break;
        case Action::LockItem: Game().ToggleShopLock(button.value); break;
        case Action::RerollShop: Game().RerollShop(); break;
        }
        gameAudio_.Click();
        gameAudio_.Sync(session_.Game(), session_.RunSerial());
    }

    void DrawButton(const MenuButton& button, glm::vec2 camera = {}) {
        const bool hover = button.enabled && pointer_ && button.rect.Contains(*pointer_);
        const auto center = camera + button.rect.center;
        batch_.Rect(center, button.rect.size + glm::vec2(.045f), button.selected || hover ? Green : glm::vec4(.32f, .37f, .25f, 1));
        batch_.Rect(center, button.rect.size, button.selected ? glm::vec4(.22f, .31f, .15f, 1) : hover ? glm::vec4(.18f, .23f, .13f, 1) : Panel);
        if (button.action == Action::ChooseCharacter) {
            const auto& character = Characters.at(button.value);
            const auto size = character.size / std::max(character.size.x, character.size.y) * 1.04f;
            batch_.Sprite(character.image, center + glm::vec2(0, .16f), size);
            const float labelPixel = std::min(.030f, 1.90f / (6.f * float(button.label.size())));
            CenteredText(button.label, center + glm::vec2(0, -.59f), labelPixel, button.selected ? Green : White);
        } else if (button.action == Action::ChooseWeapon || button.action == Action::ChooseDifficulty) {
            const char* icon = button.action == Action::ChooseWeapon ? WeaponSelections[button.value].image : Difficulties[0].image;
            const auto size = button.action == Action::ChooseWeapon ? WeaponSelections[button.value].size : Difficulties[0].size;
            if(button.action==Action::ChooseDifficulty && ProfileRulesEnabled(session_.Settings())) {
                const auto badge=center+glm::vec2(0,.18f);batch_.Ring(badge,.43f,.055f,DifficultyColor(button.value));
                CenteredText(std::to_string(button.value),badge,.085f,White);
            } else batch_.Sprite(icon, center + glm::vec2(0, .18f), size / std::max(size.x, size.y) * 1.03f);
            const float pixel = std::min(.034f, 1.91f / (6.f * float(button.label.size())));
            CenteredText(button.label, center + glm::vec2(0, -.59f), pixel, button.selected ? Green : White);
        } else if (button.action == Action::ChooseMap) {
            if (button.value == RandomMap) CenteredText("?", center + glm::vec2(0, .18f), .14f, Green);
            else DrawMapPreview(batch_, button.value, center + glm::vec2(0, .18f), {1.75f, 1.04f});
            CenteredText(button.label, center + glm::vec2(0, -.59f), .034f, button.selected ? Green : White);
        } else if (button.action == Action::ChooseUpgrade) {
            const auto kind=Game().Get<Growth>(Game().PlayerEntity()).choices[button.value];
            const auto& definition=UpgradeDefinitions[std::size_t(kind)];
            CenteredText(button.label,center+glm::vec2(0,.65f),.045f,White);
            CenteredText(definition.value,center+glm::vec2(0,.1f),.10f,Green);
            CenteredText(definition.description,center+glm::vec2(0,-.65f),.034f,Muted);
        } else if(button.action==Action::FocusEquipment) {
            const auto inventory=Game().ExtractEquipment();const auto& entry=inventory.slots[button.value];
            CenteredText("SLOT "+std::to_string(button.value+1),center+glm::vec2(0,1.36f),.038f,Muted);
            if(entry.present) {
                const auto index=WeaponIndex(entry.kind);const auto size=WeaponSelections[index].size;
                batch_.Sprite(WeaponSelections[index].image,center+glm::vec2(0,.55f),size/std::max(size.x,size.y)*1.1f,0,TierColor(entry.tier));
                const auto& name=Game().Definition(entry.kind).name;
                CenteredText(name,center+glm::vec2(0,-.25f),std::min(.033f,2.8f/(6.f*name.size())),White);
                CenteredText("TIER "+std::to_string(entry.tier+1),center+glm::vec2(0,-.68f),.048f,TierColor(entry.tier));
                CenteredText("DMG "+std::to_string(entry.damage),center+glm::vec2(0,-1.03f),.034f,Muted);
                if(Game().Settings().traits) CenteredText(AffixNames[std::size_t(entry.affix)],center+glm::vec2(0,-1.36f),.025f,Green);
            } else CenteredText("EMPTY",center,.055f,Muted);
        } else if(button.action==Action::BuyItem) {
            const auto snapshot=Game().ExtractShop(); const auto& offer=snapshot.shop.offers[button.value];
            if(offer.type==OfferType::Weapon) {
                const auto index=WeaponIndex(offer.weapon);const auto size=WeaponSelections[index].size;
                batch_.Sprite(WeaponSelections[index].image,center+glm::vec2(0,.95f),size/std::max(size.x,size.y)*1.22f,0,offer.sold?Muted:TierColor(offer.tier));
                CenteredText(Game().Definition(offer.weapon).name,center+glm::vec2(0,.08f),std::min(.041f,3.9f/(6.f*button.label.size())),White);
                const int damage=(Game().Definition(offer.weapon).damage*(2+offer.tier)+1)/2+snapshot.build.stats.bonusDamage+snapshot.build.stats.weaponDamage[WeaponIndex(offer.weapon)];
                CenteredText("TIER "+std::to_string(offer.tier+1)+"  DMG "+std::to_string(damage),center+glm::vec2(0,-.42f),.035f,TierColor(offer.tier));
                const float familyRate=Game().Settings().traits && Family(offer.weapon)==WeaponFamily::Ballistic ? 1+.1f*(snapshot.build.stats.families[std::size_t(WeaponFamily::Ballistic)]/2) : 1;
                std::ostringstream text;text<<std::fixed<<std::setprecision(2)<<"COOLDOWN "<<Game().Definition(offer.weapon).cooldown*(1-.1f*offer.tier)/snapshot.build.stats.attackSpeed/familyRate<<"S";
                CenteredText(text.str(),center+glm::vec2(0,-.74f),.029f,Muted);
                if(Game().Settings().traits) CenteredText(AffixNames[std::size_t(offer.affix)],center+glm::vec2(0,-.98f),.028f,Green);
            } else {
            const auto index=std::size_t(offer.kind); const auto& item=ItemDefinitions[index];
            const auto size=ItemArtCatalog[index].size;
            batch_.Sprite(item.image,center+glm::vec2(0,.95f),size/std::max(size.x,size.y)*1.22f,0,offer.sold?Muted:White);
            CenteredText(item.name,center+glm::vec2(0,.08f),.041f,offer.sold?Muted:White);
            const std::string description=item.description;
            CenteredText(description,center+glm::vec2(0,-.42f),std::min(.030f,3.9f/(6.f*description.size())),Muted);
            CenteredText("OWNED "+std::to_string(snapshot.build.items[index]),center+glm::vec2(0,-.84f),.033f,Muted);
            }
            CenteredText(offer.sold?"SOLD":std::to_string(button.value+1)+"  BUY  "+std::to_string(offer.price),center+glm::vec2(0,-1.3f),.051f,
                offer.sold?Muted:button.enabled?Green:glm::vec4(1,.45f,.25f,1));
        } else {
            const float pixel = std::min(.063f, (button.rect.size.x - .24f) / (6.f * float(button.label.size())));
            CenteredText(button.label, center, pixel, !button.enabled ? glm::vec4(.35f, .39f, .33f, 1) : hover ? Green : White);
        }
    }

    std::size_t Menu() {
        batch_.Rect({}, {2 * ViewHalfWidth, 12.f}, {.20f, .23f, .17f, 1});
        batch_.Rect({}, {20.8f, 11.55f}, Panel);
        const auto screen = session_.CurrentScreen();
        batch_.Rect({0, screen == Screen::Home ? -4.83f : 4.13f}, {19.7f, .035f}, {.35f, .40f, .23f, 1});
        if (screen == Screen::Home) {
            CenteredText("BROTATO", {0, 3.3f}, .30f, Green);
            CenteredText("SURVIVE. COLLECT. KEEP MOVING.", {0, 1.8f}, .060f, White);
            for (std::size_t index = 0; index < Characters.size(); ++index) {
                const auto& character = Characters[index];
                const auto size = character.size / std::max(character.size.x, character.size.y) * 1.37f;
                batch_.Sprite(character.image, {float(index) * 2.05f - 4.1f, .53f}, size);
            }
            CenteredText("CLICK START OR PRESS ENTER", {0, -3.63f}, .047f, Muted);
            CenteredText(expanded_ ? "DODGE. GROW. SHOP. BUILD YOUR RUN." : "WASD MOVE   AUTO AIM   SIX WEAPONS", {0, -4.33f}, .042f, Muted);
        } else {
            const bool characters = screen == Screen::CharacterSelect;
            CenteredText(MenuTitle(screen), {0, 4.88f}, .11f, Green);
            batch_.Rect({5.15f, .24f}, {9.15f, 7.15f}, {.10f, .12f, .085f, 1});
            if (characters) {
                const auto& character = Characters.at(session_.SelectedCharacter());
                const auto size = character.size / std::max(character.size.x, character.size.y) * 3.0f;
                batch_.Sprite(character.image, {5.15f, 1.55f}, size);
                CenteredText(character.name, {5.15f, -.78f}, .076f, White);
                if(ProfileRulesEnabled(session_.Settings())) {
                    const auto& profile=CharacterProfiles[session_.SelectedCharacter()];
                    CenteredText(profile.strength, {5.15f,-1.58f}, .038f, Green);
                    CenteredText(profile.attributes, {5.15f,-2.15f}, .037f, Muted);
                    CenteredText("BONUSES STAY WITH YOUR BUILD", {5.15f,-2.72f}, .033f, Muted);
                } else {
                    CenteredText("APPEARANCE ONLY", {5.15f, -1.58f}, .045f, Green);
                    CenteredText("SAME HEALTH AND MOVEMENT", {5.15f, -2.15f}, .039f, Muted);
                }
            } else if (screen == Screen::WeaponSelect) {
                const auto selected = WeaponIndex(session_.SelectedWeapon());
                const auto& definitions = session_.Settings().weapons;
                const auto& definition = definitions[selected];
                const auto size = WeaponSelections[selected].size;
                batch_.Sprite(WeaponSelections[selected].image, {5.15f, 1.65f}, size / std::max(size.x, size.y) * 2.8f);
                CenteredText(definition.name, {5.15f, -.42f}, .066f, White);
                const char* style = definition.mode == AttackMode::Thrust ? "THRUST AND RETURN" :
                    definition.mode == AttackMode::SegmentedBeam ? "SHORT RANGE BEAM" :
                    definition.mode == AttackMode::AuthoredBurst ? "FOUR SHOT SPREAD" : "AUTOMATIC PROJECTILE";
                CenteredText(style, {5.15f, -1.13f}, .043f, Green);
                std::ostringstream details;
                const auto& profile=ProfileRulesEnabled(session_.Settings())?CharacterProfiles[session_.SelectedCharacter()]:CharacterProfiles[0];
                details << std::fixed << std::setprecision(2) << "RECOVERY " << definition.cooldown/(1+profile.bonus.attackSpeed) << "S";
                CenteredText(details.str(), {5.15f, -1.72f}, .043f, Muted);
                if (expanded_) {
                    CenteredText("DMG " + std::to_string(definition.damage+profile.bonus.damage+profile.weaponDamage[selected]),{5.15f,-2.05f},.035f,White);
                    CenteredText(definition.splashRadius > 0 ? "EXPLOSIVE IMPACT" : definition.knockback >= 6 ? "STRONG KNOCKBACK" : definition.knockback > 0 ? "KNOCKBACK" : "HITS MULTIPLE TARGETS",{5.15f,-2.85f},.032f,Green);
                }
                CenteredText(session_.HasSelectedWeapon() ? "STARTING WEAPON SELECTED" : "CLICK A WEAPON OR PRESS SPACE",
                    {5.15f, -2.42f}, .041f, session_.HasSelectedWeapon() ? Green : White);
            } else if (screen == Screen::DifficultySelect) {
                const auto& art = Difficulties[0];
                if(ProfileRulesEnabled(session_.Settings())) {
                    batch_.Ring({5.15f,1.5f},1.12f,.11f,DifficultyColor(session_.SelectedDifficulty()));
                    CenteredText(std::to_string(session_.SelectedDifficulty()),{5.15f,1.5f},.24f,White);
                    const auto& rules=RunDifficulties[session_.SelectedDifficulty()];
                    CenteredText(rules.name,{5.15f,-.6f},.077f,White);
                    CenteredText("ENEMY HP "+std::to_string(rules.enemyHealth)+"% / DAMAGE "+std::to_string(rules.enemyDamage)+"%",{5.15f,-1.42f},.036f,Green);
                    CenteredText("MOVE "+std::to_string(rules.enemySpeed)+"% / SPAWN DELAY "+std::to_string(rules.spawnDelay)+"%",{5.15f,-2.03f},.036f,Muted);
                    CenteredText("BOSS HP "+std::to_string(rules.bossHealth)+"% / HEAL "+std::to_string(rules.shopHealing)+"%",{5.15f,-2.64f},.036f,Muted);
                } else {
                    batch_.Sprite(art.image, {5.15f, 1.5f}, art.size / std::max(art.size.x, art.size.y) * 2.7f);
                    CenteredText(art.name, {5.15f, -.6f}, .077f, White);
                    CenteredText("READY TO SURVIVE", {5.15f, -1.47f}, .05f, Green);
                    CenteredText(session_.Settings().weapons[WeaponIndex(session_.SelectedWeapon())].name, {5.15f,-2.14f}, .043f, Muted);
                }
            } else {
                const auto selected = session_.SelectedMap();
                if (selected == RandomMap) {
                    for (std::size_t index = 0; index < Maps.size(); ++index)
                        DrawMapPreview(batch_, index, {3.05f + float(index % 3) * 2.10f, 2.1f - float(index / 3) * 1.63f}, {1.8f, 1.39f});
                    CenteredText("RANDOM MAP", {5.15f, -.78f}, .076f, White);
                    CenteredText("RANDOM PICK EACH RUN", {5.15f, -1.58f}, .045f, Green);
                } else {
                    DrawMapPreview(batch_, selected, {5.15f, 1.48f}, {6.55f, 3.6f});
                    CenteredText(Maps.at(selected).name, {5.15f, -.78f}, .076f, White);
                    CenteredText("SAME WAVES AND BOUNDARIES", {5.15f, -1.58f}, .041f, Green);
                }
                CenteredText(Characters.at(session_.SelectedCharacter()).name, {5.15f, -2.15f}, .043f, Muted);
            }
            const auto selected = SelectedChoice(screen);
            const auto count = ChoiceCount(screen);
            if (count > MenuLayout::PageSize) CenteredText(std::to_string(selected / MenuLayout::PageSize + 1) + "/" + std::to_string((count + MenuLayout::PageSize - 1) / MenuLayout::PageSize), {-3.75f, -3.75f}, .047f, Muted);
            CenteredText(screen == Screen::WeaponSelect ? "CLICK / ARROWS / SPACE SELECT   ENTER CONTINUE   ESC BACK" :
                "CLICK / ARROWS TO SELECT   ENTER CONTINUE   ESC BACK", {0, -5.57f}, .039f, Muted);
        }
        for (const auto& button : Buttons()) DrawButton(button);
        if (screen == Screen::Home) CenteredText(expanded_ ? "SURVIVAL + GROWTH" : "SOURCE RULES", {0, -5.33f}, .034f, Muted);
        return 0; // Menu rendering has no ECS sprite extraction count.
    }

    template<class Predicate> void Wait(Predicate complete, const char* operation) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        while (!complete()) {
            device_->returnSystem.DrainCallbacks();
            if (complete()) break;
            if (std::chrono::steady_clock::now() >= deadline) {
                device_->StopAndRelease();
                throw std::runtime_error(std::string("Timed out waiting for ") + operation);
            }
            device_->returnSystem.WaitForCallbacks(std::chrono::milliseconds(2));
        }
    }

    void CenteredText(const std::string& text, glm::vec2 center, float pixel, glm::vec4 color) {
        batch_.Text(text, center + glm::vec2(-float(text.size()) * 3.f * pixel, 3.5f * pixel), pixel, color);
    }

    glm::vec4 TierColor(int tier) {
        constexpr glm::vec4 colors[]{White,Green,{.4f,.7f,1,1},{.92f,.45f,1,1}};
        return colors[std::clamp(tier,0,3)];
    }
    void Arena() { DrawArena(batch_, Game().Settings().map); }

    void ShopPanel(glm::vec2 camera) {
        const auto snapshot=Game().ExtractShop();
        const auto screen=[&](float x,float y){return camera+glm::vec2(x,y);};
        batch_.Rect(camera,{2*ViewHalfWidth,9.8f},{0,0,0,.65f});
        batch_.Rect(screen(0,-.25f),{20.4f,10.f},Panel);
        batch_.Rect(screen(0,4.65f),{20.4f,.055f},Green);
        CenteredText("WAVE SHOP",screen(0,4.08f),.12f,Green);
        CenteredText("WAVE "+std::to_string(Game().Stats().wave)+" CLEARED   BONUS +"+std::to_string(snapshot.shop.waveBonus)+
            "   MATERIALS "+std::to_string(snapshot.build.materials)+(Game().Settings().campaign?"   HEAL +"+std::to_string(snapshot.shop.restoredHealth):""),screen(0,3.32f),.040f,White);
        const char* message="BUY WEAPONS OR ITEMS / LOCK STOCK FOR THE NEXT WAVE";
        switch(snapshot.shop.feedback) {
        case ShopResult::Bought: message="PURCHASE ADDED TO YOUR BUILD"; break;
        case ShopResult::Merged: message="MATCHING WEAPONS MERGED / ONE SLOT FREED"; break;
        case ShopResult::WeaponSold: message="WEAPON SOLD / MATERIALS RECEIVED"; break;
        case ShopResult::NoMatch: message="MERGE NEEDS THE SAME WEAPON AND TIER"; break;
        case ShopResult::LastWeapon: message="KEEP AT LEAST ONE WEAPON"; break;
        case ShopResult::MaxTier: message="THIS WEAPON IS ALREADY TIER 4"; break;
        case ShopResult::Rerolled: message="UNLOCKED STOCK REFRESHED"; break;
        case ShopResult::Locked: message="LOCKED STOCK KEEPS ITS CURRENT PRICE"; break;
        case ShopResult::Unlocked: message="STOCK UNLOCKED"; break;
        case ShopResult::NotEnough: message="NOT ENOUGH MATERIALS"; break;
        case ShopResult::Sold: message="THIS OFFER IS SOLD"; break;
        case ShopResult::StackFull: message="ITEM STACK LIMIT REACHED"; break;
        case ShopResult::Capacity: message="NO FREE INVENTORY CAPACITY"; break;
        case ShopResult::AllLocked: message="UNLOCK STOCK BEFORE REROLLING"; break;
        case ShopResult::Invalid: message="THIS ACTION IS NOT AVAILABLE"; break;
        default: break;
        }
        CenteredText(message,screen(0,-2.13f),.034f,Muted);
        if(equipmentView_ && Game().Settings().arsenal) {
            const auto& entry=snapshot.equipment.slots[std::size_t(equipmentFocus_)];
            if(Game().Settings().traits) {
                const auto& f=snapshot.build.stats.families;
                CenteredText("ELEMENTAL "+std::to_string(f[0])+" / BURN +"+std::to_string(f[0]/2)+"   PRECISION "+std::to_string(f[1])+" / CRIT +"+std::to_string(f[1]/2*10)+"%   BALLISTIC "+std::to_string(f[2])+" / ATK +"+std::to_string(f[2]/2*10)+"%",screen(0,-1.55f),.032f,Green);
                CenteredText("PAIRS BOOST THEIR FAMILY / MERGE KEEPS SELECTED AFFIX",screen(0,-2.72f),.032f,Muted);
            } else {
                CenteredText("ALL EQUIPPED WEAPONS ATTACK AUTOMATICALLY",screen(0,-1.55f),.040f,Green);
                CenteredText("MERGE TWO IDENTICAL TIERS TO UPGRADE / SELL RETURNS HALF BASE VALUE",screen(0,-2.72f),.032f,Muted);
            }
            if(entry.present) {
                std::ostringstream text;text<<Game().Definition(entry.kind).name<<"  TIER "<<entry.tier+1<<"  DMG "<<entry.damage<<"  COOLDOWN "<<std::fixed<<std::setprecision(2)<<entry.cooldown<<"S";
                CenteredText(text.str(),screen(0,-3.2f),.035f,TierColor(entry.tier));
                if(Game().Settings().traits) CenteredText(std::string(FamilyNames[std::size_t(Family(entry.kind))])+" / "+AffixDescriptions[std::size_t(entry.affix)],screen(0,-2.42f),.027f,Green);
            }
        } else {
        for(std::size_t i=0;i<ItemCount;++i) {
            const auto size=ItemArtCatalog[i].size;
            batch_.Sprite(ItemDefinitions[i].image,screen(-9.15f+float(i)*1.95f,-2.77f),size/std::max(size.x,size.y)*.40f);
            batch_.Text("X"+std::to_string(snapshot.build.items[i]),screen(-8.86f+float(i)*1.95f,-2.66f),.030f,White);
        }
        const auto& s=snapshot.build.stats;
        CenteredText("DMG +"+std::to_string(s.bonusDamage)+"   ATK "+std::to_string(int(std::round(s.attackSpeed*100)))+
            "%   MOVE "+std::to_string(int(std::round(s.moveSpeed*100)))+"%   ARMOR "+std::to_string(s.armor)+
            "   HP "+std::to_string(snapshot.build.health)+"/"+std::to_string(snapshot.build.maximumHealth),screen(0,-3.10f),.032f,White);
        std::ostringstream extra;
        extra<<"PICKUP "<<int(std::round(s.pickupRange*100))<<"%   REGEN "<<std::fixed<<std::setprecision(1)<<s.regeneration<<" HP/S";
        if(Game().Settings().traits)extra<<"   CRIT "<<int(std::round(s.criticalChance*100))<<"%   STEAL "<<int(std::round(s.lifeSteal*100))<<"%   PIERCE "<<s.pierce<<"   BURN "<<s.burning;
        CenteredText(extra.str(),screen(0,-3.45f),.028f,Muted);
        }
        for(const auto& button:Buttons()) DrawButton(button,camera);
        batch_.Rect(screen(0,-5.57f),{2*ViewHalfWidth,.66f},Panel);
        const char* controls=equipmentView_?"TAB MARKET   1-6 SELECT   U MERGE   X SELL   ENTER NEXT WAVE   H HOME":Game().Settings().arsenal?"TAB EQUIPMENT   1-4 BUY   SHIFT+1-4 LOCK   F REROLL   ENTER NEXT WAVE":"1-4 / SPACE BUY   SHIFT+1-4 LOCK   F REROLL   ENTER NEXT WAVE   H HOME";
        CenteredText(controls,screen(0,-5.57f),.033f,Muted);
    }

    void Hud(glm::vec2 camera) {
        const auto& stats = Game().Stats();
        const auto& player = Game().Get<Player>(Game().PlayerEntity());
        const auto& vitality = Game().Get<Health>(Game().PlayerEntity());
        const auto& combat = Game().Get<CombatStats>(Game().PlayerEntity());
        const auto screen = [&](float x, float y) { return camera + glm::vec2(x, y); };
        batch_.Rect(screen(0, 5.35f), {2 * ViewHalfWidth, 1.30f}, Panel);
        batch_.Rect(screen(0, 4.7f), {2 * ViewHalfWidth, .035f}, {.35f, .40f, .23f, 1});
        batch_.Text("BROTATO", screen(-10.15f, 5.73f), .105f, Green);
        const auto setup=Game().ExtractRunSetup();
        batch_.Text(setup.profiles?std::string(Characters[setup.character].name)+" / "+RunDifficulties[setup.difficulty].name:Game().Settings().expanded?"SURVIVAL + GROWTH":"SOURCE RULES", screen(-10.15f, 4.98f), setup.profiles?.028f:.040f, Muted);
        batch_.Text("HP " + std::to_string(vitality.current) + "/" + std::to_string(vitality.maximum),
            screen(-4.85f, 5.72f), .067f, White);
        const float health = std::clamp(float(vitality.current) / vitality.maximum, 0.f, 1.f);
        batch_.Rect(screen(-2.95f, 4.99f), {3.8f, .20f}, {.24f, .17f, .14f, 1});
        if (health > 0) batch_.Rect(screen(-4.85f + 1.9f * health, 4.99f), {3.8f * health, .20f}, {.79f, .26f, .24f, 1});
        batch_.Text("WAVE " + std::to_string(stats.wave)+(Game().Settings().campaign?"/"+std::to_string(Game().Settings().campaignWaves):""), screen(.0f, 5.72f), .075f, White);
        batch_.Text("TIME " + std::to_string(DisplaySeconds(stats.remaining)) + "S", screen(.0f, 5.13f), .055f,
            stats.remaining < 5 ? glm::vec4(1, .53f, .30f, 1) : Muted);
        if(Game().Settings().encounters && Game().Settings().expanded){
            const auto encounter=Game().ExtractEncounter();
            constexpr const char* phases[]{"OPENING","PRESSURE","FINAL"};
            batch_.Text(phases[encounter.phase],screen(3.1f,5.13f),.030f,White);
            if(encounter.event!=EncounterEvent::None){
                constexpr const char* names[]{"","SWARM","CROSSFIRE","STAMPEDE","REINFORCEMENTS"};
                batch_.Rect(screen(0,3.47f),{5.5f,.40f},{.18f,.12f,.10f,.9f});
                CenteredText(names[std::size_t(encounter.event)],screen(0,3.47f),.037f,{1,.7f,.3f,1});
            }
        }
        batch_.Sprite("material", screen(4.62f, 5.39f), {.37f, .43f});
        batch_.Text("MATERIALS " + std::to_string(player.materials), screen(5.05f, 5.72f), .062f, Green);
        batch_.Text("LV " + std::to_string(player.level) + "  XP " + std::to_string(player.experience) + "/" + std::to_string(ExperienceThreshold(player.level,Game().Settings().expanded)),
            screen(5.05f, 5.13f), .047f, Muted);
        if(Game().GetState()==State::Shop) {ShopPanel(camera);return;}
        const auto bosses=Game().ExtractBossStatus();
        if(!bosses.empty()){
            const auto& boss=bosses[0];batch_.Rect(screen(0,4.19f),{10.5f,.78f},Panel);
            const auto color=boss.enraged?glm::vec4(1,.27f,.25f,1):glm::vec4(.85f,.45f,1,1);
            CenteredText(std::string(boss.finalBoss?"FINAL GUARDIAN":"GUARDIAN")+(boss.enraged?" / ENRAGED":" / PHASE 1")+"   "+std::to_string(boss.health)+"/"+std::to_string(boss.maximum),screen(0,4.38f),.040f,color);
            const float fraction=std::clamp(float(boss.health)/boss.maximum,0.f,1.f);batch_.Rect(screen(0,4.04f),{9.8f,.12f},Muted);
            if(fraction>0)batch_.Rect(screen(-4.9f+4.9f*fraction,4.04f),{9.8f*fraction,.12f},color);
        }
        batch_.Rect(screen(0, -5.42f), {2 * ViewHalfWidth, 1.16f}, Panel);
        batch_.Text("KILLS " + std::to_string(stats.kills), screen(-10.15f, -4.93f), .042f, White);
        const auto kind = Game().CurrentWeapon();
        const auto& definition = Game().Definition(kind);
        const auto& weapon = Game().Get<Weapon>(Game().WeaponEntity());
        batch_.Text(definition.name, screen(-6.70f, -4.93f), .044f, Green);
        std::ostringstream status;
        if (weapon.phase != WeaponPhase::Idle || weapon.flash > 0) status << "ATTACKING";
        else if (weapon.cooldown > 0) status << "COOLDOWN " << std::fixed << std::setprecision(2) << weapon.cooldown / combat.attackSpeed << 'S';
        else status << "READY";
        batch_.Text(status.str(), screen(-1.95f, -4.93f), .042f, White);
        const float recovery=definition.cooldown*(Game().Settings().arsenal && Game().Settings().builds ? 1-.1f*std::clamp(weapon.tier,0,3):1);
        const float ready = 1.f - std::clamp(weapon.cooldown / recovery, 0.f, 1.f);
        batch_.Rect(screen(5.20f, -5.08f), {2.8f, .13f}, {.24f, .27f, .20f, 1});
        if (ready > 0) batch_.Rect(screen(3.8f + 1.4f * ready, -5.08f), {2.8f * ready, .13f}, Green);
        batch_.Text("AUTO AIM", screen(7.15f, -4.93f), .042f, Muted);

        std::array<std::string, WeaponCount> slots{"1 WAND", "2 TORCH", "3 TASER", "4 SHIV", "5 SMG", "6 DOUBLE BARREL"};
        const auto equipment=Game().ExtractEquipment();
        if(Game().Settings().arsenal && Game().Settings().builds)for(std::size_t i=0;i<EquipmentSlots;++i)slots[i]=std::to_string(i+1)+" "+(equipment.slots[i].present?Game().Definition(equipment.slots[i].kind).name+" T"+std::to_string(equipment.slots[i].tier+1):"EMPTY");
        for (size_t index = 0; index < slots.size(); ++index) {
            const float center = -8.5f + float(index) * 3.4f;
            const bool selected = Game().Settings().arsenal && Game().Settings().builds ? equipment.slots[index].present && equipment.slots[index].entity.GetID()==Game().WeaponEntity().GetID():index == WeaponIndex(kind);
            batch_.Rect(screen(center, -5.41f), {3.28f, .36f}, selected ? glm::vec4(.23f, .32f, .14f, 1) : glm::vec4(.11f, .13f, .10f, 1));
            CenteredText(slots[index], screen(center, -5.41f), std::min(.034f,3.1f/(6.f*slots[index].size())), selected ? Green : Muted);
        }
        const std::string controls = Game().Settings().expanded ?
            "DMG " + std::to_string(Game().Settings().arsenal && Game().Settings().builds ? equipment.slots[std::clamp(weapon.equipmentSlot,0,5)].damage:definition.damage+combat.bonusDamage) + "  ATK " + std::to_string(int(std::round(combat.attackSpeed*100))) +
            "%  MOVE " + std::to_string(int(std::round(combat.moveSpeed*100))) + "%   WASD MOVE  P PAUSE  R RESTART  M MUTE" :
            "WASD MOVE   P/ESC PAUSE   R RESTART   ENTER NEXT WAVE   V REVIVE   M MUTE";
        CenteredText(controls,screen(0,-5.80f),std::min(.035f,18.f/(6.f*controls.size())),Muted);

        if (Game().GetState() == State::Playing) return;
        batch_.Rect(camera, {2 * ViewHalfWidth, 9.8f}, {0, 0, 0, .46f});
        const float panelWidth = Game().GetState() == State::LevelUp ? 18.4f : 12.7f;
        batch_.Rect(screen(0, -.1f), {panelWidth, 6.4f}, Panel);
        batch_.Rect(screen(0, 2.95f), {panelWidth, .055f}, Green);
        CenteredText(StateName(Game().GetState()), screen(0, 2.04f), .145f,
            Game().GetState() == State::Dead || Game().GetState()==State::Defeat ? glm::vec4(.95f, .39f, .30f, 1) : Green);
        std::string detail;
        switch (Game().GetState()) {
        case State::Paused: detail = "TAKE A BREATHER"; break;
        case State::WaveComplete:
            detail = "NEXT WAVE: " + std::to_string(int(Game().Settings().waveSeconds + stats.wave * Game().Settings().waveIncrement)) + " SECONDS";
            break;
        case State::Dead: detail = "WAVE " + std::to_string(stats.wave) + "   KILLS " + std::to_string(stats.kills); break;
        case State::LevelUp: detail = "CHOOSE ONE  /  " + std::to_string(Game().Get<Growth>(Game().PlayerEntity()).pending) + " CHOICE(S) LEFT"; break;
        case State::Victory: detail="FINAL GUARDIAN DEFEATED";break;
        case State::Defeat: detail=Game().ExtractResult().reason==RunEndReason::BossEscaped?"BOSS SURVIVED THE DEADLINE":"YOUR RUN HAS ENDED";break;
        default: break;
        }
        CenteredText(detail, screen(0, 1.04f), Game().GetState() == State::LevelUp ? .055f : .065f, White);
        if(Game().GetState()==State::Victory || Game().GetState()==State::Defeat){
            const auto result=Game().ExtractResult();std::ostringstream text;text<<"WAVE "<<result.wave<<"   KILLS "<<result.kills<<"   BOSSES "<<result.bossKills<<"   TIME "<<int(std::round(result.seconds))<<"S";
            CenteredText(text.str(),screen(0,.47f),.040f,Muted);
            CenteredText("LEVEL "+std::to_string(result.level)+"   WEAPONS "+std::to_string(result.weapons)+"   ITEMS "+std::to_string(result.items)+"   MATERIALS "+std::to_string(result.materials),screen(0,.03f),.036f,Muted);
            if(Game().ExtractRunSetup().profiles)CenteredText(std::string(Characters[result.character].name)+" / "+RunDifficulties[result.difficulty].name,screen(0,-2.12f),.033f,Green);
            CenteredText("R / ENTER NEW RUN   H RETURN HOME",screen(0,-2.58f),.039f,Muted);
        }
        if (Game().GetState() == State::LevelUp) CenteredText("CLICK / 1-3 SELECT   ARROWS + ENTER CONFIRM",screen(0,-2.08f),.032f,Muted);
        for (const auto& button : Buttons()) DrawButton(button, camera);
    }

    size_t RenderFrame(int width, int height, bool present) {
        Require(batch_.Ready(), batch_.Error().empty() ? "Sprite batch not ready" : batch_.Error());
        std::size_t spriteCount = 0;
        if (session_.CurrentScreen() == Screen::Run) {
            const auto playerPosition = Game().Get<Transform>(Game().PlayerEntity()).position;
            const auto camera = glm::clamp(playerPosition, glm::vec2(10.1f, -7.f), glm::vec2(12.04f, 7.f));
            Require(batch_.Begin(ViewHalfWidth, ViewHalfHeight, camera), "Sprite view could not begin");
            Arena();
            for (const auto& cue : Game().ExtractCombatCues()) {
                if (cue.kind == CombatCueKind::Blast)
                    batch_.Ring(cue.position,cue.radius*(.75f+.25f*cue.progress),.06f,{1,.7f,.2f,1-cue.progress});
                else if(cue.kind==CombatCueKind::BossRing)batch_.Ring(cue.position,cue.radius,.07f,{.95f,.3f,1,.85f});
                else if(cue.kind==CombatCueKind::Heal || cue.kind==CombatCueKind::Summon){
                    const auto color=cue.kind==CombatCueKind::Heal?glm::vec4(.3f,1,.5f,.35f+.5f*cue.progress):glm::vec4(.8f,.4f,1,.35f+.5f*cue.progress);
                    batch_.Ring(cue.position,cue.radius,.05f,color);
                    CenteredText(cue.kind==CombatCueKind::Heal?"HEAL":"SUMMON",cue.position+glm::vec2(0,cue.kind==CombatCueKind::Heal?-.9f:-1.35f),.03f,color);
                }
                else if(cue.kind==CombatCueKind::BossFan){
                    const auto direction=cue.end-cue.position;for(float angle:{-cue.progress,cue.progress}){const auto ray=Combat::Rotate(direction,angle);batch_.Line(cue.position,cue.position+ray,.05f,{1,.65f,.15f,.8f});}
                    batch_.Line(cue.position,cue.end,.035f,{1,.65f,.15f,.45f});
                } else {
                    const auto color = cue.kind == CombatCueKind::Charge ? glm::vec4(1,.25f,.1f,.5f+.4f*cue.progress) : glm::vec4(1,.8f,.2f,.55f);
                    batch_.Line(cue.position,cue.end,cue.kind == CombatCueKind::Charge ? cue.radius*2 : .045f,{color.r,color.g,color.b,.12f});
                    batch_.Line(cue.position,cue.end,.055f,color);
                    batch_.Ring(cue.end,.22f,.04f,color,16);
                }
            }
            const auto sprites = Game().Extract();
            for (const auto& sprite : sprites) {
                const auto* name = ImageName(sprite.image, Game().Settings().character);
                const bool drawn = sprite.affine
                    ? batch_.SpriteAffine(name, sprite.position, sprite.axisX, sprite.axisY, sprite.tint, sprite.flipX, sprite.flipY)
                    : batch_.Sprite(name, sprite.position, sprite.size, sprite.angle, sprite.tint, sprite.flipX, sprite.flipY);
                Require(drawn, batch_.Error());
            }
            for (const auto& text : Game().ExtractDamageText())
                CenteredText((text.damage ? "-" : "+") + std::to_string(text.value), text.position, text.size / 7.f, text.tint);
            for (const auto& enemy : Game().ExtractEnemyStatus()) {
                const auto color = enemy.kind == EnemyKind::Elite ? glm::vec4(1,.3f,.7f,1) : enemy.kind == EnemyKind::Charger ? glm::vec4(1,.25f,.1f,1) :
                    enemy.kind == EnemyKind::Ranged ? glm::vec4(1,.8f,.2f,1) : enemy.kind == EnemyKind::Healer ? Green : enemy.kind == EnemyKind::Summoner ? glm::vec4(.8f,.4f,1,1) : enemy.kind == EnemyKind::Armored ? glm::vec4(1,.55f,.3f,1) : enemy.kind == EnemyKind::Fast ? Green : Muted;
                const float fraction = std::clamp(float(enemy.health)/enemy.maximum,0.f,1.f);
                batch_.Rect(enemy.position,{.85f,.09f},Panel);
                batch_.Rect(enemy.position+glm::vec2((fraction-1)*.425f,0),{.85f*fraction,.075f},color);
                const std::string prefix=enemy.champion==ChampionKind::Swift?"SWIFT ":enemy.champion==ChampionKind::Bulwark?"BULWARK ":"";
                CenteredText(prefix+EnemyDefinitions[std::size_t(enemy.kind)].name,enemy.position+glm::vec2(0,.19f),.021f,color);
                if(enemy.burning || enemy.slow<1) CenteredText(enemy.burning?(enemy.slow<1?"BURN + SLOW":"BURN"):"SLOW",enemy.position+glm::vec2(0,.45f),.025f,enemy.burning?glm::vec4(1,.45f,.12f,1):glm::vec4(.35f,.75f,1,1));
            }
            Hud(camera);
            spriteCount = sprites.size();
        } else {
            Require(batch_.Begin(ViewHalfWidth, ViewHalfHeight, {}), "Menu view could not begin");
            spriteCount = Menu();
        }

        Render::RHICommand::BeginFrame begin;
        begin.frameIndex = ++frameIndex_;
        begin.framebufferWidth = width;
        begin.framebufferHeight = height;
        begin.clearColor = ClearColor;
        auto encoder = device_->BeginFrame(begin);
        // Retain world/HUD proportions at any window size; the full framebuffer
        // was cleared first so unused letterbox pixels stay dark.
        const auto fitted = MenuLayout::Fit(width, height);
        Render::RHICommand::SetViewport viewport;
        viewport.x = fitted.x;
        viewport.y = fitted.y;
        viewport.width = fitted.width;
        viewport.height = fitted.height;
        if (!encoder.SetViewport(viewport) || !batch_.Flush(encoder) || !encoder.End(present)) {
            encoder.Cancel();
            throw std::runtime_error("Brotato frame command recording failed");
        }
        auto completion = std::make_shared<Completion>();
        if (!batch_.Submit(encoder.GetCommandBuffer(), [completion] { completion->done = true; })) {
            encoder.Cancel();
            throw std::runtime_error("Brotato frame submission failed");
        }
        Wait([completion] { return completion->done; }, "Brotato frame completion");
        Require(batch_.Error().empty(), batch_.Error());
        return spriteCount;
    }

    std::shared_ptr<Pixels> Capture(int width, int height) {
        auto result = std::make_shared<Pixels>();
        result->width = width;
        result->height = height;
        result->rgba.resize(size_t(width) * height * 4);
        device_->async_ExecuteCode([result] {
            glReadBuffer(GL_BACK);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(0, 0, result->width, result->height, GL_RGBA, GL_UNSIGNED_BYTE, result->rgba.data());
            result->error = glGetError();
        }, [result] { result->done = true; });
        Wait([result] { return result->done; }, "framebuffer capture");
        Require(result->error == GL_NO_ERROR, "OpenGL error during Brotato render/readback: " + std::to_string(result->error));
        return result;
    }

    static void Save(const Pixels& pixels, const std::filesystem::path& path) {
        if (path.empty()) return;
        if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
        std::vector<unsigned char> topDown(pixels.rgba.size());
        const size_t row = size_t(pixels.width) * 4;
        for (int y = 0; y < pixels.height; ++y)
            std::copy_n(pixels.rgba.data() + size_t(pixels.height - y - 1) * row, row, topDown.data() + size_t(y) * row);
        std::ofstream output(path, std::ios::binary);
        Require(bool(output), "Cannot open capture output");
        const auto write = [](void* context, void* data, int size) {
            static_cast<std::ofstream*>(context)->write(static_cast<const char*>(data), size);
        };
        const int success = stbi_write_png_to_func(write, &output, pixels.width, pixels.height, 4, topDown.data(), int(row));
        output.flush();
        Require(success != 0 && bool(output), "Cannot write capture PNG");
        std::cout << "Capture: " << std::filesystem::absolute(path).string() << '\n';
    }
};
}

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
#else
int main(int argc, char** argv) {
#endif
    int result = 0;
    try {
        const auto options = Parse(argc, argv);
        Require(glfwInit() == GLFW_TRUE, "GLFW initialization failed");
        glfwWindowHint(GLFW_VISIBLE, options.smoke ? GLFW_FALSE : GLFW_TRUE);
        ApplicationWindow::Window window({1280, 720, "Brotato | Survival + Growth", !options.smoke});
        Require(window.Activate(), "OpenGL 4.5 window creation failed");
        Application application(window, options);
        application.Start(options);
        if (options.smoke) application.Smoke(options);
        else application.Interactive(options);
    } catch (const std::exception& error) {
        std::cerr << "Brotato failed: " << error.what() << '\n';
        result = 1;
    }
    glfwTerminate();
    return result;
}
