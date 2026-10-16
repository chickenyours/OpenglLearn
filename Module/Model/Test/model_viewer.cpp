#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include "ApplicationWindow/public/window.h"
#include "Model/Public/model_module.h"
#include "Render/Private/rhi_device.h"
#include "Render/Public/Pipeline/material_render_pipeline.h"
#include "Render/Public/Pipeline/toon_render_pipeline.h"

// Keep the screenshot encoder local to this executable translation unit.
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBIW_WINDOWS_UTF8
#include <stb_image_write.h>

#ifndef MODEL_SOURCE_DIR
#define MODEL_SOURCE_DIR "."
#endif

namespace {
using namespace std::chrono_literals;

void Check(bool value, const std::string& message) {
    if (!value) throw std::runtime_error(message);
}

std::string Utf8(const std::filesystem::path& path) {
    const auto value = path.u8string();
    return {value.begin(), value.end()};
}

std::filesystem::path Path(std::string_view utf8) {
    return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

struct Options {
    std::filesystem::path model = Path(MODEL_SOURCE_DIR) / "Asset/Model/Rikka_Uniform";
    std::filesystem::path screenshot;
    std::uint64_t frames = 0;
    bool smoke = false, help = false, toon = false;
};

Options Parse(const std::vector<std::string>& arguments) {
    Options options;
    for (std::size_t i = 1; i < arguments.size(); ++i) {
        const auto& argument = arguments[i];
        if (argument == "--smoke-test") options.smoke = true;
        else if (argument == "--toon") options.toon = true;
        else if (argument == "--help" || argument == "-h") options.help = true;
        else if (argument == "--model" || argument == "--screenshot" || argument == "--frames") {
            Check(i + 1 < arguments.size(), "Missing value for " + argument);
            const auto& value = arguments[++i];
            if (argument == "--model") options.model = Path(value);
            else if (argument == "--screenshot") options.screenshot = Path(value);
            else {
                std::size_t consumed = 0;
                Check(!value.empty() && value.front() != '-', "--frames must be a positive integer");
                options.frames = std::stoull(value, &consumed);
                Check(consumed == value.size() && options.frames > 0, "--frames must be a positive integer");
            }
        } else throw std::runtime_error("Unknown argument: " + argument + "; use --help");
    }
    if (options.smoke && options.frames == 0) options.frames = 3;
    return options;
}

struct OrbitCamera {
    float yaw = 0.08f, pitch = 0.04f, distance = 4.5f, initialDistance = 4.5f;
    double lastX = 0, lastY = 0;
    bool dragging = false, toon = false;

    static OrbitCamera& Get(GLFWwindow* window) {
        return *static_cast<OrbitCamera*>(glfwGetWindowUserPointer(window));
    }
    static void Cursor(GLFWwindow* window, double x, double y) {
        auto& camera = Get(window);
        if (camera.dragging) {
            camera.yaw -= static_cast<float>(x - camera.lastX) * 0.006f;
            camera.pitch = std::clamp(camera.pitch + static_cast<float>(y - camera.lastY) * 0.006f, -1.45f, 1.45f);
        }
        camera.lastX = x;
        camera.lastY = y;
    }
    static void Button(GLFWwindow* window, int button, int action, int) {
        if (button != GLFW_MOUSE_BUTTON_LEFT && button != GLFW_MOUSE_BUTTON_RIGHT) return;
        auto& camera = Get(window);
        camera.dragging = action == GLFW_PRESS;
        glfwGetCursorPos(window, &camera.lastX, &camera.lastY);
    }
    static void Scroll(GLFWwindow* window, double, double y) {
        auto& camera = Get(window);
        camera.distance = std::clamp(camera.distance * std::exp(-static_cast<float>(y) * 0.12f),
            camera.initialDistance * 0.1f, camera.initialDistance * 10.0f);
    }
    static void Key(GLFWwindow* window, int key, int, int action, int) {
        if (action != GLFW_PRESS) return;
        if (key == GLFW_KEY_ESCAPE) glfwSetWindowShouldClose(window, GLFW_TRUE);
        if (key == GLFW_KEY_T) {
            auto& camera = Get(window);
            camera.toon = !camera.toon;
            glfwSetWindowTitle(window, camera.toon ?
                "Toon Model Viewer | T: PBR | drag: orbit | wheel: zoom | R: reset | Esc: close" :
                "PBR Model Viewer | T: Toon | drag: orbit | wheel: zoom | R: reset | Esc: close");
            std::cout << "Render mode: " << (camera.toon ? "Toon" : "PBR") << '\n';
        }
        if (key == GLFW_KEY_R) {
            auto& camera = Get(window);
            camera.yaw = 0.08f;
            camera.pitch = 0.04f;
            camera.distance = camera.initialDistance;
        }
    }
    Render::PipelineCamera View(std::uint32_t width, std::uint32_t height) const {
        Render::PipelineCamera camera;
        camera.position = distance * glm::vec3(std::sin(yaw) * std::cos(pitch), std::sin(pitch),
            std::cos(yaw) * std::cos(pitch));
        camera.nearPlane = std::max(0.005f, distance / 1000.0f);
        camera.farPlane = std::max(100.0f, distance * 20.0f);
        camera.view = glm::lookAt(camera.position, glm::vec3(0), glm::vec3(0, 1, 0));
        camera.projection = glm::perspective(glm::radians(43.0f), float(width) / float(height),
            camera.nearPlane, camera.farPlane);
        return camera;
    }
};

struct Pixels {
    std::uint32_t width = 0, height = 0;
    std::vector<std::uint8_t> rgba;
};

struct Completion { bool done = false; };

class Viewer {
public:
    Viewer(const Render::OpenglBackendContext& context, const Options& options, OrbitCamera& orbit)
        : module_(device_), pipeline_(device_), toonPipeline_(device_) {
        device_.Run(Render::BackendType::Opengl, context);
        Check(module_.Startup(), module_.LastError());
        resource_ = module_.Load(options.model);
        Check(bool(resource_), "Model load failed: " + module_.LastError());
        const auto& source = resource_->Source();
        Check(source.bounds.valid, "Model has no valid bounds");
        const auto extent = source.bounds.Extent();
        const double size = extent.y > 1e-12 ? extent.y : std::max({extent.x, extent.y, extent.z});
        Check(std::isfinite(size) && size > 1e-12, "Model bounds have zero or invalid size");
        const double scale = 3.0 / size;
        // Preserve the imported double precision until the origin-relative GPU extraction.
        placement_ = glm::scale(glm::dmat4(1), glm::dvec3(scale)) *
            glm::translate(glm::dmat4(1), -source.bounds.Center());
        // The normalized character is three units tall: 4.5 units at 43 degrees
        // fills roughly 85% of the viewport height while leaving room for its shadow.
        orbit.distance = orbit.initialDistance = 4.5f;

        settings_.shadows.enabled = true;
        settings_.shadows.atlasResolution = 1024;
        settings_.shadows.distance = 20.0f;
        settings_.shadows.sunIntensity = 1.6f;
        settings_.reflection.enabled = false;
        settings_.post.bloomEnabled = false;
        settings_.post.ssaoEnabled = false;
        settings_.post.toneMap = Render::ToneMapMode::ACES;
        settings_.sky.enabled = true;
        settings_.sky.background = false;
        settings_.sky.intensity = 0.8f;
        settings_.lighting.ambientAndExposure = glm::vec4(0.08f, 0.08f, 0.08f, 1);
        settings_.lighting.lightPositions = {glm::vec4(3, 4, 5, 1), glm::vec4(-4, 2, 2, 1),
            glm::vec4(0, 3, -4, 1), glm::vec4(0, -2, 3, 1)};
        settings_.lighting.lightColors = {glm::vec4(40, 37, 34, 0.3f), glm::vec4(18, 22, 28, 0.3f),
            glm::vec4(30, 31, 34, 0.3f), glm::vec4(4, 4, 4, 0.3f)};
        Check(pipeline_.Initialize(), "Pipeline initialize failed: " + pipeline_.LastError());
        Check(toonPipeline_.Initialize(), "Toon pipeline initialize failed: " + toonPipeline_.LastError());

        toonSettings_.groundHeight = -1.5f;
        toonSettings_.backgroundColor = glm::vec4(1);
        toonSettings_.lightDirection = glm::normalize(glm::vec3(0.45f, 0.8f, 0.6f));

        std::cout << "Loaded " << Utf8(source.sourcePath) << '\n'
            << "Meshes=" << source.meshes.size() << " instances=" << source.instances.size()
            << " vertices=" << source.VertexCount() << " triangles=" << source.TriangleCount()
            << " PBR materials=" << source.materials.size() << " textures=" << source.textures.size() << '\n';
        std::cout << "Render mode: " << (orbit.toon ? "Toon" : "PBR") << "; T: switch PBR/Toon\n";
        for (const auto& warning : source.warnings) std::cerr << "Model warning: " << warning << '\n';
    }

    ~Viewer() {
        resource_.reset();
        toonPipeline_.Shutdown();
        pipeline_.Shutdown();
        module_.Shutdown();
        device_.StopAndRelease();
        while (!device_.returnSystem.callbacks.empty()) device_.returnSystem.DrainCallbacks();
    }

    void Render(const OrbitCamera& orbit, std::uint32_t width, std::uint32_t height,
                bool present, bool includeModel = true, bool releaseResourceBeforeRecord = false) {
        Check(width > 0 && height > 0 && width <= 8192 && height <= 8192, "Unsupported framebuffer extent");
        const bool toon = orbit.toon;
        Check(toon ? toonPipeline_.Resize(width, height) :
            pipeline_.Resize(width, height, settings_.shadows.atlasResolution, 1), PipelineError(toon));
        const auto camera = orbit.View(width, height);
        Render::RenderFrame frame;
        frame.frameIndex = ++frameIndex_;
        std::string error;
        if (includeModel) Check(resource_->AppendTo(frame, placement_, glm::dvec3(0), camera.view, &error), error);
        Render::RHICommand::BeginFrame begin;
        begin.frameIndex = frame.frameIndex;
        begin.framebufferWidth = width;
        begin.framebufferHeight = height;
        begin.clearColor = toon ? toonSettings_.backgroundColor : glm::vec4(0.025f, 0.035f, 0.055f, 1);
        auto encoder = device_.BeginFrame(begin);
        // The smoke test's final frame verifies that extracted material snapshots
        // also keep meshes/textures alive after the application releases the model.
        const bool retained = releaseResourceBeforeRecord || resource_->KeepAlive(encoder);
        if (releaseResourceBeforeRecord) resource_.reset();
        const bool recorded = retained && (toon ? toonPipeline_.Record(encoder, frame, camera, toonSettings_) :
            pipeline_.Record(encoder, frame, camera, settings_));
        if (!recorded || !encoder.End(present)) {
            encoder.Cancel();
            if (const auto token = FrameToken(toon)) DiscardFrame(toon, token);
            throw std::runtime_error("Model frame recording failed: " + PipelineError(toon));
        }
        const auto token = FrameToken(toon);
        auto completed = std::make_shared<Completion>();
        if (!device_.async_SubmitFrameCommands(encoder.GetCommandBuffer(), [completed] { completed->done = true; })) {
            encoder.Cancel();
            DiscardFrame(toon, token);
            throw std::runtime_error("Reliable model frame submission failed");
        }
        Wait(completed, "model frame completion");
        Check(toon ? toonPipeline_.CompleteFrame(token) : pipeline_.CompleteFrame(token),
            "Model frame completion failed: " + PipelineError(toon));
        lastWidth_ = width;
        lastHeight_ = height;
        lastPresented_ = present;
        lastToon_ = toon;
    }

    void VerifyRebasing(const OrbitCamera& orbit) const {
        Render::RenderFrame local, distant;
        std::string error;
        const auto view = orbit.View(800, 600).view;
        Check(resource_->AppendTo(local, placement_, glm::dvec3(0), view, &error), error);
        const glm::dvec3 origin(1e12, -1e12, 1e12);
        const auto distantPlacement = glm::translate(glm::dmat4(1), origin) * placement_;
        Check(resource_->AppendTo(distant, distantPlacement, origin, view, &error), error);
        Check(!local.items.empty() && local.items.size() == distant.items.size(), "Rebased extraction lost model instances");
        float maximumError = 0;
        for (std::size_t item = 0; item < local.items.size(); ++item)
            for (int column = 0; column < 4; ++column)
                for (int row = 0; row < 4; ++row)
                    maximumError = std::max(maximumError,
                        std::abs(local.items[item].model[column][row] - distant.items[item].model[column][row]));
        Check(maximumError < 1e-3f, "Origin-relative GPU extraction lost precision at 1e12 world units");
        std::cout << "Double-precision origin rebase at 1e12: maximum matrix error=" << maximumError << '\n';
    }

    Pixels Readback() {
        Check(lastWidth_ > 0 && !lastPresented_, "Readback requires a completed frame without presentation");
        struct Result : Completion { Pixels pixels; GLenum error = GL_NO_ERROR; };
        auto result = std::make_shared<Result>();
        result->pixels = {lastWidth_, lastHeight_, std::vector<std::uint8_t>(std::size_t(lastWidth_) * lastHeight_ * 4)};
        device_.async_ExecuteCode([result] {
            auto& pixels = result->pixels;
            glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
            glReadBuffer(GL_BACK);
            glReadPixels(0, 0, pixels.width, pixels.height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.rgba.data());
            result->error = glGetError();
        }, [result] { result->done = true; });
        Wait(result, "model framebuffer readback");
        Check(result->error == GL_NO_ERROR, "OpenGL error reading rendered model: " + std::to_string(result->error));
        return std::move(result->pixels);
    }

    void VerifyToonEffects(const OrbitCamera& orbit, std::uint32_t width, std::uint32_t height) {
        const auto savedSettings = toonSettings_;
        struct RestoreSettings {
            Render::ToonPipelineSettings& settings;
            const Render::ToonPipelineSettings& saved;
            ~RestoreSettings() { settings = saved; }
        } restore{toonSettings_, savedSettings};
        auto toonCamera = orbit;
        toonCamera.toon = true;
        toonSettings_.outlinesEnabled = true;
        toonSettings_.shadowsEnabled = true;
        toonSettings_.groundEnabled = true;
        toonSettings_.groundShadows = true;
        Render(toonCamera, width, height, false);
        const auto baseline = Readback();

        toonSettings_.outlinesEnabled = false;
        Render(toonCamera, width, height, false);
        const auto noOutline = Readback();

        toonSettings_.outlinesEnabled = true;
        toonSettings_.shadowsEnabled = false;
        Render(toonCamera, width, height, false);
        const auto noShadow = Readback();

        const auto changedPixels = [&baseline](const Pixels& disabled) {
            Check(baseline.width == disabled.width && baseline.height == disabled.height &&
                baseline.rgba.size() == disabled.rgba.size() && !baseline.rgba.empty(),
                "Toon-effects readback sizes differ");
            std::size_t changed = 0;
            for (std::size_t i = 0; i < baseline.rgba.size(); i += 4) {
                int difference = 0;
                for (std::size_t channel = 0; channel < 3; ++channel)
                    difference = std::max(difference,
                        std::abs(int(baseline.rgba[i + channel]) - int(disabled.rgba[i + channel])));
                if (difference > 2) ++changed;
            }
            return changed;
        };
        const auto outlinePixels = changedPixels(noOutline);
        const auto shadowPixels = changedPixels(noShadow);
        std::cout << "Toon effect pixels: outline=" << outlinePixels << " shadow=" << shadowPixels << '\n';
        Check(outlinePixels >= 20, "Toon outline toggle did not change rendered pixels");
        Check(shadowPixels >= 20, "Toon shadow toggle did not change rendered pixels");
    }

    Render::PipelineStatistics Statistics() const {
        return lastToon_ ? toonPipeline_.Statistics() : pipeline_.Statistics();
    }
    Render::ToonPipelineStatistics ToonStatistics() const { return toonPipeline_.Statistics(); }

private:
    const std::string& PipelineError(bool toon) const {
        return toon ? toonPipeline_.LastError() : pipeline_.LastError();
    }
    std::uint64_t FrameToken(bool toon) const {
        return toon ? toonPipeline_.RecordedFrameToken() : pipeline_.RecordedFrameToken();
    }
    void DiscardFrame(bool toon, std::uint64_t token) {
        if (toon) toonPipeline_.DiscardFrame(token);
        else pipeline_.DiscardFrame(token);
    }
    void Wait(const std::shared_ptr<Completion>& completion, const char* operation) {
        // The first draw can include cold driver compilation of PBR variants.
        const auto deadline = std::chrono::steady_clock::now() + 30s;
        while (!completion->done) {
            device_.returnSystem.DrainCallbacks();
            if (completion->done) break;
            Check(std::chrono::steady_clock::now() < deadline, std::string("Timed out waiting for ") + operation);
            glfwPollEvents();
            device_.returnSystem.WaitForCallbacks(2ms);
        }
    }

    Render::RHIDevice device_; // Outlives model materials, meshes, pipeline and callback leases.
    Model::ModelModule module_;
    Render::MaterialRenderPipeline pipeline_;
    Render::ToonRenderPipeline toonPipeline_;
    std::shared_ptr<Model::ModelResource> resource_;
    Render::MaterialPipelineSettings settings_;
    Render::ToonPipelineSettings toonSettings_;
    glm::dmat4 placement_{1};
    std::uint64_t frameIndex_ = 0;
    std::uint32_t lastWidth_ = 0, lastHeight_ = 0;
    bool lastPresented_ = false, lastToon_ = false;
};

void Save(const Pixels& pixels, const std::filesystem::path& path) {
    if (path.empty()) return;
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    std::vector<std::uint8_t> topDown(pixels.rgba.size());
    const std::size_t row = std::size_t(pixels.width) * 4;
    for (std::size_t y = 0; y < pixels.height; ++y)
        std::copy_n(pixels.rgba.data() + (pixels.height - 1 - y) * row, row, topDown.data() + y * row);
    const auto output = Utf8(path);
    Check(stbi_write_png(output.c_str(), pixels.width, pixels.height, 4, topDown.data(), static_cast<int>(row)) != 0,
        "Cannot save screenshot: " + output);
    std::cout << "Screenshot: " << output << '\n';
}

void VerifyVisible(const Pixels& background, const Pixels& model, const Render::PipelineStatistics& statistics) {
    Check(background.rgba.size() == model.rgba.size() && !model.rgba.empty(), "Smoke-test readback sizes differ");
    std::size_t visible = 0;
    std::uint64_t difference = 0;
    for (std::size_t i = 0; i < model.rgba.size(); i += 4) {
        int maximum = 0;
        for (std::size_t component = 0; component < 3; ++component) {
            const auto delta = std::abs(int(model.rgba[i + component]) - int(background.rgba[i + component]));
            maximum = std::max(maximum, delta);
            difference += delta;
        }
        if (maximum > 8) ++visible;
    }
    std::cout << "Rendered model pixels=" << visible << '/' << std::size_t(model.width) * model.height
        << " mean RGB delta=" << double(difference) / (std::size_t(model.width) * model.height * 3)
        << " scene passes=" << statistics.scenePasses << " shadow passes=" << statistics.shadowPasses
        << " transparent passes=" << statistics.transparentPasses << '\n';
    Check(statistics.scenePasses > 0 && visible > std::max<std::size_t>(32, std::size_t(model.width) * model.height / 2000),
        "Model smoke test failed: no sufficiently visible geometry against the empty-scene reference");
}

void VerifyModeSwitch(const Pixels& firstBackground, const Pixels& firstModel,
                      const Pixels& secondBackground, const Pixels& secondModel) {
    Check(firstModel.rgba.size() == secondModel.rgba.size(), "Pipeline-switch readback sizes differ");
    std::size_t sharedForeground = 0, changed = 0;
    for (std::size_t i = 0; i < firstModel.rgba.size(); i += 4) {
        int firstCoverage = 0, secondCoverage = 0, difference = 0;
        for (std::size_t channel = 0; channel < 3; ++channel) {
            firstCoverage = std::max(firstCoverage, std::abs(int(firstModel.rgba[i + channel]) - int(firstBackground.rgba[i + channel])));
            secondCoverage = std::max(secondCoverage, std::abs(int(secondModel.rgba[i + channel]) - int(secondBackground.rgba[i + channel])));
            difference = std::max(difference, std::abs(int(firstModel.rgba[i + channel]) - int(secondModel.rgba[i + channel])));
        }
        // Compare only pixels covered by the model in both renderers. Their
        // backgrounds differ, which alone must not satisfy this switch test.
        if (firstCoverage > 8 && secondCoverage > 8) {
            ++sharedForeground;
            if (difference > 8) ++changed;
        }
    }
    Check(sharedForeground > 32 && changed > std::max<std::size_t>(16, sharedForeground / 100),
        "PBR/Toon switch produced no meaningful change to model shading");
    std::cout << "PBR/Toon switch changed foreground pixels=" << changed << '/' << sharedForeground << '\n';
}

int Run(const std::vector<std::string>& arguments) {
    const auto options = Parse(arguments);
    if (options.help) {
        std::cout << "Usage: model_viewer [--model FILE_OR_DIRECTORY] [--toon] [--smoke-test] [--frames N] [--screenshot FILE.png]\n"
            << "Default model: Asset/Model/Rikka_Uniform\n"
            << "--toon: cel shading, outlines and a white ground with cast shadows.\n"
            << "Drag left/right mouse: orbit; wheel: zoom; T: switch PBR/Toon; R: reset view; Escape: close.\n";
        return 0;
    }
    Check(glfwInit() == GLFW_TRUE, "GLFW initialization failed");
    struct GlfwLifetime { ~GlfwLifetime() { glfwTerminate(); } } glfwLifetime;
    glfwWindowHint(GLFW_VISIBLE, options.smoke ? GLFW_FALSE : GLFW_TRUE);
    const std::uint32_t width = options.smoke ? 800 : 1200, height = options.smoke ? 600 : 900;
    ApplicationWindow::Window window({width, height, options.toon ?
        "Toon Model Viewer | T: PBR | drag: orbit | wheel: zoom | R: reset | Esc: close" :
        "PBR Model Viewer | T: Toon | drag: orbit | wheel: zoom | R: reset | Esc: close", !options.smoke});
    Check(window.Activate(), "Cannot create the OpenGL model viewer window");
    const auto context = window.GetRenderContextAsOpengl();
    Check(context.has_value(), "The viewer window has no render context");
    OrbitCamera orbit;
    orbit.toon = options.toon;
    auto* native = window.GetNativeWindow();
    glfwSetWindowUserPointer(native, &orbit);
    glfwSetCursorPosCallback(native, &OrbitCamera::Cursor);
    glfwSetMouseButtonCallback(native, &OrbitCamera::Button);
    glfwSetScrollCallback(native, &OrbitCamera::Scroll);
    glfwSetKeyCallback(native, &OrbitCamera::Key);
    Viewer viewer(*context, options, orbit);
    Pixels background;
    if (options.smoke) {
        viewer.VerifyRebasing(orbit);
        int baselineWidth = 0, baselineHeight = 0;
        glfwGetFramebufferSize(native, &baselineWidth, &baselineHeight);
        viewer.Render(orbit, baselineWidth, baselineHeight, false, false);
        background = viewer.Readback();
        viewer.Render(orbit, baselineWidth, baselineHeight, false);
        const auto selectedMode = viewer.Readback();
        VerifyVisible(background, selectedMode, viewer.Statistics());
        orbit.toon = !orbit.toon;
        viewer.Render(orbit, baselineWidth, baselineHeight, false, false);
        const auto otherBackground = viewer.Readback();
        viewer.Render(orbit, baselineWidth, baselineHeight, false);
        const auto otherMode = viewer.Readback();
        VerifyVisible(otherBackground, otherMode, viewer.Statistics());
        VerifyModeSwitch(background, selectedMode, otherBackground, otherMode);
        const auto toonStatistics = viewer.ToonStatistics();
        Check(toonStatistics.outlinePasses > 0 && toonStatistics.groundPasses > 0 && toonStatistics.shadowPasses > 0,
            "Toon smoke test did not run outline, ground and shadow passes");
        std::cout << "Toon outline passes=" << toonStatistics.outlinePasses
            << " ground passes=" << toonStatistics.groundPasses << " MSAA=" << toonStatistics.msaaSamples << '\n';
        viewer.VerifyToonEffects(orbit, baselineWidth, baselineHeight);
        orbit.toon = options.toon;
    }
    bool screenshotSaved = false;
    bool smokeVerified = false;
    for (std::uint64_t frame = 0; !window.ShouldClose() && (!options.frames || frame < options.frames);) {
        window.PollEvents();
        int framebufferWidth = 0, framebufferHeight = 0;
        glfwGetFramebufferSize(native, &framebufferWidth, &framebufferHeight);
        if (framebufferWidth == 0 || framebufferHeight == 0) { glfwWaitEventsTimeout(0.05); continue; }
        const bool last = options.frames && frame + 1 == options.frames;
        const bool capture = !options.screenshot.empty() && !screenshotSaved && (last || !options.frames);
        viewer.Render(orbit, framebufferWidth, framebufferHeight, !options.smoke && !capture, true, options.smoke && last);
        if (capture || (options.smoke && last)) {
            const auto pixels = viewer.Readback();
            if (capture) { Save(pixels, options.screenshot); screenshotSaved = true; }
            if (options.smoke && last) {
                VerifyVisible(background, pixels, viewer.Statistics());
                smokeVerified = true;
                std::cout << "Extracted material snapshots rendered after model release\n";
            }
        }
        ++frame;
    }
    if (options.smoke) {
        Check(smokeVerified, "Smoke test was interrupted before geometry validation");
        std::cout << "Model rendering smoke test passed\n";
    }
    return 0;
}
} // namespace

#ifdef _WIN32
// Wide command-line arguments preserve non-ASCII model and texture directories.
int wmain(int argc, wchar_t** argv) {
    try {
        std::vector<std::string> arguments;
        for (int i = 0; i < argc; ++i) arguments.push_back(Utf8(std::filesystem::path(argv[i])));
        return Run(arguments);
    } catch (const std::exception& error) {
        std::cerr << "Model viewer failed: " << error.what() << '\n';
        return 1;
    }
}
#else
int main(int argc, char** argv) {
    try { return Run(std::vector<std::string>(argv, argv + argc)); }
    catch (const std::exception& error) {
        std::cerr << "Model viewer failed: " << error.what() << '\n';
        return 1;
    }
}
#endif
