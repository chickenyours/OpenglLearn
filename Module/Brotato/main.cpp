#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <glad/glad.h>
#include "ApplicationWindow/public/window.h"
#include "Brotato/Public/game_module.h"
#include "Render/Private/rhi_device.h"
#include "Render/Public/Sprite/sprite_batch.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace {
using namespace Brotato;
constexpr float ViewHalfHeight = 6.f;
constexpr float ViewHalfWidth = ViewHalfHeight * 16.f / 9.f;
constexpr glm::vec4 White{.96f, .94f, .87f, 1};
constexpr glm::vec4 Muted{.65f, .69f, .65f, 1};
constexpr glm::vec4 Green{.62f, .84f, .30f, 1};
constexpr glm::vec4 Panel{.065f, .075f, .062f, .96f};

void Require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

struct Options {
    std::filesystem::path assets, capture;
    bool smoke = false, smokeArena = false;
    int smokeTicks = 720;
    int weapon = 1;
};

template<class Character> Options Parse(int argc, Character** argv) {
    Options options;
    const std::filesystem::path executable = std::filesystem::absolute(argv[0]).parent_path();
    for (int i = 1; i < argc; ++i) {
        const std::string argument = std::filesystem::path(argv[i]).string();
        if (argument == "--smoke-test") options.smoke = true;
        else if (argument == "--smoke-arena") options.smoke = options.smokeArena = true;
        else if (argument == "--assets" || argument == "--capture" || argument == "--smoke-ticks" || argument == "--weapon") {
            Require(i + 1 < argc, "Missing value for " + argument);
            if (argument == "--assets") options.assets = std::filesystem::path(argv[++i]);
            else if (argument == "--capture") options.capture = std::filesystem::path(argv[++i]);
            else {
                const std::string value = std::filesystem::path(argv[++i]).string();
                size_t consumed = 0;
                const int parsed = std::stoi(value, &consumed);
                if (argument == "--weapon") {
                    Require(consumed == value.size() && parsed >= 1 && parsed <= int(WeaponCount),
                        "--weapon must be an integer between 1 and 6");
                    options.weapon = parsed;
                } else {
                    Require(consumed == value.size() && parsed >= 0 && parsed <= 120000,
                        "--smoke-ticks must be an integer between 0 and 120000");
                    options.smokeTicks = parsed;
                }
            }
        } else throw std::runtime_error("Usage: brotato_game [--assets DIR] [--weapon 1..6] [--smoke-test | --smoke-arena] [--smoke-ticks N] [--capture FILE.png]");
    }
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
    if (options.smokeArena) {
        config.spawning = false;
        config.enemySpeed = 0;
    }
    return config;
}

