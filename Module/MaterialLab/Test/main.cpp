#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

#include <glad/glad.h>
#include "ApplicationWindow/public/window.h"
#include "MaterialLab/Public/material_lab_module.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace {
using namespace MaterialLab;
void Check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
struct Options {
    bool smoke = false, procedural = false;
    std::string screenshot, legacyDirectory;
};
Options Parse(int argc, char** argv) {
    Options result;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--smoke-test") result.smoke = true;
        else if (arg == "--procedural") result.procedural = true;
        else if ((arg == "--screenshot" || arg == "--legacy-textures") && i + 1 < argc) {
            if (arg == "--screenshot") result.screenshot = argv[++i];
            else result.legacyDirectory = argv[++i];
        } else throw std::runtime_error("Usage: material_lab_demo [--smoke-test] [--procedural] [--legacy-textures DIR] [--screenshot FILE.png]");
    }
    Check(!result.procedural || result.legacyDirectory.empty(), "Choose --procedural or --legacy-textures, not both");
    // CI smoke has a deterministic, asset-independent default. Interactive use
    // picks up the existing ignored legacy assets beside the executable.
    if (!result.smoke && !result.procedural && result.legacyDirectory.empty()) {
        const auto root = std::filesystem::absolute(argv[0]).parent_path() / "materials" / "tite";
        const std::array<const char*, 4> files{"square_tiles_03_diff_1k.png", "square_tiles_03_nor_gl_1k.png",
                                              "square_tiles_03_rough_1k.png", "square_tiles_03_ao_1k.png"};
        bool complete = true;
        for (const auto file : files) complete = complete && std::filesystem::is_regular_file(root / file);
        if (complete) result.legacyDirectory = root.string();
    }
    return result;
}
void Save(const FramePixels& pixels, const std::string& path) {
    if (path.empty()) return;
    std::vector<std::uint8_t> topDown(pixels.rgba.size());
    const std::size_t row = pixels.width * 4;
    for (std::size_t y = 0; y < pixels.height; ++y)
        std::copy_n(pixels.rgba.data() + (pixels.height - 1 - y) * row, row, topDown.data() + y * row);
    Check(stbi_write_png(path.c_str(), pixels.width, pixels.height, 4, topDown.data(), static_cast<int>(row)) != 0,
          "Cannot write screenshot: " + path);
    std::cout << "Screenshot: " << path << '\n';
}
FramePixels Capture(MaterialLabModule& lab, const FrameSettings& settings) {
    Check(lab.Render(settings), lab.LastError());
    auto pixels = lab.Readback();
    Check(!pixels.rgba.empty(), lab.LastError());
    return pixels;
}
double Difference(const FramePixels& a, const FramePixels& b) {
    Check(a.rgba.size() == b.rgba.size() && !a.rgba.empty(), "Readback sizes differ");
    double sum = 0;
    for (std::size_t i = 0; i < a.rgba.size(); ++i) if (i % 4 != 3)
        sum += std::abs(int(a.rgba[i]) - int(b.rgba[i]));
    return sum / (a.width * a.height * 3.0);
}
void Smoke(MaterialLabModule& lab, const Options& options, std::uint32_t width, std::uint32_t height) {
    Check(lab.MaterialCount() == 18, "Expected 15 scalar spheres and 3 texture/preset spheres");
    FrameSettings frame;
    frame.width = width; frame.height = height; frame.present = false;
    const auto gallery = Capture(lab, frame);
    std::size_t litPixels = 0;
    for (std::size_t i = 0; i < gallery.rgba.size(); i += 4)
        if (gallery.rgba[i] > 40 || gallery.rgba[i + 1] > 40 || gallery.rgba[i + 2] > 40) ++litPixels;
    Check(litPixels > width * height / 8, "Gallery is blank or mostly missing");
    Save(gallery, options.screenshot);
    const auto attachments = lab.InspectPipelineBuffers();
    std::cout << "Pass buffers: shadow min=" << attachments.x << " AO min=" << attachments.y << " HDR max=" << attachments.z << '\n';
    Check(attachments.x >= 0 && attachments.x < 0.999f, "CSM atlas contains no caster depth");
    Check(attachments.y >= 0 && attachments.y < 0.99f, "SSAO texture has no occlusion");
    Check(attachments.z > 1.0f, "HDR scene was clamped before post-processing");
    auto stats = lab.PipelineStatistics();
    Check(stats.shadowPasses == 4 && stats.reflectionPasses == 1 && stats.depthPasses == 1 && stats.postPasses >= 10,
          "Missing expected render passes");
    const auto checkEffect = [&](const char* name) {
        const auto changed = Capture(lab, frame);
        const double delta = Difference(gallery, changed);
        std::cout << name << " toggle delta=" << delta << '\n';
        Check(delta > 0.001, std::string("Effect has no visible contribution: ") + name);
    };
    frame.effects.shadows.enabled = false; checkEffect("CSM");
    Check(lab.PipelineStatistics().shadowPasses == 0, "Disabled CSM still recorded shadow draws");
    frame.effects.shadows.enabled = true;
    frame.effects.reflection.enabled = false; checkEffect("planar reflection");
    Check(lab.PipelineStatistics().reflectionPasses == 0, "Disabled mirror still rendered");
    frame.effects.reflection.enabled = true;
    frame.effects.post.bloomEnabled = false; checkEffect("Bloom"); frame.effects.post.bloomEnabled = true;
    frame.effects.post.ssaoEnabled = false; checkEffect("SSAO"); frame.effects.post.ssaoEnabled = true;
    for (std::uint32_t mode = 1; mode <= 7; ++mode) {
        frame.effects.post.filter = static_cast<Render::PostFilter>(mode);
        checkEffect(("post filter " + std::to_string(mode)).c_str());
    }
    frame.effects.post.filter = Render::PostFilter::None;
    frame.effects.post.toneMap = Render::ToneMapMode::ACES; checkEffect("ACES");
    frame.effects.post.toneMap = Render::ToneMapMode::None; checkEffect("no tone map");
    frame.effects.post.toneMap = Render::ToneMapMode::Reinhard;
    frame.effects.post.vignette = 0.8f; checkEffect("vignette"); frame.effects.post.vignette = 0;
    frame.effects.post.saturation = 0.2f; checkEffect("saturation"); frame.effects.post.saturation = 1;
    frame.effects.post.contrast = 1.3f; checkEffect("contrast"); frame.effects.post.contrast = 1;
    // Recreate targets at odd and minimal sizes, then restore the full gallery.
    frame.width = 319; frame.height = 197;
    Check(Capture(lab, frame).rgba.size() == 319 * 197 * 4, "Odd-size target resize failed");
    frame.width = frame.height = 1;
    Check(Capture(lab, frame).rgba.size() == 4, "Minimal target resize failed");
    frame.width = width; frame.height = height;
    Check(Difference(gallery, Capture(lab, frame)) < 0.001, "Resize did not restore the original frame");

    // Five populated textures in this material make all map branches observable.
    constexpr std::size_t selected = 16;
    frame.isolatedMaterial = static_cast<int>(selected);
    const auto original = Capture(lab, frame);
    const auto set = [&](const char* name, const Render::Material::ParameterValue& value) {
        Check(lab.SetMaterialParameter(selected, name, value), lab.LastError());
    };
    for (const char* name : {"useNormalMap", "useMetallicMap", "useRoughnessMap", "useAoMap"}) set(name, false);
    set("metallic", 0.0f); set("roughness", 0.5f);
    auto baseline = Capture(lab, frame);
    const auto changed = [&](const FramePixels& before, const char* operation) {
        const auto after = Capture(lab, frame);
        const double delta = Difference(before, after);
        std::cout << operation << " pixel delta=" << delta << '\n';
        Check(delta > 0.005, std::string("No visible response to ") + operation);
        return after;
    };
    set("metallic", 1.0f); changed(baseline, "metallic scalar"); set("metallic", 0.0f);
    set("roughness", 0.08f); changed(baseline, "roughness scalar"); set("roughness", 0.5f);
    set("baseColor", glm::vec4(0.1f, 0.3f, 1.0f, 1.0f)); changed(baseline, "base color");
    set("baseColor", glm::vec4(1.0f));
    for (const char* name : {"useNormalMap", "useMetallicMap", "useRoughnessMap", "useAoMap"}) {
        set(name, true); changed(baseline, name); set(name, false);
    }
    frame.exposure = 0.35f; changed(baseline, "pass exposure"); frame.exposure = 1.0f;
    frame.lightTime = 2.0f; changed(baseline, "pass lights"); frame.lightTime = 0.0f;
    frame.cameraDistance = 24.0f; changed(baseline, "isolated camera zoom"); frame.cameraDistance = 18.0f;
    Check(!lab.SetMaterialParameter(selected, "roughness", -0.1f), "Invalid roughness accepted");
    Check(lab.InspectMaterial(selected).roughness == 0.5f, "Rejected edit modified instance");
    const auto unchanged = lab.InspectMaterial(7);
    Check(unchanged.metallic == 0.5f && unchanged.roughness == 0.5f, "Editing one instance modified another");
    Check(lab.ResetMaterial(selected), lab.LastError());
    const auto reset = lab.InspectMaterial(selected);
    Check(reset.normalMap && reset.metallicMap && reset.roughnessMap && reset.aoMap, "Reset failed");
    Check(Difference(original, Capture(lab, frame)) < 0.001, "Reset did not restore the rendered material");
    // Exercise real legacy assets when explicitly supplied, not just uploading.
    frame.isolatedMaterial = 17;
    baseline = Capture(lab, frame);
    Check(lab.SetMaterialParameter(17, "useNormalMap", false), lab.LastError());
    changed(baseline, "legacy preset normal map");
    Check(lab.ResetMaterial(17), lab.LastError());
    frame.isolatedMaterial = 16;
    set("baseColor", glm::vec4(1, 1, 1, 0));
    set("alphaCutoff", 0.5f);
    Capture(lab, frame);
    const auto cutout = lab.InspectPipelineBuffers();
    Check(cutout.x > 0.999f && cutout.y > 0.999f && cutout.z < 0.1f,
          "Alpha-clipped surface still contributed to scene/shadow/SSAO depth");
    Check(lab.ResetMaterial(16), lab.LastError());
    set("emissiveColor", glm::vec4(1000000, 1000000, 1000000, 0));
    Capture(lab, frame);
    const auto extreme = lab.InspectPipelineBuffers();
    Check(extreme.z > 10000 && extreme.z <= 65504, "Extreme emission overflowed the HDR attachment");
    frame.effects.post.toneMap = Render::ToneMapMode::None;
    frame.effects.post.gamma = 0.1f;
    frame.effects.post.saturation = frame.effects.post.contrast = 0;
    const auto lowGamma = Capture(lab, frame);
    const auto center = (static_cast<std::size_t>(height / 2) * width + width / 2) * 4;
    Check(std::abs(int(lowGamma.rgba[center]) - 128) <= 1, "HDR/gamma extremes produced invalid display output");
    frame.effects.post.toneMap = Render::ToneMapMode::Reinhard;
    frame.effects.post.gamma = 2.2f;
    frame.effects.post.saturation = frame.effects.post.contrast = 1;
    Check(lab.ResetMaterial(16), lab.LastError());
    frame.isolatedMaterial = -1;
    frame.effects.post.bloomLevels = 0;
    Check(!lab.Render(frame), "Invalid pipeline settings accepted");
    Check(lab.Readback().rgba.empty(), "Failed frame exposed a stale readback");
    std::cout << "MaterialLab OpenGL smoke passed: HDR/CSM/mirror/Bloom/SSAO/all filters, resize, material edits; GL errors=0\n";
}

