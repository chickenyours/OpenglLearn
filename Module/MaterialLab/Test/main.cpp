#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include "ApplicationWindow/public/window.h"
#include "MaterialLab/Public/material_lab_module.h"
#include "MaterialLab/Public/gi_room_scene.h"
#include <stb_image.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace {
using namespace MaterialLab;
void Check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
struct Options {
    bool smoke = false, quality = false, shadows = false, closeup = false, advanced = false, advancedQuality = false, benchmark = false, procedural = false;
    bool skyTest=false, sky=false, probeTest=false, probes=false;
    bool realtimeGi=false,realtimeGiTest=false,realtimeGiStabilityTest=false;
    bool lumenGi=false,lumenGiTest=false;
    bool showcase=false,showcaseTest=false,showcaseStability=false,showcaseWaterDiagnostic=false,showcaseMotion=false;
    bool showcaseMeasureOnly=false;
    bool giRoom=false,giRoomTest=false,giRoomPerformance=false;
    bool vulkan=false,validation=false,softwareRays=false,vsync=false;
    bool studySizeSet=false,renderSizeSet=false,viewStability=false,benchmarkPresent=false,fullscreen=false;
    float lumenReflectionScale=.5f;
    float lumenSourceScale=.5f;
    float renderScale=1;
    std::uint32_t lumenReflectionDetail=256;
    float yaw = -1.35f, pitch = 0.38f, distance = 9.0f;
    bool yawSet = false, pitchSet = false, distanceSet = false;
    std::uint32_t benchmarkWidth=1280, benchmarkHeight=800;
    std::uint32_t msaaSamples=4;
    float fpsLimit=90;
    std::string screenshot, legacyDirectory, skyHdr;
};
Options Parse(int argc, char** argv) {
    Options result;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--smoke-test") result.smoke = true;
        else if(arg=="--showcase") result.showcase=true;
        else if(arg=="--gi-room") result.giRoom=true;
        else if(arg=="--gi-room-test") result.giRoomTest=true;
        else if(arg=="--gi-room-performance-test") result.giRoomPerformance=true;
        else if(arg=="--showcase-test") result.showcaseTest=true;
        else if(arg=="--showcase-motion-test") result.showcaseMotion=true;
        else if(arg=="--showcase-performance-test") {result.showcaseMotion=true;result.showcaseMeasureOnly=true;}
        else if(arg=="--showcase-stability-test") result.showcaseStability=true;
        else if(arg=="--showcase-water-diagnostic") {result.showcaseStability=true;result.showcaseWaterDiagnostic=true;}
        else if(arg=="--vulkan") result.vulkan=true;
        else if(arg=="--validation") result.validation=true;
        else if(arg=="--software-rays") result.softwareRays=true;
        else if(arg=="--vsync") result.vsync=true;
        else if(arg=="--lumen-view-stability-test") {result.viewStability=true;result.lumenGi=true;result.sky=true;}
        else if(arg=="--benchmark-present") result.benchmarkPresent=true;
        else if(arg=="--fullscreen") result.fullscreen=true;
        else if(arg=="--render-scale"&&i+1<argc){result.renderScale=std::stof(argv[++i]);Check(std::isfinite(result.renderScale)&&result.renderScale>=.5f&&result.renderScale<=1,"Render scale must be .5..1");}
        else if(arg=="--msaa"&&i+1<argc){result.msaaSamples=std::stoul(argv[++i]);Check(result.msaaSamples==1||result.msaaSamples==2||result.msaaSamples==4||result.msaaSamples==8,"MSAA must be 1, 2, 4 or 8");}
        else if(arg=="--lumen-reflection-scale"&&i+1<argc){result.lumenReflectionScale=std::stof(argv[++i]);Check(std::isfinite(result.lumenReflectionScale)&&result.lumenReflectionScale>=.5f&&result.lumenReflectionScale<=1,"Lumen reflection scale must be .5..1");}
        else if(arg=="--lumen-source-scale"&&i+1<argc){result.lumenSourceScale=std::stof(argv[++i]);Check(std::isfinite(result.lumenSourceScale)&&result.lumenSourceScale>=.25f&&result.lumenSourceScale<=1,"Lumen source scale must be .25..1");}
        else if(arg=="--lumen-reflection-detail"&&i+1<argc){result.lumenReflectionDetail=std::stoul(argv[++i]);Check(result.lumenReflectionDetail>=128&&result.lumenReflectionDetail<=512,"Lumen reflection detail must be 128..512");}
        else if (arg == "--quality-test") result.quality = true;
        else if (arg == "--shadow-test") result.shadows = true;
        else if (arg == "--closeup-test") result.closeup = true;
        else if (arg == "--advanced-test") result.advanced = true;
        else if (arg == "--advanced-quality-test") result.advancedQuality = true;
        else if (arg == "--benchmark") result.benchmark = true;
        else if (arg == "--sky-test") result.skyTest = true;
        else if (arg == "--probe-test") result.probeTest = true;
        else if (arg == "--probes") { result.probes=true; result.sky=true; }
        else if (arg == "--realtime-gi") {result.realtimeGi=true;result.sky=true;}
        else if (arg == "--realtime-gi-test") result.realtimeGiTest=true;
        else if (arg == "--realtime-gi-stability-test") result.realtimeGiStabilityTest=true;
        else if (arg == "--lumen-gi") {result.lumenGi=true;result.sky=true;}
        else if (arg == "--lumen-gi-test") result.lumenGiTest=true;
        else if (arg == "--sky") result.sky = true;
        else if (arg == "--sky-hdr" && i+1<argc) { result.skyHdr=argv[++i]; result.sky=true; }
        else if (arg == "--fps" && i+1<argc) {
            result.fpsLimit=std::stof(argv[++i]);
            Check(std::isfinite(result.fpsLimit) && result.fpsLimit>=0 && result.fpsLimit<=1000,"Invalid FPS limit");
        }
        else if ((arg=="--benchmark-width" || arg=="--benchmark-height" || arg=="--render-width" || arg=="--render-height") && i+1<argc) {
            const auto value=std::stoul(argv[++i]);
            Check(value>0 && value<=8192,"Invalid benchmark size");
            result.studySizeSet=true;
            if(arg=="--render-width"||arg=="--render-height")result.renderSizeSet=true;
            if(arg=="--benchmark-width"||arg=="--render-width") result.benchmarkWidth=static_cast<std::uint32_t>(value);
            else result.benchmarkHeight=static_cast<std::uint32_t>(value);
        }
        else if ((arg == "--yaw" || arg == "--pitch" || arg == "--distance") && i + 1 < argc) {
            const float value = std::stof(argv[++i]);
            Check(std::isfinite(value), "Invalid camera value");
            if (arg == "--yaw") { result.yaw = value; result.yawSet = true; }
            else if (arg == "--pitch") { result.pitch = value; result.pitchSet = true; }
            else { result.distance = value; result.distanceSet = true; }
        }
        else if (arg == "--procedural") result.procedural = true;
        else if ((arg == "--screenshot" || arg == "--legacy-textures") && i + 1 < argc) {
            if (arg == "--screenshot") result.screenshot = argv[++i];
            else result.legacyDirectory = argv[++i];
        } else throw std::runtime_error("Usage: material_lab_demo [--gi-room|--gi-room-test|--gi-room-performance-test|--showcase|--showcase-test|--showcase-motion-test|--showcase-performance-test|--showcase-stability-test|--showcase-water-diagnostic|--smoke-test|--quality-test|--shadow-test|--closeup-test|--advanced-test|--advanced-quality-test|--sky-test|--probe-test|--realtime-gi-test|--lumen-gi-test|--realtime-gi-stability-test|--lumen-view-stability-test|--benchmark] [--vulkan --validation --software-rays] [--sky --probes --realtime-gi --lumen-gi --sky-hdr FILE.hdr] [--lumen-reflection-scale .5..1 --lumen-source-scale .25..1 --lumen-reflection-detail 128..512] [--yaw RADIANS --pitch RADIANS --distance UNITS] [--fps LIMIT --vsync --fullscreen --msaa 1|2|4|8] [--benchmark-width W --benchmark-height H --benchmark-present] [--render-width W --render-height H --render-scale .5..1] [--procedural] [--legacy-textures DIR] [--screenshot FILE.png]");
    }
    Check(!result.procedural || result.legacyDirectory.empty(), "Choose --procedural or --legacy-textures, not both");
    Check(!result.probes||!result.realtimeGi,"Choose --probes or --realtime-gi");
    Check(!result.lumenGi||(!result.probes&&!result.realtimeGi),"Choose one of --lumen-gi, --probes, --realtime-gi");
    Check(!result.renderSizeSet||result.vulkan,"Explicit render dimensions require --vulkan");
    if(result.giRoom||result.giRoomTest||result.giRoomPerformance) {
        Check(result.legacyDirectory.empty(),"The GI room uses only procedural, untextured materials");
        result.procedural=true;
    }
    // CI smoke has a deterministic, asset-independent default. Interactive use
    // picks up the existing ignored legacy assets beside the executable.
    if (!result.showcaseTest && !result.smoke && !result.quality && !result.shadows && !result.closeup && !result.advanced && !result.advancedQuality && !result.skyTest && !result.probeTest && !result.realtimeGiTest && !result.realtimeGiStabilityTest && !result.lumenGiTest && !result.benchmark && !result.procedural && result.legacyDirectory.empty()) {
        const auto root = std::filesystem::absolute(argv[0]).parent_path() / "materials" / "tite";
        const std::array<const char*, 4> files{"square_tiles_03_diff_1k.png", "square_tiles_03_nor_gl_1k.png",
                                              "square_tiles_03_rough_1k.png", "square_tiles_03_ao_1k.png"};
        bool complete = true;
        for (const auto file : files) complete = complete && std::filesystem::is_regular_file(root / file);
        if (complete) result.legacyDirectory = root.string();
    }
    return result;
}
Render::SkyLightSettings LoadSky(const Options& options) {
    Render::SkyLightSettings sky; sky.enabled=true;
    if (options.skyHdr.empty()) return sky;
    std::ifstream file(std::filesystem::path(std::u8string(options.skyHdr.begin(),options.skyHdr.end())),std::ios::binary|std::ios::ate);
    Check(bool(file),"Cannot open HDR sky");
    const auto length=file.tellg();
    Check(length>0 && length<=128*1024*1024,"HDR sky file exceeds 128 MiB");
    std::vector<unsigned char> bytes(static_cast<std::size_t>(length));
    file.seekg(0); file.read(reinterpret_cast<char*>(bytes.data()),bytes.size());
    Check(bool(file)&&stbi_is_hdr_from_memory(bytes.data(),int(bytes.size())),"Sky must be a linear Radiance .hdr image");
    int w=0,h=0,channels=0;
    Check(stbi_info_from_memory(bytes.data(),int(bytes.size()),&w,&h,&channels)&&w>0&&h>0&&w<=8192&&h<=4096,
          "HDR sky dimensions must be <=8192x4096");
    stbi_set_flip_vertically_on_load_thread(0);
    std::unique_ptr<float,decltype(&stbi_image_free)> pixels(
        stbi_loadf_from_memory(bytes.data(),int(bytes.size()),&w,&h,&channels,3),stbi_image_free);
    Check(bool(pixels),"HDR sky decoding failed");
    std::vector<glm::vec3> radiance(std::size_t(w)*h);
    for(std::size_t i=0;i<radiance.size();++i) radiance[i]={pixels.get()[i*3],pixels.get()[i*3+1],pixels.get()[i*3+2]};
    sky.environment=Render::BakeSkyEnvironment(w,h,radiance);
    std::cout << "Loaded linear HDR sky: " << w << 'x' << h << '\n';
    return sky;
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
    Check(lab.MaterialCount() == 21, "Expected the gallery and three advanced materials");
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
    std::cout << "MaterialLab smoke passed: HDR/CSM/mirror/Bloom/SSAO/all filters, resize, material edits; backend errors=0\n";
}

