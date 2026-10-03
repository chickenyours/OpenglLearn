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
    bool smoke = false, smokeArena = false, mute = false;
    int smokeTicks = 720;
    int weapon = 1, character = 1;
    std::size_t map = RandomMap;
    std::string smokeMenu, smokePresentation;
};

template<class Character> Options Parse(int argc, Character** argv) {
    Options options;
    const std::filesystem::path executable = std::filesystem::absolute(argv[0]).parent_path();
    for (int i = 1; i < argc; ++i) {
        const std::string argument = std::filesystem::path(argv[i]).string();
        if (argument == "--smoke-test") options.smoke = true;
        else if (argument == "--mute") options.mute = true;
        else if (argument == "--smoke-arena") options.smoke = options.smokeArena = true;
        else if (argument == "--assets" || argument == "--capture" || argument == "--smoke-ticks" || argument == "--weapon" ||
                 argument == "--character" || argument == "--map" || argument == "--smoke-menu" ||
                 argument == "--smoke-presentation" || argument == "--capture-audio") {
            Require(i + 1 < argc, "Missing value for " + argument);
            if (argument == "--assets") options.assets = std::filesystem::path(argv[++i]);
            else if (argument == "--capture") options.capture = std::filesystem::path(argv[++i]);
            else if (argument == "--capture-audio") options.captureAudio = std::filesystem::path(argv[++i]);
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
        } else throw std::runtime_error("Usage: brotato_game [--assets DIR] [--weapon 1..6] [--character 1..5] [--map 1..5|random] [--mute] [--smoke-test | --smoke-arena | --smoke-menu SCENE | --smoke-presentation SCENE] [--smoke-ticks N] [--capture FILE.png] [--capture-audio FILE.wav]");
    }
    Require(options.smokeMenu.empty() || !options.smokeArena, "--smoke-menu and --smoke-arena cannot be combined");
    Require(options.smokePresentation.empty() || (options.smokeMenu.empty() && !options.smokeArena), "Choose only one smoke scenario");
    Require(options.captureAudio.empty() || options.smoke, "--capture-audio requires a smoke scenario");
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
    Config config;
    config.initialWeapon = static_cast<WeaponKind>(options.weapon - 1);
    config.character = std::size_t(options.character - 1);
    config.map = options.map;
    if (options.smokeArena || !options.smokeMenu.empty() || !options.smokePresentation.empty()) {
        config.spawning = false;
        config.enemySpeed = 0;
    }
    if (!options.smokeMenu.empty()) config.armed = false;
    if (!options.smokePresentation.empty()) {
        config.armed = options.smokePresentation == "muzzle";
        if (config.armed) config.initialWeapon = WeaponKind::Gun;
    }
    return config;
}

const char* ImageName(Image image, std::size_t character) {
    switch (image) {
    case Image::Player: return Characters.at(character).image;
    case Image::Enemy: return "enemy";
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
    }
    return "";
}

int DisplaySeconds(double remaining) {
    // Fixed-step subtraction may leave 14.00000000000034 after six seconds.
    return int(std::ceil(std::max(0.0, remaining - 1e-7)));
}

struct Completion { bool done = false; };
struct Pixels : Completion {
    int width = 0, height = 0;
    std::vector<unsigned char> rgba;
    GLenum error = GL_NO_ERROR;
};