struct Input { float scroll = 0; };
void Interactive(MaterialLabModule& lab, ApplicationWindow::Window& window, const Options& options) {
    std::cout << "MaterialLab: columns roughness 0.08 -> 1, rows metallic 0 -> 1.\n"
                 "Bottom: copper / five maps / migrated legacy tile.\n"
                 "Tab/Shift+Tab select | F isolate | arrows roughness/metallic | N normal | T property maps\n"
                 "R reset | PageUp/Down exposure | Space animate lights | drag orbit | wheel zoom | Esc exit\n";
    std::cout << "B Bloom | C CSM | M mirror | O SSAO | P cycle filters | L tone map | V vignette\n";
    FrameSettings frame;
    std::size_t selected = 7;
    bool focused = false, animate = false;
    std::array<bool, GLFW_KEY_LAST + 1> previous{};
    auto* native = window.GetNativeWindow();
    Input input;
    glfwSetWindowUserPointer(native, &input);
    glfwSetScrollCallback(native, [](GLFWwindow* w, double, double y) {
        static_cast<Input*>(glfwGetWindowUserPointer(w))->scroll += static_cast<float>(y);
    });
    double lastTime = glfwGetTime(), lastX = 0, lastY = 0;
    bool dragging = false, saved = false;
    while (!window.ShouldClose()) {
        window.PollEvents();
        const double now = glfwGetTime();
        const float dt = static_cast<float>(std::clamp(now - lastTime, 0.0, 0.1));
        lastTime = now;
        const auto down = [&](int key) { return glfwGetKey(native, key) == GLFW_PRESS; };
        const auto pressed = [&](int key) { const bool value = down(key); const bool edge = value && !previous[key]; previous[key] = value; return edge; };
        if (down(GLFW_KEY_ESCAPE)) break;
        if (pressed(GLFW_KEY_TAB)) selected = (selected + (down(GLFW_KEY_LEFT_SHIFT) ? lab.MaterialCount() - 1 : 1)) % lab.MaterialCount();
        if (pressed(GLFW_KEY_F)) focused = !focused;
        if (pressed(GLFW_KEY_SPACE)) animate = !animate;
        if (pressed(GLFW_KEY_B)) frame.effects.post.bloomEnabled = !frame.effects.post.bloomEnabled;
        if (pressed(GLFW_KEY_C)) frame.effects.shadows.enabled = !frame.effects.shadows.enabled;
        if (pressed(GLFW_KEY_M)) frame.effects.reflection.enabled = !frame.effects.reflection.enabled;
        if (pressed(GLFW_KEY_O)) frame.effects.post.ssaoEnabled = !frame.effects.post.ssaoEnabled;
        if (pressed(GLFW_KEY_P)) frame.effects.post.filter = static_cast<Render::PostFilter>((static_cast<unsigned>(frame.effects.post.filter) + 1) % 8);
        if (pressed(GLFW_KEY_L)) frame.effects.post.toneMap = static_cast<Render::ToneMapMode>((static_cast<unsigned>(frame.effects.post.toneMap) + 1) % 3);
        if (pressed(GLFW_KEY_V)) frame.effects.post.vignette = frame.effects.post.vignette > 0 ? 0.0f : 0.7f;
        if (pressed(GLFW_KEY_R)) Check(lab.ResetMaterial(selected), lab.LastError());
        auto info = lab.InspectMaterial(selected);
        const auto edit = [&](const char* name, const Render::Material::ParameterValue& value) {
            Check(lab.SetMaterialParameter(selected, name, value), lab.LastError());
        };
        const float roughDelta = (float(down(GLFW_KEY_RIGHT)) - float(down(GLFW_KEY_LEFT))) * dt * 0.4f;
        const float metalDelta = (float(down(GLFW_KEY_UP)) - float(down(GLFW_KEY_DOWN))) * dt * 0.4f;
        if (roughDelta != 0) {
            edit("useRoughnessMap", false);
            edit("roughness", std::clamp(info.roughness + roughDelta, 0.045f, 1.0f));
        }
        if (metalDelta != 0) {
            edit("useMetallicMap", false);
            edit("metallic", std::clamp(info.metallic + metalDelta, 0.0f, 1.0f));
        }
        if (pressed(GLFW_KEY_N)) edit("useNormalMap", !info.normalMap);
        if (pressed(GLFW_KEY_T)) {
            const bool enable = !(info.metallicMap || info.roughnessMap || info.aoMap);
            edit("useMetallicMap", enable); edit("useRoughnessMap", enable); edit("useAoMap", enable);
        }
        frame.exposure = std::clamp(frame.exposure + (float(down(GLFW_KEY_PAGE_UP)) - float(down(GLFW_KEY_PAGE_DOWN))) * dt, 0.05f, 5.0f);
        if (animate) frame.lightTime += dt;
        double x, y; glfwGetCursorPos(native, &x, &y);
        const bool mouse = glfwGetMouseButton(native, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        if (mouse && dragging) {
            frame.cameraYaw += static_cast<float>(x - lastX) * 0.005f;
            frame.cameraPitch = std::clamp(frame.cameraPitch + static_cast<float>(y - lastY) * 0.005f, -0.7f, 1.2f);
        }
        lastX = x; lastY = y; dragging = mouse;
        frame.cameraDistance = std::clamp(frame.cameraDistance - input.scroll, 9.0f, 32.0f); input.scroll = 0;
        int width = 0, height = 0; glfwGetFramebufferSize(native, &width, &height);
        if (width <= 0 || height <= 0) { glfwWaitEventsTimeout(0.05); continue; }
        frame.width = width; frame.height = height;
        frame.isolatedMaterial = focused ? static_cast<int>(selected) : -1;
        if (!saved && !options.screenshot.empty()) {
            frame.present = false; Save(Capture(lab, frame), options.screenshot); saved = true;
        }
        frame.present = true;
        Check(lab.Render(frame), lab.LastError());
        info = lab.InspectMaterial(selected);
        std::ostringstream title;
        title << "MaterialLab [" << selected + 1 << "/" << lab.MaterialCount() << "] " << info.name
              << std::fixed << std::setprecision(2) << " | M=" << info.metallic << " R=" << info.roughness
              << " maps(N/M/R/A)=" << info.normalMap << info.metallicMap << info.roughnessMap << info.aoMap
              << " Exp=" << frame.exposure << " | B/C/M/O=" << frame.effects.post.bloomEnabled << frame.effects.shadows.enabled
              << frame.effects.reflection.enabled << frame.effects.post.ssaoEnabled << " P=" << static_cast<unsigned>(frame.effects.post.filter)
              << " L=" << static_cast<unsigned>(frame.effects.post.toneMap) << " | Tab/F/arrows edit; B C M O P L V effects";
        glfwSetWindowTitle(native, title.str().c_str());
    }
    glfwSetScrollCallback(native, nullptr);
    glfwSetWindowUserPointer(native, nullptr);
}
} // namespace