void Quality(MaterialLabModule& lab, const Options& options) {
    using Render::AntialiasingMode;
    FrameSettings frame; frame.width = 640; frame.height = 400; frame.present = false;
    frame.effects.temporal.antialiasing = AntialiasingMode::None;
    const auto noAa = Capture(lab, frame);
    auto stats = lab.PipelineStatistics();
    Check(stats.reflectionWidth == frame.width && stats.reflectionHeight == frame.height, "Mirror is not full resolution by default");
    frame.effects.reflection.resolutionScale = 2;
    const auto mirror2x = Capture(lab, frame);
    Check(lab.PipelineStatistics().reflectionWidth == 1280 && lab.PipelineStatistics().reflectionHeight == 800,
          "Mirror supersampling size is incorrect");
    std::cout << "Mirror 2x delta=" << Difference(noAa, mirror2x) << '\n';
    frame.effects.reflection.resolutionScale = 1;
    frame.effects.temporal.antialiasing = AntialiasingMode::FXAA;
    const auto fxaa = Capture(lab, frame);
    const auto fxaaDelta = Difference(noAa, fxaa);
    Check(fxaaDelta > 0.01 && fxaaDelta < 8, "FXAA has no effect or excessively changes the image");
    frame.effects.temporal.antialiasing = AntialiasingMode::None;
    frame.effects.temporal.msaaSamples = 4;
    const auto msaa = Capture(lab, frame);
    Check(lab.PipelineStatistics().resolvePasses == 3 && lab.PipelineStatistics().msaaSamples == 4, "Missing main/mirror/depth MSAA resolves");
    const auto msaaDelta = Difference(noAa, msaa);
    Check(msaaDelta > 0.01 && msaaDelta < 8, "MSAA has no effect or excessively changes the image");
    frame.effects.temporal.antialiasing = AntialiasingMode::TAA;
    auto first = Capture(lab, frame);
    Check(!lab.PipelineStatistics().historyUsed, "TAA used uninitialized history");
    auto last = first;
    double earlyChange = 0, lateChange = 0;
    for (unsigned i = 1; i < 32; ++i) {
        auto current = Capture(lab, frame);
        Check(lab.PipelineStatistics().historyUsed && lab.PipelineStatistics().temporalPasses == 1, "TAA failed to reuse completed history");
        const auto delta = Difference(last, current);
        if (i <= 8) earlyChange += delta / 8;
        if (i >= 24) lateChange += delta / 8;
        last = std::move(current);
    }
    Check(lateChange < 2.0, "Static TAA produces unstable frames");
    Save(last, options.screenshot);
    frame.effects.temporal.historyWeight = 0;
    frame.effects.cameraCut = true;
    auto unfiltered = Capture(lab, frame);
    frame.effects.cameraCut = false;
    double unfilteredChange = 0;
    for (unsigned i = 1; i <= 16; ++i) {
        auto current = Capture(lab, frame);
        if (i > 8) unfilteredChange += Difference(unfiltered, current) / 8;
        unfiltered = std::move(current);
    }
    frame.effects.temporal.historyWeight = 0.9f;
    std::cout << "TAA history-off mean change=" << unfilteredChange << " accumulated=" << lateChange << '\n';
    Check(lateChange < unfilteredChange * 0.8, "TAA history did not sufficiently reduce static-frame variation");
    std::cout << "AA deltas FXAA=" << fxaaDelta << " MSAA4=" << msaaDelta
              << " TAA mean early/late=" << earlyChange << '/' << lateChange << '\n';
    // Material changes, cuts, projection/target changes must not drag stale HDR.
    Check(lab.SetMaterialParameter(7, "baseColor", glm::vec4(0.1f, 0.3f, 1, 1)), lab.LastError());
    Capture(lab, frame); Check(!lab.PipelineStatistics().historyUsed, "Material edit retained stale TAA history");
    Check(lab.ResetMaterial(7), lab.LastError()); Capture(lab, frame);
    frame.effects.cameraCut = true;
    const auto cut = Capture(lab, frame);
    Check(!lab.PipelineStatistics().historyUsed && !lab.PipelineStatistics().cameraHistoryUsed, "Camera cut reused temporal data");
    Check(Difference(first, cut) < 0.001, "Camera cut did not restore first-frame jitter/image");
    frame.effects.cameraCut = false;
    frame.width = 319; frame.height = 197; Capture(lab, frame);
    Check(!lab.PipelineStatistics().historyUsed, "Resize retained stale history");
    Capture(lab, frame); Check(lab.PipelineStatistics().historyUsed, "Odd-size TAA history failed");
    frame.width = frame.height = 1; Capture(lab, frame); Capture(lab, frame);
    frame.width = 640; frame.height = 400;
    frame.effects.temporal.antialiasing = AntialiasingMode::None;
    frame.effects.temporal.motionBlurEnabled = false;
    frame.effects.cameraCut = true;
    const auto still = Capture(lab, frame);
    frame.effects.cameraCut = false;
    frame.effects.temporal.motionBlurEnabled = true;
    Check(Difference(still, Capture(lab, frame)) < 0.001, "Static camera was blurred");
    frame.cameraYaw = 0.035f;
    const auto movingBlur = Capture(lab, frame);
    Check(lab.PipelineStatistics().motionBlurPasses == 1 && lab.PipelineStatistics().cameraHistoryUsed,
          "Camera motion blur did not use previous camera");
    frame.effects.temporal.motionBlurEnabled = false;
    const auto sharp = Capture(lab, frame);
    const auto blurDelta = Difference(movingBlur, sharp);
    Check(blurDelta > 0.01 && blurDelta < 12, "Camera motion blur missing or excessive");
    frame.effects.temporal.motionBlurEnabled = true;
    frame.effects.cameraCut = true;
    Check(Difference(sharp, Capture(lab, frame)) < 0.001, "Camera cut smeared the image");
    // A rejected configuration must preserve resources and allow a later frame.
    frame.effects.temporal.msaaSamples = 3;
    Check(!lab.Render(frame), "Invalid MSAA accepted");
    Check(lab.Readback().rgba.empty(), "Failed quality frame exposed stale pixels");
    frame.effects.temporal.msaaSamples = 1; Capture(lab, frame);
    // Compare identical TAA jitter/history sequences with blur disabled/enabled.
    // A stationary camera must never turn projection jitter into motion streaks.
    frame.effects.temporal.antialiasing = AntialiasingMode::TAA;
    frame.effects.temporal.motionBlurEnabled = false;
    std::array<FramePixels, 8> stationaryTaa;
    for (unsigned i = 0; i < stationaryTaa.size(); ++i) {
        frame.effects.cameraCut = i == 0;
        stationaryTaa[i] = Capture(lab, frame);
    }
    frame.effects.temporal.motionBlurEnabled = true;
    for (unsigned i = 0; i < stationaryTaa.size(); ++i) {
        frame.effects.cameraCut = i == 0;
        Check(Difference(stationaryTaa[i], Capture(lab, frame)) < 0.001, "TAA jitter leaked into camera motion blur");
    }
    frame.effects.cameraCut = false;
    frame.effects.temporal.msaaSamples = 4;
    for (unsigned i = 0; i < 12; ++i) {
        frame.cameraYaw += 0.007f;
        Capture(lab, frame);
        Check(lab.PipelineStatistics().historyUsed == (i != 0), "Moving camera lost TAA history");
    }
    std::cout << "Camera blur delta=" << blurDelta << "; quality smoke passed; backend errors=0\n";
}

void ShadowSmoke(MaterialLabModule& lab, const Options& options) {
    FrameSettings frame; frame.width = 960; frame.height = 600; frame.present = false;
    frame.shadowStudy = true; frame.cameraYaw = 0.38f; frame.cameraPitch = 0.46f;
    frame.effects.temporal.antialiasing = Render::AntialiasingMode::None;
    frame.effects.post.bloomEnabled = frame.effects.post.ssaoEnabled = false;
    Check(frame.effects.shadows.atlasResolution == 4096, "Expected four 2048x2048 shadow tiles by default");
    const auto shaded = Capture(lab, frame);
    Save(shaded, options.screenshot);
    Check(lab.InspectPipelineBuffers().x < 0.99f, "Shadow study contains no caster depth");
    frame.effects.shadows.debugView = Render::ShadowDebugView::Visibility;
    const auto visibility = Capture(lab, frame);
    const glm::vec3 studyTarget(0,0.5f,-0.8f);
    const auto studyEye = studyTarget + frame.cameraDistance * 0.65f * glm::vec3(
        std::sin(frame.cameraYaw)*std::cos(frame.cameraPitch), std::sin(frame.cameraPitch),
        std::cos(frame.cameraYaw)*std::cos(frame.cameraPitch));
    const auto studyVP = glm::perspective(glm::radians(42.0f), float(frame.width)/frame.height, 0.1f, 100.0f) *
        glm::lookAt(studyEye, studyTarget, glm::vec3(0,1,0));
    double faceOcclusion = 0;
    for (int y = 0; y < 24; ++y) for (int x = 0; x < 24; ++x) {
        // Front face is sunlit with no intervening caster. Stay away from its
        // silhouette/contact edges so any occlusion here is self-shadow acne.
        glm::vec4 pixel = studyVP * glm::vec4(0.55f + x*1.3f/23, 0.25f + y*1.3f/23, 0.9f, 1);
        pixel /= pixel.w;
        const auto px = std::clamp(int((pixel.x*0.5f+0.5f)*frame.width), 0, int(frame.width)-1);
        const auto py = std::clamp(int((pixel.y*0.5f+0.5f)*frame.height), 0, int(frame.height)-1);
        faceOcclusion += 1.0 - visibility.rgba[(py*frame.width+px)*4]/255.0;
    }
    faceOcclusion /= 24*24;
    std::cout << "Sunlit box mean false occlusion=" << faceOcclusion << '\n';
    Check(faceOcclusion < 0.002, "Sunlit box front face contains shadow acne");
    std::size_t lit = 0, shadowed = 0;
    for (std::size_t i = 0; i < visibility.rgba.size(); i += 4) {
        if (visibility.rgba[i] != visibility.rgba[i+1] || visibility.rgba[i] != visibility.rgba[i+2]) continue;
        lit += visibility.rgba[i] >= 254;
        shadowed += visibility.rgba[i] < 64;
    }
    Check(lit > frame.width * frame.height / 8 && shadowed > 100, "Shadow visibility debug is blank or lacks contrast");
    if (!options.screenshot.empty()) {
        auto path = std::filesystem::path(options.screenshot);
        Save(visibility, (path.parent_path() / (path.stem().string() + "_visibility.png")).string());
    }
    frame.effects.shadows.debugView = Render::ShadowDebugView::Cascades;
    const auto cascades = Capture(lab, frame);
    Check(Difference(visibility, cascades) > 1, "Cascade debug is not showing cascade colors");
    if (!options.screenshot.empty()) {
        auto path = std::filesystem::path(options.screenshot);
        Save(cascades, (path.parent_path() / (path.stem().string() + "_cascades.png")).string());
    }
    frame.effects.shadows.debugView = Render::ShadowDebugView::Visibility;
    frame.effects.shadows.atlasResolution = 2048;
    const auto low = Capture(lab, frame);
    frame.effects.shadows.atlasResolution = 4096;
    Check(Difference(visibility, Capture(lab, frame)) < 0.001, "Shadow resize failed to restore the image");
    frame.effects.shadows.atlasResolution = 8192;
    const auto high = Capture(lab, frame);
    Check(Difference(visibility, high) < 3.0, "High-resolution shadow changed scene alignment");
    frame.effects.shadows.atlasResolution = 4096;
    frame.effects.shadows.sunDirection = {-1,-0.22f,-0.4f};
    Capture(lab, frame); // grazing light + slope + thin caster, with no TAA/SSAO hiding defects
    frame.effects.shadows.debugView = Render::ShadowDebugView::None;
    frame.effects.temporal.antialiasing = Render::AntialiasingMode::TAA;
    frame.effects.temporal.msaaSamples = 4;
    for (unsigned i = 0; i < 12; ++i) {
        frame.cameraYaw += 0.001f;
        Capture(lab, frame);
    }
    frame.effects.shadows.depthBiasTexels = -1;
    Check(!lab.Render(frame), "Negative shadow texel bias accepted");
    frame.effects.shadows.depthBiasTexels = 0.05f;
    frame.effects.shadows.receiverPlaneClampTexels = std::numeric_limits<float>::quiet_NaN();
    Check(!lab.Render(frame), "NaN receiver-plane clamp accepted");
    frame.effects.shadows.receiverPlaneClampTexels = 4;
    Capture(lab, frame);
    std::cout << "Shadow study passed: lit=" << lit << " shadowed=" << shadowed
              << " 2048/4096 delta=" << Difference(low, visibility)
              << " 4096/8192 delta=" << Difference(visibility, high) << "; backend errors=0\n";
}

// A portrait, grazing gallery view reproduces the bright subpixel highlights
// and dark-side AO artifacts that a front-on gallery smoke test can miss.
void Closeup(MaterialLabModule& lab, const Options& options) {
    FrameSettings frame; frame.width = 640; frame.height = 724; frame.present = false;
    frame.cameraYaw = options.yaw; frame.cameraPitch = options.pitch; frame.cameraDistance = options.distance;
    frame.effects.temporal.antialiasing = Render::AntialiasingMode::TAA;
    frame.effects.temporal.msaaSamples = 4;
    const auto settle = [&] {
        frame.effects.cameraCut = true;
        auto result = Capture(lab, frame);
        frame.effects.cameraCut = false;
        for (int i = 0; i < 31; ++i) result = Capture(lab, frame);
        return result;
    };
    const auto saveVariant = [&](const FramePixels& pixels, const char* suffix) {
        if (options.screenshot.empty()) return;
        auto path = std::filesystem::path(options.screenshot);
        path.replace_filename(path.stem().string() + suffix + path.extension().string());
        Save(pixels, path.string());
    };
    const auto baseline = settle(); Save(baseline, options.screenshot);
    auto previous = baseline;
    double delta = 0;
    for (int i = 0; i < 16; ++i) {
        auto current = Capture(lab, frame);
        delta += Difference(previous, current) / 16;
        previous = std::move(current);
    }
    Check(delta < 2.0, "Close-up static TAA is unstable");
    std::cout << "Close-up static frame delta=" << delta << '\n';
    frame.effects.post.bloomEnabled = false; saveVariant(settle(), "_no_bloom");
    frame.effects.post.ssaoEnabled = false; saveVariant(settle(), "_no_ao_bloom");
    frame.effects.shadows.enabled = false; saveVariant(settle(), "_no_shadow_ao_bloom");
    frame.effects.shadows.enabled = true;
    frame.effects.shadows.debugView = Render::ShadowDebugView::Visibility;
    saveVariant(settle(), "_visibility");
    frame.effects.shadows.debugView = Render::ShadowDebugView::None;
    frame.effects.temporal.antialiasing = Render::AntialiasingMode::None;
    frame.effects.temporal.msaaSamples = 1;
    saveVariant(Capture(lab, frame), "_no_aa_ao_bloom");
    std::cout << "Close-up effect isolation passed; backend errors=0\n";
}