enum class Action { ChooseCharacter, ChooseWeapon, ChooseDifficulty, ChooseMap, Back, Forward, PreviousPage, NextPage, Resume, Restart, NextWave, Revive, Home, Exit };
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
            "player_leg_left", "player_leg_right", "player_shadow", "player_mark", "hit_particle"})
            addImage(name);
        for (const auto& character : Characters) addImage(character.image);
        for (const auto& weapon : WeaponSelections) addImage(weapon.image);
        for (const auto& difficulty : Difficulties) addImage(difficulty.image);
        AppendMapImages(images, options.assets);
        Require(batch_.Initialize(images), batch_.Error());
        Wait([&] { return batch_.Ready() || !batch_.Error().empty(); }, "sprite initialization");
        Require(batch_.Ready(), batch_.Error());
        Require(session_.Startup(), session_.Error());
        Require(gameAudio_.Load(options.assets), gameAudio_.Error());
        muted_ = options.mute;
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
        std::cout << "Brotato OpenGL smoke passed: scenario=" << (!options.smokePresentation.empty() ? options.smokePresentation : !options.smokeMenu.empty() ? options.smokeMenu : options.smokeArena ? "weapon-arena" : "seeded-survival");
        if (session_.Game()) {
            const auto& stats = Game().Stats();
            Require(Game().DroppedEventCount() == 0, "Application failed to drain gameplay presentation events");
            std::cout << " weapon=" << WeaponIndex(Game().CurrentWeapon()) + 1 << " character=" << Game().Settings().character + 1
                      << " map=" << Game().Settings().map + 1 << " ticks=" << stats.ticks
                      << " entities=" << 1 + stats.weapons + stats.enemies + stats.projectiles + stats.pickups + stats.effects
                      << " wave=" << stats.wave << " enemies=" << stats.enemies
                      << " shots=" << stats.shots << " kills=" << stats.kills
                      << " effects=" << stats.effects
                      << " materials=" << Game().Get<Player>(Game().PlayerEntity()).materials;
        } else std::cout << " activeGame=0";
        if (session_.Game()) std::cout << " drawSprites=" << sprites;
        else std::cout << " menuButtons=" << Buttons().size();
        std::cout << " batchDraws=1 visiblePixels=" << visible << " audioPeak=" << audioPeak_ << " GL errors=0\n";
    }

    void Interactive(const Options& options) {
        std::cout << "Brotato ECS migration 05: click or arrows + Enter to choose | Esc back\n"
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
            const bool handledPointerAction = click && pointer_ && ActivateAt(*pointer_);
            Input input;
            if (!handledPointerAction && session_.CurrentScreen() == Screen::Run) {
                if (pressed[GLFW_KEY_H] && Game().GetState() != State::Playing) session_.ReturnHome();
                else if (pressed[GLFW_KEY_ESCAPE]) {
                    if (Game().GetState() == State::Playing || Game().GetState() == State::Paused) input.pause = true;
                    else session_.ReturnHome();
                }
                if (session_.Game()) {
                    input.horizontal = float(keys[GLFW_KEY_D] || keys[GLFW_KEY_RIGHT]) - float(keys[GLFW_KEY_A] || keys[GLFW_KEY_LEFT]);
                    input.vertical = float(keys[GLFW_KEY_W] || keys[GLFW_KEY_UP]) - float(keys[GLFW_KEY_S] || keys[GLFW_KEY_DOWN]);
                    input.pause = input.pause || pressed[GLFW_KEY_P];
                    input.restart = pressed[GLFW_KEY_R];
                    input.nextWave = pressed[GLFW_KEY_ENTER];
                    input.revive = pressed[GLFW_KEY_V];
                    for (int index = 0; index < int(WeaponCount); ++index)
                        if (pressed[GLFW_KEY_1 + index]) input.selectWeapon = index + 1;
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
            session_.Advance(session_.CurrentScreen() == screenAtFrameStart ? elapsed : 0.0, input);
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
                title << "Brotato | ECS migration 05" << (muted_ ? " | MUTED" : "");
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
        case Screen::DifficultySelect: return Difficulties.size();
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

    void SmokeTick(Input input = {}) { session_.FixedTick(input); PumpOfflineAudio(); }

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
        Activate({{}, {}, Action::ChooseDifficulty, 0});
        Activate({{}, {}, Action::Forward});
        Activate({{}, {}, Action::ChooseMap, options.map});
        Activate({{}, {}, Action::Forward});
        Require(session_.CurrentScreen() == Screen::Run && session_.Game(), "Menu actions did not start a run");
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
                Require(session_.SelectDifficulty(0), session_.Error());
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
                clickButton(Action::ChooseDifficulty, 0);
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
                Game().Get<Player>(Game().PlayerEntity()).health = Game().Settings().contactDamage;
                Game().SpawnEnemy(Game().Settings().playerStart, true);
                session_.FixedTick();
                Require(Game().GetState() == State::Dead, "Defeat smoke did not take contact damage");
            } else if (scene == "wave-complete") {
                for (int tick = 0; tick < 120000 && Game().GetState() == State::Playing; ++tick) session_.FixedTick();
                Require(Game().GetState() == State::WaveComplete, "Wave smoke did not complete");
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
            const auto definitions = DefaultWeapons();
            const auto page = selected / MenuLayout::PageSize;
            for (std::size_t index = page * MenuLayout::PageSize; index < std::min(count, (page + 1) * MenuLayout::PageSize); ++index) {
                std::string name;
                Action action = Action::ChooseMap;
                if (screen == Screen::CharacterSelect) { name = Characters[index].name; action = Action::ChooseCharacter; }
                else if (screen == Screen::WeaponSelect) { name = definitions[index].name; action = Action::ChooseWeapon; }
                else if (screen == Screen::DifficultySelect) { name = Difficulties[index].name; action = Action::ChooseDifficulty; }
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
        } else if (screen == Screen::Run && Game().GetState() != State::Playing) {
            const auto state = Game().GetState();
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
            const char* icon = button.action == Action::ChooseWeapon ? WeaponSelections[button.value].image : Difficulties[button.value].image;
            const auto size = button.action == Action::ChooseWeapon ? WeaponSelections[button.value].size : Difficulties[button.value].size;
            batch_.Sprite(icon, center + glm::vec2(0, .18f), size / std::max(size.x, size.y) * 1.03f);
            const float pixel = std::min(.034f, 1.91f / (6.f * float(button.label.size())));
            CenteredText(button.label, center + glm::vec2(0, -.59f), pixel, button.selected ? Green : White);
        } else if (button.action == Action::ChooseMap) {
            if (button.value == RandomMap) CenteredText("?", center + glm::vec2(0, .18f), .14f, Green);
            else DrawMapPreview(batch_, button.value, center + glm::vec2(0, .18f), {1.75f, 1.04f});
            CenteredText(button.label, center + glm::vec2(0, -.59f), .034f, button.selected ? Green : White);
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
            CenteredText("WASD MOVE   AUTO AIM   SIX WEAPONS", {0, -4.33f}, .042f, Muted);
        } else {
            const bool characters = screen == Screen::CharacterSelect;
            CenteredText(MenuTitle(screen), {0, 4.88f}, .11f, Green);
            batch_.Rect({5.15f, .24f}, {9.15f, 7.15f}, {.10f, .12f, .085f, 1});
            if (characters) {
                const auto& character = Characters.at(session_.SelectedCharacter());
                const auto size = character.size / std::max(character.size.x, character.size.y) * 3.0f;
                batch_.Sprite(character.image, {5.15f, 1.55f}, size);
                CenteredText(character.name, {5.15f, -.78f}, .076f, White);
                CenteredText("APPEARANCE ONLY", {5.15f, -1.58f}, .045f, Green);
                CenteredText("SAME HEALTH AND MOVEMENT", {5.15f, -2.15f}, .039f, Muted);
            } else if (screen == Screen::WeaponSelect) {
                const auto selected = WeaponIndex(session_.SelectedWeapon());
                const auto definitions = DefaultWeapons();
                const auto& definition = definitions[selected];
                const auto size = WeaponSelections[selected].size;
                batch_.Sprite(WeaponSelections[selected].image, {5.15f, 1.65f}, size / std::max(size.x, size.y) * 2.8f);
                CenteredText(definition.name, {5.15f, -.42f}, .066f, White);
                const char* style = definition.mode == AttackMode::Thrust ? "THRUST AND RETURN" :
                    definition.mode == AttackMode::SegmentedBeam ? "SHORT RANGE BEAM" :
                    definition.mode == AttackMode::AuthoredBurst ? "FOUR SHOT SPREAD" : "AUTOMATIC PROJECTILE";
                CenteredText(style, {5.15f, -1.13f}, .043f, Green);
                std::ostringstream details;
                details << std::fixed << std::setprecision(2) << "COOLDOWN " << definition.cooldown << "S";
                CenteredText(details.str(), {5.15f, -1.72f}, .043f, Muted);
                CenteredText(session_.HasSelectedWeapon() ? "STARTING WEAPON SELECTED" : "CLICK A WEAPON OR PRESS SPACE",
                    {5.15f, -2.42f}, .041f, session_.HasSelectedWeapon() ? Green : White);
            } else if (screen == Screen::DifficultySelect) {
                const auto& difficulty = Difficulties.at(session_.SelectedDifficulty());
                batch_.Sprite(difficulty.image, {5.15f, 1.5f}, difficulty.size / std::max(difficulty.size.x, difficulty.size.y) * 2.7f);
                CenteredText(difficulty.name, {5.15f, -.6f}, .077f, White);
                CenteredText("READY TO SURVIVE", {5.15f, -1.47f}, .05f, Green);
                const auto definitions = DefaultWeapons();
                CenteredText(definitions[WeaponIndex(session_.SelectedWeapon())].name, {5.15f, -2.14f}, .043f, Muted);
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
        if (screen == Screen::Home) CenteredText("ECS MIGRATION 05", {0, -5.33f}, .034f, Muted);
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

    void Arena() { DrawArena(batch_, Game().Settings().map); }

    void Hud(glm::vec2 camera) {
        const auto& stats = Game().Stats();
        const auto& player = Game().Get<Player>(Game().PlayerEntity());
        const auto screen = [&](float x, float y) { return camera + glm::vec2(x, y); };
        batch_.Rect(screen(0, 5.35f), {2 * ViewHalfWidth, 1.30f}, Panel);
        batch_.Rect(screen(0, 4.7f), {2 * ViewHalfWidth, .035f}, {.35f, .40f, .23f, 1});
        batch_.Text("BROTATO", screen(-10.15f, 5.73f), .105f, Green);
        batch_.Text("ECS MIGRATION 05", screen(-10.15f, 4.98f), .040f, Muted);
        batch_.Text("HP " + std::to_string(player.health) + "/" + std::to_string(Game().Settings().initialHealth),
            screen(-4.85f, 5.72f), .067f, White);
        const float health = std::clamp(float(player.health) / Game().Settings().initialHealth, 0.f, 1.f);
        batch_.Rect(screen(-2.95f, 4.99f), {3.8f, .20f}, {.24f, .17f, .14f, 1});
        if (health > 0) batch_.Rect(screen(-4.85f + 1.9f * health, 4.99f), {3.8f * health, .20f}, {.79f, .26f, .24f, 1});
        batch_.Text("WAVE " + std::to_string(stats.wave), screen(.0f, 5.72f), .075f, White);
        batch_.Text("TIME " + std::to_string(DisplaySeconds(stats.remaining)) + "S", screen(.0f, 5.13f), .055f,
            stats.remaining < 5 ? glm::vec4(1, .53f, .30f, 1) : Muted);
        batch_.Sprite("material", screen(4.62f, 5.39f), {.37f, .43f});
        batch_.Text("MATERIALS " + std::to_string(player.materials), screen(5.05f, 5.72f), .062f, Green);
        batch_.Text("LV " + std::to_string(player.level) + "  XP " + std::to_string(player.experience) + "/100",
            screen(5.05f, 5.13f), .047f, Muted);
        batch_.Rect(screen(0, -5.42f), {2 * ViewHalfWidth, 1.16f}, Panel);
        batch_.Text("KILLS " + std::to_string(stats.kills), screen(-10.15f, -4.93f), .042f, White);
        const auto kind = Game().CurrentWeapon();
        const auto& definition = Game().Definition(kind);
        const auto& weapon = Game().Get<Weapon>(Game().WeaponEntity());
        batch_.Text(definition.name, screen(-6.70f, -4.93f), .044f, Green);
        std::ostringstream status;
        if (weapon.phase != WeaponPhase::Idle || weapon.flash > 0) status << "ATTACKING";
        else if (weapon.cooldown > 0) status << "COOLDOWN " << std::fixed << std::setprecision(2) << weapon.cooldown << 'S';
        else status << "READY";
        batch_.Text(status.str(), screen(-1.95f, -4.93f), .042f, White);
        const float ready = 1.f - std::clamp(weapon.cooldown / definition.cooldown, 0.f, 1.f);
        batch_.Rect(screen(5.20f, -5.08f), {2.8f, .13f}, {.24f, .27f, .20f, 1});
        if (ready > 0) batch_.Rect(screen(3.8f + 1.4f * ready, -5.08f), {2.8f * ready, .13f}, Green);
        batch_.Text("AUTO AIM", screen(7.15f, -4.93f), .042f, Muted);

        constexpr std::array<const char*, WeaponCount> slots{"1 WAND", "2 TORCH", "3 TASER", "4 SHIV", "5 SMG", "6 DOUBLE BARREL"};
        for (size_t index = 0; index < slots.size(); ++index) {
            const float center = -8.5f + float(index) * 3.4f;
            const bool selected = index == WeaponIndex(kind);
            batch_.Rect(screen(center, -5.41f), {3.28f, .36f}, selected ? glm::vec4(.23f, .32f, .14f, 1) : glm::vec4(.11f, .13f, .10f, 1));
            CenteredText(slots[index], screen(center, -5.41f), .034f, selected ? Green : Muted);
        }
        CenteredText("WASD MOVE   P/ESC PAUSE   R RESTART   ENTER NEXT WAVE   V REVIVE   M MUTE",
            screen(0, -5.80f), .035f, Muted);

        if (Game().GetState() == State::Playing) return;
        batch_.Rect(camera, {2 * ViewHalfWidth, 9.8f}, {0, 0, 0, .46f});
        batch_.Rect(screen(0, -.1f), {12.7f, 6.15f}, Panel);
        batch_.Rect(screen(0, 2.95f), {12.7f, .055f}, Green);
        CenteredText(StateName(Game().GetState()), screen(0, 2.04f), .145f,
            Game().GetState() == State::Dead ? glm::vec4(.95f, .39f, .30f, 1) : Green);
        std::string detail;
        switch (Game().GetState()) {
        case State::Paused: detail = "TAKE A BREATHER"; break;
        case State::WaveComplete:
            detail = "NEXT WAVE: " + std::to_string(int(Game().Settings().waveSeconds + stats.wave * Game().Settings().waveIncrement)) + " SECONDS";
            break;
        case State::Dead: detail = "WAVE " + std::to_string(stats.wave) + "   KILLS " + std::to_string(stats.kills); break;
        default: break;
        }
        CenteredText(detail, screen(0, 1.04f), .065f, White);
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
            const auto sprites = Game().Extract();
            for (const auto& sprite : sprites) {
                const auto* name = ImageName(sprite.image, Game().Settings().character);
                const bool drawn = sprite.affine
                    ? batch_.SpriteAffine(name, sprite.position, sprite.axisX, sprite.axisY, sprite.tint, sprite.flipX, sprite.flipY)
                    : batch_.Sprite(name, sprite.position, sprite.size, sprite.angle, sprite.tint, sprite.flipX, sprite.flipY);
                Require(drawn, batch_.Error());
            }
            for (const auto& text : Game().ExtractDamageText())
                CenteredText("+" + std::to_string(text.value), text.position, text.size / 7.f, text.tint);
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
        ApplicationWindow::Window window({1280, 720, "Brotato | ECS migration 05", !options.smoke});
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