int main(int argc, char** argv) {
    int result = 0;
    try {
        const auto options = Parse(argc, argv);
        Check(glfwInit() == GLFW_TRUE, "glfwInit failed");
        glfwWindowHint(GLFW_VISIBLE, options.smoke ? GLFW_FALSE : GLFW_TRUE);
        const std::uint32_t width = options.smoke ? 960 : 1280, height = options.smoke ? 600 : 800;
        ApplicationWindow::Window window({width, height, "MaterialLab", !options.smoke});
        Check(window.Activate(), "OpenGL 4.5 window creation failed");
        const auto context = window.GetRenderContextAsOpengl();
        Check(context.has_value(), "Missing render context");
        MaterialLabModule lab(*context, options.legacyDirectory);
        Check(lab.Startup(), lab.LastError());
        std::cout << "Legacy textures: " << (options.legacyDirectory.empty() ? "procedural fallback" : options.legacyDirectory) << '\n';
        if (options.smoke) Smoke(lab, options, width, height);
        else Interactive(lab, window, options);
        lab.Shutdown();
        Check(!lab.IsStarted(), "Module did not stop");
    } catch (const std::exception& error) {
        std::cerr << "MaterialLab failed: " << error.what() << '\n';
        result = 1;
    }
    glfwTerminate();
    return result;
}