void ConfigureLightingStudy(FrameSettings& frame) {
    frame.lightingStudy = true; frame.shadowStudy = false; frame.isolatedMaterial = -1;
    frame.cameraYaw = .25f; frame.cameraPitch = .28f; frame.cameraDistance = 15;
    frame.effects.areaLights.count = 1;
    auto& light = frame.effects.areaLights.lights[0];
    light.center = {0,3.8f,0}; light.halfAxisU = {1.3f,0,0}; light.halfAxisV = {0,0,.9f};
    light.radiance = {4,3.8f,3.5f};
    frame.effects.indirect.enabled = true;
    frame.effects.indirect.radius = 4;
    frame.effects.reflection.enabled = false;
    frame.effects.reflection.plane = {0,1,0,0};
    frame.effects.post.toneMap = Render::ToneMapMode::ACES;
    frame.effects.cameraCut = true;
}
void ApplyAdvancedCamera(FrameSettings& frame, const Options& options) {
    if (options.yawSet) frame.cameraYaw = options.yaw;
    if (options.pitchSet) frame.cameraPitch = options.pitch;
    if (options.distanceSet) frame.cameraDistance = options.distance;
}
void Advanced(MaterialLabModule& lab, const Options& options) {
    FrameSettings frame; ConfigureLightingStudy(frame);
    ApplyAdvancedCamera(frame, options);
    frame.width=640; frame.height=400; frame.present=false;
    frame.effects.temporal.antialiasing = Render::AntialiasingMode::FXAA;
    frame.effects.post.bloomEnabled=false;
    const auto baseline = Capture(lab, frame);
    const auto stats = lab.PipelineStatistics();
    Check(stats.transparentPasses == 1 && stats.indirectPasses == 6 + Render::IndirectLightingDenoisePasses,
          "Missing transparency/GI passes");
    const auto changed = [&](const FramePixels& before, const char* label, double minimum=.001) {
        auto after=Capture(lab,frame); const auto delta=Difference(before,after);
        std::cout << label << " delta=" << delta << '\n';
        Check(delta>minimum,std::string("No visible response: ")+label); return after;
    };
    frame.effects.indirect.enabled=false; const auto noGI=changed(baseline,"single diffuse bounce");
    Check(lab.PipelineStatistics().indirectPasses==0,"Disabled GI still drew captures");
    frame.effects.indirect.enabled=true;
    frame.effects.areaLights.count=0; const auto emissionGI=changed(baseline,"rectangle area light");
    frame.effects.indirect.enabled=false;
    changed(emissionGI,"emission illuminates neighboring surfaces");
    frame.effects.indirect.enabled=true;
    Check(lab.SetMaterialParameter(20,"emissiveIntensity",0.0f),lab.LastError());
    const auto ambientOnly=Capture(lab,frame);
    frame.effects.indirect.enabled=false;
    Check(Difference(ambientOnly,Capture(lab,frame))<.001,"Ambient was incorrectly fed into GI source radiance");
    frame.effects.indirect.enabled=true;
    frame.effects.areaLights.count=1;
    const auto diffuseGI=changed(baseline,"textured emission");
    frame.effects.indirect.enabled=false;
    changed(diffuseGI,"non-emissive diffuse surface color bounce");
    frame.effects.indirect.enabled=true; Check(lab.ResetMaterial(20),lab.LastError());
    Check(lab.SetMaterialParameter(18,"opacity",0.0f),lab.LastError());
    Check(lab.SetMaterialParameter(19,"opacity",0.0f),lab.LastError());
    changed(baseline,"transparent coverage");
    Check(lab.ResetMaterial(18)&&lab.ResetMaterial(19),lab.LastError());
    Check(lab.SetMaterialParameter(19,"useDistortionMap",false),lab.LastError());
    changed(baseline,"custom refraction map"); Check(lab.ResetMaterial(19),lab.LastError());
    frame.lightTime=2; changed(baseline,"flow map and UV motion"); frame.lightTime=0;
    frame.effects.temporal.msaaSamples=4;
    frame.effects.temporal.antialiasing=Render::AntialiasingMode::TAA;
    frame.effects.post.bloomEnabled=true;
    auto settled=Capture(lab,frame); frame.effects.cameraCut=false;
    for(int i=0;i<15;++i) settled=Capture(lab,frame);
    Check(lab.PipelineStatistics().historyUsed,"Advanced scene did not accumulate TAA");
    Save(settled,options.screenshot);
    frame.effects.temporal.motionBlurEnabled=true;
    frame.cameraYaw+=.03f; frame.lightTime=.5f; Capture(lab,frame);
    frame.effects.reflection.enabled=true; frame.effects.reflection.resolutionScale=.5f;
    Capture(lab,frame);
    Check(lab.PipelineStatistics().reflectionPasses==1,"Transparent mirror view did not render");
    frame.effects.areaLights.lights[0].halfAxisU=glm::vec3(0);
    Check(!lab.Render(frame),"Degenerate area light accepted");
    std::cout << "Advanced materials passed: transmission, refraction/flow maps, emission, area light, diffuse bounce, MSAA/TAA/blur; backend errors=0\n";
}

// Kept separate from the short advanced smoke: these captures must preserve
// enough pixels to reveal screen-space GI streaks and refractive double edges.
void AdvancedQuality(MaterialLabModule& lab, const Options& options) {
    struct View { const char* name; float yaw, pitch, distance; };
    const bool customCamera = options.yawSet || options.pitchSet || options.distanceSet;
    const std::array<View, 2> views{{{"high", .25f, .95f, 15}, {"glass", -.42f, .10f, 10.5f}}};
    for (std::size_t viewIndex = 0; viewIndex < (customCamera ? 1u : views.size()); ++viewIndex) {
        FrameSettings frame; ConfigureLightingStudy(frame);
        if (!customCamera) {
            frame.cameraYaw = views[viewIndex].yaw;
            frame.cameraPitch = views[viewIndex].pitch;
            frame.cameraDistance = views[viewIndex].distance;
        }
        ApplyAdvancedCamera(frame, options);
        const std::string viewName = customCamera ? "custom" : views[viewIndex].name;
        frame.width = 960; frame.height = 600; frame.present = false;
        frame.lightTime = 1.0f; // A fixed flowing-map phase, never wall-clock time.
        frame.effects.temporal.antialiasing = Render::AntialiasingMode::None;
        frame.effects.temporal.msaaSamples = 1;
        frame.effects.temporal.motionBlurEnabled = false;
        frame.effects.post.bloomEnabled = false;
        const auto saveVariant = [&](const FramePixels& pixels, const char* suffix) {
            if (options.screenshot.empty()) return;
            auto path = std::filesystem::path(options.screenshot);
            path.replace_filename(path.stem().string() + "_" + viewName + suffix + path.extension().string());
            Save(pixels, path.string());
        };
        const auto resetTransparent = [&] {
            Check(lab.ResetMaterial(18) && lab.ResetMaterial(19), lab.LastError());
        };
        const auto opacity = [&](std::size_t material, float strength) {
            Check(lab.SetMaterialParameter(material, "opacity", strength), lab.LastError());
        };
        resetTransparent();
        std::cout << "Advanced quality " << viewName << ": 960x600 yaw=" << frame.cameraYaw
                  << " pitch=" << frame.cameraPitch << " distance=" << frame.cameraDistance
                  << ", fixed lightTime=" << frame.lightTime << '\n';
        const auto raw = Capture(lab, frame);
        saveVariant(raw, "_raw");
        if (viewIndex == 0) Save(raw, options.screenshot);
        Check(Difference(raw, Capture(lab, frame)) < .001, "Static non-temporal advanced capture changed");
        frame.effects.indirect.enabled = false;
        const auto noGI = Capture(lab, frame); saveVariant(noGI, "_no_gi");
        Check(lab.PipelineStatistics().indirectPasses == 0, "GI-off quality capture still recorded GI passes");
        opacity(18, 0); opacity(19, 0);
        const auto opaqueNoGI = Capture(lab, frame); saveVariant(opaqueNoGI, "_opaque_no_gi");
        frame.effects.indirect.enabled = true;
        const auto opaqueGI = Capture(lab, frame); saveVariant(opaqueGI, "_no_transparency");
        const double giDelta = Difference(opaqueGI, opaqueNoGI);
        const double transparencyDelta = Difference(noGI, opaqueNoGI);
        std::cout << "Advanced quality " << viewName << " isolated GI delta=" << giDelta
                  << ", isolated transmission delta=" << transparencyDelta << '\n';
        // Default presets intentionally contain both effects. A custom view may
        // legitimately turn away from either one; still export its diagnostics.
        if (!customCamera) {
            Check(giDelta > .001, "Quality preset has no visible diffuse bounce");
            Check(transparencyDelta > .001, "Quality preset does not show transparent surfaces");
        }
        auto giDifference = opaqueGI;
        for (std::size_t i = 0; i < giDifference.rgba.size(); ++i) {
            giDifference.rgba[i] = i % 4 == 3 ? 255 : static_cast<std::uint8_t>(std::min(255,
                4 * std::abs(int(opaqueGI.rgba[i]) - int(opaqueNoGI.rgba[i]))));
        }
        saveVariant(giDifference, "_gi_difference_x4"); // Display-space diagnostic, not HDR irradiance.
        resetTransparent();
        Check(Difference(raw, Capture(lab, frame)) < .001, "Effect isolation did not restore the raw capture");

        // No GI, Bloom or temporal accumulation can hide the refraction edge.
        // Vary only the glass optical strength, keeping water at its asset value.
        frame.effects.indirect.enabled = false;
        opacity(18, .5f); saveVariant(Capture(lab, frame), "_glass_half_no_gi");
        opacity(18, 1.0f); saveVariant(Capture(lab, frame), "_glass_full_no_gi");
        resetTransparent(); frame.effects.indirect.enabled = true;
        frame.effects.post.bloomEnabled = true;
        saveVariant(Capture(lab, frame), "_bloom");
        frame.effects.post.bloomEnabled = false;

        // Keep MSAA=1 so raw/TAA comparisons isolate the temporal stage.
        frame.effects.temporal.antialiasing = Render::AntialiasingMode::TAA;
        frame.effects.cameraCut = true;
        auto taa = Capture(lab, frame);
        saveVariant(taa, "_taa_first");
        Check(!lab.PipelineStatistics().historyUsed, "Quality TAA reused history after a camera cut");
        frame.effects.cameraCut = false;
        for (int i = 0; i < 15; ++i) taa = Capture(lab, frame);
        Check(lab.PipelineStatistics().historyUsed, "Quality TAA did not accumulate static history");
        saveVariant(taa, "_taa");
        double staticDelta = 0;
        for (int i = 0; i < 8; ++i) {
            auto next = Capture(lab, frame);
            staticDelta += Difference(taa, next) / 8;
            taa = std::move(next);
        }
        Check(staticDelta < 2.0, "Advanced quality static TAA is unstable");
        frame.cameraYaw += .015f;
        const auto history = Capture(lab, frame); saveVariant(history, "_taa_moved_history");
        Check(lab.PipelineStatistics().historyUsed, "Small quality camera move unexpectedly reset TAA");
        frame.effects.cameraCut = true;
        const auto cut = Capture(lab, frame); saveVariant(cut, "_taa_moved_cut");
        Check(!lab.PipelineStatistics().historyUsed, "Quality camera cut retained old transparent history");
        // The two captures can differ from their jitter phase as well as history;
        // report the value for diagnosis, not as a claim of ground-truth error.
        std::cout << "Advanced quality " << viewName << " static TAA delta=" << staticDelta
                  << ", moved history/cut delta=" << Difference(history, cut) << '\n';
        // Presentable result at the original fixed view, separate from the
        // MSAA=1 diagnostic pairs above. Do not mistake raw silhouette aliasing
        // for an indirect-lighting or refraction reconstruction defect.
        frame.cameraYaw -= .015f;
        frame.effects.temporal.msaaSamples = 4;
        frame.effects.post.bloomEnabled = true;
        frame.effects.cameraCut = true;
        auto settled = Capture(lab, frame);
        frame.effects.cameraCut = false;
        for (int i = 0; i < 15; ++i) settled = Capture(lab, frame);
        Check(lab.PipelineStatistics().msaaSamples == 4 && lab.PipelineStatistics().historyUsed,
              "Final advanced quality image did not use MSAA4 and accumulated TAA");
        saveVariant(settled, "_settled");
        resetTransparent();
    }
    std::cout << "Advanced quality isolation passed; backend errors=0\n";
}