const char* ImageName(Image image) {
    switch (image) {
    case Image::Player: return "player";
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

class Application {
public:
    Application(ApplicationWindow::Window& window, const Options& options) : window_(window), device_(new Render::RHIDevice),
        batch_(device_.GenWeakPtr()), game_(GameConfig(options)) {}
    ~Application() {
        // Every asynchronous lambda captures owned shared state. Join the render
        // thread while the window and any in-progress readback are still alive.
        batch_.Shutdown();
        device_->StopAndRelease();
        while (device_->returnSystem.DrainCallbacks(4096)) {}
        game_.Shutdown();
    }

    void Start(const Options& options) {
        const auto context = window_.GetRenderContextAsOpengl();
        Require(context.has_value(), "Missing OpenGL render context");
        device_->Run(Render::BackendType::Opengl, *context);
        std::vector<Render::SpriteImage> images;
        for (const char* name : {"player", "enemy", "weapon", "bullet", "material", "floor", "spawn",
            "weapon_torch", "weapon_laser", "weapon_knife", "weapon_gun", "weapon_burst",
            "projectile_gun", "projectile_burst", "laser_segment", "muzzle_flash"})
            images.push_back({name, options.assets / (std::string(name) + ".png")});
        Require(batch_.Initialize(images), batch_.Error());
        Wait([&] { return batch_.Ready() || !batch_.Error().empty(); }, "sprite initialization");
        Require(batch_.Ready(), batch_.Error());
        Require(game_.Startup(), game_.Error());
        if (options.smokeArena) {
            // An explicitly requested deterministic weapon showcase. Ordinary
            // --smoke-test still uses the complete seeded survival simulation.
            const auto target = game_.SpawnEnemy(game_.Settings().playerStart + glm::vec2(2.5f, 0), true);
            Require(game_.Scene()->IsAlive(target), "Weapon smoke arena could not spawn its target");
        }
    }

    void Smoke(const Options& options) {
        for (int tick = 0; tick < options.smokeTicks; ++tick) game_.FixedTick();
        int width = 0, height = 0;
        glfwGetFramebufferSize(window_.GetNativeWindow(), &width, &height);
        Require(width > 0 && height > 0, "Smoke framebuffer has no pixels");
        const auto sprites = RenderFrame(width, height, false);
        auto pixels = Capture(width, height);
        const auto& stats = game_.Stats();
        Require(sprites > 0, "Smoke extraction produced no sprites");
        size_t visible = 0;
        for (size_t i = 0; i < pixels->rgba.size(); i += 4)
            if (pixels->rgba[i] > 50 || pixels->rgba[i + 1] > 50 || pixels->rgba[i + 2] > 50) ++visible;
        Require(visible > size_t(width) * height / 5, "Smoke framebuffer is blank or missing its arena");
        Save(*pixels, options.capture);
        std::cout << "Brotato OpenGL smoke passed: scenario=" << (options.smokeArena ? "weapon-arena" : "seeded-survival")
                  << " weapon=" << options.weapon << " ticks=" << stats.ticks
                  << " entities=" << 1 + stats.weapons + stats.enemies + stats.projectiles + stats.pickups
                  << " drawSprites=" << sprites << " batchDraws=1"
                  << " wave=" << stats.wave << " enemies=" << stats.enemies
                  << " shots=" << stats.shots << " kills=" << stats.kills
                  << " materials=" << game_.Get<Player>(game_.PlayerEntity()).materials
                  << " visiblePixels=" << visible << " GL errors=0\n";
    }

    void Interactive(const Options& options) {
        std::cout << "Brotato ECS migration 02: WASD/arrows move | 1-6 select weapon | P pause | R restart\n"
                     "1 Wand | 2 Torch | 3 Taser | 4 Lightning shiv | 5 SMG | 6 Double barrel\n"
                     "Enter next wave | V revive after defeat | Esc exit\n";
        auto* native = window_.GetNativeWindow();
        std::array<bool, GLFW_KEY_LAST + 1> previous{};
        double lastTime = glfwGetTime(), titleTime = -1;
        bool captured = false;
        while (!window_.ShouldClose()) {
            window_.PollEvents();
            const bool focused = glfwGetWindowAttrib(native, GLFW_FOCUSED) != 0;
            const auto down = [&](int key) { return focused && glfwGetKey(native, key) == GLFW_PRESS; };
            const auto pressed = [&](int key) {
                const bool value = down(key), edge = value && !previous[key];
                previous[key] = value;
                return edge;
            };
            if (down(GLFW_KEY_ESCAPE)) break;
            const double now = glfwGetTime(), elapsed = std::clamp(now - lastTime, 0.0, .1);
            lastTime = now;
            Input input;
            input.horizontal = float(down(GLFW_KEY_D) || down(GLFW_KEY_RIGHT)) - float(down(GLFW_KEY_A) || down(GLFW_KEY_LEFT));
            input.vertical = float(down(GLFW_KEY_W) || down(GLFW_KEY_UP)) - float(down(GLFW_KEY_S) || down(GLFW_KEY_DOWN));
            input.pause = pressed(GLFW_KEY_P);
            input.restart = pressed(GLFW_KEY_R);
            input.nextWave = pressed(GLFW_KEY_ENTER);
            input.revive = pressed(GLFW_KEY_V);
            for (int index = 0; index < int(WeaponCount); ++index)
                if (pressed(GLFW_KEY_1 + index)) input.selectWeapon = index + 1;
            game_.Advance(elapsed, input);
            int width = 0, height = 0;
            glfwGetFramebufferSize(native, &width, &height);
            if (width < 1 || height < 1) { glfwWaitEventsTimeout(.05); continue; }
            if (!captured && !options.capture.empty()) {
                RenderFrame(width, height, false);
                Save(*Capture(width, height), options.capture);
                captured = true;
            }
            RenderFrame(width, height, true);
            if (now - titleTime > .25) {
                const auto& stats = game_.Stats();
                const auto& player = game_.Get<Player>(game_.PlayerEntity());
                std::ostringstream title;
                title << "Brotato | ECS migration 02 | " << StateName(game_.GetState())
                      << " | " << game_.Definition(game_.CurrentWeapon()).name
                      << " | Wave " << stats.wave << " | HP " << player.health << '/' << game_.Settings().initialHealth
                      << " | " << DisplaySeconds(stats.remaining) << "s | WASD move - 1-6 weapon - P pause";
                glfwSetWindowTitle(native, title.str().c_str());
                titleTime = now;
            }
        }
    }

private:
    ApplicationWindow::Window& window_;
    ObjectPtr<Render::RHIDevice> device_;
    Render::SpriteBatch2D batch_;
    GameModule game_;
    uint64_t frameIndex_ = 0;

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

    void Arena() {
        const auto& settings = game_.Settings();
        const glm::vec2 lower = settings.minimum - glm::vec2(.45f), upper = settings.maximum + glm::vec2(.45f);
        const glm::vec2 center = (lower + upper) * .5f, size = upper - lower;
        batch_.Rect(center, size + glm::vec2(.32f), {.17f, .16f, .125f, 1});
        batch_.Rect(center, size, {.44f, .39f, .32f, 1});
        constexpr float tile = 1.28f;
        for (float y = lower.y; y < upper.y; y += tile) {
            for (float x = lower.x; x < upper.x; x += tile) {
                const glm::vec2 extent{std::min(tile, upper.x - x), std::min(tile, upper.y - y)};
                batch_.Sprite("floor", glm::vec2(x, y) + extent * .5f, extent);
            }
        }
        const glm::vec4 border{.25f, .22f, .17f, 1};
        batch_.Rect({lower.x, center.y}, {.13f, size.y}, border);
        batch_.Rect({upper.x, center.y}, {.13f, size.y}, border);
        batch_.Rect({center.x, lower.y}, {size.x, .13f}, border);
        batch_.Rect({center.x, upper.y}, {size.x, .13f}, border);
        // Bright corner pegs make the collision boundary readable while moving.
        for (float x : {lower.x, upper.x}) for (float y : {lower.y, upper.y})
            batch_.Rect({x, y}, {.23f, .23f}, {.67f, .59f, .40f, 1});
    }

    void Hud(glm::vec2 camera) {
        const auto& stats = game_.Stats();
        const auto& player = game_.Get<Player>(game_.PlayerEntity());
        const auto screen = [&](float x, float y) { return camera + glm::vec2(x, y); };
        batch_.Rect(screen(0, 5.35f), {2 * ViewHalfWidth, 1.30f}, Panel);
        batch_.Rect(screen(0, 4.7f), {2 * ViewHalfWidth, .035f}, {.35f, .40f, .23f, 1});
        batch_.Text("BROTATO", screen(-10.15f, 5.73f), .105f, Green);
        batch_.Text("ECS MIGRATION 02", screen(-10.15f, 4.98f), .040f, Muted);
        batch_.Text("HP " + std::to_string(player.health) + "/" + std::to_string(game_.Settings().initialHealth),
            screen(-4.85f, 5.72f), .067f, White);
        const float health = std::clamp(float(player.health) / game_.Settings().initialHealth, 0.f, 1.f);
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
        const auto kind = game_.CurrentWeapon();
        const auto& definition = game_.Definition(kind);
        const auto& weapon = game_.Get<Weapon>(game_.WeaponEntity());
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
        CenteredText("WASD / ARROWS MOVE   P PAUSE   R RESTART   ENTER NEXT WAVE   V REVIVE   ESC EXIT",
            screen(0, -5.80f), .035f, Muted);

        if (game_.GetState() == State::Playing) return;
        batch_.Rect(camera, {2 * ViewHalfWidth, 9.8f}, {0, 0, 0, .46f});
        batch_.Rect(camera, {12.7f, 4.1f}, Panel);
        batch_.Rect(screen(0, 2.03f), {12.7f, .055f}, Green);
        CenteredText(StateName(game_.GetState()), screen(0, 1.10f), .145f,
            game_.GetState() == State::Dead ? glm::vec4(.95f, .39f, .30f, 1) : Green);
        std::string detail, action;
        switch (game_.GetState()) {
        case State::Paused:
            detail = "TAKE A BREATHER";
            action = "P RESUME   R RESTART";
            break;
        case State::WaveComplete:
            detail = "NEXT WAVE: " + std::to_string(int(game_.Settings().waveSeconds + stats.wave * game_.Settings().waveIncrement)) + " SECONDS";
            action = "ENTER NEXT WAVE   R RESTART";
            break;
        case State::Dead:
            detail = "WAVE " + std::to_string(stats.wave) + "   KILLS " + std::to_string(stats.kills);
            action = "V REVIVE   R NEW RUN";
            break;
        default: break;
        }
        CenteredText(detail, screen(0, .08f), .065f, White);
        CenteredText(action, screen(0, -1.07f), .057f, Muted);
    }

    size_t RenderFrame(int width, int height, bool present) {
        Require(batch_.Ready(), batch_.Error().empty() ? "Sprite batch not ready" : batch_.Error());
        const auto playerPosition = game_.Get<Transform>(game_.PlayerEntity()).position;
        const auto camera = glm::clamp(playerPosition, glm::vec2(10.1f, -7.f), glm::vec2(12.04f, 7.f));
        Require(batch_.Begin(ViewHalfWidth, ViewHalfHeight, camera), "Sprite view could not begin");
        Arena();
        const auto sprites = game_.Extract();
        for (const auto& sprite : sprites) {
            if (sprite.image == Image::Player || sprite.image == Image::Enemy)
                batch_.Sprite(ImageName(sprite.image), sprite.position + glm::vec2(0, -.34f), {.66f, .13f}, 0, {0, 0, 0, .24f});
        }
        for (const auto& sprite : sprites)
            Require(batch_.Sprite(ImageName(sprite.image), sprite.position, sprite.size, sprite.angle,
                sprite.tint, sprite.flipX), batch_.Error());
        Hud(camera);

        Render::RHICommand::BeginFrame begin;
        begin.frameIndex = ++frameIndex_;
        begin.framebufferWidth = width;
        begin.framebufferHeight = height;
        begin.clearColor = {.032f, .04f, .032f, 1};
        auto encoder = device_->BeginFrame(begin);
        // Retain world/HUD proportions at any window size; the full framebuffer
        // was cleared first so unused letterbox pixels stay dark.
        int viewportWidth = width, viewportHeight = int(std::round(width * 9.0 / 16.0));
        if (viewportHeight > height) { viewportHeight = height; viewportWidth = int(std::round(height * 16.0 / 9.0)); }
        Render::RHICommand::SetViewport viewport;
        viewport.x = (width - viewportWidth) / 2;
        viewport.y = (height - viewportHeight) / 2;
        viewport.width = std::max(1, viewportWidth);
        viewport.height = std::max(1, viewportHeight);
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
        return sprites.size();
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
        ApplicationWindow::Window window({1280, 720, "Brotato | ECS migration 02", !options.smoke});
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