struct Input { float scroll = 0; };
class FramePacer {
#ifdef _WIN32
    // High-resolution wait avoids coarse Windows sleep ticks turning 60 FPS
    // pacing into a substantially lower frame rate. No polling/busy spin.
    HANDLE timer_=CreateWaitableTimerExW(nullptr,nullptr,0x00000002,TIMER_ALL_ACCESS);
#endif
public:
    ~FramePacer() {
#ifdef _WIN32
        if(timer_) CloseHandle(timer_);
#endif
    }
    void WaitUntil(std::chrono::steady_clock::time_point deadline) {
#ifdef _WIN32
        const auto remaining=deadline-std::chrono::steady_clock::now();
        if(remaining<=std::chrono::steady_clock::duration::zero()) return;
        if(timer_) {
            LARGE_INTEGER due;
            due.QuadPart=-std::max<std::int64_t>(1,
                std::chrono::duration_cast<std::chrono::duration<std::int64_t,std::ratio<1,10000000>>>(remaining).count());
            if(SetWaitableTimer(timer_,&due,0,nullptr,nullptr,FALSE)) {
                WaitForSingleObject(timer_,INFINITE); return;
            }
        }
#endif
        std::this_thread::sleep_until(deadline);
    }
};
void SkyQuality(MaterialLabModule& lab,const Options& options) {
    FrameSettings frame; ConfigureLightingStudy(frame); ApplyAdvancedCamera(frame,options);
    frame.width=960; frame.height=600; frame.present=false;
    frame.effects.indirect.enabled=false;
    frame.effects.areaLights.count=0;
    Check(lab.SetMaterialParameter(20,"emissiveIntensity",0.0f),lab.LastError());
    const auto dark=Capture(lab,frame);
    frame.effects.sky=LoadSky(options);
    const auto lit=Capture(lab,frame);
    Check(Difference(dark,lit)>3,"Sky did not light the isolated study scene");
    Check(lab.PipelineStatistics().skyPasses==1,"Main-view sky background missing");
    frame.effects.sky.background=false;
    const auto surfaceOnly=Capture(lab,frame);
    frame.effects.sky.diffuseStrength=0;
    const auto specularOnly=Capture(lab,frame);
    Check(Difference(surfaceOnly,specularOnly)>.05,"Sky diffuse response missing");
    frame.effects.sky.specularStrength=0;
    const auto noSkyEnergy=Capture(lab,frame);
    Check(Difference(specularOnly,noSkyEnergy)>.05,"Sky specular response missing on metals/glass");
    frame.effects.sky.diffuseStrength=frame.effects.sky.specularStrength=1;
    frame.effects.sky.occlusionStrength=0;
    Check(Difference(surfaceOnly,Capture(lab,frame))>.001,"Sky did not respond to AO visibility");
    frame.effects.sky.occlusionStrength=1;
    frame.effects.indirect.enabled=true;
    Check(Difference(surfaceOnly,Capture(lab,frame))>.001,"Sky-lit diffuse sources did not feed one bounce");
    frame.effects.sky.intensity=0;
    Check(Difference(noSkyEnergy,Capture(lab,frame))<.001,"Zero-intensity sky retained lighting or GI");
    frame.effects.sky.intensity=1;
    frame.effects.sky.background=true;
    frame.effects.areaLights.count=1;
    Check(lab.ResetMaterial(20),lab.LastError());
    frame.effects.temporal.msaaSamples=4;
    frame.effects.temporal.antialiasing=Render::AntialiasingMode::TAA;
    frame.effects.post.bloomEnabled=true;
    Capture(lab,frame); frame.effects.cameraCut=false;
    FramePixels settled;
    for(int i=0;i<24;++i) settled=Capture(lab,frame);
    const auto stability=Difference(settled,Capture(lab,frame));
    Check(lab.PipelineStatistics().historyUsed&&stability<.5,"Sky TAA history was unstable");
    Save(settled,options.screenshot);
    frame.effects.sky.rotation+=.4f;
    Capture(lab,frame);
    Check(!lab.PipelineStatistics().historyUsed,"Sky rotation did not invalidate history");
    frame.effects.reflection.enabled=true;
    Capture(lab,frame);
    Check(lab.PipelineStatistics().skyPasses==2&&lab.PipelineStatistics().reflectionPasses==1,
          "Reflected sky/transparent mirror view missing");
    if(!options.screenshot.empty()) {
        auto gallery=frame; gallery.lightingStudy=false; gallery.cameraYaw=-.25f;
        gallery.cameraPitch=.28f; gallery.cameraDistance=18;
        gallery.effects.indirect.enabled=false; gallery.effects.areaLights.count=0;
        gallery.effects.reflection.plane={0,1,0,3.25f}; gallery.effects.cameraCut=true;
        Capture(lab,gallery); gallery.effects.cameraCut=false;
        FramePixels pixels;
        for(int i=0;i<16;++i) pixels=Capture(lab,gallery);
        auto path=std::filesystem::path(options.screenshot);
        path.replace_filename(path.stem().string()+"_gallery"+path.extension().string());
        Save(pixels,path.string());
    }
    frame.effects.sky.intensity=-1;
    Check(!lab.Render(frame),"Invalid sky strength accepted");
    std::cout << "Sky quality passed: diffuse/specular/AO, zero intensity, sky diffuse bounce, reflected view, TAA stability="
              << stability << "; backend errors=0\n";
}
void ProbeQuality(MaterialLabModule& lab,const Options& options) {
    FrameSettings frame; ConfigureLightingStudy(frame);
    frame.width=960; frame.height=600; frame.present=false; frame.waitForProbeBake=true;
    frame.effects.sky=LoadSky(options); frame.effects.probes.enabled=true;
    frame.effects.areaLights.count=0; frame.effects.post.bloomEnabled=false;
    frame.closedLightingStudy=true; frame.cameraDistance=4; frame.cameraYaw=.25f;
    frame.effects.temporal.antialiasing=Render::AntialiasingMode::FXAA;
    Check(lab.SetMaterialParameter(20,"emissiveIntensity",0.0f),lab.LastError());
    const auto sealed=Capture(lab,frame);
    const auto mean=[](const FramePixels& pixels) {
        double sum=0; for(std::size_t i=0;i<pixels.rgba.size();i+=4) sum+=pixels.rgba[i]+pixels.rgba[i+1]+pixels.rgba[i+2];
        return sum/(pixels.width*pixels.height*3.0);
    };
    std::cout<<"Sealed room sky mean="<<mean(sealed)<<" /255\n";
    Check(mean(sealed)<.5,"Enclosed room retained unoccluded sky energy");
    Check(lab.PipelineStatistics().validDiffuseProbes>0&&lab.PipelineStatistics().indirectPasses==0,"World probes missing or redundant screen trace");
    const auto sealedVolume=lab.PreparedDiffuseProbes(); Check(bool(sealedVolume),"No prepared probe snapshot");
    frame.effects.probes.enabled=false;
    const auto leaking=Capture(lab,frame);
    Check(Difference(sealed,leaking)>10,"Local sky visibility did not suppress indoor lighting");
    frame.effects.probes.enabled=true;
    Check(lab.ResetMaterial(20),lab.LastError());
    const auto emissive=Capture(lab,frame);
    Check(Difference(sealed,emissive)>.1,"World-space emissive bounce did not illuminate the closed room");
    const auto litVolume=lab.PreparedDiffuseProbes(); Check(litVolume!=sealedVolume,"Emission edit did not replace baked radiance");
    const auto sample=Render::SampleDiffuseProbeVolume(*litVolume,{-.5f,.1f,0},{0,1,0},{0,1,0});
    frame.cameraYaw-=.4f; Capture(lab,frame);
    Check(lab.PreparedDiffuseProbes()==litVolume,"Camera motion rebuilt world-space light transport");
    const auto movedSample=Render::SampleDiffuseProbeVolume(*lab.PreparedDiffuseProbes(),{-.5f,.1f,0},{0,1,0},{0,1,0});
    Check(glm::length(sample.irradianceOverPi-movedSample.irradianceOverPi)==0,"Offscreen light changed the cached receiver lighting");
    frame.cameraYaw=.25f; frame.effects.temporal.msaaSamples=4;
    frame.effects.temporal.antialiasing=Render::AntialiasingMode::TAA;
    frame.effects.post.bloomEnabled=true;
    Capture(lab,frame); frame.effects.cameraCut=false;
    FramePixels stable;
    for(int i=0;i<24;++i) stable=Capture(lab,frame);
    const auto difference=Difference(stable,Capture(lab,frame));
    Check(difference<.5&&lab.PipelineStatistics().historyUsed,"Probe illumination did not settle under TAA");
    Save(stable,options.screenshot);
    if(!options.screenshot.empty()) {
        const auto save=[&](const FramePixels& image,const char* suffix) {
            auto path=std::filesystem::path(options.screenshot); path.replace_filename(path.stem().string()+suffix+path.extension().string()); Save(image,path.string());
        };
        save(sealed,"_sealed_dark"); save(leaking,"_unoccluded_sky");
        frame.closedLightingStudy=false; frame.cameraDistance=15; frame.cameraPitch=.55f;
        frame.effects.areaLights.count=1; frame.effects.cameraCut=true; Capture(lab,frame);
        frame.effects.cameraCut=false; for(int i=0;i<24;++i) stable=Capture(lab,frame);
        save(stable,"_open");
    }
    frame.effects.reflection.enabled=true; frame.effects.reflection.plane={0,1,0,0}; Capture(lab,frame);
    Check(lab.PipelineStatistics().reflectionPasses==1,"World probe reflected view missing");
    frame.waitForProbeBake=false;
    const auto beforeAsync=lab.PreparedDiffuseProbes();
    Check(lab.SetMaterialParameter(20,"emissiveIntensity",3.0f),lab.LastError());
    Check(lab.Render(frame),lab.LastError());
    Check(lab.PreparedDiffuseProbes()==beforeAsync,"Interactive edit discarded its previous probe snapshot");
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(15);
    while(lab.PreparedDiffuseProbes()==beforeAsync) {
        Check(std::chrono::steady_clock::now()<deadline,"Background probe bake did not finish");
        Check(lab.Render(frame),lab.LastError());
    }
    Check(!lab.PipelineStatistics().historyUsed,"Background probe replacement retained previous TAA history");
    frame.effects.probes.intensity=-1; Check(!lab.Render(frame),"Negative probe intensity accepted");
    std::cout<<"World probe scene passed: local sky occlusion, emissive bounce, view-independent cache, MSAA/TAA stability="
             <<difference<<"; backend errors=0\n";
}
void RealtimeGiQuality(MaterialLabModule& lab,const Options& options) {
    FrameSettings frame;ConfigureLightingStudy(frame);frame.width=640;frame.height=400;frame.present=false;
    frame.effects.realtimeGi.enabled=true;frame.effects.indirect.enabled=false;frame.effects.probes.enabled=false;
    frame.effects.post.bloomEnabled=false;frame.effects.areaLights.count=0;
    frame.effects.sky.enabled=true;frame.effects.sky.environment=Render::MakeProceduralSkyEnvironment(glm::vec3(1),glm::vec3(1),glm::vec3(1));
    frame.effects.realtimeGi.scene=Render::BuildRealtimeGiScene({});
    const auto step=[&](int count){for(int i=0;i<count;++i){Check(lab.Render(frame),lab.LastError());frame.effects.cameraCut=false;}};
    const auto read=[&]{auto cache=lab.ReadbackRealtimeGiCache();Check(cache.size()==64*26,lab.LastError());for(const auto& p:cache)for(int c=0;c<4;++c)Check(std::isfinite(p[c]),"Realtime GI generated nonfinite cache");return cache;};
    const auto meanDc=[](const auto& cache){glm::vec3 sum(0);float count=0;for(int i=0;i<64;++i)if(cache[25*64+i].x>.5){sum+=glm::vec3(cache[i]);++count;}return count>0?sum/count:sum;};
    step(1);auto cache=read();
    Check(lab.PipelineStatistics().realtimeGiUpdatedProbes==64&&lab.PipelineStatistics().realtimeGiRays==4096,"Fresh GI did not initialize every probe");
    for(int i=0;i<64;++i) {
        Check(glm::length(glm::vec3(cache[i])-glm::vec3(3.54491f))<.003f,"GPU ray/SH integration lost constant sky energy");
        Check(std::abs(cache[9*64+i].x-30)<.001f&&std::abs(cache[9*64+i].y-900)<.01f,"Empty-scene distance moments are wrong");
    }
    frame.effects.sky.intensity=2;step(16);const auto skyResponse=meanDc(read()).r;
    std::cout<<"Realtime sky 1->2 response after 16 frames: DC="<<skyResponse<<" (target 7.08982)\n";
    Check(skyResponse>7.08982f*.93f,"Sky intensity did not reach 93% of the new target within 16 frames");
    frame.cameraYaw=.5f;step(1);Check(lab.PipelineStatistics().realtimeGiUpdatedProbes==16,"Camera motion reset world-space GI");
    frame.effects.realtimeGi.scene.reset();frame.closedLightingStudy=true;frame.cameraYaw=.25f;frame.cameraDistance=4;
    frame.effects.sky.intensity=1;Check(lab.SetMaterialParameter(20,"emissiveIntensity",0.0f),lab.LastError());step(8);
    auto sealed=Capture(lab,frame);cache=read();
    Check(glm::length(meanDc(cache))<.0001f,"GPU closed room leaked sky radiance");
    for(int i=0;i<64;++i)Check(std::abs(cache[i].w)<.0001f,"GPU closed room leaked directional sky visibility");
    const auto geometry=lab.PreparedRealtimeGiScene();Check(bool(geometry)&&!lab.PreparedDiffuseProbes(),"Realtime mode performed CPU light baking");
    frame.effects.sky.enabled=false;frame.effects.areaLights.count=1;frame.effects.areaLights.lights[0].radiance={20,0,0};step(32);
    const auto red=meanDc(read());Check(red.r>.01f&&red.r>red.b*10,"Area-lit diffuse bounce missing from world cache");
    frame.effects.realtimeGi.bounceFeedback=0;frame.effects.realtimeGi.reset=true;step(1);frame.effects.realtimeGi.reset=false;step(16);
    const auto single=meanDc(read());
    std::cout<<"Realtime multiple/single bounce DC ratio="<<red.r/std::max(single.r,.0001f)<<'\n';
    Check(red.r>single.r*1.02f,"Previous-field feedback did not add higher-order diffuse bounce");
    frame.effects.realtimeGi.bounceFeedback=.9f;step(16);
    const auto withGi=Capture(lab,frame);frame.effects.realtimeGi.intensity=0;const auto withoutGi=Capture(lab,frame);
    Check(Difference(withGi,withoutGi)>.01,"Material shader did not consume realtime GI");frame.effects.realtimeGi.intensity=1;
    frame.effects.areaLights.lights[0].center.x-=1.2f;frame.effects.areaLights.lights[0].radiance={0,0,20};step(64);
    const auto blue=meanDc(read());Check(blue.b>.01f&&blue.b>blue.r*5,"Moving/recolored area light failed to relight realtime GI");
    Check(lab.PreparedRealtimeGiScene()==geometry,"Changing lights rebuilt CPU geometry");
    const auto lit=Capture(lab,frame);Save(lit,options.screenshot);
    frame.effects.areaLights.count=0;step(1);Check(glm::length(meanDc(read()))==0,"Source-free room retained old light feedback");
    frame.effects.realtimeGi.probesPerFrame=0;Check(!lab.Render(frame),"Invalid realtime update budget accepted");frame.effects.realtimeGi.probesPerFrame=16;
    Check(lab.ResetMaterial(20),lab.LastError());step(16);Check(lab.PreparedRealtimeGiScene()!=geometry&&meanDc(read()).r>.01,"Emission edit did not update realtime scene/radiance");
    frame.closedLightingStudy=false;frame.cameraDistance=15;frame.cameraPitch=.55f;frame.effects.sky=LoadSky(options);
    frame.effects.temporal.msaaSamples=4;frame.effects.temporal.antialiasing=Render::AntialiasingMode::TAA;frame.effects.post.bloomEnabled=true;
    step(48);const auto stable=Capture(lab,frame);const auto delta=Difference(stable,Capture(lab,frame));Check(delta<2&&lab.PipelineStatistics().historyUsed,"Realtime GI/TAA did not settle");
    if(!options.screenshot.empty()){auto path=std::filesystem::path(options.screenshot);path.replace_filename(path.stem().string()+"_open"+path.extension().string());Save(stable,path.string());}
    frame.effects.reflection.enabled=true;frame.effects.reflection.plane={0,1,0,0};Capture(lab,frame);
    Check(lab.PipelineStatistics().reflectionPasses==1&&lab.PipelineStatistics().realtimeGiPasses==2,"Mirrored view lost realtime GI");
    std::vector<Render::ProbeTriangle> divided;
    const auto quad=[&](glm::vec3 a,glm::vec3 b,glm::vec3 c,glm::vec3 d,glm::vec3 N,glm::vec3 emission=glm::vec3(0)) {
        if(glm::dot(glm::cross(b-a,c-a),N)<0)std::swap(b,d);
        divided.push_back({a,b,c,glm::vec3(.3f),emission});divided.push_back({a,c,d,glm::vec3(.3f),emission});
    };
    quad({-3,0,-3},{-3,0,3},{-3,4,3},{-3,4,-3},{1,0,0},{5,0,0});
    quad({3,0,-3},{3,0,3},{3,4,3},{3,4,-3},{-1,0,0});
    quad({-3,0,-3},{3,0,-3},{3,0,3},{-3,0,3},{0,1,0});
    quad({-3,4,-3},{3,4,-3},{3,4,3},{-3,4,3},{0,-1,0});
    quad({-3,0,-3},{3,0,-3},{3,4,-3},{-3,4,-3},{0,0,1});
    quad({-3,0,3},{3,0,3},{3,4,3},{-3,4,3},{0,0,-1});
    quad({0,0,-3},{0,0,3},{0,4,3},{0,4,-3},{1,0,0});
    frame.effects.sky.enabled=false;frame.effects.realtimeGi.scene=Render::BuildRealtimeGiScene(divided);step(32);cache=read();
    float left=0,right=0;for(int i=0;i<64;++i)(i%4<2?left:right)+=std::max(0.0f,cache[i].r);
    std::cout<<"Realtime thin-wall dark/lit DC ratio="<<right/std::max(left,.0001f)<<'\n';
    Check(left>.1f&&right<left*.02f,"Realtime transport leaked through the thin opaque divider");
    std::cout<<"Realtime GI passed: GPU sky/moments, closed sky occlusion, moving/recolored light, emission, source-off feedback, camera-independent cache and MSAA/TAA delta="<<delta<<"; backend errors=0\n";
}
void RealtimeGiStability(MaterialLabModule& lab,const Options& options) {
    FrameSettings frame;ConfigureLightingStudy(frame);frame.width=640;frame.height=400;frame.present=false;
    if(options.studySizeSet){frame.width=options.benchmarkWidth;frame.height=options.benchmarkHeight;}
    frame.effects.realtimeGi.enabled=!options.lumenGi;frame.effects.lumenGi.enabled=options.lumenGi;frame.effects.sky=LoadSky(options);
    frame.effects.indirect.enabled=false;frame.effects.post.bloomEnabled=false;frame.effects.post.ssaoEnabled=false;
    frame.effects.temporal.antialiasing=Render::AntialiasingMode::None;frame.effects.temporal.msaaSamples=1;
    const auto step=[&](int count){for(int i=0;i<count;++i){Check(lab.Render(frame),lab.LastError());frame.effects.cameraCut=false;}};
    struct Moments {
        double sum=0,square=0,lo=1e30,hi=-1e30;int count=0;
        void Add(double v){sum+=v;square+=v*v;lo=std::min(lo,v);hi=std::max(hi,v);++count;}
        double Mean() const{return sum/count;}
        double Sigma() const{return std::sqrt(std::max(0.0,square/count-Mean()*Mean()));}
    };
    // Sample flat patches high on the back wall, away from silhouettes and
    // flowing/transmissive materials. Coordinates match the Lab camera.
    const auto patches=[&] {
        const glm::vec3 target{0,1,-.6f};const float d=frame.cameraDistance*.65f,p=frame.cameraPitch,yaw=frame.cameraYaw;
        const auto eye=target+d*glm::vec3(std::sin(yaw)*std::cos(p),std::sin(p),std::cos(yaw)*std::cos(p));
        const auto vp=glm::perspective(glm::radians(42.0f),float(frame.width)/frame.height,.1f,100.0f)*glm::lookAt(eye,target,glm::vec3(0,1,0));
        std::vector<glm::ivec2> positions;
        for(float y:{2.8f,3.4f})for(float x:{-2.2f,-1.3f,-.4f,.5f,1.4f,2.3f}) {
            const auto q=vp*glm::vec4(x,y,-3,1);const auto uv=glm::vec2(q)/q.w*.5f+.5f;
            glm::ivec2 at=glm::ivec2(uv*glm::vec2(frame.width,frame.height));
            if(at.x>2&&at.y>2&&at.x<int(frame.width)-3&&at.y<int(frame.height)-3)positions.push_back(at);
        }
        Check(!positions.empty(),"Wall stability patches are outside the camera");return positions;
    };
    const auto measure=[&](const char* label) {
        const auto positions=patches();
        step(384);std::array<Moments,64> dc;std::vector<Moments> wall(positions.size());
        std::array<float,64> validity{};int flips=0;FramePixels pixels;
        std::vector<glm::vec4> geometryCache;float geometryDelta=0;
        for(int n=0;n<96;++n) {
            step(1);const auto cache=lab.ReadbackRealtimeGiCache();Check(cache.size()==64*26,lab.LastError());
            for(const auto& p:cache)for(int c=0;c<4;++c)Check(std::isfinite(p[c]),"Nonfinite static GI cache");
            if(n==0)geometryCache=cache;
            for(int row=0;row<25;++row)for(int i=0;i<64;++i) {
                const auto a=cache[row*64+i],b=geometryCache[row*64+i];
                geometryDelta=std::max(geometryDelta,row<9?std::abs(a.a-b.a):glm::length(glm::vec2(a)-glm::vec2(b)));
            }
            for(int i=0;i<64;++i) {
                const auto c=cache[i];dc[i].Add(c.r*.2126+c.g*.7152+c.b*.0722);
                const float valid=cache[25*64+i].x;if(n&&validity[i]!=valid)++flips;validity[i]=valid;
            }
            pixels=lab.Readback();Check(!pixels.rgba.empty(),lab.LastError());
            for(std::size_t k=0;k<positions.size();++k) {
                double luma=0;for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x) {
                    const auto at=(std::size_t(positions[k].y+y)*frame.width+positions[k].x+x)*4;
                    luma+=pixels.rgba[at]*.2126+pixels.rgba[at+1]*.7152+pixels.rgba[at+2]*.0722;
                }
                wall[k].Add(luma/9);
            }
        }
        double averageCv=0,peakCv=0,averageSigma=0,peakSigma=0,peakRange=0,wallMean=0;
        for(const auto& m:dc){const double cv=m.Sigma()/std::max(m.Mean(),.02);averageCv+=cv/64;peakCv=std::max(peakCv,cv);}
        for(const auto& m:wall){averageSigma+=m.Sigma()/wall.size();peakSigma=std::max(peakSigma,m.Sigma());peakRange=std::max(peakRange,m.hi-m.lo);wallMean+=m.Mean()/wall.size();}
        std::cout<<(options.lumenGi?"Hybrid GI stability ":"Realtime GI stability ")<<label<<": 96 frames, probe DC mean/peak CV="<<averageCv*100<<"%/"<<peakCv*100
                 <<"%, wall mean="<<wallMean<<", sigma mean/peak="<<averageSigma<<'/'<<peakSigma<<", peak range="<<peakRange<<" /255, visibility delta="<<geometryDelta<<", validity flips="<<flips<<'\n';
        if(!options.screenshot.empty()) {auto path=std::filesystem::path(options.screenshot);path.replace_filename(path.stem().string()+"_"+label+path.extension().string());Save(pixels,path.string());}
        Check(wallMean>1,"Stability measurement used an unlit wall");
        Check(peakCv<.001,"Static GI radiance kept oscillating after convergence");
        Check(averageSigma<.03&&peakSigma<.15,"Flat wall lighting still fluctuates over continuous frames");
        Check(geometryDelta==0&&flips==0,"Static GI visibility/classification changed between lighting updates");
    };
    measure("open_raw");
    const auto geometry=lab.ReadbackRealtimeGiCache();
    frame.effects.sky.intensity=2;step(16);const auto relit=lab.ReadbackRealtimeGiCache();
    for(int row=0;row<25;++row)for(int i=0;i<64;++i) {
        const auto a=geometry[row*64+i],b=relit[row*64+i];
        Check(row<9?a.a==b.a:glm::vec2(a)==glm::vec2(b),"Relighting changed static GI visibility");
    }
    for(int i=0;i<64;++i)Check(geometry[25*64+i].x==relit[25*64+i].x,"Relighting changed probe validity");
    frame.effects.sky.intensity=1;
    frame.closedLightingStudy=true;frame.cameraDistance=4;frame.cameraPitch=-.1f;frame.effects.cameraCut=true;measure("closed_raw");
    frame.closedLightingStudy=false;frame.cameraDistance=15;frame.cameraPitch=.28f;frame.effects.cameraCut=true;
    frame.effects.temporal.antialiasing=Render::AntialiasingMode::TAA;frame.effects.temporal.msaaSamples=4;
    frame.effects.post.bloomEnabled=true;frame.effects.post.ssaoEnabled=true;measure("open_taa");
    std::cout<<(options.lumenGi?"Hybrid GI":"Realtime GI")<<" continuous-frame stability passed; backend errors=0\n";
}
void LumenViewStability(MaterialLabModule& lab,const Options& options) {
    FrameSettings frame;ConfigureLightingStudy(frame);ApplyAdvancedCamera(frame,options);
    frame.width=options.studySizeSet?options.benchmarkWidth:960;frame.height=options.studySizeSet?options.benchmarkHeight:600;
    frame.present=false;frame.effects.lumenGi.enabled=true;frame.effects.indirect.enabled=false;frame.effects.sky=LoadSky(options);
    frame.effects.lumenGi.reflectionResolutionScale=options.lumenReflectionScale;
    frame.effects.lumenGi.reflectionSourceResolutionScale=options.lumenSourceScale;
    frame.effects.lumenGi.reflectionTraceMaxDimension=options.lumenReflectionDetail;
    frame.effects.temporal.antialiasing=Render::AntialiasingMode::TAA;frame.effects.temporal.msaaSamples=options.msaaSamples;
    frame.effects.post.bloomEnabled=true;frame.effects.post.ssaoEnabled=true;
    const float yaw=frame.cameraYaw,pitch=frame.cameraPitch;
    const auto render=[&]{Check(lab.Render(frame),lab.LastError());frame.effects.cameraCut=false;};
    for(int n=0;n<192;++n)render();
    const auto measure=[&](const char* label,bool moving) {
        constexpr std::array<glm::vec3,6> points{{{.6f,2.4f,-3},{1.4f,2.4f,-3},{2.3f,2.4f,-3},{.6f,3.2f,-3},{1.4f,3.2f,-3},{2.3f,3.2f,-3}}};
        std::array<double,6> sum{},square{},previous{};double maxStep=0;
        FramePixels pixels;
        for(int n=0;n<64;++n) {
            frame.cameraYaw=yaw+(moving?.002f*std::sin(n*.3926990817f):0);
            frame.cameraPitch=pitch+(moving?.001f*std::cos(n*.3926990817f):0);
            render();pixels=lab.Readback();Check(!pixels.rgba.empty(),lab.LastError());
            Check(!frame.effects.lumenGi.enabled||lab.PipelineStatistics().lumenHistoryUsed,"Converged view discarded GI history");
            const glm::vec3 target{0,1,-.6f};const float d=frame.cameraDistance*.65f;
            const auto eye=target+d*glm::vec3(std::sin(frame.cameraYaw)*std::cos(frame.cameraPitch),std::sin(frame.cameraPitch),std::cos(frame.cameraYaw)*std::cos(frame.cameraPitch));
            const auto vp=glm::perspective(glm::radians(42.f),float(frame.width)/frame.height,.1f,100.f)*glm::lookAt(eye,target,glm::vec3(0,1,0));
            for(unsigned k=0;k<points.size();++k) {
                auto q=vp*glm::vec4(points[k],1);glm::ivec2 at=glm::ivec2((glm::vec2(q)/q.w*.5f+.5f)*glm::vec2(frame.width,frame.height));
                Check(at.x>4&&at.y>4&&at.x<int(frame.width)-4&&at.y<int(frame.height)-4,"Wall patch outside stability view");
                double luma=0;
                for(int y=-3;y<=3;++y)for(int x=-3;x<=3;++x){const auto pixel=(std::size_t(at.y+y)*frame.width+at.x+x)*4;luma+=(pixels.rgba[pixel]*.2126+pixels.rgba[pixel+1]*.7152+pixels.rgba[pixel+2]*.0722)/49;}
                Check(luma>1,"Wall stability patch is black/outside the actual framebuffer");
                sum[k]+=luma;square[k]+=luma*luma;if(n)maxStep=std::max(maxStep,std::abs(luma-previous[k]));previous[k]=luma;
            }
        }
        double sigma=0;for(unsigned k=0;k<points.size();++k)sigma=std::max(sigma,std::sqrt(std::max(0.0,square[k]/64-std::pow(sum[k]/64,2))));
        std::cout<<"Hybrid view stability "<<label<<' '<<frame.width<<'x'<<frame.height<<" wall peak sigma="<<sigma<<", maximum adjacent step="<<maxStep<<" /255\n";
        if(!options.screenshot.empty()){auto path=std::filesystem::path(options.screenshot);path.replace_filename(path.stem().string()+"_"+label+path.extension().string());Save(pixels,path.string());}
        return std::pair{sigma,maxStep};
    };
    const auto fullStatic=measure("full_static",false),fullMotion=measure("full_micro_orbit",true);
    frame.cameraYaw=yaw;frame.cameraPitch=pitch;frame.effects.lumenGi.reflections=false;frame.effects.cameraCut=true;
    for(int n=0;n<64;++n)render();
    const auto diffuseStatic=measure("diffuse_static",false),diffuseMotion=measure("diffuse_micro_orbit",true);
    frame.cameraYaw=yaw;frame.cameraPitch=pitch;frame.effects.post.ssaoEnabled=false;frame.effects.post.bloomEnabled=false;frame.effects.cameraCut=true;
    for(int n=0;n<32;++n)render();const auto bareDiffuse=measure("diffuse_no_ao_bloom",false);
    frame.effects.lumenGi.screenTraces=false;frame.effects.cameraCut=true;for(int n=0;n<32;++n)render();const auto worldOnly=measure("world_only",false);
    frame.effects.temporal.antialiasing=Render::AntialiasingMode::None;frame.effects.cameraCut=true;for(int n=0;n<32;++n)render();const auto rawWorld=measure("world_no_taa",false);
    frame.effects.lumenGi.enabled=false;frame.effects.cameraCut=true;for(int n=0;n<32;++n)render();const auto sky=measure("sky_only",false);
    Check(fullStatic.first<.15&&diffuseStatic.first<.1,"Static full-resolution wall still flickers");
    Check(fullMotion.second<.5&&diffuseMotion.second<.35,"Slow camera motion causes wall lighting discontinuities");
    Check(bareDiffuse.first<.1&&worldOnly.first<.1&&rawWorld.first<.1&&sky.first<.03,"Isolated GI/sky path remained unstable");
    std::cout<<"Hybrid full-resolution view stability passed; backend errors=0\n";
}
void LumenGiQuality(MaterialLabModule& lab,const Options& options) {
    FrameSettings frame;ConfigureLightingStudy(frame);frame.width=640;frame.height=400;frame.present=false;
    frame.effects.lumenGi.enabled=true;frame.effects.indirect.enabled=false;
    frame.effects.post.bloomEnabled=false;frame.effects.post.ssaoEnabled=false;frame.effects.areaLights.count=0;
    frame.effects.sky=LoadSky(options);frame.effects.temporal.antialiasing=Render::AntialiasingMode::TAA;
    frame.closedLightingStudy=true;frame.cameraDistance=4;
    Check(lab.SetMaterialParameter(20,"emissiveIntensity",0.0f),lab.LastError());
    const auto step=[&](int count){for(int i=0;i<count;++i){Check(lab.Render(frame),lab.LastError());frame.effects.cameraCut=false;}};
    const auto read=[&]{auto cache=lab.ReadbackLumenSurfaceCache();Check(!cache.empty(),lab.LastError());for(const auto& v:cache)for(int c=0;c<4;++c)Check(std::isfinite(v[c]),"Nonfinite Surface Cache");return cache;};
    const auto mean=[](const auto& cache){glm::vec3 sum(0);float n=0;for(const auto& v:cache)if(v.a>.5){sum+=glm::vec3(v);++n;}return n?sum/n:sum;};
    step(16);auto cache=read();const auto sealedMean=mean(cache);float sealedPeak=0;int peakIndex=0;
    for(int i=0;i<int(cache.size());++i)if(cache[i].r>sealedPeak){sealedPeak=cache[i].r;peakIndex=i;}
    const auto asset=lab.PreparedLumenGiScene();const auto attr=asset->Attributes()[asset->Geometry()->TriangleCount()+peakIndex*2];
    std::cout<<"Hybrid sealed cache mean="<<sealedMean.r<<','<<sealedMean.g<<','<<sealedMean.b<<" peak="<<sealedPeak<<" at="<<attr.x<<','<<attr.y<<','<<attr.z<<" tri="<<attr.w<<'\n';
    const auto sealed=lab.Readback();double sealedImage=0;for(std::size_t i=0;i<sealed.rgba.size();i+=4)sealedImage+=sealed.rgba[i]+sealed.rgba[i+1]+sealed.rgba[i+2];
    sealedImage/=sealed.width*sealed.height*3.0;
    // Hidden box bottoms are outside the floor and legitimately see the sky.
    // They must not light any interior probe or visible receiver.
    const auto sealedProbes=lab.ReadbackRealtimeGiCache();float interiorPeak=0;
    for(int i=0;i<64;++i)if(sealedProbes[25*64+i].x>.5)interiorPeak=std::max(interiorPeak,glm::length(glm::vec3(sealedProbes[i])));
    std::cout<<"Hybrid sealed visible mean="<<sealedImage<<" interior probe peak="<<interiorPeak<<'\n';
    Check(sealedImage<.5&&interiorPeak<.001f,"Hybrid GI leaked sky into closed room receivers");
    auto stats=lab.PipelineStatistics();Check(stats.lumenGiPasses>10&&stats.lumenSurfaceTexels>0&&stats.lumenScreenProbes==1000&&stats.indirectPasses==0,"Missing hybrid surface/final gather passes");
    const auto geometry=lab.PreparedLumenGiScene();Check(geometry&&!lab.PreparedDiffuseProbes(),"Hybrid mode baked CPU radiance");
    frame.effects.sky.enabled=false;frame.effects.areaLights.count=1;frame.effects.areaLights.lights[0].radiance={20,0,0};step(128);
    cache=read();const auto red=mean(cache);glm::vec3 peak(0);for(const auto& p:cache)peak=glm::max(peak,glm::vec3(p));
    const auto direct=mean(lab.ReadbackLumenSurfaceCache(true));int validTexels=0;for(const auto& p:cache)validTexels+=p.a>.5;
    std::cout<<"Hybrid red cache mean="<<red.r<<','<<red.g<<','<<red.b<<" direct="<<direct.r<<','<<direct.g<<','<<direct.b<<" peak="<<peak.r<<" valid="<<validTexels<<" surfels="<<lab.PipelineStatistics().lumenSurfaceTexels<<" updates="<<lab.PipelineStatistics().lumenUpdatedSurfels<<'\n';
    Save(lab.Readback(),options.screenshot);Check(red.r>.001f&&red.r>red.b*20,"Surface Cache lost red area-light bounce");
    frame.effects.lumenGi.bounceFeedback=0;frame.effects.lumenGi.reset=true;step(1);frame.effects.lumenGi.reset=false;step(96);
    const auto single=mean(read());std::cout<<"Hybrid Surface Cache multiple/single DC ratio="<<red.r/std::max(single.r,.00001f)<<'\n';
    Check(red.r>single.r*1.02f,"Surface Cache did not propagate multiple diffuse bounces");
    frame.effects.lumenGi.bounceFeedback=.9f;step(192);const auto withGi=Capture(lab,frame);
    frame.effects.lumenGi.intensity=0;const auto withoutGi=Capture(lab,frame);
    Check(Difference(withGi,withoutGi)>.05,"Main material did not consume hybrid final gather");frame.effects.lumenGi.intensity=1;
    frame.effects.lumenGi.screenTraces=false;step(32);const auto worldOnly=Capture(lab,frame);
    Check(lab.PreparedLumenGiScene()==geometry&&lab.PipelineStatistics().lumenHistoryUsed,"Screen tracing rebuilt world cache or failed history");
    if(!options.screenshot.empty()){auto path=std::filesystem::path(options.screenshot);path.replace_filename(path.stem().string()+"_world"+path.extension().string());Save(worldOnly,path.string());}
    frame.cameraYaw-=.5f;step(32);Check(lab.PreparedLumenGiScene()==geometry,"Offscreen receivers rebuilt Surface Cache");
    frame.effects.areaLights.lights[0].center.x-=1;frame.effects.areaLights.lights[0].radiance={0,0,20};step(1);
    const auto firstBlueDirect=mean(lab.ReadbackLumenSurfaceCache(true));
    Check(firstBlueDirect.b>firstBlueDirect.r*5&&firstBlueDirect.b>.001f,"Direct relighting left old red light in inactive Surface Cache texels");
    step(127);
    Check(glm::length(firstBlueDirect-mean(lab.ReadbackLumenSurfaceCache(true)))<.00001f,"Direct illumination updated progressively instead of within one frame");
    const auto blue=mean(read());Check(blue.b>blue.r*5&&blue.b>.001f,"Hybrid GI failed moving/recolored light");
    Check(lab.PreparedLumenGiScene()==geometry,"Light changes rebuilt Surface Cache geometry");
    frame.effects.areaLights.count=0;step(1);Check(glm::length(mean(read()))==0,"Source-free hybrid scene retained old radiance");
    Check(lab.ResetMaterial(20),lab.LastError());step(192);Check(lab.PreparedLumenGiScene()!=geometry&&mean(read()).r>.001f,"Hybrid emission edit did not relight cache");
    frame.closedLightingStudy=false;frame.cameraYaw=.25f;frame.cameraDistance=15;frame.cameraPitch=.55f;
    frame.effects.sky=LoadSky(options);frame.effects.areaLights.count=1;frame.effects.areaLights.lights[0].radiance={4,3.8f,3.5f};
    frame.effects.lumenGi.screenTraces=true;frame.effects.temporal.msaaSamples=4;frame.effects.post.bloomEnabled=true;frame.effects.post.ssaoEnabled=true;
    frame.effects.cameraCut=true;step(192);const auto stable=Capture(lab,frame);double maxDelta=0;
    auto previous=stable;for(int i=0;i<24;++i){auto now=Capture(lab,frame);maxDelta=std::max(maxDelta,Difference(previous,now));previous=std::move(now);}
    Check(maxDelta<1&&lab.PipelineStatistics().lumenHistoryUsed,"Hybrid GI/TAA stayed unstable");Save(stable,options.screenshot);
    frame.effects.lumenGi.reflections=false;step(8);const auto noReflection=Capture(lab,frame);
    const auto reflectionDelta=Difference(stable,noReflection);Check(reflectionDelta>.005,"Hybrid scene reflection did not affect shading");
    frame.effects.lumenGi.enabled=false;frame.effects.realtimeGi.enabled=true;step(16);
    Check(lab.PipelineStatistics().lumenGiPasses==0&&lab.PipelineStatistics().realtimeGiPasses==2,"Preserved DDGI could not be selected");
    frame.effects.realtimeGi.enabled=false;frame.effects.lumenGi.enabled=true;frame.effects.lumenGi.surfaceUpdatesPerFrame=0;
    Check(!lab.Render(frame),"Hybrid GI accepted zero update budget");
    std::cout<<"Hybrid GI passed: Surface Cache, multiple bounces, local sky, screen/world tracing, offscreen cache, moving light/emission, AA max delta="<<maxDelta<<", reflection delta="<<reflectionDelta<<"; DDGI preserved; backend errors=0\n";
}
void Benchmark(MaterialLabModule& lab, const Options& options) {
    FrameSettings frame; ConfigureLightingStudy(frame); ApplyAdvancedCamera(frame, options);
    if(options.sky) frame.effects.sky=LoadSky(options);
    frame.effects.probes.enabled=options.probes; frame.waitForProbeBake=true;
    frame.effects.realtimeGi.enabled=options.realtimeGi;
    frame.effects.lumenGi.enabled=options.lumenGi;
    frame.effects.lumenGi.reflectionResolutionScale=options.lumenReflectionScale;
    frame.effects.lumenGi.reflectionSourceResolutionScale=options.lumenSourceScale;
    frame.effects.lumenGi.reflectionTraceMaxDimension=options.lumenReflectionDetail;
    frame.width = options.benchmarkWidth; frame.height = options.benchmarkHeight; frame.present = false;
    frame.effects.temporal.antialiasing = Render::AntialiasingMode::TAA;
    frame.effects.temporal.msaaSamples = options.msaaSamples;
    frame.effects.post.bloomEnabled = true;
    const auto measure = [&](const char* label) {
        frame.effects.cameraCut = true; Check(lab.Render(frame), lab.LastError());
        frame.effects.cameraCut = false;
        for (int i = 0; i < 64; ++i) Check(lab.Render(frame), lab.LastError());
        const auto timing = lab.MeasureFrames(frame);
        Check(timing.gpuMilliseconds > 0, lab.LastError());
        std::cout << std::fixed << std::setprecision(3) << "Benchmark " << label
                  << ' ' << frame.width << 'x' << frame.height << " GPU interval=" << timing.gpuMilliseconds
                  << " ms/frame, elapsed=" << timing.elapsedMilliseconds << " ms/frame\n";
        return timing;
    };
    FrameTiming withoutSky;
    if(options.sky&&!options.probes&&!options.realtimeGi&&!options.lumenGi) { frame.effects.sky.enabled=false; withoutSky=measure("sky disabled control"); frame.effects.sky.enabled=true; }
    const auto full=measure("full");
    const auto balanced=lab.Readback();
    Save(balanced, options.screenshot);
    if(options.benchmarkPresent) {
        auto shown=frame;shown.present=true;
        for(int i=0;i<8;++i)Check(lab.Render(shown),lab.LastError());
        FrameTiming timing;
        for(int batch=0;batch<3;++batch){shown.lightTime=batch*120.f/90;const auto sample=lab.MeasureFrames(shown,120,1.f/90);timing.gpuMilliseconds+=sample.gpuMilliseconds/3;timing.elapsedMilliseconds+=sample.elapsedMilliseconds/3;}
        std::cout<<"Benchmark with presentation (360 animated frames) "<<frame.width<<'x'<<frame.height<<" GPU="<<timing.gpuMilliseconds
                 <<" ms/frame, elapsed="<<timing.elapsedMilliseconds<<" ms/frame, FPS="<<1000/timing.elapsedMilliseconds<<'\n';
    }
    if(options.sky&&!options.probes&&!options.realtimeGi&&!options.lumenGi) std::cout << "Sky incremental GPU interval=" << full.gpuMilliseconds-withoutSky.gpuMilliseconds << " ms/frame\n";
    if(options.lumenGi) {
        const auto stats=lab.PipelineStatistics();const auto geometry=lab.PreparedLumenGiScene();
        Check(geometry&&stats.lumenGiPasses>0&&stats.indirectPasses==0,"Missing hybrid GI");
        std::cout<<"Hybrid GI: surfels="<<stats.lumenSurfaceTexels<<", updates/frame="<<stats.lumenUpdatedSurfels<<", screen probes="<<stats.lumenScreenProbes<<'\n';
        if(options.lumenReflectionDetail<320) {
            frame.effects.lumenGi.reflectionTraceMaxDimension=320;measure("320 reflection detail reference");
            const auto reference=lab.Readback();const auto delta=Difference(balanced,reference);
            std::cout<<"Balanced/320 reflection reference LDR mean difference="<<delta<<" /255\n";
            Check(delta<1,"Balanced reflection grid differs substantially from finer reference");
            frame.effects.lumenGi.reflectionTraceMaxDimension=options.lumenReflectionDetail;
        }
        frame.effects.lumenGi.screenTraces=false;measure("hybrid world tracing only");
        frame.effects.lumenGi.screenTraces=true;frame.effects.lumenGi.reflections=false;measure("hybrid diffuse only");
        frame.effects.lumenGi.enabled=false;frame.effects.realtimeGi.enabled=true;measure("preserved DDGI reference");
        frame.effects.realtimeGi.enabled=false;frame.effects.lumenGi.enabled=true;frame.effects.lumenGi.reflections=true;measure("hybrid restored");
        std::cout<<"Hybrid GI benchmark passed; no present/readback inside batches\n";return;
    }
    if(options.realtimeGi) {
        const auto stats=lab.PipelineStatistics();const auto geometry=lab.PreparedRealtimeGiScene();
        Check(geometry&&stats.realtimeGiPasses==2&&stats.indirectPasses==0,"Missing realtime GI or redundant screen trace");
        std::cout<<"Realtime GPU GI: triangles="<<geometry->TriangleCount()<<", probes/frame="<<stats.realtimeGiUpdatedProbes<<", rays/frame="<<stats.realtimeGiRays<<"; no CPU radiance bake\n";
        frame.effects.realtimeGi.enabled=false;measure("screen GI reference");frame.effects.realtimeGi.enabled=true;measure("realtime GI restored");
        frame.effects.realtimeGi.probesPerFrame=64;measure("realtime GI full update");
        std::cout<<"Realtime GI benchmark passed; no present/readback inside batches\n";return;
    }
    if(options.probes) {
        const auto volume=lab.PreparedDiffuseProbes();
        Check(volume&&lab.PipelineStatistics().validDiffuseProbes>0&&lab.PipelineStatistics().indirectPasses==0,"Probe preset traced redundant screen GI");
        std::cout << "World probes: valid=" << volume->ValidProbes() << " bake=" << volume->BakeMilliseconds() << " ms (excluded from frame timing)\n";
        frame.effects.probes.enabled=false; measure("screen GI reference");
        frame.effects.probes.enabled=true; measure("probes restored");
        std::cout << "Probe benchmark passed; no present/readback inside batches\n"; return;
    }
    frame.effects.indirect.sampleCount=24; measure("quality 24-rays");
    const auto reference=lab.Readback();
    const auto difference=Difference(balanced,reference);
    std::cout << "Balanced/24-ray reference LDR mean difference=" << difference << " /255\n";
    Check(difference<1.0,"Balanced GI differs substantially from the quality reference");
    if(!options.screenshot.empty()) {
        auto path=std::filesystem::path(options.screenshot);
        path.replace_filename(path.stem().string()+"_reference"+path.extension().string());
        Save(reference,path.string());
    }
    frame.effects.indirect.sampleCount=12;
    frame.effects.indirect.enabled = false; measure("no GI");
    frame.effects.indirect.enabled = true; frame.effects.areaLights.count = 0; measure("no area light");
    frame.effects.indirect.enabled = false; measure("neither GI nor area light");
    std::cout << "Benchmark passed; GPU intervals include CPU submission idle gaps; no present/readback inside batches\n";
}
#include "showcase_demo.inl"
#include "gi_room_demo.inl"
void Interactive(MaterialLabModule& lab, ApplicationWindow::Window& window, const Options& options) {
    std::cout << "MaterialLab: columns roughness 0.08 -> 1, rows metallic 0 -> 1.\n"
                 "Bottom: copper / five maps / migrated legacy tile.\n"
                 "Tab/Shift+Tab select | F isolate | arrows roughness/metallic | N normal | T property maps\n"
                 "R reset | PageUp/Down exposure | Space animate lights | drag orbit | wheel zoom | Esc exit\n";
    std::cout << "B Bloom | C CSM | M mirror | O SSAO | P cycle filters | L tone map | V vignette\n";
    std::cout << "A None/FXAA/TAA | X MSAA off/4x | U camera motion blur | H mirror 0.5x/1x/2x\n";
    std::cout << "G shadow study | J shadow visibility/cascades | K shadow atlas 2048/4096/8192\n";
    std::cout << "Q transmission/lighting study | I diffuse bounce | E rectangle area light\n";
    std::cout << "S sky light | D sky background | [/] sky intensity | ,/. sky rotation\n";
    std::cout << "W realtime GPU GI | Y baked diffuse probes | Z enclosed lighting study\n";
    std::cout << "2 hybrid Lumen GI | 3 hybrid screen tracing | 4 hybrid scene reflections | 5 courtyard/gallery showcase\n";
    FrameSettings frame;
    frame.effects.sky=LoadSky(options);
    frame.effects.lumenGi.reflectionResolutionScale=options.lumenReflectionScale;
    frame.effects.lumenGi.reflectionSourceResolutionScale=options.lumenSourceScale;
    frame.effects.lumenGi.reflectionTraceMaxDimension=options.lumenReflectionDetail;
    frame.effects.temporal.antialiasing = Render::AntialiasingMode::TAA;
    frame.effects.temporal.msaaSamples = options.msaaSamples;
    std::size_t selected = 7;
    if(options.realtimeGi){ConfigureLightingStudy(frame);frame.effects.realtimeGi.enabled=true;selected=18;}
    if(options.lumenGi){ConfigureLightingStudy(frame);frame.effects.lumenGi.enabled=true;selected=18;}
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
    double smoothFrameMs=1000.0/(options.fpsLimit>0?options.fpsLimit:90);
    FramePacer pacer;
    while (!window.ShouldClose()) {
        const auto frameStarted=std::chrono::steady_clock::now();
        window.PollEvents();
        const double now = glfwGetTime();
        const float dt = static_cast<float>(std::clamp(now - lastTime, 0.0001, 0.1));
        frame.effects.deltaSeconds = static_cast<float>(std::clamp(now - lastTime, 0.0001, 1.0));
        frame.effects.cameraCut = now - lastTime > 0.25;
        smoothFrameMs=smoothFrameMs*.9+std::clamp(now-lastTime,0.0001,1.0)*1000.0*.1;
        lastTime = now;
        const auto down = [&](int key) { return glfwGetKey(native, key) == GLFW_PRESS; };
        const auto pressed = [&](int key) { const bool value = down(key); const bool edge = value && !previous[key]; previous[key] = value; return edge; };
        if (down(GLFW_KEY_ESCAPE)) break;
        if(pressed(GLFW_KEY_5)){ShowcaseInteractive(lab,window,options);break;}
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
        if (pressed(GLFW_KEY_A)) frame.effects.temporal.antialiasing = static_cast<Render::AntialiasingMode>((static_cast<unsigned>(frame.effects.temporal.antialiasing) + 1) % 3);
        if (pressed(GLFW_KEY_X)) frame.effects.temporal.msaaSamples = frame.effects.temporal.msaaSamples == 1 ? 4 : 1;
        if (pressed(GLFW_KEY_U)) frame.effects.temporal.motionBlurEnabled = !frame.effects.temporal.motionBlurEnabled;
        if (pressed(GLFW_KEY_H)) frame.effects.reflection.resolutionScale = frame.effects.reflection.resolutionScale < 1 ? 1 : frame.effects.reflection.resolutionScale == 1 ? 2 : 0.5f;
        if (pressed(GLFW_KEY_Q)) {
            if (!frame.lightingStudy) { ConfigureLightingStudy(frame); frame.effects.lumenGi.enabled=true;frame.effects.realtimeGi.enabled=false;frame.effects.probes.enabled=false; selected=18; animate=true; }
            else {
                frame.lightingStudy=false; frame.closedLightingStudy=false; frame.effects.probes.enabled=false;
                frame.effects.realtimeGi.enabled=false;
                frame.effects.lumenGi.enabled=false;
                frame.effects.indirect.enabled=false; frame.effects.areaLights.count=0;
                frame.effects.reflection.enabled=true; frame.effects.reflection.plane={0,1,0,3.25f};
            }
            frame.effects.cameraCut=true;
        }
        if (pressed(GLFW_KEY_I)) {frame.effects.lumenGi.enabled=false;frame.effects.realtimeGi.enabled=false;frame.effects.probes.enabled=false;frame.effects.indirect.enabled=!frame.effects.indirect.enabled;}
        if (pressed(GLFW_KEY_E)) frame.effects.areaLights.count=frame.effects.areaLights.count ? 0 : 1;
        if (pressed(GLFW_KEY_Y)&&frame.lightingStudy) {frame.effects.lumenGi.enabled=false;frame.effects.realtimeGi.enabled=false;frame.effects.probes.enabled=!frame.effects.probes.enabled;}
        if (pressed(GLFW_KEY_W)&&frame.lightingStudy) {frame.effects.lumenGi.enabled=false;frame.effects.probes.enabled=false;frame.effects.realtimeGi.enabled=!frame.effects.realtimeGi.enabled;}
        if (pressed(GLFW_KEY_2)&&frame.lightingStudy) {frame.effects.realtimeGi.enabled=false;frame.effects.probes.enabled=false;frame.effects.lumenGi.enabled=!frame.effects.lumenGi.enabled;frame.effects.cameraCut=true;}
        if (pressed(GLFW_KEY_3)&&frame.lightingStudy)frame.effects.lumenGi.screenTraces=!frame.effects.lumenGi.screenTraces;
        if (pressed(GLFW_KEY_4)&&frame.lightingStudy)frame.effects.lumenGi.reflections=!frame.effects.lumenGi.reflections;
        if (pressed(GLFW_KEY_Z)&&frame.lightingStudy) {
            frame.closedLightingStudy=!frame.closedLightingStudy;
            frame.cameraDistance=frame.closedLightingStudy?4:15; frame.cameraPitch=.28f;
            frame.effects.cameraCut=true;
        }
        if (pressed(GLFW_KEY_S)) frame.effects.sky.enabled=!frame.effects.sky.enabled;
        if (pressed(GLFW_KEY_D)) frame.effects.sky.background=!frame.effects.sky.background;
        frame.effects.sky.intensity=std::clamp(frame.effects.sky.intensity+
            (float(down(GLFW_KEY_RIGHT_BRACKET))-float(down(GLFW_KEY_LEFT_BRACKET)))*dt,0.0f,10.0f);
        frame.effects.sky.rotation+=(float(down(GLFW_KEY_PERIOD))-float(down(GLFW_KEY_COMMA)))*dt;
        if (pressed(GLFW_KEY_G)) {
            if (frame.lightingStudy) {
                frame.effects.indirect.enabled = false;
                frame.effects.areaLights.count = 0;
                frame.effects.reflection.enabled = true;
                frame.effects.reflection.plane = {0,1,0,3.25f};
            }
            frame.lightingStudy = false;
            frame.closedLightingStudy=false; frame.effects.probes.enabled=false;
            frame.effects.realtimeGi.enabled=false;
            frame.effects.lumenGi.enabled=false;
            frame.shadowStudy = !frame.shadowStudy;
            frame.cameraPitch = frame.shadowStudy ? 0.46f : 0.08f;
            frame.cameraYaw = frame.shadowStudy ? 0.38f : 0.0f;
            frame.effects.cameraCut = true;
        }
        if (pressed(GLFW_KEY_J)) frame.effects.shadows.debugView = static_cast<Render::ShadowDebugView>((static_cast<unsigned>(frame.effects.shadows.debugView) + 1) % 3);
        if (pressed(GLFW_KEY_K)) frame.effects.shadows.atlasResolution = frame.effects.shadows.atlasResolution == 2048 ? 4096 : frame.effects.shadows.atlasResolution == 4096 ? 8192 : 2048;
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
        frame.cameraDistance = std::clamp(frame.cameraDistance - input.scroll,frame.closedLightingStudy?3.0f:9.0f,
                                         frame.closedLightingStudy?6.0f:32.0f); input.scroll = 0;
        int width = 0, height = 0; glfwGetFramebufferSize(native, &width, &height);
        if (width <= 0 || height <= 0) { glfwWaitEventsTimeout(0.05); continue; }
        frame.width = width; frame.height = height;
        if(options.renderSizeSet){frame.width=options.benchmarkWidth;frame.height=options.benchmarkHeight;}
        frame.isolatedMaterial = focused ? static_cast<int>(selected) : -1;
        if (!saved && !options.screenshot.empty()) {
            frame.present = false; Save(Capture(lab, frame), options.screenshot); saved = true;
        }
        frame.present = true;
        Check(lab.Render(frame), lab.LastError());
        info = lab.InspectMaterial(selected);
        std::ostringstream title;
        title << (options.vulkan?"MaterialLab Vulkan [":"MaterialLab OpenGL [") << selected + 1 << "/" << lab.MaterialCount() << "] " << info.name
              << std::fixed << std::setprecision(2) << " | M=" << info.metallic << " R=" << info.roughness
              << " maps(N/M/R/A)=" << info.normalMap << info.metallicMap << info.roughnessMap << info.aoMap
              << " Exp=" << frame.exposure << " | B/C/M/O=" << frame.effects.post.bloomEnabled << frame.effects.shadows.enabled
              << frame.effects.reflection.enabled << frame.effects.post.ssaoEnabled << " P=" << static_cast<unsigned>(frame.effects.post.filter)
              << " L=" << static_cast<unsigned>(frame.effects.post.toneMap)
              << " AA=" << std::array<const char*, 3>{"None", "FXAA", "TAA"}[static_cast<unsigned>(frame.effects.temporal.antialiasing)]
              << " MSAA=" << frame.effects.temporal.msaaSamples << " Blur=" << frame.effects.temporal.motionBlurEnabled
              << " Mirror=" << frame.effects.reflection.resolutionScale << "x CSM=" << frame.effects.shadows.atlasResolution
              << " Debug=" << static_cast<unsigned>(frame.effects.shadows.debugView) << " Study=" << frame.shadowStudy << " | A X U H / G J K";
        title << " Q=" << frame.lightingStudy << " GI=" << frame.effects.indirect.enabled << " Area=" << frame.effects.areaLights.count;
        title << " Sky=" << frame.effects.sky.enabled << '/' << frame.effects.sky.intensity;
        title << " Probes=" << lab.PipelineStatistics().validDiffuseProbes << " Closed=" << frame.closedLightingStudy;
        title << " RTGI=" << frame.effects.realtimeGi.enabled << " rays=" << lab.PipelineStatistics().realtimeGiRays;
        title << " Hybrid="<<frame.effects.lumenGi.enabled<<" Surf="<<lab.PipelineStatistics().lumenUpdatedSurfels<<" S/R="<<frame.effects.lumenGi.screenTraces<<frame.effects.lumenGi.reflections;
        title << " FPS=" << std::setprecision(1) << 1000.0/smoothFrameMs;
        glfwSetWindowTitle(native, title.str().c_str());
        if(options.fpsLimit>0) {
            const auto period=std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                std::chrono::duration<double>(1.0/options.fpsLimit));
            pacer.WaitUntil(frameStarted+period);
        }
    }
    glfwSetScrollCallback(native, nullptr);
    glfwSetWindowUserPointer(native, nullptr);
}
} // namespace

int main(int argc, char** argv) {
    std::cout << std::unitbuf;
    int result = 0;
    try {
        const auto options = Parse(argc, argv);
        Check(glfwInit() == GLFW_TRUE, "glfwInit failed");
        const bool hidden = options.giRoomTest || (options.giRoomPerformance&&!options.benchmarkPresent) || (options.showcaseMotion&&!(options.showcaseMeasureOnly&&options.benchmarkPresent)) || options.showcaseStability || options.showcaseTest || options.smoke || options.quality || options.shadows || options.closeup || options.advanced || options.advancedQuality || options.skyTest || options.probeTest || options.realtimeGiTest || options.realtimeGiStabilityTest || options.lumenGiTest || options.viewStability || (options.benchmark&&!options.benchmarkPresent);
        glfwWindowHint(GLFW_VISIBLE, hidden ? GLFW_FALSE : GLFW_TRUE);
        if(options.fullscreen)glfwWindowHint(GLFW_AUTO_ICONIFY,GLFW_FALSE);
        const bool sizedStudy=options.benchmark||((options.giRoom||options.giRoomTest||options.giRoomPerformance||options.showcaseMotion||options.showcaseStability||options.showcaseTest||options.viewStability||options.realtimeGiStabilityTest)&&options.studySizeSet);
        const std::uint32_t width = sizedStudy ? options.benchmarkWidth : options.giRoomPerformance ? 2560 : options.closeup ? 640 : hidden ? 960 : 1280;
        const std::uint32_t height = sizedStudy ? options.benchmarkHeight : options.giRoomPerformance ? 1440 : options.closeup ? 724 : hidden ? 600 : 800;
        ApplicationWindow::Window window({width, height, "MaterialLab", options.vsync,options.vulkan?Render::BackendType::Vulkan:Render::BackendType::Opengl});
        Check(window.Activate(), "Render window creation failed");
        if(options.fullscreen&&!hidden) {
            auto* monitor=glfwGetPrimaryMonitor();const auto* mode=glfwGetVideoMode(monitor);Check(mode!=nullptr,"Fullscreen monitor unavailable");
            const auto fullscreenWidth=options.studySizeSet&&!options.renderSizeSet?int(options.benchmarkWidth):mode->width;
            const auto fullscreenHeight=options.studySizeSet&&!options.renderSizeSet?int(options.benchmarkHeight):mode->height;
            glfwSetWindowMonitor(window.GetNativeWindow(),monitor,0,0,fullscreenWidth,fullscreenHeight,GLFW_DONT_CARE);glfwPollEvents();
        }
        std::unique_ptr<MaterialLabModule> module;
        if(options.vulkan){auto context=*window.GetRenderContextAsVulkan();context.validation=options.validation;context.hardwareRayTracing=!options.softwareRays;module=std::make_unique<MaterialLabModule>(context,options.legacyDirectory);}
        else module=std::make_unique<MaterialLabModule>(*window.GetRenderContextAsOpengl(),options.legacyDirectory);
        auto& lab=*module;
        Check(lab.Startup(), lab.LastError());
        std::cout << "Legacy textures: " << (options.legacyDirectory.empty() ? "procedural fallback" : options.legacyDirectory) << '\n';
        if(options.giRoomTest)GiRoomQuality(lab,options);
        else if(options.giRoomPerformance)GiRoomPerformance(lab,options);
        else if(options.giRoom)GiRoomInteractive(lab,window,options);
        else if(options.showcaseMotion)ShowcaseMotion(lab,options);
        else if(options.showcaseStability)ShowcaseStability(lab,options);
        else if(options.showcaseTest)ShowcaseQuality(lab,options);
        else if(options.showcase)ShowcaseInteractive(lab,window,options);
        else if (options.smoke) Smoke(lab, options, width, height);
        else if (options.quality) Quality(lab, options);
        else if (options.shadows) ShadowSmoke(lab, options);
        else if (options.closeup) Closeup(lab, options);
        else if (options.advanced) Advanced(lab, options);
        else if (options.advancedQuality) AdvancedQuality(lab, options);
        else if (options.skyTest) SkyQuality(lab,options);
        else if (options.probeTest) ProbeQuality(lab,options);
        else if (options.realtimeGiTest) RealtimeGiQuality(lab,options);
        else if (options.realtimeGiStabilityTest) RealtimeGiStability(lab,options);
        else if (options.viewStability) LumenViewStability(lab,options);
        else if (options.lumenGiTest) LumenGiQuality(lab,options);
        else if (options.benchmark) Benchmark(lab, options);
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
