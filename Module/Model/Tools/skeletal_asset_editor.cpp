#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <commdlg.h>
// Let Optimus select the high-performance adapter for this executable. User
// and system application profiles still take precedence; no setting is edited.
extern "C" { __declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001; }
#endif
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "ApplicationWindow/public/window.h"
#include "Model/Public/model_module.h"
#include "Model/Test/model_geometry_revision_test.h"
#include "Model/Public/character_material_profile.h"
#include "Model/Public/skeletal_asset.h"
#include "Model/Public/vmd_animation.h"
#include "Model/Public/cloth_asset.h"
#include "Model/Public/hair_asset.h"
#include "Model/Private/cloth_preview_worker.h"
#include "Model/Private/preview_animation_clock.h"
#include "Model/Private/garment_recognition.h"
#include "Render/Private/rhi_device.h"
#include "Render/Public/Pipeline/material_render_pipeline.h"
#include "Render/Public/Pipeline/toon_render_pipeline.h"
#include "skin_preview_profile.h"
#include "strand_hair_preview.h"
#include "hair_lighting_profile.h"
#include "hair_direct_lighting_profile.h"
#include "hair_linear_capture.h"
#include "source_hair_display.h"
#include "diana_camera_reference.h"
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBIW_WINDOWS_UTF8
#include <stb_image_write.h>

#ifndef MODEL_SOURCE_DIR
#define MODEL_SOURCE_DIR "."
#endif

namespace {
namespace fs = std::filesystem;
using namespace Model;
using namespace std::chrono_literals;
void Check(bool value, const std::string& message) { if (!value) throw std::runtime_error(message); }
std::string Utf8(const fs::path& path) { auto text=path.u8string(); return {text.begin(),text.end()}; }
fs::path Path(const std::string& text) { return fs::path(std::u8string(text.begin(),text.end())); }
bool NativeHairMomentKeyMatches(const Render::StrandMaterial& material){
    const float ratio=(material.ior-1.f)/(material.ior+1.f);
    return material.nativeMomentParameters==glm::vec4(material.ior,ratio*ratio,material.azimuthalRoughness,material.cuticleShift);
}
const char* NativeHairMomentStatus(const Render::StrandMaterial& material){
    if(!material.nativeMomentTable)return "legacy-no-table";
    if(!material.useNativeMomentLut)return "legacy-disabled";
    return NativeHairMomentKeyMatches(material)?"native64x64x16":"legacy-key-mismatch";
}
const char* HairOitMode(const Render::MaterialPipelineSettings& settings){
    if(settings.strandNativeOit)return settings.strandNativeOitLayers==8?"native8":"native4";
    return settings.strandFrontLayerTransparency?"depth-groups":"weighted";
}
bool NativeDSUnitFraction(float value){return std::isfinite(value)&&value>=0.f&&value<=1.f;}
bool NativeDSInjectionQuality(float value){return std::isfinite(value)&&value>=0.f&&value<=3.f&&std::floor(value)==value;}
void ApplyNativeDSInjectionParameters(Render::StrandMaterial& material,float rate,float lodFraction,float quality,bool minLodFactor){
    Check(!material.sourceDSVolume||NativeDSUnitFraction(rate),"Native DSVolume injection rate must be finite and within0..1 (source clamps requested rate to at least.01 before count calculation)");
    Check(!material.sourceDSVolume||NativeDSUnitFraction(lodFraction),"Native DSVolume LOD fraction must be finite and within0..1");
    Check(!material.sourceDSVolume||NativeDSInjectionQuality(quality),"Native DSVolume injection quality must be an integer0..3");
    // Legacy rendering ignores invalid unused source settings, including NaN.
    // Keep finite usable settings so enabling the native UI later is predictable.
    if(NativeDSUnitFraction(rate))material.volumeInjectionRate=rate;
    if(NativeDSUnitFraction(lodFraction))material.volumeInjectionLodFraction=lodFraction;
    if(NativeDSInjectionQuality(quality))material.volumeInjectionQuality=std::uint32_t(quality);
    material.volumeInjectionMinLodFactor=minLodFactor;
}
// Keep the system scheduling policy unless a controlled comparison explicitly
// requests MODEL_PREVIEW_QOS=1. Priority and system power policy stay unchanged.
// Resolve dynamically so older Windows versions still run normally.
class ScopedPreviewQos {
#ifdef _WIN32
    using Query = BOOL(WINAPI*)(HANDLE,PROCESS_INFORMATION_CLASS,LPVOID,DWORD);
    using Set = BOOL(WINAPI*)(HANDLE,PROCESS_INFORMATION_CLASS,LPVOID,DWORD);
    PROCESS_POWER_THROTTLING_STATE previous_{};
    Set set_=nullptr;
    bool restore_=false;
#endif
    const char* status_="system default";
public:
    ScopedPreviewQos() {
#ifdef _WIN32
        char option[2]{};
        if(GetEnvironmentVariableA("MODEL_PREVIEW_QOS",option,DWORD(sizeof(option)))!=1||option[0]!='1')return;
        status_="unavailable";
        const auto kernel=GetModuleHandleW(L"kernel32.dll");
        if(!kernel)return;
        const auto query=reinterpret_cast<Query>(GetProcAddress(kernel,"GetProcessInformation"));
        set_=reinterpret_cast<Set>(GetProcAddress(kernel,"SetProcessInformation"));
        if(!query||!set_)return;
        previous_.Version=PROCESS_POWER_THROTTLING_CURRENT_VERSION;
        if(!query(GetCurrentProcess(),ProcessPowerThrottling,&previous_,sizeof(previous_)))return;
        auto state=previous_;
        state.ControlMask|=PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
        state.StateMask&=~PROCESS_POWER_THROTTLING_EXECUTION_SPEED;
        restore_=set_(GetCurrentProcess(),ProcessPowerThrottling,&state,sizeof(state))!=FALSE;
        status_=restore_?"high":"request failed";
#endif
    }
    ~ScopedPreviewQos(){
#ifdef _WIN32
        if(restore_)set_(GetCurrentProcess(),ProcessPowerThrottling,&previous_,sizeof(previous_));
#endif
    }
    ScopedPreviewQos(const ScopedPreviewQos&)=delete;
    ScopedPreviewQos& operator=(const ScopedPreviewQos&)=delete;
    const char* Status() const noexcept{return status_;}
};
struct Options {
    fs::path source, package, screenshot, retargetSource,character,captureDirectory,hair;
    fs::path benchmarkOutput,skinProfile;
    fs::path materialProfile;
    fs::path hairVolumeReport;
    fs::path hairLinearCapture;
    fs::path hairLightingProfile;
    fs::path hairDirectProfile;
    bool sourceEnvironmentOverride=false;
    bool sourceDirectOverride=false;
    float directSunScale=1.f,directPunctualScale=1.f;
    Render::DirectionalShadowFilter sunShadowFilter=Render::DirectionalShadowFilter::Tent;
    float sunShadowRadius=.003f;
    float sunSurfaceShadowRadius=0.f;
    Render::StrandShadowCoverage hairShadowCoverage=Render::StrandShadowCoverage::Ordered;
    float hairShadowExtinctionScale=1.f;
    bool diana=false,dianaTest=false,dianaFace=false;
    std::string cameraReference;
    fs::path cameraReferenceProfile,cameraReferenceExport;
    bool noSkinDiffusion=false,noEyeRefraction=false,noEyeShell=false,noHairScattering=false,noHairMultipleScattering=false,noHairVolume=false;
    bool noHairOpaqueShadows=false;
    float hairOpaqueShadowPower=1.f;
    bool hairOpaqueShadowPowerSet=false;
    int hairRoughnessInput=-1; // absent uses the material/profile default
    float hairHostAmbientScale=-1.f; // absent keeps the lighting profile default
    bool noHairSourceColor=false;
    bool noHairNtt=false;
    bool noHairSourceIndirect=false;
    bool noHairMotion=false,noHairNativeLut=false,hairDynamic=false;
    int hairVolumeFilter=-1;
    int hairVolumeResolution=-1;
    int hairVolumeIntegration=-1,hairPairedTransparency=-1,hairAdaptiveHistory=-1;
    float hairFastMotionPixels=-1;
    int hairProxyBlockers=-1;
    int hairStableCache=-1,hairPointIbl=-1,hairDebug=0;
    int hairShading=-1;
    int hairGeometry=-1;
    bool hairShutter=false,hairOrbitStudy=false;
    bool noHairNearShadow=false;
    int hairTangents=-1,hairIblIncident=-1;
    int hairCoverage=-1;
    int hairTransparency=-1;
    int hairOit=-1; // Explicit stage2 selection: 0=host depth groups, 4/8=native arithmetic study.
    int hairDsVolume=-1; // Explicit stage3 selection: 0=legacy density, 1=source Texture occupancy.
    float hairDsInjectionRate=1.f,hairDsLodFraction=1.f,hairDsInjectionQuality=2.f;
    bool hairDsInjectionRateSet=false,hairDsLodFractionSet=false,hairDsInjectionQualitySet=false;
    int hairDsMinLod=-1;
    int hairQuality=1;
    int displayStudy=0;
    bool noHairSourceDirect=false,noHairBlockers=false,noHairCastShadows=false,noHairTemporalDepth=false;
    bool hairVolumeSky=false;
    int hairIbl=-1;
    bool noHairBackscatter=false;
    Render::SkinDiffusionKernel skinKernel=Render::SkinDiffusionKernel::SourceBurley;
    fs::path strandHair;
    bool strandTest=false;
    HairSimulation::GravityMode gravityMode=HairSimulation::GravityMode::Normal;
    std::string previewQos="unavailable";
    std::vector<fs::path> animations, vmds;
    bool smoke=false, motionTest=false,clothTest=false,equipTest=false,cleanClothView=false,meshRegions=false;
    bool benchmark=false,noCloth=false,noHair=false,hairTest=false,noVsync=false,vsyncExplicit=false,syncCloth=false,asyncClothTest=false;
    bool pbr=false,bindPose=false;
    int skin=-1,lighting=0;
    double startTime=0;
    double holdAt=-1,yaw=.12,distance=4.7;
    double maxFps=-1; // Automatic: interactive 60; benchmark/smoke uncapped.
    std::uint32_t captureEvery=6;
    std::string curveBone,focusBone;
    int regionCategory=-1,frameRegion=-2;
    std::uint32_t frames=0;
    std::uint32_t benchmarkWarmup=30;
};
Options Parse(const std::vector<std::string>& args) {
    Options result;
    for (std::size_t i=1;i<args.size();++i) {
        const auto& a=args[i];
        if (a=="--smoke-test") result.smoke=true;
        else if(a=="--diana"||a=="--diana-test"||a=="--diana-face"){result.diana=true;result.dianaTest=result.dianaTest||a=="--diana-test";result.dianaFace=result.dianaFace||a=="--diana-face";if(result.dianaTest)result.smoke=true;}
        else if(a=="--no-skin-diffusion")result.noSkinDiffusion=true;
        else if(a=="--no-eye-refraction")result.noEyeRefraction=true;
        else if(a=="--no-eye-shell")result.noEyeShell=true;
        else if(a=="--no-hair-scattering")result.noHairScattering=true;
        else if(a=="--no-hair-multiple-scattering")result.noHairMultipleScattering=true;
        else if(a=="--no-hair-volume")result.noHairVolume=true;
        else if(a=="--no-hair-opaque-shadows")result.noHairOpaqueShadows=true;
        else if(a=="--no-hair-source-color")result.noHairSourceColor=true;
        else if(a=="--no-hair-ntt")result.noHairNtt=true;
        else if(a=="--no-hair-source-indirect")result.noHairSourceIndirect=true;
        else if(a=="--no-hair-motion")result.noHairMotion=true;
        else if(a=="--no-hair-native-lut")result.noHairNativeLut=true;
        else if(a=="--hair-dynamic")result.hairDynamic=true;
        else if(a=="--hair-shadow-cache"){
            Check(++i<args.size(),"Missing hair opaque-cache mode");
            Check(args[i]=="phase"||args[i]=="stable","Hair shadow cache must be phase or stable");
            result.hairStableCache=args[i]=="stable"?1:0;
        }
        else if(a=="--hair-ibl-paths"){
            Check(++i<args.size(),"Missing hair IBL path mode");
            Check(args[i]=="voxel"||args[i]=="point","Hair IBL paths must be voxel or point");
            result.hairPointIbl=args[i]=="point"?1:0;
        }
        else if(a=="--hair-debug"){
            Check(++i<args.size(),"Missing hair diagnostic mode");
            const std::array<std::string,15> names{"beauty","base","direct","indirect","direct-ballistic","direct-multiple","ibl-ballistic","ibl-multiple","r","tt","trt","n-sun","n-ibl","visibility","incident"};
            const auto found=std::find(names.begin(),names.end(),args[i]);Check(found!=names.end(),"Unknown hair diagnostic mode");
            result.hairDebug=int(found-names.begin());
        }
        else if(a=="--hair-shading"){
            Check(++i<args.size(),"Missing hair shading mode");
            Check(args[i]=="fragment"||args[i]=="segment","Hair shading must be fragment or segment");
            result.hairShading=args[i]=="segment"?1:0;
        }
        else if(a=="--hair-geometry"){
            Check(++i<args.size(),"Missing hair geometry mode");
            Check(args[i]=="linear"||args[i]=="curve","Invalid hair geometry mode (linear, curve)");
            result.hairGeometry=args[i]=="curve"?1:0;
        }
        else if(a=="--hair-shutter")result.hairShutter=true;
        else if(a=="--hair-orbit-study")result.hairOrbitStudy=true;
        else if(a=="--hair-proxy-blockers"){
            Check(++i<args.size(),"Missing head proxy blocker mode");
            Check(args[i]=="off"||args[i]=="on","Head proxy blockers must be off or on");
            result.hairProxyBlockers=args[i]=="on"?1:0;
        }
        else if(a=="--hair-volume-integration"){
            Check(++i<args.size(),"Missing hair volume integration mode");
            Check(args[i]=="growth"||args[i]=="exact","Hair volume integration must be growth or exact");
            result.hairVolumeIntegration=args[i]=="exact"?1:0;
        }
        else if(a=="--hair-transparency-passes"){
            Check(++i<args.size(),"Missing hair transparency pass mode");
            Check(args[i]=="legacy"||args[i]=="paired","Hair transparency passes must be legacy or paired");
            result.hairPairedTransparency=args[i]=="paired"?1:0;
        }
        else if(a=="--hair-history"){
            Check(++i<args.size(),"Missing hair history mode");
            Check(args[i]=="legacy"||args[i]=="adaptive","Hair history must be legacy or adaptive");
            result.hairAdaptiveHistory=args[i]=="adaptive"?1:0;
        }
        else if(a=="--hair-fast-motion"){
            Check(++i<args.size(),"Missing hair motion threshold");std::size_t used=0;
            result.hairFastMotionPixels=std::stof(args[i],&used);
            Check(used==args[i].size()&&std::isfinite(result.hairFastMotionPixels)&&result.hairFastMotionPixels>=1&&result.hairFastMotionPixels<=256,"Hair motion threshold must be 1..256 pixels");
        }
        else if(a=="--no-hair-near-shadow")result.noHairNearShadow=true;
        else if(a=="--hair-tangents"){
            Check(++i<args.size(),"Missing hair tangent mode");
            Check(args[i]=="guide"||args[i]=="render","Hair tangents must be guide or render");
            result.hairTangents=args[i]=="render"?1:0;
        }
        else if(a=="--hair-ibl-incident"){
            Check(++i<args.size(),"Missing hair IBL incident mode");
            Check(args[i]=="angular"||args[i]=="spatial","Hair IBL incident must be angular or spatial");
            result.hairIblIncident=args[i]=="spatial"?1:0;
        }
        else if(a=="--hair-volume-resolution"){
            Check(++i<args.size(),"Missing hair shadow volume budget");
            Check(args[i]=="16"||args[i]=="32"||args[i]=="48"||args[i]=="64"||args[i]=="128","Hair volume budget must be16,32,48,64 or128");
            result.hairVolumeResolution=std::stoi(args[i]);
        }
        else if(a=="--hair-volume-filter"){
            Check(++i<args.size(),"Missing hair volume filter");
            Check(args[i]=="linear"||args[i]=="legacy","Invalid hair volume filter (linear, legacy)");
            result.hairVolumeFilter=args[i]=="linear"?1:0;
        }
        else if(a=="--hair-ds-volume"){
            Check(++i<args.size(),"Missing hair DSVolume mode");
            Check(args[i]=="legacy"||args[i]=="native","Hair DSVolume must be legacy or native");
            result.hairDsVolume=args[i]=="native"?1:0;
        }
        else if(a=="--hair-ds-injection-rate"||a=="--hair-ds-lod-fraction"||a=="--hair-ds-quality"){
            Check(++i<args.size(),"Missing value for "+a);std::size_t used=0;
            const float value=std::stof(args[i],&used);
            Check(used==args[i].size(),"Expected a numeric value for "+a);
            if(a=="--hair-ds-injection-rate"){result.hairDsInjectionRate=value;result.hairDsInjectionRateSet=true;}
            else if(a=="--hair-ds-lod-fraction"){result.hairDsLodFraction=value;result.hairDsLodFractionSet=true;}
            else {result.hairDsInjectionQuality=value;result.hairDsInjectionQualitySet=true;}
        }
        else if(a=="--hair-ds-min-lod")result.hairDsMinLod=1;
        else if(a=="--no-hair-ds-min-lod")result.hairDsMinLod=0;
        else if(a=="--hair-coverage"){
            Check(++i<args.size(),"Missing hair coverage mode");
            Check(args[i]=="area"||args[i]=="legacy","Invalid hair coverage mode (area, legacy)");
            result.hairCoverage=args[i]=="area"?1:0;
        }
        else if(a=="--hair-transparency"){
            Check(++i<args.size(),"Missing hair transparency mode");
            Check(args[i]=="weighted"||args[i]=="layered","Invalid hair transparency mode (weighted, layered)");
            result.hairTransparency=args[i]=="layered"?1:0;
        }
        else if(a=="--hair-oit"){
            Check(++i<args.size(),"Missing hair OIT mode");
            Check(args[i]=="depth-groups"||args[i]=="native4"||args[i]=="native8","Hair OIT must be depth-groups, native4 or native8");
            result.hairOit=args[i]=="native4"?4:args[i]=="native8"?8:0;
        }
        else if(a=="--display"){
            Check(++i<args.size(),"Missing display mode");const std::array<std::string,3> modes{"aces","st30-100","st30-200"};
            auto found=std::find(modes.begin(),modes.end(),args[i]);Check(found!=modes.end(),"Invalid display (aces, st30-100, st30-200)");result.displayStudy=int(found-modes.begin());
        }
        else if(a=="--hair-quality"){
            Check(++i<args.size(),"Missing hair quality");
            const std::array<std::string,3> modes{"balanced","high","ultra"};
            auto found=std::find(modes.begin(),modes.end(),args[i]);Check(found!=modes.end(),"Invalid hair quality (balanced, high, ultra)");
            result.hairQuality=int(found-modes.begin());
        }
        else if(a=="--no-hair-source-direct")result.noHairSourceDirect=true;
        else if(a=="--no-hair-blockers")result.noHairBlockers=true;
        else if(a=="--no-hair-cast-shadows")result.noHairCastShadows=true;
        else if(a=="--hair-opaque-shadow-power"){
            Check(++i<args.size(),"Missing hair opaque shadow power");std::size_t used=0;
            result.hairOpaqueShadowPower=std::stof(args[i],&used);result.hairOpaqueShadowPowerSet=true;
            Check(used==args[i].size()&&std::isfinite(result.hairOpaqueShadowPower)&&result.hairOpaqueShadowPower>=.05f&&result.hairOpaqueShadowPower<=4.f,"Hair opaque shadow power must be finite and within0.05..4");
        }
        else if(a=="--hair-roughness-input"){
            Check(++i<args.size(),"Missing hair roughness input policy");
            Check(args[i]=="raw"||args[i]=="source","Hair roughness input must be raw or source");
            result.hairRoughnessInput=args[i]=="source"?1:0;
        }
        else if(a=="--sun-shadow-filter"){
            Check(++i<args.size(),"Missing sun shadow filter");
            Check(args[i]=="tent"||args[i]=="source-poisson8","Sun shadow filter must be tent or source-poisson8");
            result.sunShadowFilter=args[i]=="source-poisson8"?Render::DirectionalShadowFilter::SourcePoisson8:Render::DirectionalShadowFilter::Tent;
        }
        else if(a=="--hair-shadow-coverage"){
            Check(++i<args.size(),"Missing hair shadow coverage mode");
            Check(args[i]=="ordered"||args[i]=="deep-opacity","Hair shadow coverage must be ordered or deep-opacity");
            result.hairShadowCoverage=args[i]=="deep-opacity"?Render::StrandShadowCoverage::DeepOpacity:Render::StrandShadowCoverage::Ordered;
        }
        else if(a=="--hair-shadow-extinction-scale"){
            Check(++i<args.size(),"Missing DOM hair shadow extinction scale");std::size_t used=0;
            result.hairShadowExtinctionScale=std::stof(args[i],&used);
            Check(used==args[i].size()&&std::isfinite(result.hairShadowExtinctionScale)&&result.hairShadowExtinctionScale>=0.f&&result.hairShadowExtinctionScale<=2.f,"DOM hair shadow extinction scale must be finite and within0..2");
        }
        else if(a=="--sun-shadow-radius"){
            Check(++i<args.size(),"Missing sun shadow radius");std::size_t used=0;
            result.sunShadowRadius=std::stof(args[i],&used);
            Check(used==args[i].size()&&std::isfinite(result.sunShadowRadius)&&result.sunShadowRadius>=0.f&&result.sunShadowRadius<=.05f,"Sun shadow radius must be finite and within0..0.05 normalized UV");
        }
        else if(a=="--sun-surface-shadow-radius"){
            Check(++i<args.size(),"Missing opaque surface shadow radius");std::size_t used=0;
            result.sunSurfaceShadowRadius=std::stof(args[i],&used);
            Check(used==args[i].size()&&std::isfinite(result.sunSurfaceShadowRadius)&&result.sunSurfaceShadowRadius>=0.f&&result.sunSurfaceShadowRadius<=.05f,"Opaque surface shadow radius must be finite and within0..0.05 normalized UV (0 inherits hair radius)");
        }
        else if(a=="--no-hair-temporal-depth")result.noHairTemporalDepth=true;
        else if(a=="--hair-volume-sky")result.hairVolumeSky=true;
        else if(a=="--hair-volume-report"){Check(++i<args.size(),"Missing volume report path");result.hairVolumeReport=Path(args[i]);}
        else if(a=="--hair-linear-capture"){Check(++i<args.size(),"Missing linear hair capture directory");result.hairLinearCapture=Path(args[i]);}
        else if(a=="--hair-lighting-profile"){Check(++i<args.size(),"Missing hair lighting profile");result.hairLightingProfile=Path(args[i]);}
        else if(a=="--hair-direct-profile"){Check(++i<args.size(),"Missing direct light profile");result.hairDirectProfile=Path(args[i]);}
        else if(a=="--hair-host-ambient-scale"){
            Check(++i<args.size(),"Missing hair host ambient scale");std::size_t used=0;
            result.hairHostAmbientScale=std::stof(args[i],&used);
            Check(used==args[i].size()&&std::isfinite(result.hairHostAmbientScale)&&result.hairHostAmbientScale>=0&&result.hairHostAmbientScale<=1,"Hair host ambient scale must be finite and within0..1");
        }
        else if(a=="--direct-sun-scale"||a=="--direct-punctual-scale"){
            Check(++i<args.size(),"Missing direct light input scale");std::size_t used=0;
            const float scale=std::stof(args[i],&used);
            Check(used==args[i].size()&&std::isfinite(scale)&&scale>=0.f&&scale<=16.f,"Direct light input scale must be finite and within0..16");
            (a=="--direct-sun-scale"?result.directSunScale:result.directPunctualScale)=scale;
        }
        else if(a=="--hair-direct-input"){
            Check(++i<args.size(),"Missing direct light input");Check(args[i]=="source"||args[i]=="preset","Direct light input must be source or preset");
            result.sourceDirectOverride=args[i]=="source";
        }
        else if(a=="--hair-environment"){
            Check(++i<args.size(),"Missing hair environment input");
            Check(args[i]=="preset"||args[i]=="source","Hair environment must be preset or source");
            result.sourceEnvironmentOverride=args[i]=="source";
        }
        else if(a=="--no-hair-backscatter")result.noHairBackscatter=true;
        else if(a=="--hair-ibl"){
            Check(++i<args.size(),"Missing hair IBL mode");
            const std::array<std::string,3> modes{"legacy","forward","source"};
            const auto found=std::find(modes.begin(),modes.end(),args[i]);
            Check(found!=modes.end(),"Invalid hair IBL mode (legacy, forward, source)");
            result.hairIbl=int(found-modes.begin());
        }
        else if(a=="--skin-kernel"){Check(++i<args.size(),"Missing skin kernel");Check(args[i]=="gaussian"||args[i]=="source-burley","Invalid skin kernel (gaussian, source-burley)");result.skinKernel=args[i]=="gaussian"?Render::SkinDiffusionKernel::Gaussian:Render::SkinDiffusionKernel::SourceBurley;}
        else if(a=="--material-profile"){Check(++i<args.size(),"Missing material profile");result.materialProfile=Path(args[i]);}
        else if(a=="--pragmata-hair")result.strandHair=Path(MODEL_SOURCE_DIR)/"Asset/HairSimulation/diana_lod0.hairbin";
        else if(a=="--strand-test"){result.strandTest=true;result.smoke=true;result.strandHair=Path(MODEL_SOURCE_DIR)/"Asset/HairSimulation/diana_lod0.hairbin";}
        else if(a=="--strand-hair"||a=="--gravity") {
            Check(++i<args.size(),"Missing value for "+a);
            if(a=="--strand-hair")result.strandHair=Path(args[i]);
            else {Check(args[i]=="normal"||args[i]=="low"||args[i]=="zero","Invalid gravity (normal, low, zero)");result.gravityMode=args[i]=="normal"?HairSimulation::GravityMode::Normal:args[i]=="low"?HairSimulation::GravityMode::Low:HairSimulation::GravityMode::Zero;}
        }
        else if (a=="--motion-test") {result.smoke=true;result.motionTest=true;}
        else if (a=="--cloth-test") {result.smoke=true;result.motionTest=true;result.clothTest=true;}
        else if (a=="--cloth-equip-test") {result.smoke=true;result.motionTest=true;result.clothTest=true;result.equipTest=true;}
        else if (a=="--clean-cloth-view")result.cleanClothView=true;
        else if (a=="--mesh-regions")result.meshRegions=true;
        else if (a=="--bind-pose")result.bindPose=true;
        else if(a=="--camera-reference"){
            Check(++i<args.size(),"Missing camera reference mode");
            Check(args[i]=="game-shot"||args[i]=="off","Invalid camera reference (game-shot, off)");
            result.cameraReference=args[i];if(args[i]=="game-shot"){result.diana=true;result.dianaFace=true;}
        }
        else if(a=="--camera-reference-profile"){
            Check(++i<args.size(),"Missing reference camera profile path");
            result.cameraReferenceProfile=Path(args[i]);result.cameraReference="game-shot";result.diana=true;result.dianaFace=true;
        }
        else if(a=="--camera-reference-export"){
            Check(++i<args.size(),"Missing reference camera report path");result.cameraReferenceExport=Path(args[i]);
        }
        else if (a=="--benchmark")result.benchmark=true;
        else if (a=="--no-cloth")result.noCloth=true;
        else if (a=="--no-hair")result.noHair=true;
        else if (a=="--hair-test"){result.smoke=true;result.motionTest=true;result.hairTest=true;}
        else if (a=="--no-vsync"){result.noVsync=true;result.vsyncExplicit=true;}
        else if (a=="--vsync"){result.noVsync=false;result.vsyncExplicit=true;}
        else if (a=="--sync-cloth")result.syncCloth=true;
        else if (a=="--async-cloth-test"){result.asyncClothTest=true;result.benchmark=true;}
        else if (a=="--source"||a=="--package"||a=="--screenshot"||a=="--frames"||a=="--retarget-source"||a=="--animation"||a=="--vmd"||a=="--time"||a=="--curve-bone"||a=="--focus-bone"||a=="--character"||a=="--capture-dir"||a=="--capture-every"||a=="--cloth-hold-at"||a=="--yaw"||a=="--distance"||a=="--benchmark-frames"||a=="--benchmark-warmup"||a=="--benchmark-output"||a=="--max-fps"||a=="--hair"||a=="--region-category"||a=="--frame-region"||a=="--pipeline"||a=="--skin-profile"||a=="--skin"||a=="--lighting") {
            Check(++i<args.size(),"Missing value for "+a);
            if(a=="--source")result.source=Path(args[i]);
            else if(a=="--package")result.package=Path(args[i]);
            else if(a=="--screenshot")result.screenshot=Path(args[i]);
            else if(a=="--retarget-source")result.retargetSource=Path(args[i]);
            else if(a=="--character")result.character=Path(args[i]);
            else if(a=="--hair")result.hair=Path(args[i]);
            else if(a=="--skin-profile")result.skinProfile=Path(args[i]);
            else if(a=="--capture-dir")result.captureDirectory=Path(args[i]);
            else if(a=="--benchmark-output")result.benchmarkOutput=Path(args[i]);
            else if(a=="--animation")result.animations.push_back(Path(args[i]));
            else if(a=="--vmd")result.vmds.push_back(Path(args[i]));
            else if(a=="--curve-bone")result.curveBone=args[i];
            else if(a=="--focus-bone")result.focusBone=args[i];
            else if(a=="--skin") {
                Check(args[i]=="on"||args[i]=="off","Invalid --skin value (use on or off)");result.skin=args[i]=="on"?1:0;
            }
            else if(a=="--lighting") {
                const std::array<std::string,6> presets{"studio","side","back","reference","source-cube","reference-neutral"};
                const auto found=std::find(presets.begin(),presets.end(),args[i]);
                Check(found!=presets.end(),"Invalid --lighting value (studio, side, back, reference, source-cube, reference-neutral)");result.lighting=int(found-presets.begin());
            }
            else if(a=="--pipeline") {
                auto pipeline=args[i];std::transform(pipeline.begin(),pipeline.end(),pipeline.begin(),[](unsigned char c){return char(std::tolower(c));});
                Check(pipeline=="toon"||pipeline=="pbr","Invalid pipeline (use pbr or toon)");result.pbr=pipeline=="pbr";
            }
            else if(a=="--region-category"||a=="--frame-region") {
                auto category=args[i];std::transform(category.begin(),category.end(),category.begin(),[](unsigned char c){return char(std::tolower(c));});
                const std::array<std::string,6> names{"all","unknown","body","hair","garment","accessory"};
                const auto found=std::find(names.begin(),names.end(),category);
                Check(found!=names.end(),"Invalid region category (use all, unknown, body, hair, garment or accessory)");
                if(a=="--frame-region")result.frameRegion=int(found-names.begin())-1;
                else {result.regionCategory=int(found-names.begin())-1;result.meshRegions=true;}
            }
            else if(a=="--time"){result.startTime=std::stod(args[i]);Check(std::isfinite(result.startTime)&&result.startTime>=0,"Invalid preview time");}
            else if(a=="--cloth-hold-at"){result.holdAt=std::stod(args[i]);Check(std::isfinite(result.holdAt)&&result.holdAt>=0,"Invalid cloth hold time");}
            else if(a=="--yaw"){result.yaw=std::stod(args[i]);Check(std::isfinite(result.yaw),"Invalid camera yaw");}
            else if(a=="--distance"){result.distance=std::stod(args[i]);Check(std::isfinite(result.distance)&&result.distance>=1&&result.distance<=20,"Invalid camera distance");}
            else if(a=="--max-fps"){std::size_t used=0;result.maxFps=std::stod(args[i],&used);Check(used==args[i].size()&&std::isfinite(result.maxFps)&&(result.maxFps==0||(result.maxFps>=1&&result.maxFps<=1000)),"Invalid maximum FPS (use 0 for uncapped, or 1 to 1000)");}
            else if(a=="--capture-every"){std::size_t used=0;result.captureEvery=std::stoul(args[i],&used);Check(used==args[i].size()&&result.captureEvery>0,"Invalid capture interval");}
            else if(a=="--benchmark-warmup"){std::size_t used=0;result.benchmarkWarmup=std::stoul(args[i],&used);Check(used==args[i].size(),"Invalid benchmark warmup");}
            else if(a=="--benchmark-frames"){std::size_t used=0;result.frames=std::stoul(args[i],&used);Check(used==args[i].size()&&result.frames>0,"Invalid benchmark frame count");result.benchmark=true;}
            else { std::size_t used=0;result.frames=std::stoul(args[i],&used);Check(used==args[i].size()&&result.frames>0,"Invalid frame count"); }
        } else throw std::runtime_error("Unknown argument: "+a);
    }
    if(result.diana){
        if(result.materialProfile.empty())result.materialProfile=Path(MODEL_SOURCE_DIR)/"Asset/Diana/materials.json";
        if(result.strandHair.empty())result.strandHair=Path(MODEL_SOURCE_DIR)/"Asset/HairSimulation/diana_lod0.hairbin";
        if(result.source.empty()&&result.package.empty()&&result.character.empty())result.source=Path(MODEL_SOURCE_DIR)/"Asset/Diana/diana.glb";
        result.bindPose=true;
    }
    if(!result.strandHair.empty())result.pbr=true;
    if(result.source.empty()&&result.package.empty()&&result.character.empty()) {
        if(!result.strandHair.empty())result.source=Path(MODEL_SOURCE_DIR)/"Asset/HairSimulation/diana_head.glb";
        else {
        auto outfit=Path(MODEL_SOURCE_DIR)/"Asset/Generated/MahiroCloth/Character.asset.json";
        auto generated=Path(MODEL_SOURCE_DIR)/"Asset/Generated/MahiroMaid/Package.asset.json";
        if(fs::exists(outfit))result.character=outfit;
        else if(fs::exists(generated))result.package=generated;
        else result.source=Path(MODEL_SOURCE_DIR)/"Asset/Model/Rikka_Uniform/Rikka_Uniform.pmx";
        }
    }
    Check(int(!result.source.empty())+int(!result.package.empty())+int(!result.character.empty())==1,"Choose --source, --package or --character");
    Check(result.skin!=1||!result.skinProfile.empty(),"--skin on requires --skin-profile with an exact material list");
    Check(!(result.hairOit>0&&result.hairShading==0),"--hair-oit native4/native8 requires segment shading; remove --hair-shading fragment or choose depth-groups");
    Check(!(result.hairDsVolume>0&&result.hairVolumeResolution>0&&result.hairVolumeResolution!=32&&result.hairVolumeResolution!=64&&result.hairVolumeResolution!=128),
        "--hair-ds-volume native requires --hair-volume-resolution32,64 or128;16 and48 are legacy-only budgets");
    if(result.smoke&&!result.frames)result.frames=result.clothTest||result.hairTest||result.strandTest?180:7;
    Check(!result.benchmark||!result.smoke,"Benchmark requires real-time delta; do not combine it with fixed-delta smoke tests");
    if(result.benchmark){if(!result.frames)result.frames=300;Check(result.frames>result.benchmarkWarmup,"Benchmark frame count must exceed its warmup");}
    if(result.maxFps<0)result.maxFps=result.benchmark||result.smoke?0:60;
    // Use one pacing mechanism by default. Sleeping after a vblank-limited
    // swap can miss the following vblank and turn a 60 FPS cap into 30-45 FPS.
    if(!result.vsyncExplicit&&result.maxFps>0)result.noVsync=true;
    return result;
}
using PerfClock=std::chrono::steady_clock;
double Milliseconds(PerfClock::duration value){return std::chrono::duration<double,std::milli>(value).count();}
template<class Function> void Measure(double& milliseconds,Function&& function){const auto start=PerfClock::now();function();milliseconds+=Milliseconds(PerfClock::now()-start);}
class ScopedFrameWaiter {
#ifdef _WIN32
    HANDLE timer_=nullptr;
#endif
    const char* backend_="std::sleep_until";
public:
    ScopedFrameWaiter(){
#ifdef _WIN32
        // A private high-resolution timer avoids Windows' coarse sleep quantum
        // without requesting a global timer period or changing scheduling QoS.
        using Create=HANDLE(WINAPI*)(LPSECURITY_ATTRIBUTES,LPCWSTR,DWORD,DWORD);
        const auto kernel=GetModuleHandleW(L"kernel32.dll");
        const auto create=kernel?reinterpret_cast<Create>(GetProcAddress(kernel,"CreateWaitableTimerExW")):nullptr;
        constexpr DWORD highResolution=0x00000002; // CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
        if(create){
            timer_=create(nullptr,nullptr,highResolution,TIMER_MODIFY_STATE|SYNCHRONIZE);
            if(timer_)backend_="high_resolution_waitable_timer";
            else {timer_=create(nullptr,nullptr,0,TIMER_MODIFY_STATE|SYNCHRONIZE);if(timer_)backend_="waitable_timer";}
        }
        if(!timer_){timer_=CreateWaitableTimerW(nullptr,FALSE,nullptr);if(timer_)backend_="waitable_timer";}
#endif
    }
    ~ScopedFrameWaiter(){
#ifdef _WIN32
        if(timer_){CancelWaitableTimer(timer_);CloseHandle(timer_);}
#endif
    }
    ScopedFrameWaiter(const ScopedFrameWaiter&)=delete;
    ScopedFrameWaiter& operator=(const ScopedFrameWaiter&)=delete;
    const char* Backend()const noexcept{return backend_;}
    void WaitUntil(PerfClock::time_point deadline){
        const auto now=PerfClock::now();if(now>=deadline)return;
#ifdef _WIN32
        if(timer_){
            const auto nanoseconds=std::chrono::duration_cast<std::chrono::nanoseconds>(deadline-now).count();
            LARGE_INTEGER due{};due.QuadPart=-std::max<LONGLONG>(1,(nanoseconds+99)/100);
            if(SetWaitableTimer(timer_,&due,0,nullptr,nullptr,FALSE)&&WaitForSingleObject(timer_,INFINITE)==WAIT_OBJECT_0)return;
            CancelWaitableTimer(timer_);CloseHandle(timer_);timer_=nullptr;backend_="std::sleep_until (timer failed)";
        }
#endif
        std::this_thread::sleep_until(deadline);
    }
};
struct FramePerformance {
    double frame=0,wallDelta=0,acceptedDelta=0,animationSeconds=0,simulationSeconds=0;
    double pose=0,skin=0,upload=0,cloth=0,apply=0,render=0,ui=0,swap=0,sync=0,gpu=-1;
    double clothWorker=0,clothLag=0;
    double hair=0,hairWorker=0,hairPenetration=0,hairAttachmentDrift=0;
    std::uint64_t hairSteps=0,hairSleepSteps=0;
    double frameLimit=0,limiterWait=0,minimizedWait=0;
    double processUserCpu=0,processKernelCpu=0,mainUserCpu=0,mainKernelCpu=0;
    bool cpuTimeAvailable=false,focused=true;
    std::uint64_t steps=0,constraintIterations=0,clothSnapshots=0;
};
struct CpuTimes {
    std::uint64_t processUser=0,processKernel=0,mainUser=0,mainKernel=0;
    bool available=false;
};
CpuTimes ReadCpuTimes() {
    CpuTimes result;
#ifdef _WIN32
    FILETIME created{},ended{},processKernel{},processUser{},mainKernel{},mainUser{};
    if(GetProcessTimes(GetCurrentProcess(),&created,&ended,&processKernel,&processUser)&&GetThreadTimes(GetCurrentThread(),&created,&ended,&mainKernel,&mainUser)) {
        const auto ticks=[](FILETIME value){return (std::uint64_t(value.dwHighDateTime)<<32)|value.dwLowDateTime;};
        result={ticks(processUser),ticks(processKernel),ticks(mainUser),ticks(mainKernel),true};
    }
#endif
    return result;
}
void RecordCpuTimes(FramePerformance& frame,const CpuTimes& start,const CpuTimes& end) {
    if(!start.available||!end.available||end.processUser<start.processUser||end.processKernel<start.processKernel||end.mainUser<start.mainUser||end.mainKernel<start.mainKernel)return;
    // FILETIME reports 100 ns units; aggregate all measured frames before
    // comparing against wall time so accounting granularity does not dominate.
    frame.processUserCpu=double(end.processUser-start.processUser)*1e-4;
    frame.processKernelCpu=double(end.processKernel-start.processKernel)*1e-4;
    frame.mainUserCpu=double(end.mainUser-start.mainUser)*1e-4;
    frame.mainKernelCpu=double(end.mainKernel-start.mainKernel)*1e-4;
    frame.cpuTimeAvailable=true;
}
std::string JsonString(const std::string& value) {
    std::string escaped="\"";
    constexpr char hex[]="0123456789abcdef";
    for(const unsigned char c:value) {
        if(c=='"'||c=='\\'){escaped+='\\';escaped+=char(c);}
        else if(c<0x20){escaped+="\\u00";escaped+=hex[c>>4];escaped+=hex[c&15];}
        else escaped+=char(c);
    }
    return escaped+'"';
}
std::string Browse(bool package,const wchar_t* filter=nullptr) {
#ifdef _WIN32
    std::array<wchar_t,32768> buffer{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize=sizeof(dialog);dialog.lpstrFile=buffer.data();dialog.nMaxFile=DWORD(buffer.size());
    dialog.lpstrFilter=filter?filter:(package?L"Asset packages (*.json)\0*.json\0All files\0*.*\0":L"Models (*.blend;*.pmx;*.fbx;*.obj;*.gltf;*.glb)\0*.blend;*.pmx;*.fbx;*.obj;*.gltf;*.glb\0All files\0*.*\0");
    dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
    if(GetOpenFileNameW(&dialog))return Utf8(fs::path(buffer.data()));
#endif
    return {};
}
enum class Selection { Mesh, Skeleton, Animation, Ik, Morphs, Retarget, Diagnostics, Cloth, Hair, MeshRegions, StrandHair, CharacterMaterials };
class Editor {
    Render::RHIDevice device_;
    ModelModule models_{device_};
    Render::ToonRenderPipeline toon_{device_};
    Render::MaterialRenderPipeline pbr_{device_};
    EditorPreview::StrandHairPreview strandHair_;
    std::optional<CharacterMaterialProfile> characterMaterials_;
    CharacterMaterialReport characterMaterialReport_;
    std::vector<PbrMaterial> characterMaterialEdits_;
    std::vector<std::map<std::string,Render::Material::ParameterValue>> characterParameterEdits_;
    int selectedCharacterMaterial_=0;
    bool dianaPreview_=false;
    bool disableSkinDiffusion_=false,disableEyeRefraction_=false;
    bool pbrEnabled_=false,pbrReady_=false,toonReady_=false;
    std::shared_ptr<ModelResource> resource_;
    std::shared_ptr<ModelResource> garmentResource_;
    std::uint64_t opaqueBodyGeometryRevision_=0,opaqueGarmentGeometryRevision_=0;
    std::optional<ModularCharacterBundle> modular_;
    ClothSimulator clothSimulator_;
    std::unique_ptr<detail::ClothPreviewWorker> clothWorker_;
    std::shared_ptr<const detail::ClothPreviewSnapshot> clothSnapshot_;
    std::uint64_t clothGeneration_=0,clothSnapshotRevision_=0,clothSnapshotSteps_=0,clothSnapshotIterations_=0;
    double clothSubmittedSeconds_=0,clothLagSeconds_=0;
    // Keep the animation clock paired with completed physics, including spans
    // where animation was paused but the cloth continued settling.
    detail::PreviewAnimationClock clothAnimationTimeline_;
    bool clothResumePending_=false;
    std::uint64_t clothResumeSerial_=0;
    std::uint64_t asyncSnapshotsObserved_=0,asyncTotalSteps_=0;
    double asyncMaximumSurfacePenetration_=0;
    double asyncMaximumResolvableSurfacePenetration_=0;
    double asyncMaximumAttachmentError_=0;
    std::size_t asyncMaximumCollapsedTriangles_=0;
    ClothSolverSettings clothSettings_;
    std::shared_ptr<const HairAsset> hairAsset_;
    HairSimulator hairSimulator_;
    bool hairSettingsPending_=false;
    HairSolverSettings hairSettings_;
    SkeletalPose hairPose_;
    SkeletalPose uploadedBodyPose_;
    std::vector<HairSurfaceOffset> uploadedHairSurfaceOffsets_;
    bool uploadedBodyPoseValid_=false;
    bool hairEnabled_=true,hairFrozen_=false,hairReset_=true,showHairNodes_=false,hairWasShared_=false;
    double previousHairTime_=-1;
    std::uint64_t hairSnapshotSteps_=0,hairSnapshotSleepSteps_=0;
    int selectedHairChain_=0;
    int trackedCameraBone_=-1;
    fs::path hairPath_;
    std::array<char,1024> hairExport_{};
    StaticModel garmentAnimated_,garmentDeformed_;
    bool clothEnabled_=true,showBody_=true,showGarment_=true,showClothWire_=false,showClothColliders_=false,clothReset_=true,clothFrozen_=false;
    double previousClothTime_=-1;
    int selectedClothParticle_=0;
    std::shared_ptr<SkeletalAssetPackage> asset_, retargetSource_;
    fs::path openedPath_;
    RetargetMap retargetMap_;
    std::optional<AnimationSequenceAsset> retargetPreview_;
    int retargetPreviewClip_=-1;
    double retargetPreviewRate_=0,retargetPreviewScale_=0;
    StaticModel deformed_;
    SkeletalPose pose_;
    Render::PipelineCamera camera_;
    Render::ToonPipelineSettings settings_;
    Render::MaterialPipelineSettings pbrSettings_;
    int displayStudy_=0;
    std::array<std::shared_ptr<const Render::DisplayColorCube>,2> displayCubes_;
    EditorPreview::SkinPreviewProfile skinProfile_;
    EditorPreview::SkinPreparedModel skinPrepared_;
    int lightingPreset_=0;
    float directSunScale_=1.f,directPunctualScale_=1.f;
    float unscaledDirectSunIntensity_=0.f;
    std::array<glm::vec3,4> unscaledDirectPoints_{};
    std::array<glm::vec3,Render::MaxStrandSourceLights> unscaledDirectSourcePoints_{};
    fs::path hairLightingPath_;
    std::optional<EditorPreview::HairLightingProfile> sourceHairLighting_;
    bool sourceEnvironmentOverride_=false;
    float hairHostAmbientScaleOverride_=-1.f;
    bool sourceDirectOverride_=false;
    fs::path hairDirectPath_;
    std::optional<EditorPreview::HairDirectLightingProfile> sourceHairDirect_;
    EditorPreview::HairDirectLightingProfile::Applied appliedSourceDirect_;
    glm::dmat4 placement_{1};
    GLFWwindow* window_;
    Render::OpenglBackendContext context_;
    bool guiReady_=false, rendererReady_=false, playing_=true, loop_=true, showBones_=true, showNames_=false;
    bool enableIk_=false, retargetPlaying_=false, bindPose_=false, dirty_=true, fontsDirty_=false;
    Selection selection_=Selection::Skeleton;
    int selectedBone_=0, selectedClip_=0, selectedChain_=0, selectedMesh_=0, selectedVertex_=0;
    int retargetClip_=0, addRole_=0, addRoleBone_=0;
    int selectedMorphCurve_=0;
    int selectedRegionMesh_=0,regionCategoryFilter_=-1;
    bool isolateRegion_=false,regionEdits_=false;
    std::array<char,128> regionFilter_{};
    AnimationBakeOptions bakeOptions_;
    AnimationBakeReport bakeReport_;
    float cameraVerticalFovDegrees_=43.f;
    float yaw_=0.12f,pitch_=0.05f,distance_=4.7f;
    EditorPreview::DianaCameraReference referenceCamera_;
    bool referenceCameraEnabled_=false;
    bool referenceLightingReapplied_=false,captureCameraMetadata_=false;
    Json::Value completedCameraReferenceMetadata_;
    struct ReferenceCameraSnapshot {
        glm::dmat4 placement{1};glm::vec3 focus{0};
        float yaw=.12f,pitch=.05f,distance=4.7f,verticalFov=43.f;
        int trackedBone=-1;bool valid=false;
    } referenceCameraSnapshot_;
    bool cameraPanning_=false;
    glm::vec3 cameraFocus_{0};
    double time_=0;
    double hairRenderTime_=0;
    std::vector<float> manualMorphs_;
    std::vector<std::uint8_t> morphOverrides_;
    glm::vec3 ikTarget_{0};
    std::array<char,1024> sourcePath_{},packagePath_{},exportPath_{},retargetPath_{};
    std::array<char,1024> vmdPath_{},vmdFacePath_{},animationOutput_{};
    std::array<char,1024> outfitExport_{},replacementPackage_{},replacementCloth_{};
    std::array<char,128> animationName_{};
    std::array<char,128> boneFilter_{};
    std::string status_,lastError_;
    std::string gpuVendor_,gpuRenderer_;
    GLuint previewTexture_=0;
    std::uint64_t frameIndex_=0;
    std::uint32_t previewWidth_=0,previewHeight_=0;
    ImVec2 viewportOrigin_{},viewportSize_{800,700};
    ImVector<ImWchar> fontRanges_;
    FramePerformance performance_,displayPerformance_;
    struct GpuTimer {std::array<GLuint,2> query{};bool pending=false;};
    std::array<GpuTimer,4> gpuTimers_{};
    int activeGpuTimer_=-1;

    const ClothSimulationStats& ClothStats()const{static const ClothSimulationStats empty;if(clothWorker_)return clothSnapshot_?clothSnapshot_->stats:empty;return clothSimulator_.Stats();}
    const std::vector<glm::dvec3>& ClothPositions()const{static const std::vector<glm::dvec3> empty;if(clothWorker_)return clothSnapshot_?clothSnapshot_->positions:empty;return clothSimulator_.Positions();}
    bool AsyncPreview()const{return clothWorker_&&modular_&&((clothEnabled_&&showGarment_)||(hairAsset_&&hairEnabled_));}
    const SkeletalPose& DisplayPose()const{return AsyncPreview()&&clothSnapshot_&&clothSnapshot_->generation==clothGeneration_&&clothSnapshot_->pose.global.size()==asset_->skeleton.bones.size()?clothSnapshot_->pose:hairAsset_&&hairEnabled_&&!hairPose_.global.empty()?hairPose_:pose_;}
    void ClearClothPreview(){clothSnapshot_.reset();clothGeneration_=clothSnapshotRevision_=clothSnapshotSteps_=clothSnapshotIterations_=hairSnapshotSteps_=hairSnapshotSleepSteps_=0;clothSubmittedSeconds_=clothLagSeconds_=0;clothAnimationTimeline_.Clear();clothResumePending_=false;clothResumeSerial_=0;}

    void BeginGpuTimer() {
        // Poll previous frames without GL_QUERY_RESULT until the result is ready.
        // A busy GPU skips a sample instead of forcing a GPU/CPU rendezvous.
        for(auto& timer:gpuTimers_)if(timer.pending) {
            GLint ready=0;glGetQueryObjectiv(timer.query[1],GL_QUERY_RESULT_AVAILABLE,&ready);
            if(ready){GLuint64 start=0,end=0;glGetQueryObjectui64v(timer.query[0],GL_QUERY_RESULT,&start);glGetQueryObjectui64v(timer.query[1],GL_QUERY_RESULT,&end);performance_.gpu=double(end-start)/1e6;timer.pending=false;}
        }
        activeGpuTimer_=-1;
        for(std::size_t i=0;i<gpuTimers_.size();++i)if(!gpuTimers_[i].pending){activeGpuTimer_=int(i);glQueryCounter(gpuTimers_[i].query[0],GL_TIMESTAMP);break;}
    }
    void EndGpuTimer() {
        if(activeGpuTimer_<0)return;
        auto& timer=gpuTimers_[activeGpuTimer_];glQueryCounter(timer.query[1],GL_TIMESTAMP);timer.pending=true;activeGpuTimer_=-1;
    }

    template<class Function> void OnRender(Function&& function) {
        const auto started=PerfClock::now();
        auto done=std::make_shared<bool>(false);
        auto failure=std::make_shared<std::exception_ptr>();
        device_.async_ExecuteCode([function=std::forward<Function>(function),failure]() mutable {
            try { function(); } catch(...) { *failure=std::current_exception(); }
        },[done]{*done=true;});
        const auto deadline=std::chrono::steady_clock::now()+30s;
        while(!*done) {
            device_.returnSystem.DrainCallbacks();
            if(*done)break;
            Check(std::chrono::steady_clock::now()<deadline,"Timed out waiting for editor render task");
            device_.returnSystem.WaitForCallbacks(2ms);
        }
        performance_.sync+=Milliseconds(PerfClock::now()-started);
        if(*failure)std::rethrow_exception(*failure);
    }
    void SetText(auto& target,const std::string& text) {
        std::snprintf(target.data(),target.size(),"%s",text.c_str());
    }
    static int GeneratedClip(const SkeletalAssetPackage& asset,const char* name) {
        for(std::size_t i=0;i<asset.animations.size();++i)if(asset.animations[i].generated&&asset.animations[i].name==name)return int(i);
        return -1;
    }
    static const char* ClipKind(const AnimationSequenceAsset& clip) {
        return clip.generated?"Generated preview":clip.baked?"Baked animation":"Imported animation";
    }
    void AddAnimation(AnimationSequenceAsset sequence) {
        const auto existing=std::find_if(asset_->animations.begin(),asset_->animations.end(),[&](const auto& clip){return clip.id==sequence.id;});
        if(existing!=asset_->animations.end()){selectedClip_=int(existing-asset_->animations.begin());*existing=std::move(sequence);}
        else {selectedClip_=int(asset_->animations.size());asset_->animations.push_back(std::move(sequence));}
        time_=0;dirty_=true;bindPose_=false;retargetPlaying_=false;enableIk_=false;clothReset_=true;
        std::fill(morphOverrides_.begin(),morphOverrides_.end(),0);
        selection_=Selection::Animation;fontsDirty_=true;selectedMorphCurve_=0;
        double largestRange=-1;for(std::size_t i=0;i<asset_->animations[selectedClip_].morphTracks.size();++i){const auto& keys=asset_->animations[selectedClip_].morphTracks[i].keys;if(keys.empty())continue;double low=keys.front().value,high=low;for(const auto& key:keys){low=std::min(low,key.value);high=std::max(high,key.value);}if(high-low>largestRange){largestRange=high-low;selectedMorphCurve_=int(i);}}
        SetText(animationOutput_,Utf8(Path(MODEL_SOURCE_DIR)/"Asset/Generated/Animations"/(asset_->animations[selectedClip_].id.substr(asset_->animations[selectedClip_].id.find_last_of('/')+1)+".anim.json")));
    }
    void OpenAnimation(const fs::path& path) {
        std::string error;auto sequence=LoadAnimationSequenceAsset(path,*asset_,&error);Check(sequence.has_value(),error);
        AddAnimation(std::move(*sequence));status_="Loaded independent animation: "+Utf8(path);
    }
    void ImportMotion(const std::vector<fs::path>& paths,const std::string& name) {
        std::vector<fs::path> files;
        for(const auto& path:paths) {
            if(fs::is_directory(path)) {for(const auto& entry:fs::directory_iterator(path))if(entry.is_regular_file()) {auto extension=entry.path().extension().string();std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return char(std::tolower(c));});if(extension==".vmd")files.push_back(entry.path());}}
            else files.push_back(path);
        }
        std::sort(files.begin(),files.end());files.erase(std::unique(files.begin(),files.end()),files.end());
        std::vector<VmdMotion> motions;std::vector<std::string> skipped;std::string error;
        for(const auto& file:files) {auto motion=LoadVmdMotion(file,&error);Check(motion.has_value(),error);if(motion->bones.empty()&&motion->morphs.empty()){skipped.push_back("Skipped non-character VMD: "+Utf8(file.filename()));continue;}motions.push_back(std::move(*motion));}
        Check(!motions.empty(),"No bone/expression VMD motion in the selected files");
        auto motion=MergeVmdMotions(motions,name,&error);Check(motion.has_value(),error);
        auto sequence=BakeVmdAnimation(*motion,*asset_,bakeOptions_,name,&bakeReport_,&error);Check(sequence.has_value(),error);
        for(const auto& warning:skipped)sequence->diagnostics.push_back(warning);
        AddAnimation(std::move(*sequence));
        status_="VMD baked: "+std::to_string(bakeReport_.matchedBones)+" bone tracks, "+std::to_string(bakeReport_.matchedMorphs)+" morph tracks; "+std::to_string(bakeReport_.samples)+" samples";
    }
    const AnimationSequenceAsset* ActiveClip() const {
        const auto package=retargetPlaying_&&retargetSource_?retargetSource_:asset_;
        if(!package||package->animations.empty())return nullptr;
        if(bindPose_&&!retargetPlaying_) {
            const int bind=GeneratedClip(*package,"BindPose");return bind>=0?&package->animations[bind]:nullptr;
        }
        const int index=retargetPlaying_&&retargetSource_?retargetClip_:selectedClip_;
        return &package->animations[std::clamp(index,0,int(package->animations.size())-1)];
    }
    void RebuildFonts() {
        if(!guiReady_)return;
        // Called between frames, after all previous draw commands finished.
        // New source/retarget names contribute their exact Unicode glyphs.
        auto& io=ImGui::GetIO();io.Fonts->Clear();
        ImFontGlyphRangesBuilder glyphs;glyphs.AddRanges(io.Fonts->GetGlyphRangesDefault());
        glyphs.AddRanges(io.Fonts->GetGlyphRangesChineseSimplifiedCommon());glyphs.AddRanges(io.Fonts->GetGlyphRangesJapanese());
        glyphs.AddText("骨骼资产预览模型材质网格动画序列表情权重识别约束重映射数据详情播放暂停重置父节点搜索导入导出生成诊断报告");
        for(const auto& package:{asset_,retargetSource_})if(package) {
            glyphs.AddText(package->name.c_str());
            for(const auto& bone:package->skeleton.bones){glyphs.AddText(bone.name.c_str());glyphs.AddText(bone.englishName.c_str());}
            for(const auto& morph:package->morphs){glyphs.AddText(morph.name.c_str());glyphs.AddText(morph.englishName.c_str());}
            for(const auto& sequence:package->animations)glyphs.AddText(sequence.name.c_str());
            for(const auto& region:package->meshRegions) {
                glyphs.AddText(region.sourceObject.c_str());glyphs.AddText(region.sourceMesh.c_str());glyphs.AddText(region.sourceMaterial.c_str());glyphs.AddText(region.clothPinGroup.c_str());
                for(const auto* list:{&region.collections,&region.vertexGroups,&region.modifiers,&region.evidence})for(const auto& text:*list)glyphs.AddText(text.c_str());
            }
        }
        glyphs.BuildRanges(&fontRanges_);io.Fonts->TexDesiredWidth=2048;
        const fs::path font="C:/Windows/Fonts/msyh.ttc";
        if(fs::exists(font))io.Fonts->AddFontFromFileTTF(font.string().c_str(),16,nullptr,fontRanges_.Data);
        else io.Fonts->AddFontDefault();
        Check(io.Fonts->Build(),"Could not build editor font atlas");
        if(rendererReady_)OnRender([]{ImGui_ImplOpenGL3_DestroyFontsTexture();Check(ImGui_ImplOpenGL3_CreateFontsTexture(),"Could not refresh editor Unicode font texture");});
        fontsDirty_=false;
    }
    void AssignRole(const std::string& role,int bone) {
        for(auto& mapping:asset_->ikRig.humanoid)if(mapping.role!=role&&mapping.bone==bone&&bone>=0) {
            mapping.bone=-1;mapping.confidence=0;mapping.method="unmapped by one-to-one manual assignment";
        }
        auto existing=std::find_if(asset_->ikRig.humanoid.begin(),asset_->ikRig.humanoid.end(),[&](const auto& m){return m.role==role;});
        if(existing==asset_->ikRig.humanoid.end())asset_->ikRig.humanoid.push_back({role,bone,bone>=0?1.f:0.f,"manual override"});
        else {existing->bone=bone;existing->confidence=bone>=0?1.f:0.f;existing->method="manual override";}
        // Regenerate only the four automatically generated humanoid chains;
        // native authored PMX constraints and any custom chains are retained.
        std::erase_if(asset_->ikRig.chains,[](const auto& c){return !c.native&&(c.name=="leftArm"||c.name=="rightArm"||c.name=="leftLeg"||c.name=="rightLeg");});
        const auto rest=GlobalBoneTransforms(asset_->skeleton,RestPose(asset_->skeleton));
        auto find=[&](const std::string& wanted){for(const auto& m:asset_->ikRig.humanoid)if(m.role==wanted)return m.bone;return -1;};
        for(const auto side:{"left","right"})for(bool leg:{true,false}) {
            const std::string prefix(side);const int upper=find(prefix+(leg?"UpperLeg":"UpperArm")),lower=find(prefix+(leg?"LowerLeg":"LowerArm")),end=find(prefix+(leg?"Foot":"Hand"));
            if(upper<0||lower<0||end<0)continue;
            IkChain chain;chain.name=prefix+(leg?"Leg":"Arm");chain.role=prefix+(leg?"Foot":"Hand");chain.root=upper;chain.effector=end;
            chain.target=glm::dvec3(rest[end][3]);chain.iterations=48;chain.angleLimit=.18;
            IkLink distal;distal.bone=std::uint32_t(lower);IkLink proximal;proximal.bone=std::uint32_t(upper);
            chain.links={distal,proximal};asset_->ikRig.chains.push_back(std::move(chain));
        }
        if(!asset_->ikRig.chains.empty())selectedChain_=std::clamp(selectedChain_,0,int(asset_->ikRig.chains.size())-1);
        ResetIkTarget();
        // Preview tracks are authored from semantic roles, so repairing a role
        // must also move its diagnostic animation to the newly assigned bone.
        // Keep the selected source clip stable when removing/reappending a
        // generated clip, even when the package has duplicate clip names.
        const int previousClip=selectedClip_;
        int retainedClip=previousClip;
        bool selectedPreview=false;
        for(std::size_t i=0;i<asset_->animations.size();++i) {
            const auto& sequence=asset_->animations[i];
            if(!sequence.generated||sequence.name!="RigPreview")continue;
            if(int(i)==previousClip)selectedPreview=true;
            else if(int(i)<previousClip)--retainedClip;
        }
        std::erase_if(asset_->animations,[](const auto& sequence){return sequence.generated&&sequence.name=="RigPreview";});
        AddGeneratedPreviewSequences(*asset_);
        selectedClip_=selectedPreview?std::max(0,GeneratedClip(*asset_,"RigPreview")):
            std::clamp(retainedClip,0,int(asset_->animations.size())-1);
        if(retargetSource_)retargetMap_=BuildRetargetMap(retargetSource_->skeleton,retargetSource_->ikRig,asset_->skeleton,asset_->ikRig);
        retargetPreview_.reset();
        dirty_=true;status_="Assigned "+role+" -> "+BoneName(bone)+"; semantic map remains one-to-one";
    }
    static const std::vector<std::string>& CanonicalRoles() {
        static const std::vector<std::string> roles=[] {
            std::vector<std::string> result={"root","hips","spine","chest","upperChest","neck","head","jaw"};
            for(const auto side:{"left","right"}) {
                for(const auto part:{"Shoulder","UpperArm","LowerArm","Hand","UpperLeg","LowerLeg","Foot","Toes","Eye"})result.push_back(std::string(side)+part);
                for(const auto finger:{"Thumb","Index","Middle","Ring","Little"})for(const auto segment:{"Proximal","Intermediate","Distal"})result.push_back(std::string(side)+finger+segment);
            }
            return result;
        }();return roles;
    }
    void LoadRetargetSource(const fs::path& path) {
        std::string error;auto other=LoadSkeletalAssetPackage(path,&error);Check(bool(other),error);
        auto map=BuildRetargetMap(other->skeleton,other->ikRig,asset_->skeleton,asset_->ikRig);
        retargetSource_=std::move(other);retargetMap_=std::move(map);
        retargetPreview_.reset();
        retargetClip_=std::max(0,GeneratedClip(*retargetSource_,"RigPreview"));
        for(std::size_t i=0;i<retargetSource_->animations.size();++i)if(!retargetSource_->animations[i].generated){retargetClip_=int(i);break;}
        time_=0;dirty_=true;fontsDirty_=true;if(retargetSource_->animations.empty())retargetPlaying_=false;
        SetText(retargetPath_,Utf8(path));status_="Retarget mapping ready";
    }
    void AdoptAsset(std::shared_ptr<SkeletalAssetPackage> candidate,const fs::path& path) {
        // Legacy packages have no reports. Preserve every imported/manual
        // report and fill only absent sections with offline recognition.
        const auto recognized=detail::RecognizeGarmentSections(*candidate);
        for(const auto& region:recognized)if(std::none_of(candidate->meshRegions.begin(),candidate->meshRegions.end(),[&](const auto& old){return old.mesh==region.mesh;}))candidate->meshRegions.push_back(region);
        const auto skinPrepared=skinProfile_.Prepare(candidate->geometry);
        std::string materialError;
        const auto materialGeometry=characterMaterials_?characterMaterials_->Prepare(skinPrepared.geometry,&characterMaterialReport_,&materialError):skinPrepared.geometry;
        Check(bool(materialGeometry),materialError);
        auto uploaded=models_.Upload(materialGeometry,ModelUploadOptions{.includeSecondUV=dianaPreview_});
        Check(bool(uploaded),models_.LastError());
        skinProfile_.Apply(*uploaded,skinPrepared);
        if(characterMaterials_){
            Check(characterMaterials_->Apply(*uploaded,&materialError),materialError);
            characterMaterialEdits_=uploaded->Source().materials;characterParameterEdits_.clear();
            for(std::size_t i=0;i<characterMaterialEdits_.size();++i){
                auto& values=characterParameterEdits_.emplace_back();
                for(const auto& entry:characterMaterials_->Entries())if(entry.name==characterMaterialEdits_[i].name)values=entry.parameters;
                if(disableEyeRefraction_&&values.contains("eyeRefractionStrength")){
                    values["eyeRefractionStrength"]=0.f;
                    Check(uploaded->Materials().SetParameter(uploaded->MaterialHandles()[i],"eyeRefractionStrength",0.f,&materialError),materialError);
                }
            }
        }
        garmentResource_.reset();modular_.reset();garmentAnimated_={};garmentDeformed_={};clothReset_=true;previousClothTime_=-1;
        hairAsset_.reset();hairSimulator_=HairSimulator{};hairPose_={};hairPath_.clear();hairReset_=true;hairSettingsPending_=false;previousHairTime_=-1;uploadedBodyPoseValid_=false;
        ClearClothPreview();
        resource_=std::move(uploaded);asset_=std::move(candidate);openedPath_=path;trackedCameraBone_=-1;
        if(referenceCameraEnabled_){referenceCameraEnabled_=false;referenceCameraSnapshot_.valid=false;cameraVerticalFovDegrees_=43.f;}
        skinPrepared_=skinPrepared;
        if(skinProfile_.loaded) {
            std::cout<<"Skin profile: "<<Utf8(skinProfile_.path)<<" | matched "<<skinPrepared_.materialIndices.size()<<" opaque materials | skin="<<(skinProfile_.settings.enabled?"on":"off")<<" | corrected roughness="<<skinProfile_.settings.roughness<<'\n';
            for(const auto i:skinPrepared_.materialIndices)std::cout<<"  Skin material: "<<resource_->Source().materials[i].name<<" | detail="<<(skinPrepared_.detailMaterials.contains(i)?"original resolution":"none")<<" | control="<<(skinPrepared_.controlMaterials.contains(i)?"RGB + G roughness baseline":"none")<<'\n';
            for(const auto& name:skinPrepared_.unmatchedNames)std::cout<<"  Skin profile name not present: "<<name<<'\n';
        }
        deformed_={};deformed_.meshes=asset_->geometry->meshes;deformed_.instances=asset_->geometry->instances;
        const auto bounds=asset_->geometry->bounds;
        const double height=std::max(bounds.Extent().y,1e-5);
        placement_=glm::scale(glm::dmat4(1),glm::dvec3(3./height))*glm::translate(glm::dmat4(1),-bounds.Center());
        cameraFocus_={0,0,0};distance_=4.7f;
        manualMorphs_.assign(asset_->morphs.size(),0);
        morphOverrides_.assign(asset_->morphs.size(),0);
        pose_=EvaluateSkeletalPose(*asset_,nullptr,0,false);
        selectedBone_=0;selectedClip_=std::max(0,GeneratedClip(*asset_,"RigPreview"));
        for(const auto& role:asset_->ikRig.humanoid)if(role.role=="head"&&role.bone>=0)selectedBone_=role.bone;
        selectedChain_=0;ResetIkTarget();time_=0;dirty_=true;retargetSource_.reset();retargetPreview_.reset();retargetMap_={};retargetPlaying_=false;
        selectedMesh_=selectedVertex_=0;addRoleBone_=selectedBone_;enableIk_=false;bindPose_=false;fontsDirty_=true;
        selectedRegionMesh_=0;regionCategoryFilter_=-1;isolateRegion_=regionEdits_=false;regionFilter_.fill(0);
        for(const auto& region:asset_->meshRegions)if(region.kind==MeshRegionKind::Garment){selectedRegionMesh_=int(region.mesh);break;}
        // An interchange source with no authored clip opens as a static asset
        // inspection. Do not skin/upload a high-resolution mesh continuously
        // just to play the generated diagnostic sequence.
        if((asset_->sourceFormat=="blend"||asset_->sourceFormat=="fbx")&&std::all_of(asset_->animations.begin(),asset_->animations.end(),[](const auto& clip){return clip.generated;})) {
            selectedClip_=std::max(0,GeneratedClip(*asset_,"BindPose"));bindPose_=true;playing_=false;
        }
        if(asset_->sourceFormat=="blend")if(const auto sourcePose=GeneratedClip(*asset_,"SourcePose");sourcePose>=0) {
            selectedClip_=sourcePose;bindPose_=false;playing_=false;
        }
        if(const auto shoesFit=GeneratedClip(*asset_,"ShoesFit");shoesFit>=0) {
            selectedClip_=shoesFit;bindPose_=false;playing_=false;
        }
        status_="Loaded "+asset_->name+" | "+std::to_string(asset_->skeleton.bones.size())+" bones, "+std::to_string(asset_->morphs.size())+" morphs";
        SetText(exportPath_,Utf8(Path(MODEL_SOURCE_DIR)/"Asset/Generated"/("Revision_"+asset_->id.substr(0,12))));
    }
    void Load(const fs::path& path,bool package) {
        std::string error;auto candidate=package?LoadSkeletalAssetPackage(path,&error):ImportSkeletalSource(path,{},&error);Check(bool(candidate),error);AdoptAsset(std::move(candidate),path);
    }
    void LoadCharacter(const fs::path& path) {
        std::string error;auto candidate=LoadModularCharacter(path,&error);Check(candidate.has_value(),error);
        auto uploaded=models_.Upload(candidate->garment->geometry);Check(bool(uploaded),models_.LastError());
        AdoptAsset(candidate->body,path);modular_=std::move(*candidate);garmentResource_=std::move(uploaded);clothSettings_=modular_->cloth.settings;
        if(modular_->hair){hairAsset_=std::make_shared<const HairAsset>(*modular_->hair);hairSettings_=hairAsset_->settings;hairPath_=(fs::is_directory(path)?path:path.parent_path())/modular_->assembly.hairPhysics;hairReset_=true;}
        showGarment_=modular_->assembly.slots.empty()||modular_->assembly.slots.front().enabled;showBody_=true;clothEnabled_=true;clothReset_=true;showBones_=false;
        selectedClip_=std::max(0,GeneratedClip(*asset_,"RigPreview"));for(std::size_t i=0;i<asset_->animations.size();++i)if(!asset_->animations[i].generated){selectedClip_=int(i);break;}
        selection_=Selection::Cloth;status_="Loaded modular character: body + independent skirt / shared skeleton";
        SetText(outfitExport_,Utf8(Path(MODEL_SOURCE_DIR)/"Asset/Generated/MahiroClothRevision"));
    }
    void LoadHair(const fs::path& path){
        std::string error;auto candidate=LoadHairAsset(path,*asset_,&error);Check(bool(candidate),error);
        hairAsset_=std::make_shared<const HairAsset>(std::move(*candidate));hairSettings_=hairAsset_->settings;hairSettingsPending_=false;
        hairPath_=path;hairReset_=true;hairEnabled_=true;hairPose_={};previousHairTime_=-1;
        if(modular_)modular_->hair=*hairAsset_;
        clothReset_=true;dirty_=true;selection_=Selection::Hair;
        SetText(hairExport_,Utf8(Path(MODEL_SOURCE_DIR)/"Asset/Generated/HairRevision.hair.json"));
        status_="Loaded independent hair physics: "+hairAsset_->name;
    }
    bool SharedHairPreview()const{return AsyncPreview();}
    const HairSimulationStats& HairStats()const{return SharedHairPreview()&&clothSnapshot_&&clothSnapshot_->hairEnabled?clothSnapshot_->hairStats:hairSimulator_.Stats();}
    const std::vector<glm::dvec3>& HairPositions()const{return SharedHairPreview()&&clothSnapshot_&&clothSnapshot_->hairEnabled?clothSnapshot_->hairPositions:hairSimulator_.Positions();}
    std::span<const HairSurfaceOffset> HairOffsets()const{
        if(SharedHairPreview())return clothSnapshot_&&clothSnapshot_->generation==clothGeneration_&&clothSnapshot_->hairEnabled?std::span<const HairSurfaceOffset>(clothSnapshot_->hairSurfaceOffsets):std::span<const HairSurfaceOffset>{};
        return hairAsset_&&hairEnabled_&&!hairPose_.global.empty()?std::span<const HairSurfaceOffset>(hairSimulator_.SurfaceOffsets()):std::span<const HairSurfaceOffset>{};
    }
    void UpdateBodySkin(const SkeletalPose& display,std::string& error){
        // A frozen pose or a presentation-only frame may be identical. Compare
        // the actual skin inputs: step counters alone miss animated subframes.
        const auto offsets=HairOffsets();
        const bool sameOffsets=uploadedHairSurfaceOffsets_.size()==offsets.size()&&std::equal(offsets.begin(),offsets.end(),uploadedHairSurfaceOffsets_.begin());
        if(uploadedBodyPoseValid_&&uploadedBodyPose_.global==display.global&&uploadedBodyPose_.morphWeights==display.morphWeights&&sameOffsets)return;
        Measure(performance_.skin,[&]{Check(SkinSkeletalMesh(*asset_,display,deformed_,&error),error);if(!offsets.empty())Check(ApplyHairSurfaceOffsets(*asset_,display,offsets,deformed_,&error),error);});
        Measure(performance_.upload,[&]{Check(resource_->UpdateGeometry(deformed_,&error),error);});
        uploadedBodyPose_=display;uploadedBodyPoseValid_=true;
        uploadedHairSurfaceOffsets_.assign(offsets.begin(),offsets.end());
    }
    void EquipGarment(const fs::path& packagePath,const fs::path& clothPath) {
        Check(modular_.has_value(),"Open a modular character first");std::string error;
        auto garment=LoadSkeletalAssetPackage(packagePath,&error);Check(bool(garment),error);Check(GarmentCompatible(*asset_,*garment,&error),error);
        auto cloth=LoadClothAsset(clothPath,*garment,&error);Check(cloth.has_value(),error);
        auto uploaded=models_.Upload(garment->geometry);Check(bool(uploaded),models_.LastError());
        modular_->garment=std::move(garment);modular_->cloth=std::move(*cloth);garmentResource_=std::move(uploaded);clothSettings_=modular_->cloth.settings;
        if(!modular_->assembly.slots.empty()){modular_->assembly.slots.front().garmentId=modular_->garment->id;modular_->assembly.slots.front().enabled=true;}
        garmentAnimated_={};garmentDeformed_={};showGarment_=true;clothReset_=true;dirty_=true;status_="Equipped compatible garment; body and animation remain active";
        ClearClothPreview();
    }
    void ResetIkTarget() {
        if(!asset_||asset_->ikRig.chains.empty())return;
        selectedChain_=std::clamp(selectedChain_,0,int(asset_->ikRig.chains.size())-1);
        const auto& chain=asset_->ikRig.chains[selectedChain_];
        if(chain.targetBone>=0&&std::size_t(chain.targetBone)<pose_.global.size())ikTarget_=glm::vec3(pose_.global[chain.targetBone][3]);
        else ikTarget_=glm::vec3(chain.target);
    }
    std::string BoneName(int index) const {
        return index>=0&&std::size_t(index)<asset_->skeleton.bones.size()?asset_->skeleton.bones[index].name:"<none>";
    }
    void Evaluate(double delta) {
        Check(strandHair_.Step(delta),strandHair_.Error());
        const bool asyncCloth=AsyncPreview();
        bool previewSubmissionAccepted=false;
        if(hairWasShared_!=asyncCloth){hairWasShared_=asyncCloth;hairReset_=true;hairPose_={};clothReset_=true;if(clothWorker_)clothWorker_->SetPaused(!asyncCloth||clothFrozen_);}
        const bool warmingCloth=asyncCloth&&(clothReset_||!clothSnapshot_||clothResumePending_);
        if(playing_&&!warmingCloth){time_+=delta;performance_.animationSeconds=delta;}
        const bool updatePose=playing_||dirty_;
        if(!updatePose&&!(modular_&&clothEnabled_&&showGarment_&&(!clothFrozen_||asyncCloth))&&!(hairAsset_&&hairEnabled_))return;
        std::string error;
        bool sampleLoop=loop_;
        if(const auto clip=ActiveClip();!playing_&&clip&&std::abs(time_-clip->duration)<1e-9)sampleLoop=false;
        if(updatePose) {
        const auto poseStarted=PerfClock::now();
        const auto oldGlobal=!playing_?pose_.global:std::vector<glm::dmat4>{};
        const auto oldMorphs=!playing_?pose_.morphWeights:std::vector<float>{};
        if(retargetPlaying_&&retargetSource_&&!retargetSource_->animations.empty()) {
            if(!retargetPreview_||retargetPreviewClip_!=retargetClip_||retargetPreviewRate_!=bakeOptions_.framesPerSecond||retargetPreviewScale_!=bakeOptions_.translationScale) {
                std::string error;auto preview=BakeRetargetedAnimation(*retargetSource_,retargetSource_->animations[retargetClip_],*asset_,retargetMap_,bakeOptions_,"Retarget preview",nullptr,&error);
                if(preview){retargetPreview_=std::move(*preview);retargetPreviewClip_=retargetClip_;retargetPreviewRate_=bakeOptions_.framesPerSecond;retargetPreviewScale_=bakeOptions_.translationScale;}
                else{retargetPlaying_=false;lastError_="Retarget preview: "+error;pose_=EvaluateSkeletalPose(*asset_,nullptr,0,false);}
            }
            if(retargetPlaying_)pose_=EvaluateSkeletalPose(*asset_,&*retargetPreview_,time_,sampleLoop);
        } else {
            auto clip=bindPose_||asset_->animations.empty()?nullptr:&asset_->animations[std::clamp(selectedClip_,0,int(asset_->animations.size())-1)];
            pose_=EvaluateSkeletalPose(*asset_,clip,time_,sampleLoop);
        }
        for(std::size_t i=0;i<manualMorphs_.size();++i)
            if(morphOverrides_[i])pose_.morphWeights[i]=manualMorphs_[i];
        if(enableIk_&&!asset_->ikRig.chains.empty())Check(SolveIk(asset_->skeleton,asset_->ikRig.chains[selectedChain_],glm::dvec3(ikTarget_),pose_,&error),error);
        performance_.pose+=Milliseconds(PerfClock::now()-poseStarted);
        // The first resume evaluation moves the input clock back to its retained
        // completed pose. Later paused edits/seeks still require a fresh solve.
        const bool internalClockRebase=clothResumePending_&&!clothResumeSerial_;
        if(!playing_&&!internalClockRebase&&(oldGlobal!=pose_.global||oldMorphs!=pose_.morphWeights)){clothReset_=true;hairReset_=true;}
        }
        if(!asyncCloth){
            if(hairAsset_&&hairEnabled_){
                const auto active=ActiveClip();const double hairTime=sampleLoop&&active&&active->duration>0?std::fmod(time_,active->duration):time_;
                const bool jump=previousHairTime_>=0&&(hairTime<previousHairTime_-1e-8||std::abs(hairTime-previousHairTime_)>std::max(.25,delta*2));
                Measure(performance_.hair,[&]{
                    if(hairReset_||jump)Check(hairSimulator_.Reset(*hairAsset_,*asset_,pose_,&error),error);
                    const auto before=hairSimulator_.Stats().steps;
                    const auto beforeSleep=hairSimulator_.Stats().sleepTicks;
                    Check(hairSimulator_.Step(*hairAsset_,*asset_,pose_,hairFrozen_?0:delta,hairSettings_,hairPose_,&error),error);
                    const auto after=hairSimulator_.Stats().steps;performance_.hairSteps=after>=before?after-before:after;
                    const auto afterSleep=hairSimulator_.Stats().sleepTicks;performance_.hairSleepSteps=afterSleep>=beforeSleep?afterSleep-beforeSleep:afterSleep;
                });
                hairReset_=false;previousHairTime_=hairTime;performance_.hairPenetration=hairSimulator_.Stats().maxSurfacePenetration;performance_.hairAttachmentDrift=hairSimulator_.Stats().maxAttachmentDrift;
            }
            if(updatePose||(hairAsset_&&hairEnabled_)){
                UpdateBodySkin(DisplayPose(),error);
            }
        }
        if(modular_&&garmentResource_&&(showGarment_||asyncCloth)) {
            if(updatePose||garmentAnimated_.meshes.empty())Measure(performance_.skin,[&]{Check(SkinSkeletalMesh(*modular_->garment,pose_,garmentAnimated_,&error),error);});
            // Apply overwrites positions/normals/tangents for the cloth bindings.
            // While the pose is stationary, reuse the output's topology and UVs.
            if(!asyncCloth&&(updatePose||garmentDeformed_.meshes.empty()))Measure(performance_.apply,[&]{garmentDeformed_=garmentAnimated_;});
            const auto clip=ActiveClip();const double duration=clip?clip->duration:0;
            double sampleTime=sampleLoop&&duration>0?std::fmod(time_,duration):time_;
            const bool discontinuity=previousClothTime_>=0&&(sampleTime<previousClothTime_-1e-8||std::abs(sampleTime-previousClothTime_)>std::max(.25,delta*2));
            if(asyncCloth){
                Measure(performance_.cloth,[&]{
                    if(clothFrozen_&&!updatePose&&!clothReset_&&!discontinuity&&clothGeneration_&&!clothResumePending_)return;
                    const double physicalDelta=clothFrozen_||warmingCloth||discontinuity?0:delta;
                    const auto activeHair=hairEnabled_?hairAsset_:nullptr;
                    const auto beforeSerial=clothWorker_->SubmissionSerial();
                    const auto generation=clothWorker_->Submit(modular_->cloth,pose_,garmentAnimated_,physicalDelta,clothSettings_,clothReset_||discontinuity,modular_->garment,activeHair,activeHair?asset_:nullptr,hairSettings_,clothEnabled_&&showGarment_);
                    if(clothWorker_->SubmissionSerial()==beforeSerial)return;
                    previewSubmissionAccepted=true;
                    if(generation!=clothGeneration_){clothGeneration_=generation;clothSnapshot_.reset();clothSnapshotRevision_=clothSnapshotSteps_=clothSnapshotIterations_=0;clothSubmittedSeconds_=0;clothAnimationTimeline_.Reset(0,time_);clothResumePending_=false;clothResumeSerial_=0;}
                    clothSubmittedSeconds_+=physicalDelta;
                    if(physicalDelta>0)clothAnimationTimeline_.Record(clothSubmittedSeconds_,time_);
                    if(clothResumePending_&&!clothResumeSerial_)clothResumeSerial_=clothWorker_->SubmissionSerial();
                });
                auto ready=clothWorker_->Latest();
                if(ready&&ready->generation==clothGeneration_&&ready->revision!=clothSnapshotRevision_){
                    Check(ready->error.empty(),ready->error);
                    if(clothResumePending_&&clothResumeSerial_&&ready->serial>=clothResumeSerial_){
                        // A pause can interrupt a two-tick job after our first
                        // Latest read. Acknowledge the zero-time rebase before
                        // discarding its original animation-time mapping.
                        time_=clothAnimationTimeline_.Rebase(ready->simulatedSeconds,time_);
                        clothSubmittedSeconds_=ready->simulatedSeconds;
                        clothResumePending_=false;clothResumeSerial_=0;
                        sampleTime=sampleLoop&&duration>0?std::fmod(time_,duration):time_;
                    }
                    clothAnimationTimeline_.Consume(ready->simulatedSeconds);
                    performance_.steps=ready->clothEnabled?(ready->totalFixedSteps>=clothSnapshotSteps_?ready->totalFixedSteps-clothSnapshotSteps_:ready->totalFixedSteps):0;
                    performance_.constraintIterations=ready->cumulativeIterations>=clothSnapshotIterations_?ready->cumulativeIterations-clothSnapshotIterations_:ready->cumulativeIterations;
                    performance_.simulationSeconds=performance_.steps*clothSettings_.fixedTimeStep;
                    performance_.clothWorker=ready->solveMilliseconds;
                    performance_.clothSnapshots=1;
                    if(ready->hairEnabled){
                        performance_.hairPenetration=ready->hairStats.maxSurfacePenetration;
                        performance_.hairAttachmentDrift=ready->hairStats.maxAttachmentDrift;
                        performance_.hairSteps=ready->hairStats.steps>=hairSnapshotSteps_?ready->hairStats.steps-hairSnapshotSteps_:ready->hairStats.steps;
                        performance_.hairSleepSteps=ready->hairStats.sleepTicks>=hairSnapshotSleepSteps_?ready->hairStats.sleepTicks-hairSnapshotSleepSteps_:ready->hairStats.sleepTicks;
                        if(performance_.hairSteps)performance_.hairWorker=ready->hairStats.simulationMilliseconds;
                        hairSnapshotSteps_=ready->hairStats.steps;
                        hairSnapshotSleepSteps_=ready->hairStats.sleepTicks;
                    }
                    ++asyncSnapshotsObserved_;asyncTotalSteps_+=performance_.steps;
                    asyncMaximumSurfacePenetration_=std::max(asyncMaximumSurfacePenetration_,ready->stats.maxSurfacePenetration/modular_->cloth.unitsPerMeter);
                    asyncMaximumResolvableSurfacePenetration_=std::max(asyncMaximumResolvableSurfacePenetration_,ready->stats.maxResolvableSurfacePenetration/modular_->cloth.unitsPerMeter);
                    asyncMaximumAttachmentError_=std::max(asyncMaximumAttachmentError_,ready->stats.maxAttachmentError/modular_->cloth.unitsPerMeter);
                    asyncMaximumCollapsedTriangles_=std::max(asyncMaximumCollapsedTriangles_,ready->stats.collapsedTriangles);
                    clothSnapshotRevision_=ready->revision;clothSnapshotSteps_=ready->totalFixedSteps;clothSnapshotIterations_=ready->cumulativeIterations;clothSnapshot_=std::move(ready);
                    // Body, garment and debug colliders use this same completed
                    // pose. Rendering a newer body would introduce false contact
                    // penetration even when the simulated frame was valid.
                    UpdateBodySkin(clothSnapshot_->pose,error);
                    Measure(performance_.apply,[&]{garmentDeformed_=clothSnapshot_->garment;});
                    Measure(performance_.upload,[&]{Check(garmentResource_->UpdateGeometry(garmentDeformed_,&error),error);});
                }
                if(!clothSnapshot_){
                    UpdateBodySkin(pose_,error);
                    Measure(performance_.apply,[&]{garmentDeformed_=garmentAnimated_;});
                    Measure(performance_.upload,[&]{Check(garmentResource_->UpdateGeometry(garmentDeformed_,&error),error);});
                }
                clothLagSeconds_=std::max(0.,clothSubmittedSeconds_-(clothSnapshot_?clothSnapshot_->simulatedSeconds:0.));performance_.clothLag=clothLagSeconds_*1000;
            }else if(clothEnabled_) {
                Measure(performance_.cloth,[&]{
                    if(clothReset_||discontinuity)Check(clothSimulator_.Reset(modular_->cloth,pose_,garmentAnimated_,&error),error);
                    const auto previousSteps=clothSimulator_.Stats().steps;
                    Check(clothSimulator_.Step(modular_->cloth,pose_,garmentAnimated_,clothFrozen_?0:delta,clothSettings_,&error),error);
                    const auto currentSteps=clothSimulator_.Stats().steps;
                    performance_.steps=currentSteps>=previousSteps?currentSteps-previousSteps:currentSteps;
                    performance_.constraintIterations=clothSimulator_.Stats().constraintIterations;
                    performance_.simulationSeconds=performance_.steps*clothSettings_.fixedTimeStep;
                });
                Measure(performance_.apply,[&]{Check(clothSimulator_.Apply(modular_->cloth,garmentDeformed_,&error),error);});
            }
            if(!asyncCloth)Measure(performance_.upload,[&]{Check(garmentResource_->UpdateGeometry(garmentDeformed_,&error),error);});
            if(!asyncCloth||previewSubmissionAccepted){clothReset_=false;previousClothTime_=sampleTime;}
        }
        dirty_=false;
    }
    const MeshRegionInfo* RegionInfo(std::uint32_t mesh)const {
        const auto found=std::find_if(asset_->meshRegions.begin(),asset_->meshRegions.end(),[&](const auto& region){return region.mesh==mesh;});
        return found==asset_->meshRegions.end()?nullptr:&*found;
    }
    bool RegionPreviewFilter()const{return isolateRegion_||regionCategoryFilter_>=0;}
    bool RegionVisible(std::uint32_t mesh)const {
        if(isolateRegion_&&mesh!=std::uint32_t(selectedRegionMesh_))return false;
        const auto* region=RegionInfo(mesh);
        const auto kind=region?region->kind:MeshRegionKind::Unknown;
        return regionCategoryFilter_<0||int(kind)==regionCategoryFilter_;
    }
    void FramePreviewRegions(int kind=-1,bool visibleOnly=false) {
        Model::Bounds bounds;
        // Eight cached bound corners per instance, independent of high-poly
        // vertex count. Keep the model placement unchanged when focusing.
        for(const auto& instance:deformed_.instances) {
            if(instance.meshIndex>=deformed_.meshes.size())continue;
            if(visibleOnly&&!RegionVisible(instance.meshIndex))continue;
            const auto* region=RegionInfo(instance.meshIndex);
            if(kind>=0&&(!region||int(region->kind)!=kind))continue;
            const auto& local=deformed_.meshes[instance.meshIndex].bounds;
            if(!local.valid)continue;
            const auto transform=placement_*instance.transform;
            for(unsigned corner=0;corner<8;++corner)bounds.Include(glm::dvec3(transform*glm::dvec4(
                corner&1?local.maximum.x:local.minimum.x,
                corner&2?local.maximum.y:local.minimum.y,
                corner&4?local.maximum.z:local.minimum.z,1)));
        }
        if(!bounds.valid){status_="No matching visible sections to frame";return;}
        trackedCameraBone_=-1;cameraFocus_=glm::vec3(bounds.Center());
        distance_=std::clamp(float(glm::length(bounds.Extent())*.56/std::sin(glm::radians(21.5))),1.f,20.f);
    }
    void AppendPreviewBody(Render::RenderFrame& frame,std::string& error)const {
        if(!RegionPreviewFilter()){Check(resource_->AppendTo(frame,placement_,glm::dvec3(0),camera_.view,&error),error);return;}
        Render::RenderFrame extracted;
        Check(resource_->AppendTo(extracted,placement_,glm::dvec3(0),camera_.view,&error),error);
        const auto& instances=resource_->Source().instances;
        // AppendTo emits one item per source instance, in source order; filter
        // whole draw packets without touching geometry, materials or physics.
        Check(extracted.items.size()==instances.size(),"Preview section filtering requires one draw item per source instance");
        for(std::size_t i=0;i<instances.size();++i)if(RegionVisible(instances[i].meshIndex))frame.items.push_back(std::move(extracted.items[i]));
    }
    const std::string& PipelineError()const{return pbrEnabled_?pbr_.LastError():toon_.LastError();}
    std::uint64_t PipelineToken()const{return pbrEnabled_?pbr_.RecordedFrameToken():toon_.RecordedFrameToken();}
    void DiscardPipelineFrame(std::uint64_t token){if(pbrEnabled_)pbr_.DiscardFrame(token);else toon_.DiscardFrame(token);}
    void SelectPipeline(bool pbr) {
        if(pbr&&!pbrReady_){ConfigurePbrPreview();Check(pbr_.Initialize(),pbr_.LastError());pbrReady_=true;}
        if(!pbr&&!toonReady_){Check(toon_.Initialize(),toon_.LastError());toonReady_=true;}
        pbrEnabled_=pbr;
    }
    void ConfigurePbrPreview() {
        // Neutral studio lighting with side/back comparison presets. Optional
        // skin profiles apply recovered controls only to exact opaque material
        // names; source assets stay unchanged. Strands use their Render pass.
        pbrSettings_.shadows.enabled=false;pbrSettings_.shadows.atlasResolution=512;
        pbrSettings_.shadows.sunIntensity=0;
        pbrSettings_.reflection.enabled=false;
        pbrSettings_.post.bloomEnabled=false;pbrSettings_.post.ssaoEnabled=false;
        pbrSettings_.post.toneMap=Render::ToneMapMode::ACES;
        pbrSettings_.post.exposure=1.35f;pbrSettings_.post.gamma=2.2f;
        pbrSettings_.temporal.antialiasing=Render::AntialiasingMode::FXAA;
        pbrSettings_.temporal.msaaSamples=1;pbrSettings_.temporal.motionBlurEnabled=false;
        pbrSettings_.indirect.enabled=false;pbrSettings_.probes.enabled=false;
        pbrSettings_.realtimeGi.enabled=false;pbrSettings_.lumenGi.enabled=false;
        pbrSettings_.screenProbePlacement.enabled=false;pbrSettings_.surfaceCacheGi.enabled=false;
        pbrSettings_.areaLights.count=0;pbrSettings_.spotLight.enabled=false;
        pbrSettings_.sky.enabled=true;pbrSettings_.sky.background=true;pbrSettings_.sky.intensity=1.0f;
        pbrSettings_.sky.environment=Render::MakeProceduralSkyEnvironment(glm::vec3(.38f),glm::vec3(.25f),glm::vec3(.12f));
        pbrSettings_.lighting.ambientAndExposure=glm::vec4(.025f,.025f,.025f,1);
        if(dianaPreview_){
            pbrSettings_.shadows.enabled=true;pbrSettings_.shadows.atlasResolution=2048;pbrSettings_.shadows.distance=8;
            pbrSettings_.shadows.sunIntensity=1.2f;pbrSettings_.shadows.sunDirection={-.45f,-.8f,-.35f};
            pbrSettings_.shadows.sunColor={1,.94f,.88f};
            pbrSettings_.shadows.depthBiasTexels=.4f;pbrSettings_.shadows.normalBiasTexels=.6f;
            pbrSettings_.temporal.antialiasing=Render::AntialiasingMode::TAA;
            pbrSettings_.skinDiffusion.enabled=!disableSkinDiffusion_;
            pbrSettings_.skinDiffusion.albedoPreservation=1.f;
        }
        ApplyLightingPreset();
    }
    void ApplyLightingPreset() {
        if(referenceCameraEnabled_)referenceLightingReapplied_=true;
        static const auto studio=Render::MakeProceduralSkyEnvironment(glm::vec3(.38f),glm::vec3(.25f),glm::vec3(.12f));
        // An offline environment study based on the supplied screenshot, not
        // extracted game illumination. All pixels remain scene-linear.
        static const auto reference=[] {
            constexpr unsigned width=128,height=64;
            std::array<std::shared_ptr<const Render::SkyEnvironment>,2> variants;
            const auto cool=glm::normalize(glm::vec3(-.65f,.65f,.4f));
            const auto warm=glm::normalize(glm::vec3(.7f,.35f,-.6f));
            // Host lighting calibration, not a recovered game scene. Match
            // Rec.709 incoming luminance, while testing the cool lobe's tint.
            constexpr float neutralLobe=.90f*.2126f+1.22f*.7152f+1.34f*.0722f; // 1.160632
            for(unsigned variant=0;variant<variants.size();++variant){
                std::vector<glm::vec3> pixels(width*height);
                const auto key=variant==0?glm::vec3(.90f,1.22f,1.34f):glm::vec3(neutralLobe);
                for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){
                    const float theta=(float(y)+.5f)*3.14159265359f/height,phi=(float(x)+.5f)*6.28318530718f/width;
                    const glm::vec3 d(std::sin(theta)*std::cos(phi),std::cos(theta),std::sin(theta)*std::sin(phi));
                    pixels[y*width+x]=glm::vec3(.008f,.012f,.012f)+
                        key*std::pow(std::max(glm::dot(d,cool),0.f),3.f)+
                        glm::vec3(.40f,.30f,.16f)*std::pow(std::max(glm::dot(d,warm),0.f),10.f);
                }
                variants[variant]=Render::BakeSkyEnvironment(width,height,pixels);
            }
            return variants;
        }();
        pbrSettings_.sky.environment=(lightingPreset_==3||lightingPreset_==5)?reference[lightingPreset_==5?1:0]:studio;
        pbrSettings_.strandIndirect={};
        pbrSettings_.sky.rotation=0;
        pbrSettings_.post.exposure=1.35f;
        pbrSettings_.shadows.sunIntensity=dianaPreview_?1.2f:0.f;
        if(dianaPreview_){pbrSettings_.shadows.sunDirection={-.45f,-.8f,-.35f};pbrSettings_.shadows.sunColor={1,.94f,.88f};}
        pbrSettings_.lighting.ambientAndExposure=glm::vec4(.025f,.025f,.025f,1);
        pbrSettings_.sky.intensity=1.f;
        pbrSettings_.lighting.lightPositions={glm::vec4(3,4,5,1),glm::vec4(-4,2,3,1),glm::vec4(0,3,-4,1),glm::vec4(0,-2,3,1)};
        pbrSettings_.lighting.lightColors={glm::vec4(60,57,54,1.1f),glm::vec4(28,30,33,1.3f),glm::vec4(35,35,35,1),glm::vec4(6,6,6,1)};
        if(lightingPreset_==1) {
            pbrSettings_.lighting.ambientAndExposure=glm::vec4(.008f,.008f,.008f,1);pbrSettings_.sky.intensity=.28f;
            pbrSettings_.lighting.lightPositions={glm::vec4(4,2.2f,1.5f,1),glm::vec4(-4,2,1,1),glm::vec4(0,3,-4,1),glm::vec4(0,-2,3,1)};
            pbrSettings_.lighting.lightColors={glm::vec4(70,65,60,1.1f),glm::vec4(3,3.2f,3.6f,1.3f),glm::vec4(6,6,6,1),glm::vec4(1,1,1,1)};
        } else if(lightingPreset_==2) {
            pbrSettings_.lighting.ambientAndExposure=glm::vec4(.006f,.006f,.006f,1);pbrSettings_.sky.intensity=.18f;
            pbrSettings_.lighting.lightPositions={glm::vec4(0,2,-3,1),glm::vec4(0,2,4,1),glm::vec4(-4,2,0,1),glm::vec4(4,2,0,1)};
            pbrSettings_.lighting.lightColors={glm::vec4(75,65,55,1.1f),glm::vec4(8,8,8,1.3f),glm::vec4(2,2,2,1),glm::vec4(2,2,2,1)};
        } else if(lightingPreset_==3||lightingPreset_==5) {
            pbrSettings_.lighting.ambientAndExposure=glm::vec4(.006f,.007f,.007f,1);
            pbrSettings_.lighting.lightPositions={glm::vec4(-3,3,3,1),glm::vec4(3,2,-3,1),glm::vec4(3,1,3,1),glm::vec4(0,-2,3,1)};
            pbrSettings_.lighting.lightColors={glm::vec4(39,48,52,1.5f),glm::vec4(22,17,9,1),glm::vec4(4,4.5f,5,1.3f),glm::vec4(1,1,1,1)};
            if(lightingPreset_==5){
                constexpr float neutralKey=39.f*.2126f+48.f*.7152f+52.f*.0722f; // 46.3754
                pbrSettings_.lighting.lightColors[0]=glm::vec4(glm::vec3(neutralKey),1.5f);
            }
            if(dianaPreview_){pbrSettings_.shadows.sunIntensity=.65f;pbrSettings_.shadows.sunDirection={.6f,-.7f,-.4f};pbrSettings_.shadows.sunColor={.83f,.93f,1};}
        }
        // Environment selection is independent of the direct-light rig, so
        // source/procedural A/Bs keep identical sun and point illumination.
        if(lightingPreset_==4||sourceEnvironmentOverride_) {
            if(!sourceHairLighting_)sourceHairLighting_=EditorPreview::HairLightingProfile::Load(hairLightingPath_);
            pbrSettings_.sky.environment=sourceHairLighting_->environment;
            pbrSettings_.strandIndirect=sourceHairLighting_->indirect;
            if(hairHostAmbientScaleOverride_>=0)pbrSettings_.strandIndirect.hostAmbientScale=hairHostAmbientScaleOverride_;
            pbrSettings_.sky.intensity=std::exp2(pbrSettings_.strandIndirect.exposureEV);
            pbrSettings_.sky.rotation=pbrSettings_.strandIndirect.rotation;
        }
        // Orient once when selecting a preset. Subsequent camera orbits keep
        // world-space lights fixed so the material's moving highlights remain visible.
        const glm::vec3 right(std::cos(yaw_),0,-std::sin(yaw_));
        const glm::vec3 up(0,1,0),forwardToCamera(std::sin(yaw_),0,std::cos(yaw_));
        for(auto& position:pbrSettings_.lighting.lightPositions) {
            const glm::vec3 local(position);
            position=glm::vec4(cameraFocus_+right*local.x+up*local.y+forwardToCamera*local.z,position.w);
        }
        ApplySourceDirectLighting();
        ApplyDirectInputScales(true);
        if(pbrReady_)pbr_.ResetHistory();
    }
    void ApplyDirectInputScales(bool refresh){
        auto& source=pbrSettings_.strandSourceDirect;
        if(refresh){
            unscaledDirectSunIntensity_=pbrSettings_.shadows.sunIntensity;
            for(unsigned i=0;i<4;++i)unscaledDirectPoints_[i]=glm::vec3(pbrSettings_.lighting.lightColors[i]);
            for(unsigned i=0;i<source.count;++i)unscaledDirectSourcePoints_[i]=glm::vec3(source.lights[i].colorTolerance);
        }
        // Scaling a cached input preserves world-space light placement after
        // camera orbits and cannot accumulate across slider/source changes.
        pbrSettings_.shadows.sunIntensity=unscaledDirectSunIntensity_*directSunScale_;
        for(unsigned i=0;i<4;++i){auto& color=pbrSettings_.lighting.lightColors[i];
            color=glm::vec4(unscaledDirectPoints_[i]*directPunctualScale_,color.w);
        }
        for(unsigned i=0;i<source.count;++i){auto& color=source.lights[i].colorTolerance;
            color=glm::vec4(unscaledDirectSourcePoints_[i]*directPunctualScale_,color.w);
        }
    }
    void ApplySourceDirectLighting(){
        if(!sourceDirectOverride_){pbrSettings_.strandSourceDirect={};return;}
        if(!sourceHairDirect_)sourceHairDirect_=EditorPreview::HairDirectLightingProfile::Load(hairDirectPath_);
        // Lighting stays in the scene when the reference profile tilts the
        // character. Do not rotate the source lights with the presentation pose.
        const auto& base=referenceCameraSnapshot_.valid?referenceCameraSnapshot_.placement:placement_;
        appliedSourceDirect_=sourceHairDirect_->Apply(base);
        pbrSettings_.strandSourceDirect=appliedSourceDirect_.hair;
        pbrSettings_.shadows.sunDirection=appliedSourceDirect_.sunDirection;
        pbrSettings_.shadows.sunColor=appliedSourceDirect_.sunColor;
        pbrSettings_.shadows.sunIntensity=appliedSourceDirect_.sunIntensity;
        // Opaque PBR retains its existing four-light interface. Its local
        // proxy is documented separately; hair consumes ALL source candidates.
        pbrSettings_.lighting.lightPositions=appliedSourceDirect_.pointPositions;
        pbrSettings_.lighting.lightColors=appliedSourceDirect_.pointColors;
    }
    void ApplyEnvironmentPreservingDirect(){
        const auto lighting=pbrSettings_.lighting;
        const auto shadows=pbrSettings_.shadows;
        const auto exposure=pbrSettings_.post.exposure;
        const auto sourceDirect=pbrSettings_.strandSourceDirect;
        const auto applied=appliedSourceDirect_;
        const auto rawSun=unscaledDirectSunIntensity_;
        const auto rawPoints=unscaledDirectPoints_;
        const auto rawSource=unscaledDirectSourcePoints_;
        const auto restore=[&]{
            pbrSettings_.lighting=lighting;pbrSettings_.shadows=shadows;pbrSettings_.post.exposure=exposure;
            pbrSettings_.strandSourceDirect=sourceDirect;appliedSourceDirect_=applied;
            unscaledDirectSunIntensity_=rawSun;unscaledDirectPoints_=rawPoints;unscaledDirectSourcePoints_=rawSource;
        };
        try {ApplyLightingPreset();}catch(...){restore();throw;}
        restore();
    }
    void ApplyDirectPreservingEnvironment(){
        // Selecting only a direct-light input must not reset manually edited
        // environment gain/rotation, probe coefficients, ambient or display.
        const auto sky=pbrSettings_.sky;
        const auto indirect=pbrSettings_.strandIndirect;
        const auto post=pbrSettings_.post;
        const auto ambient=pbrSettings_.lighting.ambientAndExposure;
        const auto restore=[&]{pbrSettings_.sky=sky;pbrSettings_.strandIndirect=indirect;
            pbrSettings_.post=post;pbrSettings_.lighting.ambientAndExposure=ambient;};
        try {ApplyLightingPreset();}catch(...){restore();throw;}
        restore();
    }
    void RenderPreview(std::uint32_t width,std::uint32_t height) {
        // Source geometry is in metres; placement scales both the model and
        // the physical diffusion radius, independently of camera distance.
        pbrSettings_.skinDiffusion.worldUnitScale=float(glm::length(glm::dvec3(placement_[0])));
        Check(pbrEnabled_?pbr_.Resize(width,height,pbrSettings_.shadows.atlasResolution,1):toon_.Resize(width,height,1024,4),PipelineError());
        if(trackedCameraBone_>=0&&std::size_t(trackedCameraBone_)<DisplayPose().global.size())cameraFocus_=glm::vec3(placement_*glm::dvec4(glm::dvec3(DisplayPose().global[trackedCameraBone_][3]),1));
        camera_.position=cameraFocus_+glm::vec3(distance_*std::cos(pitch_)*std::sin(yaw_),distance_*std::sin(pitch_),distance_*std::cos(pitch_)*std::cos(yaw_));
        camera_.view=glm::lookAt(camera_.position,cameraFocus_,glm::vec3(0,1,0));
        camera_.nearPlane=.03f;camera_.farPlane=100;
        camera_.projection=glm::perspective(glm::radians(cameraVerticalFovDegrees_),float(width)/height,camera_.nearPlane,camera_.farPlane);
        if(referenceCameraEnabled_)camera_.projection=EditorPreview::DianaReferenceProjection(camera_.projection,referenceCamera_);
        // Deformation updates keep the same mesh handles. Refresh the opaque
        // occupancy only when uploaded content actually changed, retaining TAA.
        if(pbrEnabled_){
            const auto bodyRevision=resource_->GeometryRevision();
            const auto garmentRevision=garmentResource_?garmentResource_->GeometryRevision():0;
            if(bodyRevision!=opaqueBodyGeometryRevision_||garmentRevision!=opaqueGarmentGeometryRevision_)pbr_.InvalidateOpaqueDepth();
            opaqueBodyGeometryRevision_=bodyRevision;opaqueGarmentGeometryRevision_=garmentRevision;
        }
        Render::RenderFrame frame;frame.frameIndex=++frameIndex_;
        std::string error;if(showBody_||!modular_)AppendPreviewBody(frame,error);
        Check(strandHair_.AppendTo(frame,device_,placement_),strandHair_.Error());
        if(modular_&&showGarment_&&garmentResource_&&!RegionPreviewFilter())Check(garmentResource_->AppendTo(frame,placement_,glm::dvec3(0),camera_.view,&error),error);
        Render::RHICommand::BeginFrame begin;begin.frameIndex=frame.frameIndex;begin.framebufferWidth=width;begin.framebufferHeight=height;
        auto encoder=device_.BeginFrame(begin);
        if(pbrEnabled_){pbrSettings_.time=float(time_);pbrSettings_.deltaSeconds=float(std::max(performance_.acceptedDelta,1./240));pbrSettings_.strandTimeSeconds=hairRenderTime_;}
        if(!resource_->KeepAlive(encoder)||(garmentResource_&&!garmentResource_->KeepAlive(encoder))||!(pbrEnabled_?pbr_.Record(encoder,frame,camera_,pbrSettings_):toon_.Record(encoder,frame,camera_,settings_))||!encoder.End(false)) {
            encoder.Cancel();if(auto token=PipelineToken())DiscardPipelineFrame(token);
            throw std::runtime_error("Preview frame recording failed: "+PipelineError());
        }
        const auto token=PipelineToken();
        // The UI backend and GPU timer share this synchronization point. There
        // is no framebuffer readback in the ordinary interactive preview.
        OnRender([this]{ImGui_ImplOpenGL3_NewFrame();BeginGpuTimer();});
        auto done=std::make_shared<bool>(false);
        if(!device_.async_SubmitFrameCommands(encoder.GetCommandBuffer(),[done]{*done=true;})) {
            encoder.Cancel();DiscardPipelineFrame(token);throw std::runtime_error("Could not submit editor preview frame");
        }
        auto deadline=std::chrono::steady_clock::now()+30s;
        const auto waitStarted=PerfClock::now();
        while(!*done){device_.returnSystem.DrainCallbacks();Check(std::chrono::steady_clock::now()<deadline,"Preview frame timed out");if(!*done)device_.returnSystem.WaitForCallbacks(2ms);}
        performance_.sync+=Milliseconds(PerfClock::now()-waitStarted);
        Check(pbrEnabled_?pbr_.CompleteFrame(token):toon_.CompleteFrame(token),PipelineError());
        strandHair_.CommitRenderedFrame(frame.frameIndex,pbrEnabled_);
        if(pbrEnabled_){const auto handle=pbr_.DebugTextures().displayColor;OnRender([this,handle]{EndGpuTimer();const auto texture=device_.GetResourcePool().TextureTable.Get(handle);Check(texture!=nullptr,"Missing PBR display texture");previewTexture_=texture->rhi_id;});}
        else {
            auto handle=toon_.DebugTextures().sceneColor;
            OnRender([this,handle]{EndGpuTimer();auto texture=device_.GetResourcePool().TextureTable.Get(handle);Check(texture!=nullptr,"Missing preview texture");previewTexture_=texture->rhi_id;});
        }
        previewWidth_=width;previewHeight_=height;
        if(captureCameraMetadata_||referenceCameraEnabled_)completedCameraReferenceMetadata_=BuildCameraReferenceMetadata();
    }
    std::optional<ImVec2> Project(const glm::dvec3& point) const {
        auto p=camera_.projection*camera_.view*glm::vec4(placement_*glm::dvec4(point,1));
        if(p.w<=0||!std::isfinite(p.w))return {};
        p/=p.w;
        return ImVec2(viewportOrigin_.x+(p.x*.5f+.5f)*viewportSize_.x,viewportOrigin_.y+(.5f-p.y*.5f)*viewportSize_.y);
    }
    void BoneOverlay() {
        if(!showBones_)return;
        const auto& pose=DisplayPose();
        auto* draw=ImGui::GetWindowDrawList();
        draw->PushClipRect(viewportOrigin_,ImVec2(viewportOrigin_.x+viewportSize_.x,viewportOrigin_.y+viewportSize_.y),true);
        for(std::size_t i=0;i<pose.global.size();++i) {
            auto p=Project(glm::dvec3(pose.global[i][3]));if(!p)continue;
            const bool selected=int(i)==selectedBone_;
            const auto color=selected?IM_COL32(255,189,67,255):IM_COL32(30,174,217,155);
            int parent=asset_->skeleton.bones[i].parent;
            if(parent>=0&&std::size_t(parent)<pose.global.size())if(auto a=Project(glm::dvec3(pose.global[parent][3])))draw->AddLine(*a,*p,color,selected?2.5f:1.f);
            if(selected||i<100)draw->AddCircleFilled(*p,selected?4.f:1.8f,color);
            if(selected||showNames_)draw->AddText(ImVec2(p->x+5,p->y-10),selected?IM_COL32(152,83,0,255):IM_COL32(0,90,130,255),asset_->skeleton.bones[i].name.c_str());
            auto mouse=ImGui::GetIO().MousePos;
            if(ImGui::IsMouseClicked(0)&&!ImGui::IsMouseDragging(0)&&std::abs(mouse.x-p->x)<5&&std::abs(mouse.y-p->y)<5){selectedBone_=int(i);selection_=Selection::Skeleton;}
        }
        if(enableIk_&&!asset_->ikRig.chains.empty())if(auto target=Project(glm::dvec3(ikTarget_))) {
            draw->AddCircle(*target,8,IM_COL32(235,97,78,255),12,2);draw->AddText(ImVec2(target->x+10,target->y),IM_COL32(190,54,40,255),"IK target");
        }
        draw->PopClipRect();
    }
    void BoneTree(int index,int depth=0) {
        if(depth>256)return;
        const auto& bone=asset_->skeleton.bones[index];
        bool children=false;for(const auto& b:asset_->skeleton.bones)children|=b.parent==index;
        auto flags=ImGuiTreeNodeFlags_OpenOnArrow|ImGuiTreeNodeFlags_SpanAvailWidth;
        if(index==selectedBone_)flags|=ImGuiTreeNodeFlags_Selected;
        if(!children)flags|=ImGuiTreeNodeFlags_Leaf;
        if(depth<2)flags|=ImGuiTreeNodeFlags_DefaultOpen;
        ImGui::PushID(index);
        const bool open=ImGui::TreeNodeEx(bone.name.c_str(),flags);
        if(ImGui::IsItemClicked()&&!ImGui::IsItemToggledOpen()){selectedBone_=index;selection_=Selection::Skeleton;}
        if(open){for(std::size_t i=0;i<asset_->skeleton.bones.size();++i)if(asset_->skeleton.bones[i].parent==index)BoneTree(int(i),depth+1);ImGui::TreePop();}
        ImGui::PopID();
    }
    void ClothOverlay() {
        if(!modular_||(!showClothWire_&&!showClothColliders_))return;
        auto* draw=ImGui::GetWindowDrawList();draw->PushClipRect(viewportOrigin_,ImVec2(viewportOrigin_.x+viewportSize_.x,viewportOrigin_.y+viewportSize_.y),true);
        auto line=[&](glm::dvec3 a,glm::dvec3 b,ImU32 color,float width=1.f){const auto x=Project(a),y=Project(b);if(x&&y)draw->AddLine(*x,*y,color,width);};
        if(showClothWire_&&showGarment_&&clothEnabled_) {
            const auto& positions=ClothPositions();
            for(const auto& t:modular_->cloth.triangles)if(t[0]<positions.size()&&t[1]<positions.size()&&t[2]<positions.size())for(int i=0;i<3;++i)line(positions[t[i]],positions[t[(i+1)%3]],IM_COL32(165,75,210,155));
            for(std::size_t i=0;i<positions.size();++i)if(auto p=Project(positions[i]))draw->AddCircleFilled(*p,modular_->cloth.particles[i].inverseMass==0?3.f:1.5f,modular_->cloth.particles[i].inverseMass==0?IM_COL32(68,232,134,255):IM_COL32(255,172,75,220));
        }
        if(showClothColliders_)for(const auto& c:EvaluateClothCapsules(modular_->cloth,DisplayPose())) {
            glm::dvec3 axis=c.b-c.a;if(glm::length2(axis)<1e-12)axis={0,1,0};axis=glm::normalize(axis);
            const auto u=glm::normalize(glm::cross(axis,std::abs(axis.y)<.9?glm::dvec3(0,1,0):glm::dvec3(1,0,0))),v=glm::cross(axis,u);
            for(int i=0;i<16;++i){const double a=glm::two_pi<double>()*i/16.,b=glm::two_pi<double>()*(i+1)/16.;const auto ra=(u*std::cos(a)+v*std::sin(a))*c.radius,rb=(u*std::cos(b)+v*std::sin(b))*c.radius;line(c.a+ra,c.a+rb,IM_COL32(238,105,59,210));line(c.b+ra,c.b+rb,IM_COL32(238,105,59,210));if(i%4==0)line(c.a+ra,c.b+ra,IM_COL32(238,105,59,210));}
            line(c.a,c.b,IM_COL32(238,105,59,170));
        }
        if(showClothColliders_)for(const auto& body:EvaluateClothConvexes(modular_->cloth,DisplayPose()))
            for(const auto& face:body.triangles)for(int edge=0;edge<3;++edge)
                if(face[edge]<body.vertices.size()&&face[(edge+1)%3]<body.vertices.size())
                    line(body.vertices[face[edge]],body.vertices[face[(edge+1)%3]],IM_COL32(55,185,220,180));
        draw->PopClipRect();
    }
    void HairOverlay(){
        if(!showHairNodes_||!hairAsset_||!hairEnabled_)return;
        auto* draw=ImGui::GetWindowDrawList();draw->PushClipRect(viewportOrigin_,ImVec2(viewportOrigin_.x+viewportSize_.x,viewportOrigin_.y+viewportSize_.y),true);
        const auto& pose=DisplayPose();
        for(std::size_t c=0;c<hairAsset_->chains.size();++c)for(const auto& link:hairAsset_->chains[c].links){
            if(link.bone>=pose.global.size())continue;
            const auto a=Project(glm::dvec3(pose.global[link.bone][3]));
            const auto b=Project(glm::dvec3(pose.global[link.bone]*glm::dvec4(link.restTip,1)));
            if(a&&b){const auto color=int(c)==selectedHairChain_?IM_COL32(255,165,35,255):IM_COL32(35,210,175,200);draw->AddLine(*a,*b,color,2);draw->AddCircleFilled(*b,2.5f,color);}
        }
        draw->PopClipRect();
    }
    void AssetsPanel() {
        ImGui::TextColored(ImVec4(.3f,.75f,1,1),"CONTENT / ASSETS");
        ImGui::TextWrapped("%s",asset_->name.c_str());
        if(ImGui::TreeNodeEx("Skeletal Mesh",ImGuiTreeNodeFlags_DefaultOpen)) {
            if(ImGui::Selectable("SK / Skeletal mesh",selection_==Selection::Mesh))selection_=Selection::Mesh;
            if(ImGui::Selectable("Skeleton / 骨架",selection_==Selection::Skeleton))selection_=Selection::Skeleton;
            if(ImGui::Selectable("Morph Targets / 表情",selection_==Selection::Morphs))selection_=Selection::Morphs;
            ImGui::TreePop();
        }
        if(ImGui::TreeNodeEx("Animation Sequences",ImGuiTreeNodeFlags_DefaultOpen)) {
            for(std::size_t i=0;i<asset_->animations.size();++i)if(ImGui::Selectable(asset_->animations[i].name.c_str(),selection_==Selection::Animation&&selectedClip_==int(i))) {
                selectedClip_=int(i);selection_=Selection::Animation;time_=0;dirty_=true;clothReset_=true;bindPose_=false;retargetPlaying_=false;
            }
            ImGui::TreePop();
        }
        if(ImGui::Selectable("IK Rig / 识别与约束",selection_==Selection::Ik))selection_=Selection::Ik;
        if(ImGui::Selectable("Retarget / 动画重映射",selection_==Selection::Retarget))selection_=Selection::Retarget;
        if(ImGui::Selectable("Clothing / 裙摆与换装",selection_==Selection::Cloth))selection_=Selection::Cloth;
        if(ImGui::Selectable("Hair Physics / 头发惯性",selection_==Selection::Hair))selection_=Selection::Hair;
        if(ImGui::Selectable("GPU Strand Hair / 发丝模拟",selection_==Selection::StrandHair))selection_=Selection::StrandHair;
        if(characterMaterials_&&ImGui::Selectable("Diana Materials / 角色材质",selection_==Selection::CharacterMaterials))selection_=Selection::CharacterMaterials;
        if(ImGui::Selectable("Mesh Regions / 网格分类",selection_==Selection::MeshRegions))selection_=Selection::MeshRegions;
        if(ImGui::Selectable("Diagnostics / 导入报告",selection_==Selection::Diagnostics))selection_=Selection::Diagnostics;
        ImGui::SeparatorText("SKELETON HIERARCHY");
        ImGui::InputTextWithHint("##filter","Search bone / 骨骼搜索",boneFilter_.data(),boneFilter_.size());
        ImGui::BeginChild("bone-tree",ImVec2(0,0));
        if(boneFilter_[0]) {
            for(std::size_t i=0;i<asset_->skeleton.bones.size();++i) {
                const auto& bone=asset_->skeleton.bones[i];
                if(bone.name.find(boneFilter_.data())==std::string::npos&&bone.englishName.find(boneFilter_.data())==std::string::npos)continue;
                ImGui::PushID(int(i));if(ImGui::Selectable(bone.name.c_str(),selectedBone_==int(i))){selectedBone_=int(i);selection_=Selection::Skeleton;}ImGui::PopID();
            }
        } else for(std::size_t i=0;i<asset_->skeleton.bones.size();++i)if(asset_->skeleton.bones[i].parent<0)BoneTree(int(i));
        ImGui::EndChild();
    }
    void MatrixDetails(const char* label,const glm::dmat4& matrix) {
        if(ImGui::TreeNode(label)){for(int row=0;row<4;++row)ImGui::Text("% .5f % .5f % .5f % .5f",matrix[0][row],matrix[1][row],matrix[2][row],matrix[3][row]);ImGui::TreePop();}
    }
    void SkeletonDetails() {
        selectedBone_=std::clamp(selectedBone_,0,int(asset_->skeleton.bones.size())-1);
        const auto& bone=asset_->skeleton.bones[selectedBone_];
        ImGui::TextColored(ImVec4(.3f,.75f,1,1),"BONE #%d",selectedBone_);
        ImGui::TextWrapped("%s",bone.name.c_str());ImGui::TextWrapped("%s",bone.englishName.c_str());
        ImGui::TextWrapped("Parent [%d]: %s",bone.parent,BoneName(bone.parent).c_str());
        ImGui::Text("Source flags: 0x%04X",bone.sourceFlags);
        for(const auto& role:asset_->ikRig.humanoid)if(role.bone==selectedBone_){ImGui::SeparatorText("AUTO RECOGNITION");ImGui::Text("Role: %s",role.role.c_str());ImGui::ProgressBar(role.confidence);ImGui::TextWrapped("%s",role.method.c_str());}
        ImGui::SeparatorText("REST / CURRENT TRANSFORM");
        const auto& t=bone.restLocal.translation;
        ImGui::Text("Rest T: %.5f, %.5f, %.5f",t.x,t.y,t.z);
        const auto euler=glm::degrees(glm::eulerAngles(bone.restLocal.rotation));
        ImGui::Text("Rest R: %.3f, %.3f, %.3f",euler.x,euler.y,euler.z);
        const auto restScale=bone.restLocal.scale;ImGui::Text("Rest S: %.4f, %.4f, %.4f",restScale.x,restScale.y,restScale.z);
        const auto& current=pose_.local[selectedBone_];const auto currentEuler=glm::degrees(glm::eulerAngles(current.rotation));
        ImGui::Text("Current T: %.4f %.4f %.4f",current.translation.x,current.translation.y,current.translation.z);
        ImGui::Text("Current R: %.3f %.3f %.3f",currentEuler.x,currentEuler.y,currentEuler.z);
        ImGui::Text("Current S: %.4f %.4f %.4f",current.scale.x,current.scale.y,current.scale.z);
        auto p=glm::dvec3(pose_.global[selectedBone_][3]);ImGui::Text("World: %.4f, %.4f, %.4f",p.x,p.y,p.z);
        MatrixDetails("Inverse bind matrix",bone.inverseBind);
        MatrixDetails("Animated global matrix",pose_.global[selectedBone_]);
        std::size_t vertices=0;
        for(const auto& skin:asset_->skins)for(const auto& vertex:skin.vertices)for(int slot=0;slot<4;++slot)if(vertex.joints[slot]==std::uint32_t(selectedBone_)&&vertex.weights[slot]>1e-6f){++vertices;break;}
        ImGui::Text("Weighted vertices: %zu",vertices);
        if(ImGui::TreeNode("Native bone metadata")) {
            ImGui::TextWrapped("Append parent: %s / %.4f",BoneName(bone.inheritParent).c_str(),bone.inheritWeight);
            ImGui::Text("Fixed axis: %.4f %.4f %.4f",bone.fixedAxis.x,bone.fixedAxis.y,bone.fixedAxis.z);
            ImGui::Text("Local X: %.4f %.4f %.4f",bone.localAxisX.x,bone.localAxisX.y,bone.localAxisX.z);
            ImGui::Text("Local Z: %.4f %.4f %.4f",bone.localAxisZ.x,bone.localAxisZ.y,bone.localAxisZ.z);ImGui::TreePop();
        }
        if(ImGui::Button("Focus selected bone")){showNames_=true;cameraFocus_=glm::vec3(placement_*glm::dvec4(p,1));distance_=1.8f;}
    }
    void MeshDetails() {
        ImGui::Text("Meshes: %zu",asset_->geometry->meshes.size());
        ImGui::Text("Vertices: %llu",static_cast<unsigned long long>(asset_->geometry->VertexCount()));
        ImGui::Text("Triangles: %llu",static_cast<unsigned long long>(asset_->geometry->TriangleCount()));
        ImGui::Text("Materials: %zu  Textures: %zu",asset_->geometry->materials.size(),asset_->geometry->textures.size());
        ImGui::Text("Morph targets: %zu",asset_->morphs.size());
        ImGui::SeparatorText("VERTEX / SKIN DATA");
        ImGui::SliderInt("Mesh",&selectedMesh_,0,int(asset_->geometry->meshes.size())-1);
        const auto& mesh=asset_->geometry->meshes[selectedMesh_];const auto& skin=asset_->skins[selectedMesh_];
        ImGui::TextWrapped("%s",mesh.name.c_str());
        ImGui::InputInt("Vertex",&selectedVertex_);selectedVertex_=std::clamp(selectedVertex_,0,int(mesh.vertices.size())-1);
        const auto& v=mesh.vertices[selectedVertex_];const auto& s=skin.vertices[selectedVertex_];
        ImGui::Text("Position: %.4f %.4f %.4f",v.position.x,v.position.y,v.position.z);
        ImGui::Text("UV: %.4f %.4f",v.uv.x,v.uv.y);ImGui::Text("Skin mode: %d",int(s.mode));
        ImGui::TextUnformatted(skin.influenceOffsets.empty()?"Four-weight skin":"Complete source skin influences");
        ForEachSkinInfluence(skin,selectedVertex_,[&](std::uint32_t joint,float weight){if(weight>0)ImGui::TextWrapped("[%u] %s : %.6f",joint,BoneName(int(joint)).c_str(),weight);});
        if(!skin.sourceVertexIds.empty())ImGui::Text("Source vertex ID: %u",skin.sourceVertexIds[selectedVertex_]);
    }
    void MeshRegionDetails() {
        ImGui::TextColored(ImVec4(.3f,.75f,1,1),"MESH REGIONS / 网格分类");
        ImGui::TextWrapped("A region is one material section. Garment classification does not enable cloth physics. Export revision saves classifications and manual review.");
        if(modular_)ImGui::TextWrapped("This list describes the body package. Category/isolation preview hides the separate equipped garment resource.");
        std::array<std::size_t,5> counts{};std::size_t authored=0,review=0;
        for(const auto& region:asset_->meshRegions){const auto kind=std::size_t(region.kind);if(kind<counts.size())++counts[kind];authored+=region.authoredCloth;review+=region.requiresReview;}
        ImGui::Text("Body %zu | Hair %zu | Garment %zu",counts[1],counts[2],counts[3]);
        ImGui::Text("Accessory %zu | Unknown %zu",counts[4],counts[0]);
        ImGui::Text("Authored Cloth %zu | Review needed %zu",authored,review);
        if(regionEdits_)ImGui::TextColored(ImVec4(1,.78f,.35f,1),"Unsaved classification edits: Export revision");
        ImGui::InputTextWithHint("##region-search","Search source object / material",regionFilter_.data(),regionFilter_.size());
        const char* categories[]={"All sections","Unknown","Body","Hair","Garment","Accessory"};
        int category=regionCategoryFilter_+1;
        if(ImGui::Combo("Category preview",&category,categories,int(std::size(categories)))) {
            regionCategoryFilter_=category-1;
            if(regionCategoryFilter_>=0)for(const auto& region:asset_->meshRegions)if(int(region.kind)==regionCategoryFilter_){selectedRegionMesh_=int(region.mesh);break;}
        }
        ImGui::Checkbox("Isolate selected section",&isolateRegion_);
        ImGui::SameLine();if(ImGui::Button("Show all")){isolateRegion_=false;regionCategoryFilter_=-1;}
        if(ImGui::Button("Frame visible"))FramePreviewRegions(-1,true);
        ImGui::BeginChild("region-sections",ImVec2(0,190),ImGuiChildFlags_Borders);
        for(const auto& region:asset_->meshRegions) {
            if(region.mesh>=asset_->geometry->meshes.size())continue;
            if(regionCategoryFilter_>=0&&int(region.kind)!=regionCategoryFilter_)continue;
            const auto& mesh=asset_->geometry->meshes[region.mesh];
            const auto& material=region.sourceMaterial;
            const auto name=material.empty()?mesh.name:material;
            if(regionFilter_[0]&&name.find(regionFilter_.data())==std::string::npos&&
                mesh.name.find(regionFilter_.data())==std::string::npos&&region.sourceObject.find(regionFilter_.data())==std::string::npos&&
                std::none_of(region.evidence.begin(),region.evidence.end(),[&](const auto& item){return item.find(regionFilter_.data())!=std::string::npos;}))continue;
            ImGui::PushID(int(region.mesh));
            const auto label="#"+std::to_string(region.mesh)+" "+(region.sourceObject.empty()?std::string{}:region.sourceObject+" | ")+name;
            if(ImGui::Selectable(label.c_str(),selectedRegionMesh_==int(region.mesh)))selectedRegionMesh_=int(region.mesh);
            ImGui::TextDisabled("%s | %.0f%%%s%s",MeshRegionKindName(region.kind),region.confidence*100,region.authoredCloth?" | Cloth":"",region.requiresReview?" | Review":"");
            ImGui::PopID();
        }
        ImGui::EndChild();
        auto selected=std::find_if(asset_->meshRegions.begin(),asset_->meshRegions.end(),[&](const auto& region){return region.mesh==std::uint32_t(selectedRegionMesh_);});
        if(selected==asset_->meshRegions.end())return;
        auto& region=*selected;
        if(region.mesh>=asset_->geometry->meshes.size())return;
        const auto& mesh=asset_->geometry->meshes[region.mesh];
        ImGui::SeparatorText("SELECTED MATERIAL SECTION");
        ImGui::Text("Section #%u | %zu vertices | %zu triangles",region.mesh,mesh.vertices.size(),mesh.indices.size()/3);
        ImGui::TextWrapped("Object: %s",region.sourceObject.empty()?"(not supplied)":region.sourceObject.c_str());
        ImGui::TextWrapped("Source mesh: %s",region.sourceMesh.empty()?mesh.name.c_str():region.sourceMesh.c_str());
        ImGui::TextWrapped("Material #%u: %s",mesh.materialIndex,region.sourceMaterial.empty()?"(unnamed)":region.sourceMaterial.c_str());
        ImGui::Text("Classification: %s",MeshRegionKindName(region.kind));
        ImGui::Text("Import confidence %.1f%%",region.confidence*100);
        ImGui::ProgressBar(std::clamp(region.confidence,0.f,1.f));
        int kind=int(region.kind);
        if(ImGui::Combo("Manual category",&kind,categories+1,5)) {
            region.kind=static_cast<MeshRegionKind>(kind);region.userOverride=true;region.requiresReview=true;regionEdits_=true;
            status_="Changed section "+std::to_string(region.mesh)+" classification; confirm review and Export revision to save";
        }
        ImGui::Text("Manual override: %s",region.userOverride?"yes":"no");
        bool reviewed=!region.requiresReview;
        if(ImGui::Checkbox("Reviewed / 已核对",&reviewed)){region.requiresReview=!reviewed;regionEdits_=true;status_="Updated section review; Export revision to save";}
        ImGui::Text("Source Cloth modifier: %s",region.authoredCloth?"authored":"none");
        if(!region.clothPinGroup.empty())ImGui::TextWrapped("Cloth pin vertex group: %s",region.clothPinGroup.c_str());
        const auto textList=[](const char* title,const std::vector<std::string>& values) {
            if(ImGui::TreeNode(title)){if(values.empty())ImGui::TextDisabled("(none)");for(const auto& text:values){ImGui::PushTextWrapPos();ImGui::BulletText("%s",text.c_str());ImGui::PopTextWrapPos();}ImGui::TreePop();}
        };
        textList("Recognition evidence",region.evidence);
        textList("Source collections",region.collections);
        textList("Source modifiers",region.modifiers);
        textList("Source vertex groups",region.vertexGroups);
    }
    void MorphDetails() {
        ImGui::TextWrapped("Morph weights / 表情混合（动画曲线 + 手动覆盖）");
        if(ImGui::Button("Reset all / 重置")){std::fill(manualMorphs_.begin(),manualMorphs_.end(),0);std::fill(morphOverrides_.begin(),morphOverrides_.end(),0);dirty_=true;}
        for(std::size_t i=0;i<asset_->morphs.size();++i) {
            const auto& morph=asset_->morphs[i];ImGui::PushID(int(i));
            ImGui::TextWrapped("%s",morph.name.c_str());
            if(ImGui::IsItemHovered())ImGui::SetTooltip("%s\n%zu vertex deltas; type=%d panel=%d",morph.englishName.c_str(),morph.deltas.size(),morph.sourceType,morph.panel);
            bool overrideEnabled=morphOverrides_[i]!=0;
            if(ImGui::Checkbox("Manual override",&overrideEnabled)){morphOverrides_[i]=overrideEnabled?1:0;dirty_=true;}
            if(ImGui::IsItemHovered())ImGui::SetTooltip("Enabled: use the slider, including 0. Disabled: follow the animation curve.");
            ImGui::SetNextItemWidth(-1);if(ImGui::SliderFloat("##weight",&manualMorphs_[i],0,1,"%.2f")){morphOverrides_[i]=1;dirty_=true;}
            ImGui::PopID();
        }
    }
    void IkDetails() {
        ImGui::Checkbox("Enable selected chain",&enableIk_);if(ImGui::IsItemEdited())dirty_=true;
        if(!asset_->ikRig.chains.empty()) {
            if(ImGui::BeginCombo("Chain",asset_->ikRig.chains[selectedChain_].name.c_str())){
                for(std::size_t i=0;i<asset_->ikRig.chains.size();++i)if(ImGui::Selectable(asset_->ikRig.chains[i].name.c_str(),selectedChain_==int(i))){selectedChain_=int(i);ResetIkTarget();dirty_=true;}
                ImGui::EndCombo();
            }
            auto& chain=asset_->ikRig.chains[selectedChain_];
            ImGui::Text("%s / %s",chain.native?"Native PMX":"Recognized",chain.role.c_str());
            ImGui::TextWrapped("Effector: %s",BoneName(chain.effector).c_str());
            ImGui::Text("Iterations: %u  Angle limit: %.3f",chain.iterations,chain.angleLimit);
            if(ImGui::DragFloat3("Target",&ikTarget_.x,.01f)){enableIk_=true;dirty_=true;}
            if(ImGui::Button("Reset target")){ResetIkTarget();dirty_=true;}
            ImGui::TextWrapped("Root: %s",BoneName(chain.root).c_str());ImGui::TextWrapped("Target bone: %s",BoneName(chain.targetBone).c_str());
            for(const auto& link:chain.links) {
                ImGui::BulletText("%s%s",BoneName(int(link.bone)).c_str(),link.limited?" [limited]":"");
                if(link.limited) {
                    const auto low=glm::degrees(link.minimum),high=glm::degrees(link.maximum);
                    ImGui::Text("  Min deg: %.2f %.2f %.2f",low.x,low.y,low.z);
                    ImGui::Text("  Max deg: %.2f %.2f %.2f",high.x,high.y,high.z);
                }
            }
        }
        ImGui::SeparatorText("HUMANOID SEMANTIC MAP");
        for(auto& role:asset_->ikRig.humanoid) {
            ImGui::PushID(role.role.c_str());ImGui::Text("%s  %.0f%%",role.role.c_str(),role.confidence*100);
            if(ImGui::BeginCombo("##bone",BoneName(role.bone).c_str())) {
                if(ImGui::Selectable("<unmapped>",role.bone<0))AssignRole(role.role,-1);
                for(std::size_t i=0;i<asset_->skeleton.bones.size();++i)if(ImGui::Selectable(asset_->skeleton.bones[i].name.c_str(),role.bone==int(i)))AssignRole(role.role,int(i));
                ImGui::EndCombo();
            }
            if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",role.method.c_str());ImGui::PopID();
        }
        ImGui::SeparatorText("ADD / REPAIR MISSING ROLE");
        const auto& roles=CanonicalRoles();
        if(ImGui::BeginCombo("Semantic role",roles[addRole_].c_str())) {
            for(std::size_t i=0;i<roles.size();++i)if(ImGui::Selectable(roles[i].c_str(),addRole_==int(i)))addRole_=int(i);
            ImGui::EndCombo();
        }
        if(ImGui::BeginCombo("Bone for role",BoneName(addRoleBone_).c_str())) {
            for(std::size_t i=0;i<asset_->skeleton.bones.size();++i)if(ImGui::Selectable(asset_->skeleton.bones[i].name.c_str(),addRoleBone_==int(i)))addRoleBone_=int(i);
            ImGui::EndCombo();
        }
        if(ImGui::Button("Assign role / repair mapping"))AssignRole(roles[addRole_],addRoleBone_);
        ImGui::TextWrapped("Assigning an occupied bone clears its previous role. Existing retarget previews rebuild immediately.");
    }
    template<class Keys,class Value> static Value CurveSample(const Keys& keys,double time,Value fallback) {
        if(keys.empty())return fallback;
        if(time<=keys.front().time)return keys.front().value;
        if(time>=keys.back().time)return keys.back().value;
        const auto b=std::upper_bound(keys.begin(),keys.end(),time,[](double t,const auto& k){return t<k.time;});const auto& a=*(b-1);
        const double f=(time-a.time)/std::max(1e-12,b->time-a.time);
        if constexpr(std::is_same_v<Value,glm::dquat>){auto q=b->value;if(glm::dot(a.value,q)<0)q=-q;return glm::normalize(glm::slerp(a.value,q,f));}
        else return a.value*(1-f)+b->value*f;
    }
    void AnimationExportPopup() {
        if(ImGui::BeginPopupModal("Export animation asset",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped("Standalone target animation; the source VMD is not required to play it.");
            ImGui::SetNextItemWidth(710);ImGui::InputText("New .anim.json file",animationOutput_.data(),animationOutput_.size());
            if(ImGui::Button("Save animation"))Try([&]{std::string error;Check(SaveAnimationSequenceAsset(asset_->animations[selectedClip_],*asset_,Path(animationOutput_.data()),&error),error);status_="Exported animation: "+std::string(animationOutput_.data());ImGui::CloseCurrentPopup();});
            ImGui::SameLine();if(ImGui::Button("Cancel export"))ImGui::CloseCurrentPopup();
            if(!lastError_.empty())ImGui::TextWrapped("%s",lastError_.c_str());ImGui::EndPopup();
        }
    }
    void CurvePlot(const char* label,const std::vector<std::array<float,4>>& values,int channels,
        const char* const* names,double duration,const std::vector<double>& keys) {
        ImGui::TextUnformatted(label);
        const ImVec2 origin=ImGui::GetCursorScreenPos(),size(std::max(100.f,ImGui::GetContentRegionAvail().x),112);
        ImGui::PushID(label);ImGui::InvisibleButton("curve",size);
        const bool hover=ImGui::IsItemHovered();const auto mouse=ImGui::GetIO().MousePos;
        if((hover&&ImGui::IsMouseClicked(0))||ImGui::IsItemActive()) {time_=std::clamp(double(mouse.x-origin.x)/size.x,0.,1.)*duration;playing_=false;dirty_=true;}
        float low=0,high=0;for(const auto& value:values)for(int c=0;c<channels;++c){low=std::min(low,value[c]);high=std::max(high,value[c]);}
        const float padding=std::max((high-low)*.08f,.02f);low-=padding;high+=padding;
        auto* draw=ImGui::GetWindowDrawList();draw->AddRectFilled(origin,ImVec2(origin.x+size.x,origin.y+size.y),IM_COL32(18,25,36,255),3);
        const auto y=[&](float value){return origin.y+size.y-18-(value-low)/(high-low)*(size.y-34);};
        for(int i=1;i<4;++i){float row=origin.y+16+(size.y-34)*i/4.f;draw->AddLine(ImVec2(origin.x,row),ImVec2(origin.x+size.x,row),IM_COL32(57,66,80,110));}
        const ImU32 colors[]={IM_COL32(248,104,109,255),IM_COL32(111,216,140,255),IM_COL32(91,175,251,255),IM_COL32(240,204,100,255)};
        for(int c=0;c<channels;++c) {
            for(std::size_t i=1;i<values.size();++i)draw->AddLine(ImVec2(origin.x+float(i-1)/(values.size()-1)*size.x,y(values[i-1][c])),ImVec2(origin.x+float(i)/(values.size()-1)*size.x,y(values[i][c])),colors[c],1.5f);
            draw->AddText(ImVec2(origin.x+4+c*45,origin.y+2),colors[c],names[c]);
        }
        const double shown=loop_&&duration>0&&(playing_||std::abs(time_-duration)>1e-9)?std::fmod(time_,duration):std::min(time_,duration);
        const float cursor=origin.x+float(shown/std::max(duration,.001))*size.x;
        draw->AddLine(ImVec2(cursor,origin.y+16),ImVec2(cursor,origin.y+size.y-1),IM_COL32(255,255,255,180));
        const std::size_t stride=std::max<std::size_t>(1,keys.size()/180);
        for(std::size_t i=0;i<keys.size();i+=stride){float x=origin.x+float(keys[i]/std::max(duration,.001))*size.x;draw->AddCircleFilled(ImVec2(x,origin.y+size.y-3),1.2f,IM_COL32(137,154,180,200));}
        char range[100];std::snprintf(range,sizeof(range),"%.3f .. %.3f | 0 .. %.2fs",low,high,duration);draw->AddText(ImVec2(origin.x+4,origin.y+size.y-17),IM_COL32(165,179,199,255),range);
        if(hover&&!values.empty()) {const auto index=std::min(values.size()-1,std::size_t(std::clamp((mouse.x-origin.x)/size.x,0.f,1.f)*(values.size()-1)));ImGui::BeginTooltip();ImGui::Text("t = %.4f s",double(index)/std::max<std::size_t>(1,values.size()-1)*duration);for(int c=0;c<channels;++c)ImGui::Text("%s = %.6f",names[c],values[index][c]);ImGui::TextUnformatted("Click / drag to scrub");ImGui::EndTooltip();}
        ImGui::PopID();
    }
    void AnimationDetails() {
        if(asset_->animations.empty()){ImGui::TextWrapped("No animation sequence in this package");return;}
        const auto& clip=asset_->animations[selectedClip_];
        ImGui::TextWrapped("%s",clip.name.c_str());ImGui::Text("%s",ClipKind(clip));
        ImGui::Text("Duration: %.3f s / %.1f FPS",clip.duration,clip.framesPerSecond);
        ImGui::Text("Bone tracks: %zu  Morph curves: %zu",clip.boneTracks.size(),clip.morphTracks.size());
        if(ImGui::Button("Export this animation"))ImGui::OpenPopup("Export animation asset");
        if(!clip.boneTracks.empty()) {
            auto found=std::find_if(clip.boneTracks.begin(),clip.boneTracks.end(),[&](const auto& track){return int(track.bone)==selectedBone_;});
            if(found==clip.boneTracks.end())found=clip.boneTracks.begin();
            if(ImGui::BeginCombo("Bone curve",BoneName(found->bone).c_str())) {for(const auto& track:clip.boneTracks)if(ImGui::Selectable(BoneName(track.bone).c_str(),int(track.bone)==selectedBone_))selectedBone_=int(track.bone);ImGui::EndCombo();}
            found=std::find_if(clip.boneTracks.begin(),clip.boneTracks.end(),[&](const auto& track){return int(track.bone)==selectedBone_;});if(found==clip.boneTracks.end())found=clip.boneTracks.begin();
            const auto& track=*found;const auto& rest=asset_->skeleton.bones[track.bone].restLocal;
            std::vector<std::array<float,4>> translations,rotations,scales;
            for(int sample=0;sample<180;++sample){double time=clip.duration*sample/179.;auto t=CurveSample(track.translations,time,rest.translation),s=CurveSample(track.scales,time,rest.scale);auto q=CurveSample(track.rotations,time,rest.rotation);translations.push_back({float(t.x),float(t.y),float(t.z),0});rotations.push_back({float(q.x),float(q.y),float(q.z),float(q.w)});scales.push_back({float(s.x),float(s.y),float(s.z),0});}
            const char* vectorNames[]={"X","Y","Z"};const char* quaternionNames[]={"X","Y","Z","W"};
            auto times=[](const auto& keys){std::vector<double> result;for(const auto& key:keys)result.push_back(key.time);return result;};
            CurvePlot("Translation",translations,3,vectorNames,clip.duration,times(track.translations));
            CurvePlot("Rotation / quaternion",rotations,4,quaternionNames,clip.duration,times(track.rotations));
            if(ImGui::TreeNode("Scale curve")){CurvePlot("Scale",scales,3,vectorNames,clip.duration,times(track.scales));ImGui::TreePop();}
            if(ImGui::TreeNode("Bone key values")) {
                ImGui::Text("T %zu | R %zu | S %zu",track.translations.size(),track.rotations.size(),track.scales.size());
                ImGui::BeginChild("bone-keys",ImVec2(0,160),ImGuiChildFlags_Borders);
                ImGuiListClipper rows;rows.Begin(int(track.translations.size()+track.rotations.size()+track.scales.size()));
                while(rows.Step())for(int row=rows.DisplayStart;row<rows.DisplayEnd;++row){std::size_t i=row;if(i<track.translations.size()){const auto& k=track.translations[i];ImGui::Text("%.3f T %.4f %.4f %.4f",k.time,k.value.x,k.value.y,k.value.z);}else if((i-=track.translations.size())<track.rotations.size()){const auto& k=track.rotations[i];ImGui::Text("%.3f Q %.3f %.3f %.3f %.3f",k.time,k.value.w,k.value.x,k.value.y,k.value.z);}else{const auto& k=track.scales[i-track.rotations.size()];ImGui::Text("%.3f S %.3f %.3f %.3f",k.time,k.value.x,k.value.y,k.value.z);}}
                ImGui::EndChild();ImGui::TreePop();
            }
        }
        if(!clip.morphTracks.empty()) {
            ImGui::SeparatorText("MORPH / EXPRESSION CURVE");selectedMorphCurve_=std::clamp(selectedMorphCurve_,0,int(clip.morphTracks.size())-1);
            if(ImGui::BeginCombo("Expression",asset_->morphs[clip.morphTracks[selectedMorphCurve_].morph].name.c_str())) {for(std::size_t i=0;i<clip.morphTracks.size();++i)if(ImGui::Selectable(asset_->morphs[clip.morphTracks[i].morph].name.c_str(),selectedMorphCurve_==int(i)))selectedMorphCurve_=int(i);ImGui::EndCombo();}
            const auto& track=clip.morphTracks[selectedMorphCurve_];std::vector<std::array<float,4>> values;std::vector<double> times;for(const auto& key:track.keys)times.push_back(key.time);
            for(int i=0;i<180;++i)values.push_back({float(CurveSample(track.keys,clip.duration*i/179.,0.)),0,0,0});const char* names[]={"Weight"};CurvePlot("Morph weight",values,1,names,clip.duration,times);
            if(ImGui::TreeNode("Morph key values")){ImGui::BeginChild("morph-keys",ImVec2(0,140),ImGuiChildFlags_Borders);ImGuiListClipper rows;rows.Begin(int(track.keys.size()));while(rows.Step())for(int i=rows.DisplayStart;i<rows.DisplayEnd;++i)ImGui::Text("%.5f s -> %.5f",track.keys[i].time,track.keys[i].value);ImGui::EndChild();ImGui::TreePop();}
        }
        if(ImGui::TreeNode("Provenance / matching diagnostics")){ImGui::TextWrapped("%s",clip.source.c_str());ImGui::TextWrapped("Source skeleton: %s",clip.sourceSkeletonId.c_str());for(const auto& message:clip.diagnostics)ImGui::BulletText("%s",message.c_str());ImGui::TreePop();}
        AnimationExportPopup();
    }
    void RetargetDetails() {
        ImGui::TextWrapped("Source animation package -> current target skeleton");
        ImGui::InputTextWithHint("##retarget-path","Package.asset.json",retargetPath_.data(),retargetPath_.size());
        if(ImGui::Button("Browse source package")){auto p=Browse(true);if(!p.empty())SetText(retargetPath_,p);}
        if(ImGui::Button("Build automatic mapping"))Try([&]{LoadRetargetSource(Path(retargetPath_.data()));});
        if(retargetSource_) {
            ImGui::TextWrapped("Source: %s",retargetSource_->name.c_str());
            ImGui::Text("Root translation scale: %.4f",retargetMap_.translationScale);
            if(!retargetSource_->animations.empty()) {
                if(ImGui::BeginCombo("Source sequence",retargetSource_->animations[retargetClip_].name.c_str())) {
                    for(std::size_t i=0;i<retargetSource_->animations.size();++i)if(ImGui::Selectable(retargetSource_->animations[i].name.c_str(),retargetClip_==int(i))) {
                        retargetClip_=int(i);time_=0;dirty_=true;clothReset_=true;
                    }
                    ImGui::EndCombo();
                }
                const auto& clip=retargetSource_->animations[retargetClip_];
                ImGui::Text("%.3f s / %.1f FPS / %s",clip.duration,clip.framesPerSecond,ClipKind(clip));
                if(ImGui::Checkbox("Preview retargeted motion",&retargetPlaying_)){time_=0;dirty_=true;clothReset_=true;bindPose_=false;}
                if(ImGui::InputDouble("Bake FPS",&bakeOptions_.framesPerSecond,1,10,"%.1f"))dirty_=true;
                if(ImGui::Button("Bake sequence to target"))Try([&]{
                    std::string error;auto baked=BakeRetargetedAnimation(*retargetSource_,retargetSource_->animations[retargetClip_],*asset_,retargetMap_,bakeOptions_,retargetSource_->animations[retargetClip_].name+" -> "+asset_->name,&bakeReport_,&error);
                    Check(baked.has_value(),error);AddAnimation(std::move(*baked));status_="Retargeted animation baked to current skeleton; export from Animation inspector";
                });
            } else ImGui::TextWrapped("Source package has no animation sequences.");
            for(const auto& mapping:retargetMap_.bones){ImGui::Text("%s / %.0f%%",mapping.role.c_str(),mapping.confidence*100);const auto sourceName=mapping.sourceBone>=0&&std::size_t(mapping.sourceBone)<retargetSource_->skeleton.bones.size()?retargetSource_->skeleton.bones[mapping.sourceBone].name:"<unmapped>";ImGui::TextWrapped("%s -> %s",sourceName.c_str(),BoneName(mapping.targetBone).c_str());}
            for(const auto& diagnostic:retargetMap_.diagnostics)ImGui::BulletText("%s",diagnostic.c_str());
        }
        ImGui::TextWrapped("Name aliases + hierarchy + geometry; ambiguous roles remain unmapped. Correct roles in IK Rig before export.");
    }
    void ClothDetails() {
        ImGui::TextColored(ImVec4(.75f,.55f,1,1),"CLOTHING / MODULAR CHARACTER");
        if(!modular_){ImGui::TextWrapped("Open a Character.asset.json assembly to simulate an independent garment.");if(ImGui::Button("Open modular character"))Try([&]{auto path=Browse(true);if(!path.empty())LoadCharacter(Path(path));});return;}
        ImGui::PushItemWidth(155);
        ImGui::TextWrapped("Slot: %s / %s",modular_->assembly.slots.empty()?"skirt":modular_->assembly.slots.front().slot.c_str(),modular_->garment->name.c_str());
        if(ImGui::Checkbox("Body mesh",&showBody_))dirty_=true;ImGui::SameLine();if(ImGui::Checkbox("Equipped skirt",&showGarment_)){clothReset_=true;dirty_=true;}
        if(ImGui::Checkbox("XPBD cloth simulation",&clothEnabled_)){clothReset_=true;dirty_=true;}
        ImGui::SameLine();if(ImGui::Checkbox("Freeze physics",&clothFrozen_))dirty_=true;
        ImGui::TextWrapped("Pause the animation to let the cloth settle. Freeze physics holds the simulation.");
        ImGui::Checkbox("Simulation mesh / pinned points",&showClothWire_);ImGui::Checkbox("Body collision shapes",&showClothColliders_);
        if(ImGui::Button("Reset cloth to animated pose")){clothReset_=true;dirty_=true;}
        const auto& cloth=modular_->cloth;const auto& stats=ClothStats();
        if(clothWorker_){ImGui::Text("Physics worker / completed frame lag %.1f ms",clothLagSeconds_*1000);ImGui::TextWrapped("Body and garment share the completed physics pose. Rendering does not wait for the solver.");}
        ImGui::SeparatorText("SEPARATE ASSETS");
        ImGui::Text("Body: %llu triangles",static_cast<unsigned long long>(asset_->geometry->TriangleCount()));
        ImGui::Text("Garment: %llu triangles",static_cast<unsigned long long>(modular_->garment->geometry->TriangleCount()));
        ImGui::Text("Simulation: %zu particles / %zu triangles",cloth.particles.size(),cloth.triangles.size());
        ImGui::Text("Pinned %zu | stretch %zu | bend %zu",stats.pinned,cloth.stretch.size(),cloth.bend.size());
        ImGui::Text("Tethers %zu | capsules %zu | skin hulls %zu",cloth.tethers.size(),cloth.capsules.size(),cloth.convexes.size());
        ImGui::TextWrapped("Shared skeleton: %s",asset_->skeleton.id.c_str());
        ImGui::SeparatorText("SOLVER / SI UNITS");
        int iterations=int(clothSettings_.iterations),substeps=int(clothSettings_.substeps);
        if(ImGui::SliderInt("Max iterations",&iterations,2,24)){clothSettings_.iterations=iterations;dirty_=true;}
        if(ImGui::SliderInt("Substeps",&substeps,1,8)){clothSettings_.substeps=substeps;dirty_=true;}
        double convergenceMm=clothSettings_.convergenceTolerance*1000,convergenceLow=0,convergenceHigh=2;
        if(ImGui::SliderScalar("Convergence (mm)",ImGuiDataType_Double,&convergenceMm,&convergenceLow,&convergenceHigh,"%.3f")){clothSettings_.convergenceTolerance=convergenceMm/1000;dirty_=true;}
        if(ImGui::IsItemHovered())ImGui::SetTooltip("Stop only after consecutive converged iterations. Zero uses the full iteration limit.");
        double low=1e-9,high=.01;
        if(ImGui::SliderScalar("Stretch compliance",ImGuiDataType_Double,&clothSettings_.stretchCompliance,&low,&high,"%.2e",ImGuiSliderFlags_Logarithmic))dirty_=true;
        double bendLow=.01,bendHigh=1e5;
        if(ImGui::SliderScalar("Hinge bend compliance",ImGuiDataType_Double,&clothSettings_.bendCompliance,&bendLow,&bendHigh,"%.2e",ImGuiSliderFlags_Logarithmic))dirty_=true;
        if(ImGui::SliderScalar("Area compliance",ImGuiDataType_Double,&clothSettings_.areaCompliance,&low,&high,"%.2e",ImGuiSliderFlags_Logarithmic))dirty_=true;
        if(ImGui::SliderScalar("Animation drive",ImGuiDataType_Double,&clothSettings_.animationCompliance,&low,&high,"%.2e",ImGuiSliderFlags_Logarithmic))dirty_=true;
        double dampLow=0,dampHigh=8;
        if(ImGui::SliderScalar("Global damping",ImGuiDataType_Double,&clothSettings_.damping,&dampLow,&dampHigh,"%.2f /s"))dirty_=true;
        if(ImGui::SliderScalar("Local damping",ImGuiDataType_Double,&clothSettings_.localDamping,&dampLow,&dampHigh,"%.2f /s"))dirty_=true;
        float wind[3]={float(clothSettings_.wind.x),float(clothSettings_.wind.y),float(clothSettings_.wind.z)};
        if(ImGui::DragFloat3("Wind (m/s)",wind,.05f,-15,15)){clothSettings_.wind={wind[0],wind[1],wind[2]};dirty_=true;}
        if(ImGui::Checkbox("Body collisions",&clothSettings_.collision)){clothReset_=true;dirty_=true;}
        if(ImGui::Checkbox("Self collision (particles)",&clothSettings_.selfCollision)){clothReset_=true;dirty_=true;}
        ImGui::Text("Fixed step %.5f s / units per m %.3f",clothSettings_.fixedTimeStep,cloth.unitsPerMeter);
        ImGui::SeparatorText("RUNTIME STATS");
        ImGui::Text("Step %llu / resets %llu",static_cast<unsigned long long>(stats.steps),static_cast<unsigned long long>(stats.resets));
        ImGui::Text("Solver %.3f ms | contacts %zu",stats.simulationMilliseconds,stats.contacts);
        ImGui::Text("Constraint iterations: %llu",static_cast<unsigned long long>(stats.constraintIterations));
        ImGui::Text("Displacement %.4f m",stats.maxDisplacement/cloth.unitsPerMeter);
        ImGui::Text("Dynamic edge error max %.2f / p95 %.2f mm",stats.maxDynamicEdgeError/cloth.unitsPerMeter*1000,stats.dynamicEdgeError95/cloth.unitsPerMeter*1000);
        ImGui::Text("All-edge strain %.3f | pinned %.3f",stats.maxStretchError,stats.maxKinematicStretchError);
        ImGui::Text("Penetration %.5f m | self %zu",stats.maxPenetration/cloth.unitsPerMeter,stats.selfContacts);
        ImGui::Text("Surface error %.2f mm | raw %.2f mm",stats.maxResolvableSurfacePenetration/cloth.unitsPerMeter*1000,stats.maxSurfacePenetration/cloth.unitsPerMeter*1000);
        ImGui::Text("Pinned contact conflict %.2f mm | collapsed %zu",stats.maxKinematicColliderPenetration/cloth.unitsPerMeter*1000,stats.collapsedTriangles);
        if(ImGui::IsItemHovered())ImGui::SetTooltip("Includes the configured collision thickness. This reports the original source seam/body conflict; it is not removed from the raw diagnostics.");
        ImGui::Text("Waist attachment error %.4f mm",stats.maxAttachmentError/cloth.unitsPerMeter*1000);
        if(stats.infeasibleAnimatedTargets||stats.kinematicColliderOverlaps) {
            ImGui::TextColored(ImVec4(1,.75f,.35f,1),"Target conflicts %zu | pinned overlaps %zu",stats.infeasibleAnimatedTargets,stats.kinematicColliderOverlaps);
            if(ImGui::IsItemHovered())ImGui::SetTooltip("Source waist pins can already be embedded in the animated body. Pins retain the authored seam; adjacent convex contacts account for that kinematic offset. Raw overlap remains reported. Free cloth retains the full collision thickness.");
        }
        if(ImGui::TreeNode("Particle / painted weight data")) {
            ImGui::InputInt("Particle",&selectedClothParticle_);selectedClothParticle_=std::clamp(selectedClothParticle_,0,int(cloth.particles.size())-1);
            const auto& p=cloth.particles[selectedClothParticle_];ImGui::Text("Inv mass %.5f | Max distance %.5f",p.inverseMass,p.maxDistance);ImGui::Text("Animation drive weight %.4f",p.animationDrive);
            ImGui::Text("Rest %.4f %.4f %.4f",p.restPosition.x,p.restPosition.y,p.restPosition.z);
            if(std::size_t(selectedClothParticle_)<ClothPositions().size()){const auto value=ClothPositions()[selectedClothParticle_];ImGui::Text("Current %.4f %.4f %.4f",value.x,value.y,value.z);}
            ImGui::TreePop();
        }
        if(ImGui::TreeNode("Garment construction / diagnostics")){for(const auto& text:cloth.diagnostics)ImGui::BulletText("%s",text.c_str());for(const auto& text:modular_->diagnostics)ImGui::BulletText("%s",text.c_str());ImGui::TreePop();}
        if(ImGui::TreeNode("Replace garment / shared skeleton")) {
            ImGui::InputTextWithHint("##replace-garment","Garment Package.asset.json",replacementPackage_.data(),replacementPackage_.size());
            if(ImGui::Button("Browse garment package")){auto path=Browse(true);if(!path.empty())SetText(replacementPackage_,path);}
            ImGui::InputTextWithHint("##replace-cloth","Garment .cloth.json",replacementCloth_.data(),replacementCloth_.size());
            if(ImGui::Button("Browse cloth asset")){auto path=Browse(true);if(!path.empty())SetText(replacementCloth_,path);}
            if(ImGui::Button("Equip compatible garment"))Try([&]{EquipGarment(Path(replacementPackage_.data()),Path(replacementCloth_.data()));});
            ImGui::TextWrapped("Equipping replaces the garment resource and solver only. The body, skeleton, animation and face curves continue.");ImGui::TreePop();
        }
        if(ImGui::Button("Export modular character"))ImGui::OpenPopup("Export character assembly");
        if(ImGui::BeginPopupModal("Export character assembly",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::SetNextItemWidth(680);ImGui::InputText("New directory",outfitExport_.data(),outfitExport_.size());
            if(ImGui::Button("Save character assets"))Try([&]{auto bundle=*modular_;bundle.body=asset_;bundle.cloth.settings=clothSettings_;if(!bundle.assembly.slots.empty())bundle.assembly.slots.front().enabled=showGarment_;std::string error;Check(SaveModularCharacter(bundle,Path(outfitExport_.data()),&error),error);status_="Exported modular character: "+std::string(outfitExport_.data());ImGui::CloseCurrentPopup();});
            ImGui::SameLine();if(ImGui::Button("Cancel outfit export"))ImGui::CloseCurrentPopup();if(!lastError_.empty())ImGui::TextWrapped("%s",lastError_.c_str());ImGui::EndPopup();
        }
        ImGui::PopItemWidth();
    }
    void HairDetails(){
        ImGui::TextColored(ImVec4(.8f,.6f,1,1),"HAIR / SECONDARY MOTION");
        if(ImGui::Button("Open hair physics asset"))Try([&]{const auto path=Browse(true);if(!path.empty())LoadHair(Path(path));});
        if(!hairAsset_){ImGui::TextWrapped("Import a source PMX with hair_asset_import, then load its independent .hair.json asset. No hair physics asset is registered for this character.");return;}
        ImGui::TextWrapped("%s",hairAsset_->name.c_str());
        if(ImGui::Checkbox("Hair inertia simulation",&hairEnabled_)){hairReset_=clothReset_=true;dirty_=true;hairPose_={};}
        ImGui::Checkbox("Show hair chains",&showHairNodes_);
        auto& frozen=SharedHairPreview()?clothFrozen_:hairFrozen_;
        ImGui::Checkbox(SharedHairPreview()?"Freeze shared preview physics":"Freeze hair physics",&frozen);
        if(ImGui::Button("Reset hair to current animation")){hairReset_=clothReset_=true;dirty_=true;}
        const auto& stats=HairStats();
        ImGui::SeparatorText("RUNTIME / SURFACE CONTACT");
        ImGui::Text("%zu chains | %zu bones | %zu surface probes",stats.chains,stats.bones,stats.probes);
        ImGui::Text("Actual body collision triangles: %zu",stats.bodyTriangles);
        ImGui::Text("Bone body queries %zu | certified skips %zu",stats.collisionQueries,stats.clearanceSkips);
        ImGui::Text("Exact probe measurements reused: %zu",stats.measurementReuses);
        ImGui::Text("Hair step %.3f ms | collision %.3f ms",stats.simulationMilliseconds,stats.collisionMilliseconds);
        ImGui::Text("Mesh contact: %zu vertices | %zu clusters",stats.surfaceVertices,stats.surfaceClusters);
        ImGui::Text("Mesh correction %.3f ms | sweeps %zu | rejected %zu",stats.surfaceMilliseconds,stats.surfaceSweeps,stats.surfaceBroadphaseRejects);
        ImGui::Text("Queried mesh residual %.3f mm | contacts %zu",stats.maxSurfacePenetration*1000,stats.surfaceContacts);
        ImGui::Text("Bone probe residual %.3f mm | contacts %zu",stats.maxPenetration*1000,stats.contacts);
        ImGui::Text("Initial source overlap %.3f mm",stats.sourcePenetration*1000);
        ImGui::Text("Authored scalp attachments: %zu | drift %.4f mm",hairAsset_->attachments.size(),stats.maxAttachmentDrift*1000);
        ImGui::Text("Root error %.6f mm | tip movement %.3f m",stats.maxRootError*1000,stats.maxTipDisplacement);
        ImGui::Text("Steps %llu | resets %llu",static_cast<unsigned long long>(stats.steps),static_cast<unsigned long long>(stats.resets));
        ImGui::Text("State: %s | sleep ticks %llu",stats.sleeping?"sleeping":"active",static_cast<unsigned long long>(stats.sleepTicks));
        if(SharedHairPreview())ImGui::TextWrapped("Hair and displayed body share one completed background pose. Enabled clothing physics joins the same worker. Focus suspension pauses simulation work.");
        double zero=0,one=1,low=.0002,high=.005;
        bool changed=ImGui::SliderScalar("Inertia",ImGuiDataType_Double,&hairSettings_.inertia,&zero,&one,"%.2f");
        changed=ImGui::SliderScalar("Surface margin (m)",ImGuiDataType_Double,&hairSettings_.thickness,&low,&high,"%.4f")||changed;
        changed=ImGui::Checkbox("Hair / body collision",&hairSettings_.collision)||changed;
        hairSettingsPending_=hairSettingsPending_||changed;
        // Rebuilding dense contact bindings for every slider pixel stalls UI.
        // Commit one immutable configuration when the current edit finishes.
        if(hairSettingsPending_&&!ImGui::IsAnyItemActive()){auto updated=*hairAsset_;updated.settings=hairSettings_;hairAsset_=std::make_shared<const HairAsset>(std::move(updated));if(modular_)modular_->hair=*hairAsset_;hairReset_=clothReset_=true;dirty_=true;hairSettingsPending_=false;}
        ImGui::SeparatorText("CHAINS / SOURCE PHYSICS");
        if(!hairAsset_->chains.empty()){
            selectedHairChain_=std::clamp(selectedHairChain_,0,int(hairAsset_->chains.size())-1);
            if(ImGui::BeginCombo("Chain",hairAsset_->chains[selectedHairChain_].name.c_str())){
                for(std::size_t i=0;i<hairAsset_->chains.size();++i)if(ImGui::Selectable(hairAsset_->chains[i].name.c_str(),int(i)==selectedHairChain_))selectedHairChain_=int(i);
                ImGui::EndCombo();
            }
            ImGui::TextWrapped("Source positions/sizes use source units; rotations/limits use radians. Units per meter: %.6g",hairAsset_->unitsPerMeter);
            for(const auto& link:hairAsset_->chains[selectedHairChain_].links){
                ImGui::PushID(int(link.bone));
                ImGui::TextWrapped("%s",BoneName(int(link.bone)).c_str());
                ImGui::Text("  response %.2f | damping %.2f | limit %.1f deg",link.stiffness,link.damping,glm::degrees(link.maxAngle));
                if(link.sourceRigidBody>=0&&std::size_t(link.sourceRigidBody)<hairAsset_->sourceRigidBodies.size()){
                    const auto& body=hairAsset_->sourceRigidBodies[link.sourceRigidBody];ImGui::Text("  PMX body %d | mass %.4f | shape %u",link.sourceRigidBody,body.mass,unsigned(body.shape));
                    if(ImGui::TreeNode("Source rigid body")){
                        ImGui::TextWrapped("%s | bone %s",body.name.c_str(),BoneName(body.bone).c_str());
                        ImGui::Text("Dimensions %.5g %.5g %.5g",body.dimensions.x,body.dimensions.y,body.dimensions.z);
                        ImGui::Text("Position %.5g %.5g %.5g",body.position.x,body.position.y,body.position.z);
                        ImGui::Text("Rotation %.5g %.5g %.5g",body.rotationEuler.x,body.rotationEuler.y,body.rotationEuler.z);
                        ImGui::Text("Damping linear %.6g | angular %.6g",body.linearDamping,body.angularDamping);
                        ImGui::Text("Friction %.6g | restitution %.6g",body.friction,body.restitution);
                        ImGui::Text("Mode %u | group %u | mask %04x",unsigned(body.mode),unsigned(body.collisionGroup),unsigned(body.collisionMask));ImGui::TreePop();
                    }
                }
                if(link.sourceJoint>=0&&std::size_t(link.sourceJoint)<hairAsset_->sourceJoints.size()){
                    const auto& joint=hairAsset_->sourceJoints[link.sourceJoint];ImGui::Text("  PMX joint %d | angular spring %.3f %.3f %.3f",link.sourceJoint,joint.rotationSpring.x,joint.rotationSpring.y,joint.rotationSpring.z);
                    if(ImGui::TreeNode("Source joint")){
                        ImGui::TextWrapped("%s | bodies %d / %d | type %u",joint.name.c_str(),joint.rigidBodyA,joint.rigidBodyB,unsigned(joint.type));
                        const auto vector=[](const char* label,const glm::dvec3& v){ImGui::Text("%s %.5g %.5g %.5g",label,v.x,v.y,v.z);};
                        vector("Position",joint.position);vector("Rotation",joint.rotationEuler);
                        vector("Translation min",joint.translationMinimum);vector("Translation max",joint.translationMaximum);
                        vector("Rotation min",joint.rotationMinimum);vector("Rotation max",joint.rotationMaximum);
                        vector("Translation spring",joint.translationSpring);ImGui::TreePop();
                    }
                }
                ImGui::PopID();
            }
        }
        ImGui::Text("Preserved PMX records: %zu bodies / %zu joints",hairAsset_->sourceRigidBodies.size(),hairAsset_->sourceJoints.size());
        if(ImGui::TreeNode("Hair asset diagnostics")){for(const auto& diagnostic:hairAsset_->diagnostics)ImGui::BulletText("%s",diagnostic.c_str());ImGui::TreePop();}
        if(ImGui::Button("Export hair physics asset")){if(!hairExport_[0])SetText(hairExport_,Utf8(Path(MODEL_SOURCE_DIR)/"Asset/Generated/HairRevision.hair.json"));ImGui::OpenPopup("Export hair asset");}
        if(ImGui::BeginPopupModal("Export hair asset",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
            ImGui::SetNextItemWidth(650);ImGui::InputText("New .hair.json file",hairExport_.data(),hairExport_.size());
            if(ImGui::Button("Save hair asset"))Try([&]{auto copy=*hairAsset_;copy.settings=hairSettings_;std::string error;Check(SaveHairAsset(copy,*asset_,Path(hairExport_.data()),&error),error);status_="Exported hair physics asset";ImGui::CloseCurrentPopup();});
            ImGui::SameLine();if(ImGui::Button("Cancel hair export"))ImGui::CloseCurrentPopup();ImGui::EndPopup();
        }
    }
    template<class Function> void Try(Function function){try{function();lastError_.clear();}catch(const std::exception& e){lastError_=e.what();}}
    void ApplyDisplayStudy(int mode){
        Check(mode>=0&&mode<=2,"Invalid source display study");
        if(mode){
            auto& cube=displayCubes_[mode-1];
            if(!cube){std::string error;const auto path=Path(MODEL_SOURCE_DIR)/"Asset/Diana"/(mode==1?"cc-st30-100-display.lutbin":"cc-st30-200-display.lutbin");
                const auto table=Model::LoadStrandTextureTable(path,&error);Check(bool(table),error);
                Check(table->width==33&&table->height==1089,"Source display cube dimensions");
                auto data=std::make_shared<Render::DisplayColorCube>();data->edge=33;data->texels=table->texels;cube=data;
            }
            const auto coefficients=Model::EditorPreview::SourceHairDisplay::MakeSectionStudy(1.f,.22f,.4f,1.f,1.f);
            auto& display=pbrSettings_.sourceDisplay;display.cube=cube;
            for(int i=0;i<4;++i){display.curve0[i]=coefficients.curve0[i];display.curve1[i]=coefficients.curve1[i];display.curve2[i]=coefficients.curve2[i];}
        }
        displayStudy_=mode;pbrSettings_.sourceDisplay.enabled=mode!=0;pbr_.ResetHistory();
    }
    void ApplyStrandMaterialProfile(int dsVolumeOverride=-1){
        const auto& values=characterMaterials_->StrandParameters();auto& m=strandHair_.material;
        const auto scalar=[&](const char* key,float& target){auto it=values.find(key);if(it!=values.end())if(const auto p=std::get_if<float>(&it->second))target=*p;};
        const auto flag=[&](const char* key,bool& target){auto it=values.find(key);if(it!=values.end())if(const auto p=std::get_if<bool>(&it->second))target=*p;};
        const auto integer=[&](const char* key,std::uint32_t& target){auto it=values.find(key);if(it!=values.end())if(const auto p=std::get_if<float>(&it->second)){Check(std::isfinite(*p)&&*p>=0&&*p<=128&&std::floor(*p)==*p,"Invalid integer strand profile parameter: "+std::string(key));target=std::uint32_t(*p);}};
        const auto color=[&](const char* key,glm::vec3& target){auto it=values.find(key);if(it!=values.end()){if(const auto p=std::get_if<glm::vec3>(&it->second))target=*p;else if(const auto p=std::get_if<glm::vec4>(&it->second))target=glm::vec3(*p);}};
        color("rootColor",m.rootColor);color("tipColor",m.tipColor);scalar("roughness",m.roughness);scalar("secondaryRoughness",m.secondaryRoughness);scalar("specularStrength",m.specularStrength);scalar("secondarySpecular",m.secondarySpecular);scalar("transmission",m.transmission);scalar("widthScale",m.widthScale);scalar("opacity",m.opacity);scalar("cuticleShift",m.cuticleShift);
        flag("sourceColor",m.sourceColor);color("gradientWeights",m.gradientWeights);color("hsvVariation",m.hsvVariation);
        flag("useNttLookup",m.useNttLookup);
        flag("useNativeMomentLut",m.useNativeMomentLut);
        flag("strandMotionVectors",pbrSettings_.strandMotionVectors);
        flag("areaPreservingCoverage",m.areaPreservingCoverage);
        scalar("minWidthMetres",m.minWidthMetres);scalar("maxWidthMetres",m.maxWidthMetres);
        if(const auto it=values.find("colorRandomOffsets");it!=values.end())if(const auto p=std::get_if<glm::vec4>(&it->second))m.colorRandomOffsets=*p;
        if(const auto it=values.find("nativeMomentParameters");it!=values.end())if(const auto p=std::get_if<glm::vec4>(&it->second))m.nativeMomentParameters=*p;
        for(const auto& [name,path]:characterMaterials_->StrandAssets()){
            std::string error;
            if(name=="colorCoordinates"){
                std::vector<Render::StrandColorCoordinate> coordinates;const auto& counts=strandHair_.simulation.Asset()->header;
                Check(LoadStrandColorCoordinates(path,counts.renderVertexCount,counts.strandCount,coordinates,&error),error);
                Check(strandHair_.SetColorCoordinates(std::move(coordinates)),strandHair_.Error());
            }else{
                auto table=LoadStrandTextureTable(path,&error);Check(bool(table),error);
                if(name=="gradientTable")m.gradientTable=std::move(table);else if(name=="nttTable")m.nttTable=std::move(table);
                else if(name=="nativeMomentTable"){
                    Check(table->width==64&&table->height==1024&&table->texels.size()==65536,"Native hair DSLUT expects a complete64x64x16 atlas");
                    for(const auto& texel:table->texels)for(int channel=0;channel<4;++channel)
                        Check(texel[channel]>=0&&texel[channel]<=1,"Native hair DSLUT coefficient outside [0,1]");
                    m.nativeMomentTable=std::move(table);
                }
            }
        }
        Check(!m.sourceColor||(m.gradientTable&&characterMaterials_->StrandAssets().contains("colorCoordinates")),"Source hair color requires a gradient table and authored appearance coordinates");
        flag("physicalScattering",m.physicalScattering);scalar("ior",m.ior);scalar("longitudinalRoughness",m.longitudinalRoughness);scalar("azimuthalRoughness",m.azimuthalRoughness);scalar("absorptionScale",m.absorptionScale);scalar("multipleScattering",m.multipleScattering);scalar("scatteringDensity",m.scatteringDensity);
        flag("sourceRoughnessMapping",m.sourceRoughnessMapping);
        scalar("albedoBlendRate",m.albedoBlendRate);scalar("indirectLightingMultiplier",m.indirectLightingMultiplier);
        flag("spatialVolume",m.spatialVolume);flag("normalizedVolumeWidth",m.normalizedVolumeWidth);flag("receiveOpaqueShadows",m.receiveOpaqueShadows);flag("sourceSpatialIbl",m.sourceSpatialIbl);flag("sourceSegmentShading",m.sourceSegmentShading);flag("sourceCurveGeometry",m.sourceCurveGeometry);scalar("volumeVoxelSize",m.volumeVoxelSize);integer("volumeResolution",m.volumeResolution);
        if(const auto it=values.find("opaqueShadowPower");it!=values.end())Check(std::holds_alternative<float>(it->second),"Hair profile opaqueShadowPower must be numeric");
        scalar("opaqueShadowPower",m.opaqueShadowPower);
        Check(std::isfinite(m.opaqueShadowPower)&&m.opaqueShadowPower>=.05f&&m.opaqueShadowPower<=4.f,"Hair profile opaqueShadowPower must be finite and within0.05..4");
        flag("volumeHardwareFiltering",m.volumeHardwareFiltering);
        flag("sourceDSVolume",m.sourceDSVolume);
        if(dsVolumeOverride>=0)m.sourceDSVolume=dsVolumeOverride!=0;
        float injectionRate=m.volumeInjectionRate,lodFraction=m.volumeInjectionLodFraction,injectionQuality=float(m.volumeInjectionQuality);
        bool injectionMinLod=m.volumeInjectionMinLodFactor;
        scalar("volumeInjectionRate",injectionRate);scalar("volumeInjectionLodFraction",lodFraction);
        scalar("volumeInjectionQuality",injectionQuality);flag("volumeInjectionMinLodFactor",injectionMinLod);
        ApplyNativeDSInjectionParameters(m,injectionRate,lodFraction,injectionQuality,injectionMinLod);
        scalar("volumeRasterTraceStep",m.volumeRasterTraceStep);
        scalar("volumeTraceLimit",m.volumeTraceLimit);
        scalar("volumeInterpolationSamples",m.volumeInterpolationSamples);
        flag("volumePreserveNearField",m.volumePreserveNearField);
        flag("volumeExactIntegration",m.volumeExactIntegration);
        flag("volumeProxyBlockers",m.volumeProxyBlockers);
        flag("volumeStableOpaqueCache",m.volumeStableOpaqueCache);flag("volumePointIbl",m.volumePointIbl);
        scalar("volumeAverageWidth",m.volumeAverageWidth);
        flag("sourceDirectScattering",m.sourceDirectScattering);flag("volumeOpaqueBlockers",m.volumeOpaqueBlockers);
        flag("castDirectionalShadows",m.castDirectionalShadows);scalar("volumeDepthThreshold",m.volumeDepthThreshold);
        flag("volumeSkyTransmission",m.volumeSkyTransmission);flag("dualScatteringIbl",m.dualScatteringIbl);scalar("backscatterScale",m.backscatterScale);
        scalar("volumeRasterWidthScale",m.volumeRasterWidthScale);scalar("volumeTraceStep",m.volumeTraceStep);scalar("volumeBiasVoxels",m.volumeBiasVoxels);
        Check(!m.sourceDSVolume||(m.volumeResolution==32||m.volumeResolution==64||m.volumeResolution==128),
            "Native DSVolume profile requires volumeResolution32,64 or128");
        Check(!m.sourceDSVolume||(std::isfinite(m.volumeTraceLimit)&&m.volumeTraceLimit>=1&&std::floor(m.volumeTraceLimit)==m.volumeTraceLimit&&
            std::isfinite(m.volumeInterpolationSamples)&&m.volumeInterpolationSamples>=0&&std::floor(m.volumeInterpolationSamples)==m.volumeInterpolationSamples),
            "Native DSVolume traceLimit and interpolationSamples must be whole nonnegative values; traceLimit must be positive");
    }
    void ApplyReferenceCamera(bool enabled){
        Check(!enabled||dianaPreview_,"The reference camera profile requires Diana's extracted metre-space asset");
        if(enabled){
            EditorPreview::ValidateDianaCameraReference(referenceCamera_);
            if(!referenceCameraEnabled_){
                referenceCameraSnapshot_={placement_,cameraFocus_,yaw_,pitch_,distance_,cameraVerticalFovDegrees_,trackedCameraBone_,true};
                referenceLightingReapplied_=false;
            }
            referenceCameraEnabled_=true;
            placement_=EditorPreview::DianaReferencePlacement(referenceCameraSnapshot_.placement,referenceCamera_);
            cameraFocus_=glm::vec3(placement_*glm::dvec4(referenceCamera_.focusLocal,1));
            yaw_=glm::radians(referenceCamera_.yawDegrees);pitch_=glm::radians(referenceCamera_.pitchDegrees);
            distance_=float(glm::length(glm::dvec3(referenceCameraSnapshot_.placement[0])))*referenceCamera_.distanceLocalMetres;
            cameraVerticalFovDegrees_=referenceCamera_.verticalFovDegrees;trackedCameraBone_=-1;
        }else if(referenceCameraEnabled_&&referenceCameraSnapshot_.valid){
            placement_=referenceCameraSnapshot_.placement;cameraFocus_=referenceCameraSnapshot_.focus;
            yaw_=referenceCameraSnapshot_.yaw;pitch_=referenceCameraSnapshot_.pitch;distance_=referenceCameraSnapshot_.distance;
            cameraVerticalFovDegrees_=referenceCameraSnapshot_.verticalFov;trackedCameraBone_=referenceCameraSnapshot_.trackedBone;
            referenceCameraEnabled_=false;referenceCameraSnapshot_.valid=false;
        }
        // All body/garment/strand extraction already consumes placement_. The
        // latter also moves its local capsules and DSVolume model together.
        if(pbrReady_){pbr_.InvalidateOpaqueDepth();pbr_.ResetHistory();}
    }
    Json::Value BuildCameraReferenceMetadata()const{
        auto result=EditorPreview::DianaCameraReferenceMetadata(referenceCamera_,referenceCameraEnabled_,
            referenceCameraSnapshot_.valid?referenceCameraSnapshot_.placement:placement_,placement_,camera_.view,camera_.projection,
            camera_.position,cameraFocus_,previewWidth_,previewHeight_);
        result["actualVerticalFovDegrees"]=cameraVerticalFovDegrees_;result["actualYawDegrees"]=glm::degrees(yaw_);
        result["actualPitchDegrees"]=glm::degrees(pitch_);result["actualDistanceDisplayUnits"]=distance_;
        result["strandSimulationPaused"]=strandHair_.paused;result["bindPose"]=bindPose_;
        result["lightingReappliedAfterReferenceCamera"]=referenceLightingReapplied_;
        result["lightingApplicationOrder"]="Existing lights are initialized before reference alignment. The camera profile does not call ApplyLightingPreset; later explicit light UI changes are tracked separately.";
        result["describesLastExecutedFrame"]=true;
        return result;
    }
    Json::Value CameraReferenceMetadata()const{
        if(!completedCameraReferenceMetadata_.isNull())return completedCameraReferenceMetadata_;
        auto result=BuildCameraReferenceMetadata();result["describesLastExecutedFrame"]=false;return result;
    }
    void ReferenceCameraControls(){
        if(!ImGui::TreeNode("参考构图 / Reference camera"))return;
        bool enabled=referenceCameraEnabled_;
        if(ImGui::Checkbox("实机参考构图 / Game-shot study",&enabled))Try([&]{ApplyReferenceCamera(enabled);});
        ImGui::TextWrapped("可选刚体姿态与相机研究；发丝、人物和遮挡体积一起变换。未恢复原表情、眼睛视线或动态发型，不更改光照与发色。");
        if(referenceCameraEnabled_){
            bool changed=false;
            changed|=ImGui::SliderFloat("垂直视野 / FOV",&referenceCamera_.verticalFovDegrees,20,75,"%.2f deg");
            if(ImGui::IsItemHovered())ImGui::SetTooltip("真实透视视野；单张截图不能唯一确定焦距，初始值保留宿主43度。");
            changed|=ImGui::SliderFloat("方位 / Yaw",&referenceCamera_.yawDegrees,-45,45,"%.2f deg");
            changed|=ImGui::SliderFloat("仰俯 / Pitch",&referenceCamera_.pitchDegrees,-40,40,"%.2f deg");
            changed|=ImGui::SliderFloat("距离 / Source metres",&referenceCamera_.distanceLocalMetres,.15f,.8f,"%.4f m");
            if(ImGui::IsItemHovered())ImGui::SetTooltip("以原人物米单位记录，与预览归一化缩放同步；不缩放截图。");
            changed|=ImGui::SliderFloat3("整体姿态 / XYZ",&referenceCamera_.modelEulerDegrees.x,-35,35,"%.2f deg");
            if(ImGui::IsItemHovered())ImGui::SetTooltip("绕眼中点的整个人物刚体旋转；当前静态GLB没有可恢复笑容的脸部骨架。");
            changed|=ImGui::SliderFloat2("构图主点 / UV",&referenceCamera_.principalPoint.x,.1f,.9f,"%.4f");
            if(ImGui::IsItemHovered())ImGui::SetTooltip("真实离轴投影视锥：左上角为(0,0)，右下角为(1,1)，不是图片平移或拉伸。");
            changed|=ImGui::Checkbox("保持参考宽高比 / Aspect",&referenceCamera_.lockReferenceAspect);
            if(changed)Try([&]{ApplyReferenceCamera(true);});
            if(ImGui::Button("恢复参考参数 / Reset study")){referenceCamera_={};Try([&]{ApplyReferenceCamera(true);});}
            if(ImGui::Button("导出相机与锚点 / Export"))Try([&]{
                const auto path=Path(MODEL_SOURCE_DIR)/"output/Diana/Quality14/reference-camera.json";
                EditorPreview::SaveDianaCameraReferenceMetadata(path,CameraReferenceMetadata());status_="Camera report: "+Utf8(path);
            });
        }
        ImGui::TreePop();
    }
    void FrameCharacter(bool face){
        if(referenceCameraEnabled_)ApplyReferenceCamera(false);
        if(!face){FramePreviewRegions();return;}
        // The extracted head and the character share the original metre space.
        trackedCameraBone_=-1;cameraFocus_=glm::vec3(placement_*glm::dvec4(0,.958,.015,1));
        distance_=float(glm::length(glm::dvec3(placement_[0]))*.34);distance_=std::clamp(distance_,.6f,20.f);pitch_=0;
    }
    void CharacterMaterialDetails(){
        if(!characterMaterials_){ImGui::TextWrapped("No extracted character material profile.");return;}
        ImGui::TextColored(ImVec4(.4f,.8f,1,1),"DIANA / EXTRACTED PBR");
        ImGui::Text("%zu meshes / %llu triangles",resource_->Source().meshes.size(),static_cast<unsigned long long>(resource_->Source().TriangleCount()));
        ImGui::Text("%zu materials / %zu texture bindings",characterMaterialReport_.matchedMaterials,characterMaterialReport_.textureBindings);
        for(const auto& [category,count]:characterMaterialReport_.categories)ImGui::Text("%s: %zu",category.c_str(),count);
        ImGui::TextWrapped("Render / MaterialLab PBR. Skin diffusion, eye-shell postprocess and spatial fiber self-occlusion; RT/realtime GI disabled.");
        if(ImGui::Button("全身 / Full"))FrameCharacter(false);ImGui::SameLine();if(ImGui::Button("面部 / Face"))FrameCharacter(true);
        ReferenceCameraControls();
        const char* lights[]={"Studio","Side","Back","Reference study","ST30_101 cube study","Reference neutral key study"};if(ImGui::Combo("Lighting",&lightingPreset_,lights,6))ApplyLightingPreset();
        if(ImGui::IsItemHovered())ImGui::SetTooltip("Reference neutral key: host lighting calibration at equal Rec.709 incoming luminance.\nOnly the cool environment lobe and primary point tint change; not recovered game illumination.");
        if(ImGui::Checkbox("源场景环境输入 / Source environment",&sourceEnvironmentOverride_))ApplyEnvironmentPreservingDirect();
        if(ImGui::IsItemHovered())ImGui::SetTooltip("独立切换原HDR与六向间接光输入，保留当前直射灯和曝光，供同条件对比。ST30_101已有第三章检查点关联；具体房间、探针混合与运行时倍率仍需确认。");
        if(ImGui::Checkbox("源直射输入 / Source direct",&sourceDirectOverride_))Try([&]{ApplyDirectPreservingEnvironment();});
        if(ImGui::IsItemHovered())ImGui::SetTooltip("独立切换章节候选太阳和局部灯；头发使用完整灯表、有限范围和锥形分布。原截图位置、动态灯状态和场景阴影尚未捕获。");
        if(ImGui::SliderFloat("Sun input scale",&directSunScale_,0.f,4.f,"%.3f")){ApplyDirectInputScales(false);if(pbrReady_)pbr_.ResetHistory();}
        if(ImGui::IsItemHovered())ImGui::SetTooltip("太阳直射输入倍率。0关闭太阳，用于隔离体积自阴影；同时作用于头发和不透明模型，保持环境光、曝光与材质参数。");
        if(ImGui::SliderFloat("Punctual input scale",&directPunctualScale_,0.f,4.f,"%.3f")){ApplyDirectInputScales(false);if(pbrReady_)pbr_.ResetHistory();}
        if(ImGui::IsItemHovered())ImGui::SetTooltip("局部灯直射RGB倍率。0关闭四点灯或完整源灯表及不透明代理；保留衰减/范围/IES参数，不改变发色和散射强度。");
        int sunFilter=int(pbrSettings_.shadows.filter);
        if(ImGui::Combo("Sun shadow filter",&sunFilter,"Tent (host)\0Source Poisson8 study\0")){pbrSettings_.shadows.filter=Render::DirectionalShadowFilter(sunFilter);if(pbrReady_)pbr_.ResetHistory();}
        if(ImGui::IsItemHovered())ImGui::SetTooltip("Tent保持此前4x4滤波。Source Poisson8按源固定八点分布采样，原运行时半径写入尚未闭合；不是PCSS或太阳角半径。只改变CSM接收滤波，保留DSVolume和光能。");
        ImGui::BeginDisabled(pbrSettings_.shadows.filter!=Render::DirectionalShadowFilter::SourcePoisson8);
        if(ImGui::SliderFloat("Sun shadow radius UV",&pbrSettings_.shadows.sourcePoissonRadius,0.f,.05f,"%.5f")){if(pbrReady_)pbr_.ResetHistory();}
        if(ImGui::IsItemHovered())ImGui::SetTooltip("第一层级归一化UV半径，其它层级保持同世界尺度。默认.003来自作者场景字段，运行时writer未核实；0用于八点滤波退化对照。不改变曝光或SSS半径。");
        if(ImGui::SliderFloat("Surface shadow radius UV",&pbrSettings_.shadows.sourceSurfaceRadius,0.f,.05f,"%.5f")){if(pbrReady_)pbr_.ResetHistory();}
        if(ImGui::IsItemHovered())ImGui::SetTooltip("仅覆盖皮肤、衣服等不透明模型接收的太阳CSM滤波。0继承上方半径；不改变头发接收半径、DS或曝光。这是宿主近景适配，原皮肤运行时阴影律尚未证实。");
        ImGui::EndDisabled();
        if(sourceDirectOverride_&&sourceHairDirect_&&ImGui::TreeNode("直射输入说明 / Direct inputs")){
            ImGui::TextWrapped("%s",sourceHairDirect_->label.c_str());
            ImGui::TextWrapped("Parameter origin: %s",sourceHairDirect_->parameterOrigin.c_str());
            ImGui::Text("Hair: %u source lights",pbrSettings_.strandSourceDirect.count);
            if(ImGui::SliderFloat("直射单位 / Direct unit EV",&sourceHairDirect_->hostUnitEV,-16,0,"%.2f"))Try([&]{ApplyDirectPreservingEnvironment();});
            if(ImGui::IsItemHovered())ImGui::SetTooltip("宿主光度单位适配；不是已恢复的原相机曝光，不改头发底色或散射强度。");
            ImGui::TextWrapped("Opaque four-light proxy: %.2f%% of unshadowed local Y at groom center; hair uses the full table.",appliedSourceDirect_.proxyLuminanceCoverage*100.);
            ImGui::TreePop();
        }
        if(sourceHairLighting_){
            auto& indirect=pbrSettings_.strandIndirect;
            if(ImGui::Checkbox("Source indirect cube",&indirect.enabled))pbr_.ResetHistory();
            ImGui::SameLine();ImGui::TextDisabled("(?)");if(ImGui::IsItemHovered())ImGui::SetTooltip("源StrandIndirectLightCS按方向符号和分量平方组合六向系数，不额外除pi。ST30_101已由原检查点目录关联第三章；当前输入仍为单个立方图mip轴心候选值，完整探针场、混合和截图房间尚未确认。");
            if(ImGui::TreeNode("Source IBL controls")){
                ImGui::TextWrapped("%s",sourceHairLighting_->label.c_str());
                if(ImGui::SliderFloat("Host hair ambient",&indirect.hostAmbientScale,0,1,"%.3f"))pbr_.ResetHistory(false);
                ImGui::SameLine();ImGui::TextDisabled("(?)");if(ImGui::IsItemHovered())ImGui::SetTooltip("仅缩放源探针启用时额外叠加给头发的宿主常量填光。0遵循已追踪的原IBL采样链，1恢复旧预览；不改探针、材质、皮肤照明或投影参数。");
                if(ImGui::SliderFloat("HDR unit EV",&indirect.exposureEV,-16,0,"%.2f")){if(lightingPreset_==4||sourceEnvironmentOverride_)pbrSettings_.sky.intensity=std::exp2(indirect.exposureEV);pbr_.ResetHistory();}
                ImGui::SameLine();ImGui::TextDisabled("(?)");if(ImGui::IsItemHovered())ImGui::SetTooltip("宿主光度单位适配，默认-7 EV；同时缩放源HDR与头发六向系数。不是已恢复的原场景曝光，不修改材质.8或发色。");
                if(ImGui::SliderFloat("Probe multiplier",&indirect.multiplier,0,4,"%.3f"))pbr_.ResetHistory();
                ImGui::SameLine();ImGui::TextDisabled("(?)");if(ImGui::IsItemHovered())ImGui::SetTooltip("原CPU代码将Diana静态字段.8分别写入生成端CB+24与材质端+168，两处各乘一次，静态净倍率.64。这里控制生成端；运行时覆盖值未捕获，不合并或删除材质端.8。");
                float degrees=glm::degrees(indirect.rotation);if(ImGui::SliderFloat("Probe rotation",&degrees,-180,180,"%.1f deg")){indirect.rotation=glm::radians(degrees);if(lightingPreset_==4||sourceEnvironmentOverride_)pbrSettings_.sky.rotation=indirect.rotation;pbr_.ResetHistory();}
                ImGui::SameLine();ImGui::TextDisabled("(?)");if(ImGui::IsItemHovered())ImGui::SetTooltip("世界Y轴环境旋转；源LC对象旋转为单位四元数，默认0。保持HDR背景/反射与头发六向输入方向一致。");
                ImGui::TextWrapped("Original HDR, candidate probe coefficients; selected direct-light rig retained. No RT/realtime GI.");
                ImGui::TreePop();
            }
        }
        if(ImGui::Checkbox("Eye shell postprocess",&pbrSettings_.eyeShell.enabled))pbr_.ResetHistory();
        ImGui::SameLine();ImGui::TextDisabled("(?)");if(ImGui::IsItemHovered())ImGui::SetTooltip("只处理标记的原始EyeShell网格；读取独立HDR和深度实现遮蔽/模糊。关闭后显示底层眼球，便于同视角对照。");
        ImGui::SeparatorText("MATERIAL / 原始材质槽");
        selectedCharacterMaterial_=std::clamp(selectedCharacterMaterial_,0,int(characterMaterialEdits_.size())-1);
        if(ImGui::BeginCombo("##character-material",characterMaterialEdits_[selectedCharacterMaterial_].name.c_str())){for(std::size_t i=0;i<characterMaterialEdits_.size();++i)if(ImGui::Selectable(characterMaterialEdits_[i].name.c_str(),selectedCharacterMaterial_==int(i)))selectedCharacterMaterial_=int(i);ImGui::EndCombo();}
        auto& m=characterMaterialEdits_[selectedCharacterMaterial_];
        const auto set=[&](const char* name,const Render::Material::ParameterValue& value){Try([&]{std::string error;Check(resource_->Materials().SetParameter(resource_->MaterialHandles()[selectedCharacterMaterial_],name,value,&error),error);pbr_.ResetHistory();});};
        const auto help=[](const char* text){ImGui::SameLine();ImGui::TextDisabled("(?)");if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",text);};
        auto& values=characterParameterEdits_.at(selectedCharacterMaterial_);
        const auto scalar=[&](const char* key,const char* label,const char* explanation,float low,float high){
            const auto it=values.find(key);if(it==values.end())return;
            if(auto* value=std::get_if<float>(&it->second)){ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x*.50f);if(ImGui::SliderFloat(label,value,low,high,"%.3f"))set(key,*value);help(explanation);}
        };
        const auto flag=[&](const char* key,const char* label,const char* explanation){
            const auto it=values.find(key);if(it==values.end())return;
            if(auto* value=std::get_if<bool>(&it->second)){if(ImGui::Checkbox(label,value))set(key,*value);help(explanation);}
        };
        const auto component=[&](const char* key,int index,const char* label,const char* explanation,float low,float high){
            const auto it=values.find(key);if(it==values.end())return;
            float* value=nullptr;
            if(auto* v=std::get_if<glm::vec4>(&it->second);v&&index<4)value=&(*v)[index];
            else if(auto* v=std::get_if<glm::vec3>(&it->second);v&&index<3)value=&(*v)[index];
            else if(auto* v=std::get_if<glm::vec2>(&it->second);v&&index<2)value=&(*v)[index];
            if(value){ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x*.5f);if(ImGui::SliderFloat(label,value,low,high,"%.3f"))set(key,it->second);help(explanation);}
        };
        if(ImGui::ColorEdit3("Base color",&m.baseColor.x))set("baseColor",m.baseColor);
        ImGui::BeginDisabled(m.textures[3]>=0);
        if(ImGui::SliderFloat("Roughness",&m.roughness,.045f,1))set("roughness",m.roughness);
        ImGui::EndDisabled();ImGui::BeginDisabled(m.textures[2]>=0);
        if(ImGui::SliderFloat("Metallic",&m.metallic,0,1))set("metallic",m.metallic);
        ImGui::EndDisabled();
        if(ImGui::SliderFloat("Normal scale",&m.normalScale,0,2))set("normalScale",m.normalScale);
        ImGui::TextWrapped("Scalar maps already include source factors. Roughness/metallic sliders control only materials without the corresponding map.");
        const std::array<const char*,8> slots{"Albedo sRGB","Normal","Metallic","Roughness","AO","Control / Eye packed","Detail / Sclera","Emission / Eye reflection"};
        if(ImGui::TreeNode("纹理绑定 / Textures")){for(std::size_t i=0;i<slots.size();++i)if(m.textures[i]>=0){ImGui::TextWrapped("%s: %s",slots[i],resource_->Source().textures[m.textures[i]].name.c_str());}ImGui::TreePop();}
        if(values.contains("reSkinEnabled")&&ImGui::CollapsingHeader("SKIN / 皮肤散射",ImGuiTreeNodeFlags_DefaultOpen)){
            auto& s=pbrSettings_.skinDiffusion;
            if(ImGui::Checkbox("Spatial diffusion",&s.enabled))pbr_.ResetHistory();help("只扩散选中皮肤的漫反射；保留高光，使用深度、法线和材质边界保护。");
            int kernel=int(s.kernel);ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x*.5f);
            if(ImGui::Combo("Diffusion kernel",&kernel,"Gaussian\0Source Burley candidate\0")){s.kernel=Render::SkinDiffusionKernel(kernel);pbr_.ResetHistory();}
            help("Gaussian为此前的重建核；Source Burley为源SSSSSMainPass发现的二维径向核，但Diana实际运行分支和profile选择尚未证实。");
            if(s.kernel==Render::SkinDiffusionKernel::SourceBurley){int taps=int(s.diskSamples);ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x*.5f);if(ImGui::SliderInt("Disk samples",&taps,8,64)){s.diskSamples=std::uint32_t(taps);pbr_.ResetHistory();}help("候选二维径向核的确定性圆盘采样数。增加可降低采样误差；原始时序噪声/历史路径尚未移植。");}
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x*.5f);
            if(ImGui::SliderFloat("Radius mm",&s.radiusMillimeters,.05f,5.f,"%.2f"))pbr_.ResetHistory();help("Gaussian的sigma或候选径向核的d，单位毫米；随模型尺度和相机投影换算。数值仍为重建参数。");
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x*.5f);
            if(ImGui::SliderFloat("SSS strength",&s.strength,0,1))pbr_.ResetHistory();help("空间扩散总混合比例；0可查看无空间散射的基准。");
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x*.5f);
            if(ImGui::SliderFloat("Albedo detail",&s.albedoPreservation,0,1))pbr_.ResetHistory();help("底色与光照的分解指数：0扩散全部底色，0.5平方根分解，1完整保留纹理中的眉毛、唇纹与毛孔。此项属于重建策略。");
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x*.5f);
            if(ImGui::DragFloat3("RGB radii",&s.channelRadii.x,.01f,.01f,4.f,"%.2f"))pbr_.ResetHistory();help("RGB扩散距离的独立倍率。原profile记录到Shader常量的转换及运行时选择尚未核实。");
            flag("skinDiffusionEnabled","Material diffusion","原始SSS_Channel为3时启用；Body槽原始值0，不参与扩散。");
            scalar("skinDiffusionBlend","Material blend","源SSS_BlendRate，面部还乘原控制图R。",0,1);
            scalar("reSkinSpecularReflectance","Reflectance","源SpecularReflectance：介电F0=0.04×该值，面部再乘控制图A。",0,2);
            scalar("reSkinTranslucency","Translucency","源Translucency，面部乘控制图R；默认MDF为0。",0,1);
        }
        if(values.contains("eyeEnabled")&&ImGui::CollapsingHeader("EYE / 眼部折射",ImGuiTreeNodeFlags_DefaultOpen)){
            flag("eyeEnabled","Eye shading","使用原始UV0虹膜与UV1巩膜坐标；关闭后显示通用PBR基准。");
            scalar("eyeRefractionStrength","Refraction","虹膜位移/视差强度；0可与无折射结果对照。",0,1);
            scalar("ior","Cornea IOR","源MDF值1.4；当前用于Fresnel适配，原Shader直接消费IOR的路径尚未确认。虹膜视差使用源深度公式。",1.01f,2.f);
            component("eyeIrisShape",0,"Iris radius","源Iris_Radius，控制虹膜与巩膜的边界。",.1f,1.f);
            component("eyeIrisShape",3,"Iris rotation","源左右眼独立的虹膜旋转角，单位度。",-180,180);
            component("eyeParallax",0,"Lens zoom","源LensDepthZoom，改变虹膜采样缩放。",0,1);
            component("eyeParallax",2,"Iris depth","源IrisDepth，控制虹膜位移的深度。",0,1);
            component("eyeParallax",3,"Ray offset","源IrisOffset：倾斜视线下的虹膜位移尺度。",0,1);
            component("eyeDisplacement",0,"POM layers","源DisplacementLayers，控制深度相交采样步数；增加会增加GPU开销。",4,32);
            component("eyeDisplacement",1,"Displacement","源DisplacementHeight，配合原始虹膜位移图。",0,1);
            component("eyeDisplacement",2,"Pupil radius","源Pupil_Radius，允许负值；保留左右眼独立的默认参数。",-.4f,.4f);
            component("eyePupilOffset",0,"Pupil offset X","源瞳孔水平偏移，左右眼分别保存。",-.3f,.3f);
            component("eyePupilOffset",1,"Pupil offset Y","源瞳孔垂直偏移。",-.3f,.3f);
            component("eyeSpecularReflectance",0,"Lens F0","源LensSpecReflectance；IOR为1.4时直接保留该反射率。",0,.15f);
            component("eyeSpecularReflectance",1,"Sclera F0","源ScleraSpecReflectance；巩膜与虹膜按源边界权重混合。",0,.15f);
        }
        if(values.contains("eyeShellEnabled")&&ImGui::CollapsingHeader("EYE SHELL / 眼壳后处理",ImGuiTreeNodeFlags_DefaultOpen)){
            ImGui::TextWrapped("Source mesh + independent HDR/depth snapshot. Use the global checkbox above for an A/B.");
            component("eyeShellSurface",0,"Depth window","源CheckDepthDifference；仅处理眼壳后方、深度窗口内的表面。",.001f,1.f);
            component("eyeShellSurface",1,"Shell occlusion","源OcclusionStrength=.47，与原UV轮廓/填充遮罩生成眼周遮蔽。",0,1);
            component("eyeShellSurface",2,"Shell blur","源BlurStrength=.86，径向双向采样HDR；源投影尺度到宿主的适配仍有限。",0,2);
            component("eyeShellSurface",3,"Highlight reject","源SpecBlurReduction=8，限制高亮样本向邻域扩散。",0,16);
            const auto group=[&](const char* key,const char* label,const char* explanation,float maximum){
                auto it=values.find(key);if(it==values.end())return;if(auto* value=std::get_if<glm::vec4>(&it->second)){
                    ImGui::TextUnformatted(label);help(explanation);ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x*.93f);
                    if(ImGui::DragFloat4((std::string("##")+key).c_str(),&value->x,.01f,0,maximum,"%.2f"))set(key,*value);
                }
            };
            if(ImGui::TreeNode("Occlusion shape / 遮蔽形状")){
                group("eyeShellOutlineArea","Outline area: T B I O","源Top/Bottom/Inner/OuterOutlineArea，依次为上、下、内、外边界。",2);
                group("eyeShellOutlineSmooth","Outline smooth: T B I O","四个边界的源OutlineSmooth，控制轮廓过渡。",4);
                group("eyeShellFillArea","Fill area: T B I O","四个方向的源FillArea，控制内部填充范围。",2);
                group("eyeShellFillSmooth","Fill smooth: T B I O","四个方向的源FillSmooth，控制内部填充过渡。",4);
                ImGui::TreePop();
            }
            if(ImGui::TreeNode("Blur shape / 模糊形状")){
                group("eyeShellBlurOutlineArea","Blur outline: T B I O","源四方向OutlineArea_Blur，限制径向模糊的轮廓范围。",2);
                group("eyeShellBlurOutlineSmooth","Blur outline smooth","源四方向OutlineSmooth_Blur，控制模糊轮廓过渡。",4);
                group("eyeShellBlurFillArea","Blur fill: T B I O","源四方向FillArea_Blur，控制模糊填充范围。",2);
                group("eyeShellBlurFillSmooth","Blur fill smooth","源四方向FillSmooth_Blur，控制模糊填充过渡。",4);
                ImGui::TreePop();
            }
            component("eyeShellSpecular",0,"Shell roughness","源Roughness=.01；宿主GGX保持最小稳定粗糙度，额外直接高光属于适配。",.01f,1);
            component("eyeShellSpecular",1,"Shell specular","源Specular=.85，眼壳附加高光幅度。",0,1);
            component("eyeShellSpecular",3,"Specular threshold","源SpecularClamp=1，高光输出阈值；按源Shader分支处理。",0,4);
            component("eyeShellCavity",0,"Cavity area","源CavityFillArea=1，眼角填充范围。",0,2);
            component("eyeShellCavity",1,"Cavity smooth","源CavityFillSmooth=.54，眼角填充过渡。",.01f,4);
            component("eyeShellCavity",2,"Tearline normal","源NormalStrength=.2，使用实际泪线法线图。",0,1);
            component("eyeShellCavity",3,"Depth unit adapter","宿主适配：每米模型空间对应的源线性深度单位。默认1，原运行时ZToLinear单位尚未确认；不能将DSVolume单位直接套用到眼壳。",.1f,100);
            if(ImGui::TreeNode("Fake specular / 源假高光")){
                component("eyeShellFake",0,"Fake intensity","源FakeSpecularIntensity=0，默认不叠加假高光。",0,4);
                component("eyeShellFake",1,"Fake offset X","源FakeSpecularX=-.31，假高光纹理水平偏移。",-1,1);
                component("eyeShellFake",2,"Fake offset Y","源FakeSpecularY=.03，假高光纹理垂直偏移。",-1,1);
                component("eyeShellFake",3,"Fake size","源FakeSpecularSize=1.02，假高光纹理缩放。",.01f,3);
                ImGui::TreePop();
            }
            group("eyeShellDebug","Debug: occlusion / blur / cavity / normal","源四个DebugView，0关闭、1以源诊断颜色显示各遮罩。",1);
        }
        if(ImGui::Combo("Display transform",&displayStudy_,"Host ACES\0Source ST30_100 study\0Source ST30_200 study\0"))ApplyDisplayStudy(displayStudy_);
        help("Source研究模式：原三段曲线、ACEScct域33^3 ColorCube及sRGB输出，通过Render/RHI执行。OCIO矩阵取单位阵、RGC关闭、曝光沿用宿主；尚未确认参考图的运行时显示链，默认保留ACES。");
        ImGui::SeparatorText("STRAND MATERIAL / 头发");
        auto& h=strandHair_.material;ImGui::Checkbox("Visible hair",&strandHair_.visible);
        bool hairChanged=ImGui::Checkbox("Fiber scattering",&h.physicalScattering);help("R表面反射、TT穿透、TRT内部反射与局部多次散射。关闭可对照此前双高光近似。");
        if(h.gradientTable){hairChanged|=ImGui::Checkbox("Source gradient / HSV",&h.sourceColor);help("使用原始顶点中的渐变位置与每根发丝的随机字节，选择A/B/C渐变后在线性RGB上执行源HSV公式；关闭使用此前端点均值颜色。未执行运行时BAB覆盖。");}
        if(h.nttTable){hairChanged|=ImGui::Checkbox("Native Ntt lookup",&h.useNttLookup);help("使用从游戏可执行文件提取的128×128双通道表，保留原float到半精度截断上传值。关闭后对照此前的computeDSLUT解析拟合；源RG共同控制TT方位分布。");}
        if(ImGui::Checkbox("原生逐段着色 / Segment shading",&h.sourceSegmentShading)){
            hairChanged=true;
            if(!h.sourceSegmentShading)pbrSettings_.strandNativeOit=false;
        }
        help("段起点提供视向和切线；段终点查询灯光、体积遮蔽与颜色坐标，再插值已着色端色。关闭可对照逐片元路径；不改变发丝宽度或覆盖率。");
        hairChanged|=ImGui::Checkbox("原生曲线 / Native curves",&h.sourceCurveGeometry);
        help("四控制点曲线让弯曲发丝更连续；采用原硬件分支的可见性和屏幕长度细分公式，最多15段。列表调度暂由CPU完成，可能增加调度成本。关闭对照控制点直线带。");
        if(h.sourceCurveGeometry)ImGui::TextDisabled("Visible segments %llu / %llu; curve pieces %llu",
            static_cast<unsigned long long>(pbr_.Statistics().strandCurveVisibleSegments),
            static_cast<unsigned long long>(pbr_.Statistics().strandCurveOriginalSegments),
            static_cast<unsigned long long>(pbr_.Statistics().strandCurvePieces));
        if(h.nativeMomentTable){
            hairChanged|=ImGui::Checkbox("Native full DSLUT",&h.useNativeMomentLut);
            help("完整64×64×16散射表，按原computeDSLUT采样估计器离线生成并量化至RGBA16F；运行时在角度、粗糙度和吸收三轴插值。关闭可对照此前16×16当前粗糙度切片，不会重新生成完整表。");
            if(h.useNativeMomentLut&&!NativeHairMomentKeyMatches(h))ImGui::TextColored(ImVec4(1,.72f,.28f,1),"DSLUT key changed: legacy16x16 fallback");
            else ImGui::TextDisabled("DSLUT: %s",NativeHairMomentStatus(h));
            ImGui::TextDisabled("Baked IOR %.3f / F0 %.6f / az %.3f / tilt %.3f",h.nativeMomentParameters.x,h.nativeMomentParameters.y,h.nativeMomentParameters.z,h.nativeMomentParameters.w);
            help("离线表不可变。改变IOR、方位粗糙度或鳞片倾角时自动回退旧切片；恢复原值即可复用缓存。纵向粗糙度使用表的64层动态采样，不触发表失配。");
        }
        int oitMode=pbrSettings_.strandNativeOit?(pbrSettings_.strandNativeOitLayers==8?3:2):(pbrSettings_.strandFrontLayerTransparency?1:0);
        if(ImGui::Combo("透明模式 / Hair OIT",&oitMode,"加权混合（旧）\0深度分组（宿主）\0原生 Wu + MLAB4\0原生 Wu + MLAB8\0")){
            pbrSettings_.strandNativeOit=oitMode>=2;
            if(oitMode<2)pbrSettings_.strandFrontLayerTransparency=oitMode==1;
            else {pbrSettings_.strandNativeOitLayers=oitMode==3?8u:4u;h.sourceSegmentShading=true;}
            hairChanged=true;
        }
        help("原生研究模式使用软件Wu覆盖、每像素4/8槽局部合并和全局4槽透明合成，自动启用逐段着色；可保留直线或原生曲线。采用确定的GPU像素归属调度，未复现原atomic争用或确认参考帧分支；计算与显存开销可能明显增加。深度分组保留此前宿主适配作对照。");
        if(pbrSettings_.strandNativeOit)ImGui::TextDisabled("Native OIT %s | K%u | %u compute passes | %.2f MiB",
            pbr_.Statistics().strandNativeOitActive?"active":"inactive",
            pbr_.Statistics().strandNativeOitLayers,pbr_.Statistics().strandNativeOitComputePasses,
            double(pbr_.Statistics().strandNativeOitBytes)/(1024.0*1024.0));
        ImGui::BeginDisabled(pbrSettings_.strandNativeOit);
        hairChanged|=ImGui::Checkbox("Area preserving coverage",&h.areaPreservingCoverage);help("修复旧软边重复削减亚像素覆盖的问题，保留投影物理宽度的积分面积。原生Wu模式使用源覆盖公式，不使用此宿主滤波选项。");
        ImGui::EndDisabled();
        int quality=pbrSettings_.strandFrontDepthGroups==2?0:pbrSettings_.strandFrontDepthGroups==3?1:2;
        int hairDebug=int(pbrSettings_.strandDebugMode);
        if(ImGui::Combo("Hair diagnostics",&hairDebug,"Beauty\0Base color\0Direct\0Indirect\0Direct ballistic\0Direct multiple\0IBL ballistic\0IBL multiple\0Reflection R\0Transmission TT\0Internal TRT\0Sun N\0IBL N+/N-\0Opaque visibility\0IBL incident\0")){
            pbrSettings_.strandDebugMode=Render::StrandDebugMode(hairDebug);hairChanged=true;
        }
        help("分开显示原发色、白色反射、透射、多重散射和遮挡，定位偏灰与阴影来源。仅影响头发显示，保留实际覆盖率；Beauty恢复正常渲染，不修改材质。");
        ImGui::BeginDisabled(pbrSettings_.strandNativeOit);
        hairChanged|=ImGui::Checkbox("Paired transparency",&pbrSettings_.strandPairedLayerTransparency);
        help("一次绘制同时累积HDR颜色和透射率，减少重复着色；颜色使用32位浮点以保留很薄的发丝尾层。关闭可对照旧分离绘制，排序组数与物理宽度不变。");
        ImGui::EndDisabled();
        if(ImGui::Combo("Hair quality",&quality,"Balanced: 2 groups / default32^3\0High: 3 groups / default64^3\0Ultra: 7 groups / default64^3\0")){
            pbrSettings_.strandFrontDepthGroups=quality==0?2:quality==1?3:7;h.volumeResolution=quality==0?32:64;hairChanged=true;
        }help("预设同时设置前层排序和体积预算，下方Volume budget可独立覆盖内部阴影精度；保留全部17500根发丝、物理宽度和源颜色。Ultra增加显存与GPU开销。");
        if(ImGui::Checkbox("Hair depth for TAA",&pbrSettings_.strandTemporalDepth))hairChanged=true;
        help("覆盖率达到.95时将最近发丝深度送入TAA及深度历史，减少按头皮重投影；与下面的逐丝运动和独立颜色历史开关分别控制。");
        if(ImGui::Checkbox("Hair motion vectors",&pbrSettings_.strandMotionVectors))hairChanged=true;
        help("在单采样TAA中，以上一次实际完成帧的发丝位置和模型变换计算运动，分别积累预乘颜色与完整覆盖率，再与身体复合。暂停模拟仍保留相机/模型运动；多采样或非TAA不启用该路径。");
        ImGui::TextDisabled("Motion passes %u | hair TAA %u | history %s",pbr_.Statistics().strandMotionPasses,pbr_.Statistics().strandHairTemporalPasses,pbr_.Statistics().strandHairHistoryUsed?"reused":"reset");
        ImGui::TextDisabled("Transparency %u passes / %u geometry draws | %s",pbr_.Statistics().strandPasses,pbr_.Statistics().strandTransparencyRasterPasses,pbr_.Statistics().strandPairedTransparencyActive?"paired HDR":"legacy");
        hairChanged|=ImGui::Checkbox("Adaptive hair history",&pbrSettings_.temporal.hairCoverageHistoryRejection);
        help("按发丝运动、覆盖率变化和高光颜色变化限制旧帧贡献，减少快运动拖影；静止和亚像素运动仍保留抗锯齿积累，只作用于独立头发层。");
        if(pbrSettings_.temporal.hairCoverageHistoryRejection){
            hairChanged|=ImGui::SliderFloat("Fast motion px",&pbrSettings_.temporal.hairFastMotionPixels,1,32,"%.1f");
            help("每帧移动达到此像素数时严格限制历史覆盖率；较小更快清除拖影，较大保留更多时间平滑。不是曝光或发丝宽度。");
        }
        hairChanged|=ImGui::Checkbox("Hair geometry shutter",&pbrSettings_.temporal.hairMotionBlurEnabled);
        help("用4个独立时间的发丝几何样本平均颜色与覆盖率，表现真实发丝运动模糊。不会把时间样本叠厚；开销明显增加。需要连续执行历史，遮挡和灯光冻结当前帧。");
        if(pbrSettings_.temporal.hairMotionBlurEnabled){
            float shutterMs=pbrSettings_.temporal.hairMotionBlurShutterSeconds*1000.f;
            ImGui::TextDisabled("Shutter %u samples / %u passes",pbr_.Statistics().strandShutterSamples,pbr_.Statistics().strandShutterPasses);
            if(ImGui::SliderFloat("Hair shutter ms",&shutterMs,.1f,33.3f,"%.2f")){pbrSettings_.temporal.hairMotionBlurShutterSeconds=shutterMs*.001f;hairChanged=true;}
            help("快门曝光时间；较长产生更长的运动轨迹。最多使用上次执行帧到当前帧的区间，不对缺失的运动作外推。");
        }
        hairChanged|=ImGui::Checkbox("Source direct scattering",&h.sourceDirectScattering);
        help("使用原Emissive Shader的单次、局部回散射及传播前/后散射闭包。关闭保留旧通用直射模型作对照；原单次闭包固定IOR1.5。");
        if(pbr_.Statistics().strandTransparencyMultisampleFallback)ImGui::TextDisabled("MSAA: weighted transparency fallback");
        float minWidthMm=h.minWidthMetres*1000.f,maxWidthMm=h.maxWidthMetres*1000.f;
        if(ImGui::DragFloat("Min width mm",&minWidthMm,.001f,0.f,maxWidthMm>0?maxWidthMm:2.f,"%.3f")){h.minWidthMetres=minWidthMm*.001f;hairChanged=true;}help("源软件光栅最小全宽，CPU按毫米转米；Diana为0。投影前限制，和最小像素滤波支持范围不同。");
        if(ImGui::DragFloat("Max width mm",&maxWidthMm,.01f,h.minWidthMetres*1000.f,4.f,"%.3f")){h.maxWidthMetres=maxWidthMm*.001f;hairChanged=true;}help("源软件光栅最大全宽，Diana为1毫米；0不限制。LOD减少抽样时，按源CPU公式对宽度及上下限同时作倒数补偿。");
        ImGui::BeginDisabled(h.sourceColor);
        hairChanged|=ImGui::ColorEdit3("Root tint",&h.rootColor.x);help("原始三个渐变加权后的发根线性颜色，也决定吸收系数。");
        hairChanged|=ImGui::ColorEdit3("Tip tint",&h.tipColor.x);help("发梢颜色；渐变位置沿实际发丝弧长插值。");
        ImGui::EndDisabled();
        if(h.sourceColor&&ImGui::TreeNode("Source hair color / 原始逐丝颜色")){
            hairChanged|=ImGui::DragFloat3("Weights A/B/C",&h.gradientWeights.x,.01f,0,4,"%.3f");help("三张原始渐变图的相对选择权重；由原随机字节加选择偏移决定，原值1 / .4 / .2。三者为0时按源阈值选择C。");
            hairChanged|=ImGui::DragFloat4("Offsets C/H/S/V",&h.colorRandomOffsets.x,.005f,-1,1,"%.3f");help("依次为渐变选择、色相、饱和度和明度的随机偏移，按源公式取小数部分。原选择偏移.3，其余0。");
            hairChanged|=ImGui::DragFloat3("Variation H/S/V",&h.hsvVariation.x,.005f,0,1,"%.3f");help("色相扰动及饱和度/明度混合强度；原值.02 / .3 / .03。源Shader将后两者限制在.999以内，不引入新的随机哈希。");
            ImGui::TreePop();
        }
        const auto hairScalar=[&](const char* label,float& value,float low,float high,const char* explanation){ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x*.50f);hairChanged|=ImGui::SliderFloat(label,&value,low,high,"%.3f");help(explanation);};
        hairScalar("Hair roughness",h.roughness,.05f,1,"源Roughness，控制表面R瓣和内部反射瓣的纵向宽度。");
        if(h.physicalScattering){
            hairChanged|=ImGui::Checkbox("Native longitudinal transfer",&h.sourceRoughnessMapping);
            help("恢复原CPU从作者LongitudialRoughness到Shader hm_v的非线性映射：(.726r+.812r²+3.7r²⁰)²，作者.5得到约.32036。关闭为此前raw宽度对照；不修改原色、吸收、灯光或投影消光。");
            hairScalar("Longitudinal input",h.longitudinalRoughness,.05f,1,"启用Native transfer时编辑原作者参数，Render派生hm_v用于散射与LUT；关闭时直接作为Shader宽度。保留原作者.5，不把派生值写回资产。");
            ImGui::Text("Input %.6f -> shader hm_v %.9f",h.longitudinalRoughness,Render::EffectiveStrandLongitudinalRoughness(h));
            hairScalar("Azimuthal",h.azimuthalRoughness,.05f,1,"源AzimuthalRoughness，控制TRT闪光与TT的Ntt采样纵坐标。启用Native Ntt时读取已提取的原始双通道表；关闭则使用旧解析拟合作对照。");
            hairScalar("Fiber IOR",h.ior,1.01f,2.f,"源DSLUT生成IOR=1.55；原直射/IBL单次闭包固定IOR1.5。此项调整散射矩表，旧通用直射也会读取。");
            hairScalar("Cuticle tilt",h.cuticleShift,-.15f,.15f,"源CuticleTiltAngle=.02弧度，分别偏移R、TT和TRT高光位置。");
            hairScalar("Absorption",h.absorptionScale,0,3,"源log2颜色平方吸收公式的调节系数；1保留提取的映射。");
            hairScalar("Multiple scatter",h.multipleScattering,0,1,"源MultipleScattering/FakeMultipleScatteringFactor默认1；结合材质前后向散射矩和空间光路重建多次散射。");
            hairScalar("Density scale",h.scatteringDensity,0,4,"空间体积启用时缩放实际发丝积分光路；关闭时使用此前的局部密度估计。");
            hairScalar("Albedo blend",h.albedoBlendRate,0,1,"源AlbedoBlendRate=.51，将环境辐照度在原色与亮度乘发色之间混合。");
            hairScalar("Ambient scale",h.indirectLightingMultiplier,0,2,"源IndirectLightingMultiplier=.8，只缩放已有预计算环境光。");
            bool renderTangents=strandHair_.simulation.Settings().reconstructRenderTangents;
            if(ImGui::Checkbox("Actual strand tangents",&renderTangents)){Check(strandHair_.SetRenderTangents(renderTangents),strandHair_.Error());hairChanged=true;}
            help("从实际渲染发丝的相邻位置重建曲线方向，保留冠部和分缝的局部弯曲；关闭对照原三引导发方向。只更新着色方向，不推进或重置模拟。");
            hairChanged|=ImGui::Checkbox("Spatial IBL incident",&h.sourceSpatialIbl);
            help("按原Shader的P减中心坐标查询一次入射环境光，两路散射共享该颜色；以groom包围盒中心估计原探针中心，尚非原运行时位置。关闭使用原有按两路方向分别查询的宿主适配。");
            ImGui::SeparatorText("DSVOLUME / 体积自遮蔽");
            hairChanged|=ImGui::Checkbox("Spatial hair volume",&h.spatialVolume);help("从真实发丝累积空间密度，并对太阳与点光积分光路。关闭可对照局部散射基准；不运行光追或实时GI。");
            if(ImGui::Checkbox("Native DSVolume / 原生占用体积",&h.sourceDSVolume)){
                if(h.sourceDSVolume&&h.volumeResolution!=32&&h.volumeResolution!=64&&h.volumeResolution!=128)h.volumeResolution=64;
                hairChanged=true;
            }
            help("原Texture路径：全宽归一化注入24位定点占用量、8位不透明薄壳、六级平均mip，再按源cursor/LOD规则查询N/G。使用恢复的前缀计数与RasterWidthScale补偿，WidthAve保持源header值；实际运行时列表内容和Texture格式仍需捕获验证。");
            if(h.sourceDSVolume){
                ImGui::TextDisabled("Texture cube R32/64/128, 6 mips, manual trilinear RGBA32F adapter");
                hairScalar("Injection step",h.volumeRasterTraceStep,.1f,1.f,"源DSVolumeRasterTraceStepScale=.5，原CPU限制为.1..1。原控制段注入间距=min(voxel.xyz)乘此值；相邻重复baseCell跳过，不改变屏幕发丝宽度。");
                hairScalar("Injection rate",h.volumeInjectionRate,0.f,1.f,"仅quality<2或Min LOD factor启用时应用；计算计数前将请求rate钳至至少.01。最终前缀还受incoming上限与单精度截断影响，实际比例读取下方提交统计。前缀与RasterWidthScale补偿成对执行，WidthAve保持源header值。");
                hairScalar("Injection LOD fraction",h.volumeInjectionLodFraction,0.f,1.f,"先生成incoming=floor(double(totalSegments)*double(float(LOD fraction)))，再进行注入率选择；0产生空注入。该项是传入原列表的显式LOD适配，不改变渲染发丝数量。");
                int injectionQuality=int(h.volumeInjectionQuality);
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x*.5f);
                if(ImGui::SliderInt("Injection quality",&injectionQuality,0,3)){h.volumeInjectionQuality=std::uint32_t(injectionQuality);hairChanged=true;}
                help("原CPU quality0/1应用注入率；quality2/3且Min LOD factor关闭时使用完整incoming列表。该枚举仅控制DSVolume注入，不等于预览总质量预设。");
                hairChanged|=ImGui::Checkbox("Injection Min LOD factor",&h.volumeInjectionMinLodFactor);
                help("启用后quality2/3也应用注入率，并同步补偿RasterWidthScale；WidthAve不随选择率改变。与前缀选择是同一源策略，不独立缩放吸收或高光。");
                ImGui::TextDisabled("Rate %s; LOD fraction %.4f",h.volumeInjectionMinLodFactor||h.volumeInjectionQuality<2?"requested by policy":"bypassed at quality2/3",h.volumeInjectionLodFraction);
                const auto& submittedDs=pbr_.Statistics();
                ImGui::Text("Previous frame DS segments: %llu -> %llu",
                    static_cast<unsigned long long>(submittedDs.strandNativeDsInputSegments),
                    static_cast<unsigned long long>(submittedDs.strandNativeDsInjectedSegments));
                ImGui::Text("Last native item: fraction %.6f | RasterWidthScale %.6f",
                    submittedDs.strandNativeDsInjectionFraction,submittedDs.strandNativeDsRasterWidthScale);
                ImGui::Text("Submitted WidthAve: %.9g",submittedDs.strandNativeDsAverageWidth);
                help("以上是上一帧Render实际提交统计：段计数为所有Native item总计，比例、RasterWidthScale与WidthAve为最后一个Native item。空incoming不计算倒数；不把请求rate当作实际注入比例。");
                int traceCap=int(h.volumeTraceLimit),filteredCursor=int(h.volumeInterpolationSamples);
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x*.5f);
                if(ImGui::SliderInt("Trace factor cap",&traceCap,1,32)){h.volumeTraceLimit=float(traceCap);hairChanged=true;}
                help("源DSInfo_VolumeTraceLimit限制增长后的步长倍数，不是循环次数。已恢复原CPU默认上限32；降低此项增加远段采样精度与开销。");
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x*.5f);
                if(ImGui::SliderInt("Filtered cursor",&filteredCursor,0,32)){h.volumeInterpolationSamples=float(filteredCursor);hairChanged=true;}
                help("源DSTraceInterpolationSampleNum=4：cursor小于此阈值时使用过滤采样，之后按整数坐标与LOD读取；不是成功读取的样本数量。");
                help("Native路径不使用旧inverse-metre积分、近场修复或Exact integration；负LOD钳0为宿主安全适配。注入率、LOD输入前缀和RasterWidthScale补偿采用恢复的CPU策略，WidthAve保持源header值；实际帧绑定仍待捕获。");
            }
            hairChanged|=ImGui::Checkbox("Hardware volume filtering",&h.volumeHardwareFiltering);help("每个体积采样用两次硬件XY线性过滤加Z层插值，保留原分辨率、密度和积分步长。关闭使用此前八点手工插值作性能/画质对照；硬件权重精度可能带来很小数值差异。");
            hairChanged|=ImGui::Checkbox("Source volume widths",&h.normalizedVolumeWidth);help("源DSVolume使用相对平均宽度×.005厘米（50微米），独立于屏幕发丝宽度。Diana的WidthAve保持STRD文件头平均全宽，Native注入选择率补偿写入RasterWidthScale。实际帧DSInfo尚未捕获。旧路径关闭此项使用物理直径。");
            hairChanged|=ImGui::Checkbox("Exact volume integration",&h.volumeExactIntegration);
            help("逐个插值单元积分重建的密度场，减少大步长漏过细发束的阴影；保持颜色、密度和宽度。动态或旋转相机时成本较高，静止可缓存；这是宿主精度选项。");
            if(h.normalizedVolumeWidth){float microns=h.volumeAverageWidth*1e6f;ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x*.5f);if(ImGui::SliderFloat("Source mean um",&microns,0,300,"%.2f")){h.volumeAverageWidth=microns*1e-6f;hairChanged=true;}help("源STRD头部平均全宽，单位微米；Diana原值138.52。0改用上传几何的样本平均全宽；固定参考宽度50微米不随此项改变。");}
            hairChanged|=ImGui::Checkbox("Opaque sun shadows",&h.receiveOpaqueShadows);help("复用Render现有太阳CSM，使头部/衣服等不透明物体遮挡照向发丝的阳光；不包含点光阴影。");
            hairScalar("Opaque shadow power",h.opaqueShadowPower,.05f,4.f,"仅对头发接收的外部太阳CSM可见度作pow。1保持旧适配；原生hm_shadowPow构造默认.5且发现CPU常量写入，当前运行时值尚未捕获。小于1抬升部分阴影，保留可见度0/1端点；不改DS、IBL、发色或头发投到皮肤的阴影。");
            hairChanged|=ImGui::Checkbox("Opaque volume blockers",&h.volumeOpaqueBlockers);help("从当前相机可见头部/衣物表面生成体积遮挡壳，独立积分每个光照方向的最大遮挡；不改变发丝密度，不是封闭头部体积。");
            hairChanged|=ImGui::Checkbox("Closed head proxies",&h.volumeProxyBlockers);
            help("将提取的两条头部碰撞胶囊作为封闭遮挡补充，改善背面与离屏头部漏光；只影响遮挡，不改变发丝密度。碰撞代理不等同于皮肤网格，也不覆盖全身或场景。");
            hairChanged|=ImGui::Checkbox("Stable opaque shadows",&h.volumeStableOpaqueCache);
            help("物理视角与不透明模型不变时复用首个体积遮挡采样，避免仅由抗锯齿投影抖动触发重算。镜头或模型运动会重建；关闭可对照逐帧采样，不增加几何细节。");
            hairChanged|=ImGui::Checkbox("Point IBL paths",&h.volumePointIbl);
            help("按实际发丝点与方向积分两路环境遮挡，修正体素平均方向与实际着色方向不同的问题。保留原密度、偏移和散射参数；增加GPU开销。");
            hairScalar("Blocker shell m",h.volumeDepthThreshold,.001f,.10f,"源DSVolumeRasterDepthThreshold=.05米，限制不透明表面后方被注入的壳厚度；并非发丝宽度或吸收倍率。");
            hairChanged|=ImGui::Checkbox("Hair casts sun shadow",&h.castDirectionalShadows);help("使头发遮挡照向脸部、头皮和衣服的太阳光。下方选择Ordered二值覆盖或DeepOpacity连续透射；头发接收独立的不透明CSM，避免与DSVolume重复自遮蔽。");
            int shadowCoverage=int(pbrSettings_.shadows.strandCoverage);
            if(ImGui::Combo("Hair shadow coverage",&shadowCoverage,"Ordered coverage (host)\0Deep opacity 8 nodes (host)\0")){pbrSettings_.shadows.strandCoverage=Render::StrandShadowCoverage(shadowCoverage);hairChanged=true;}
            help("Ordered保持此前稳定8x8二值覆盖。DeepOpacity使用连续解析覆盖和8深度节点透射，避免二值覆盖抖动；仅改变头发投到模型的阴影，不改DS或发丝照明公式。这是HOST深度不透明度近似，未恢复原游戏shadow PSO。");
            ImGui::BeginDisabled(pbrSettings_.shadows.strandCoverage!=Render::StrandShadowCoverage::DeepOpacity);
            if(ImGui::SliderFloat("DOM extinction scale",&pbrSettings_.shadows.strandOpacityScale,0.f,2.f,"%.3f"))hairChanged=true;
            help("HOST有效投影消光倍率，只缩放DOM深度节点的光学厚度。1保持此前结果；较小可减轻发影，0保留模型自身阴影并移除DOM发影。不改头发接收阴影、DS、发色或曝光；不会自动保存到材质profile。");
            ImGui::EndDisabled();
            if(pbrSettings_.shadows.strandCoverage==Render::StrandShadowCoverage::DeepOpacity)ImGui::TextDisabled("Opacity shadow: %u passes | %u atlas",pbr_.Statistics().strandOpacityShadowPasses,pbr_.Statistics().strandOpacityShadowResolution);
            int iblMode=!h.volumeSkyTransmission?0:h.dualScatteringIbl?2:1;
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x*.5f);
            if(ImGui::Combo("Hair IBL",&iblMode,"Legacy ambient\0Forward only\0Source dual scattering\0")){h.volumeSkyTransmission=iblMode!=0;h.dualScatteringIbl=iblMode==2;hairChanged=true;}
            help("同光照对照：Legacy为此前无环境体积遮蔽；Forward只保留六方向透射；Source补入原Shader的双方向角分布、局部/传播回散射与方差补偿。间接光可用宿主SH或六向源公式适配；体素平均切线的双方向密度积分仍为重建。");
            if(iblMode==2)hairScalar("TT / back gain",h.backscatterScale,0,4,"源BackscatterScale，默认1，同时缩放TT与传播回散射。0仍保留局部回散射；不会改变源发色或体积密度。");
            hairScalar("Voxel size m",h.volumeVoxelSize,.002f,.02f,"源DSVolumeVoxelSize=.5，结合Shader厘米坐标换算为.005米；受体积预算限制时有效体素可增大。");
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x*.5f);
            if(h.sourceDSVolume){
                int resolution=h.volumeResolution==32?0:h.volumeResolution==64?1:2;
                if(ImGui::Combo("Volume budget",&resolution,"32^3\0" "64^3\0" "128^3\0")){h.volumeResolution=std::array<std::uint32_t,3>{32,64,128}[resolution];hairChanged=true;}
            }else{
                int resolution=h.volumeResolution==16?0:h.volumeResolution==32?1:h.volumeResolution==48?2:h.volumeResolution==64?3:4;
                if(ImGui::Combo("Volume budget",&resolution,"16^3\0" "32^3\0" "48^3\0" "64^3\0" "128^3\0")){h.volumeResolution=std::array<std::uint32_t,5>{16,32,48,64,128}[resolution];hairChanged=true;}
            }
            help("独立的内部阴影体积预算，不增加透明排序组或发丝数。128预算可达到Diana请求的5毫米体素，分离冠部细束；增加体积积分时间与显存，静止几何和光照会缓存。");
            hairScalar("Raster width",h.volumeRasterWidthScale,.5f,3.f,"源体积栅格化宽度倍率1.5，影响密度覆盖；独立于屏幕最小发丝宽度。");
            hairScalar("Trace growth",h.volumeTraceStep,1.f,1.5f,"源光路积分步长增长因子1.1；越小采样越细，开销越高。");
            hairScalar("Volume bias",h.volumeBiasVoxels,0,3,"源偏移系数默认1，用于避免自身密度自遮挡。启用近场阴影时乘请求体素尺寸；关闭时乘预算限制后的实际体素尺寸。过大会漏掉近邻阴影。");
            hairChanged|=ImGui::Checkbox("Preserve near shadows",&h.volumePreserveNearField);
            help("宿主适配：以请求的体素尺寸计算物理偏移，避免预算不足时将5毫米偏移扩大到16毫米而漏掉发束近场阴影。保留原偏移系数和密度；关闭可对照旧粗体素偏移。");
            if(h.spatialVolume)ImGui::TextDisabled("Trace exclusion: %.2f mm",pbr_.Statistics().strandVolumeBiasMetres*1000.f/std::max(pbrSettings_.skinDiffusion.worldUnitScale,1e-6f));
            if(h.spatialVolume){const auto& volume=pbr_.Statistics();ImGui::Text("Volume max axis: %u / %.2f mm",volume.strandVolumeResolution,volume.strandVolumeVoxelSize*1000.f/std::max(pbrSettings_.skinDiffusion.worldUnitScale,1e-6f));ImGui::Text("Volume passes: %u | blockers: %u | %s",volume.strandVolumePasses,volume.strandBlockerPasses,volume.strandVolumeUpdated?"updated":"cached");}
        }else{
            hairScalar("Hair second lobe",h.secondarySpecular,0,2,"此前双高光近似的次级高光强度，仅旧模式生效。");
            hairScalar("Hair transmission",h.transmission,0,2,"此前背透近似强度，仅旧模式生效。");
        }
        if(hairChanged)pbr_.ResetHistory(false);
        ImGui::TextWrapped("Render/RHI area coverage, optional front-layer sorting and a weighted tail; spatial strand-density and light paths remain cached independently.");
    }
    void PreviewMaterialControls() {
        if(!pbrEnabled_&&!skinProfile_.loaded)return;
        if(!ImGui::CollapsingHeader("Skin / lighting",ImGuiTreeNodeFlags_DefaultOpen))return;
        if(!pbrEnabled_)ImGui::TextDisabled("Skin lighting requires the PBR preview.");
        ImGui::SetNextItemWidth(150);
        if(ImGui::Combo("Lighting",&lightingPreset_,"Studio\0Side\0Back\0Reference study\0ST30_101 cube study\0Reference neutral key study\0"))ApplyLightingPreset();
        if(ImGui::IsItemHovered())ImGui::SetTooltip("Lights orient to camera when a preset is selected.\nReference neutral key is a host lighting calibration at equal Rec.709 incoming luminance; not recovered game illumination.");
        if(!skinProfile_.loaded){ImGui::TextDisabled("No skin profile loaded.");return;}
        auto& s=skinProfile_.settings;bool changed=ImGui::Checkbox("Skin response",&s.enabled);
        ImGui::TextWrapped("Off: base color + corrected roughness baseline.");
        ImGui::Text("Exact materials: %zu",skinPrepared_.materialIndices.size());
        if(ImGui::IsItemHovered()){ImGui::BeginTooltip();for(const auto i:skinPrepared_.materialIndices)ImGui::TextUnformatted(resource_->Source().materials[i].name.c_str());ImGui::EndTooltip();}
        changed|=ImGui::Checkbox("Skin control map",&s.controlEnabled);
        const bool hasControlMap=!skinPrepared_.controlMaterials.empty();
        const bool nativeControl=s.controlEnabled&&hasControlMap;
        ImGui::TextDisabled("G roughness baseline: %zu materials",skinPrepared_.controlMaterials.size());
        if(nativeControl) {
            ImGui::TextWrapped("Control R: detail / G: roughness / B: specular.");
            changed|=ImGui::SliderFloat("Direct roughness bias",&s.roughnessBias,-.5f,.5f,"%.3f");
            changed|=ImGui::SliderFloat("Environment roughness bias",&s.roughness2Bias,-.5f,.5f,"%.3f");
            changed|=ImGui::SliderFloat("Direct specular scale",&s.specularScale1,0.f,1.f,"%.3f");
            changed|=ImGui::SliderFloat("Environment specular scale",&s.specularScale2,0.f,1.f,"%.3f");
            changed|=ImGui::SliderFloat("Coarse wrap scale",&s.coarseWrapScale,0.f,1.f,"%.3f");
            changed|=ImGui::SliderFloat("Coarse wrap base",&s.coarseWrapBase,0.f,1.f,"%.3f");
            changed|=ImGui::SliderFloat("Straight diffuse",&s.diffuseStraight,0.f,1.f,"%.3f");
            changed|=ImGui::SliderFloat("Steep diffuse",&s.diffuseSteep,0.f,1.f,"%.3f");
            changed|=ImGui::SliderFloat("Diffuse angle exponent",&s.diffuseAngleExponent,.01f,4.f,"%.3f");
            changed|=ImGui::SliderFloat("Diffuse distribution",&s.diffuseDistribution,.01f,4.f,"%.3f");
            changed|=ImGui::SliderFloat("Diffuse scale",&s.diffuseScale,0.f,2.f,"%.3f");
            changed|=ImGui::ColorEdit3("Coarse color (source / 255)",&s.coarseColor.x);
            changed|=ImGui::ColorEdit3("Detail bleed (source / 255)",&s.detailColorBleed.x);
        } else {
            if(!hasControlMap)changed|=ImGui::SliderFloat("Roughness fallback",&s.roughness,.045f,1.f,"%.3f");
            changed|=ImGui::ColorEdit3("Scatter color (linear)",&s.scatterColor.x);
            changed|=ImGui::SliderFloat("Scatter",&s.scatterStrength,0.f,1.f,"%.3f");
            changed|=ImGui::SliderFloat("Diffuse wrap",&s.wrap,0.f,1.f,"%.3f");
            changed|=ImGui::SliderFloat("Diffuse normal blend",&s.diffuseNormalBlend,0.f,1.f,"%.3f");
            changed|=ImGui::SliderFloat("Specular mix",&s.specularMix,0.f,1.f,"%.3f");
            changed|=ImGui::SliderFloat("Second roughness",&s.roughness2,.045f,1.f,"%.3f");
        }
        changed|=ImGui::Checkbox("Detail normal",&s.detailEnabled);
        changed|=ImGui::SliderFloat("Detail strength",&s.detailStrength,0.f,2.f,"%.3f");
        changed|=ImGui::DragFloat2("Detail tile XY",&s.detailTiling.x,.1f,.01f,128.f,"%.2f");
        ImGui::TextDisabled("Transmission: %.3f (no thickness map)",s.transmission);
        if(!skinPrepared_.unmatchedNames.empty())ImGui::TextWrapped("Unmatched names: %zu (see console)",skinPrepared_.unmatchedNames.size());
        if(changed)Try([&]{skinProfile_.Apply(*resource_,skinPrepared_);});
        ImGui::Separator();
    }
    void DetailsPanel() {
        ImGui::TextColored(ImVec4(.3f,.75f,1,1),"DETAILS / INSPECTOR");
        ImGui::BeginChild("inspector-scroll",ImVec2(0,0));
        if(selection_!=Selection::StrandHair&&!characterMaterials_)PreviewMaterialControls();
        switch(selection_) {
            case Selection::Mesh:MeshDetails();break;
            case Selection::Skeleton:SkeletonDetails();break;
            case Selection::Morphs:MorphDetails();break;
            case Selection::Animation:AnimationDetails();break;
            case Selection::Ik:IkDetails();break;
            case Selection::Retarget:RetargetDetails();break;
            case Selection::Cloth:ClothDetails();break;
            case Selection::Hair:HairDetails();break;
            case Selection::StrandHair:strandHair_.Controls();break;
            case Selection::CharacterMaterials:CharacterMaterialDetails();break;
            case Selection::MeshRegions:MeshRegionDetails();break;
            case Selection::Diagnostics:
                ImGui::TextWrapped("Asset: %s",asset_->id.c_str());ImGui::TextWrapped("Source: %s",Utf8(asset_->sourcePath).c_str());
                ImGui::Text("Schema version: %u",asset_->version);
                ImGui::Text("Source physics: %u bodies / %u joints",asset_->sourceRigidBodies,asset_->sourceJoints);
                for(const auto& warning:asset_->warnings)ImGui::BulletText("%s",warning.c_str());
                for(const auto& warning:asset_->ikRig.diagnostics)ImGui::BulletText("%s",warning.c_str());break;
        }
        ImGui::EndChild();
    }
    void Timeline() {
        ImGui::SeparatorText("ANIMATION TIMELINE");
        if(ImGui::Button(playing_?"Pause / 暂停":"Play / 播放")){if(playing_)if(const auto clip=ActiveClip();loop_&&clip&&clip->duration>0)time_=std::fmod(time_,clip->duration);playing_=!playing_;dirty_=true;}
        ImGui::SameLine();if(ImGui::Checkbox("Loop",&loop_))dirty_=true;ImGui::SameLine();
        if(ImGui::Button("Bind pose")){selectedClip_=std::max(0,GeneratedClip(*asset_,"BindPose"));time_=0;playing_=false;retargetPlaying_=false;bindPose_=true;enableIk_=false;std::fill(manualMorphs_.begin(),manualMorphs_.end(),0);std::fill(morphOverrides_.begin(),morphOverrides_.end(),0);dirty_=true;clothReset_=true;}
        if(const auto sourcePose=GeneratedClip(*asset_,"SourcePose");sourcePose>=0) {
            ImGui::SameLine();if(ImGui::Button("Source pose")){selectedClip_=sourcePose;time_=0;playing_=false;retargetPlaying_=false;bindPose_=false;enableIk_=false;std::fill(manualMorphs_.begin(),manualMorphs_.end(),0);std::fill(morphOverrides_.begin(),morphOverrides_.end(),0);dirty_=true;clothReset_=true;hairReset_=true;}
        }
        if(const auto shoesFit=GeneratedClip(*asset_,"ShoesFit");shoesFit>=0) {
            ImGui::SameLine();if(ImGui::Button("Shoes fit")){selectedClip_=shoesFit;time_=0;playing_=false;retargetPlaying_=false;bindPose_=false;enableIk_=false;std::fill(manualMorphs_.begin(),manualMorphs_.end(),0);std::fill(morphOverrides_.begin(),morphOverrides_.end(),0);dirty_=true;clothReset_=true;hairReset_=true;}
        }
        if(const auto active=ActiveClip()) {
            const auto& clip=*active;double begin=0,end=std::max(.001,clip.duration);
            double shown=loop_&&(playing_||std::abs(time_-end)>1e-9)?std::fmod(time_,end):std::min(time_,end);
            ImGui::SetNextItemWidth(-1);if(ImGui::SliderScalar("##time",ImGuiDataType_Double,&shown,&begin,&end,"%.3f s")){time_=shown;playing_=false;dirty_=true;}
            ImGui::Text("%s%s | %.1f fps | %s",retargetPlaying_?"Retarget: ":"",clip.name.c_str(),clip.framesPerSecond,ClipKind(clip));
        }
    }
    void Toolbar() {
        ImGui::TextColored(ImVec4(.35f,.78f,1,1),"MODEL / SKELETAL ASSET EDITOR");ImGui::SameLine();
        if(ImGui::Button("Import source"))Try([&]{auto p=Browse(false);if(!p.empty())Load(Path(p),false);});
        ImGui::SameLine();if(ImGui::Button("Open package"))Try([&]{auto p=Browse(true);if(!p.empty())Load(Path(p),true);});
        ImGui::SameLine();if(ImGui::Button("Open character"))Try([&]{auto p=Browse(true);if(!p.empty())LoadCharacter(Path(p));});
        ImGui::SameLine();if(ImGui::Button("Export revision"))ImGui::OpenPopup("Export intermediate assets");
        ImGui::SameLine();if(ImGui::Button("Import VMD"))ImGui::OpenPopup("Import VMD motion");
        ImGui::SameLine();if(ImGui::Button("Open animation"))Try([&]{auto path=Browse(true,L"Animation (*.anim.json)\0*.anim.json\0All JSON\0*.json\0");if(!path.empty())OpenAnimation(Path(path));});
        ImGui::SameLine();ImGui::Checkbox("Bones",&showBones_);ImGui::SameLine();ImGui::Checkbox("Names",&showNames_);
        ImGui::SameLine();if(ImGui::Button("Frame model")){if(modular_){trackedCameraBone_=-1;cameraFocus_={0,0,0};distance_=4.7f;}else FramePreviewRegions();}
        if(ImGui::BeginPopupModal("Export intermediate assets",nullptr,ImGuiWindowFlags_AlwaysAutoResize)){
            ImGui::TextWrapped("%s",modular_?"Save body, independent garment, cloth settings and character assembly to a new directory.":"A new portable asset package is created; source files are read-only.");
            ImGui::SetNextItemWidth(700);ImGui::InputText("Directory",exportPath_.data(),exportPath_.size());
            if(ImGui::Button("Generate assets"))Try([&]{std::string error;if(modular_){auto bundle=*modular_;bundle.body=asset_;bundle.cloth.settings=clothSettings_;if(!bundle.assembly.slots.empty())bundle.assembly.slots.front().enabled=showGarment_;Check(SaveModularCharacter(bundle,Path(exportPath_.data()),&error),error);}else Check(SaveSkeletalAssetPackage(*asset_,Path(exportPath_.data()),&error),error);regionEdits_=false;status_="Exported "+std::string(exportPath_.data());ImGui::CloseCurrentPopup();});
            ImGui::SameLine();if(ImGui::Button("Cancel"))ImGui::CloseCurrentPopup();ImGui::EndPopup();
        }
        if(ImGui::BeginPopupModal("Import VMD motion",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextWrapped("VMD file or directory. A directory merges bone + expression motions and skips camera-only VMDs.");
            ImGui::SetNextItemWidth(680);ImGui::InputText("Motion path",vmdPath_.data(),vmdPath_.size());
            ImGui::SameLine();if(ImGui::Button("Browse VMD")){auto path=Browse(false,L"MMD motion (*.vmd)\0*.vmd\0");if(!path.empty())SetText(vmdPath_,path);}
            ImGui::SetNextItemWidth(680);ImGui::InputText("Extra expression VMD",vmdFacePath_.data(),vmdFacePath_.size());
            ImGui::InputText("Animation name",animationName_.data(),animationName_.size());
            ImGui::InputDouble("Sampling FPS",&bakeOptions_.framesPerSecond,1,10,"%.1f");ImGui::InputDouble("Translation scale",&bakeOptions_.translationScale,.05,.5,"%.4f");
            ImGui::Checkbox("Bake animated native IK",&bakeOptions_.solveNativeIk);ImGui::SameLine();ImGui::Checkbox("Bake PMX append / grant",&bakeOptions_.evaluateAppend);
            if(ImGui::Button("Match and bake animation"))Try([&]{std::vector<fs::path> paths{Path(vmdPath_.data())};if(vmdFacePath_[0])paths.push_back(Path(vmdFacePath_.data()));ImportMotion(paths,animationName_[0]?animationName_.data():"VMD animation");ImGui::CloseCurrentPopup();});
            ImGui::SameLine();if(ImGui::Button("Cancel import"))ImGui::CloseCurrentPopup();
            if(!lastError_.empty())ImGui::TextWrapped("%s",lastError_.c_str());ImGui::EndPopup();
        }
    }
    void BuildUi() {
        ImGui::SetNextWindowPos(ImVec2(0,0));ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
        ImGui::Begin("SkeletalAssetEditor",nullptr,ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoCollapse|ImGuiWindowFlags_NoSavedSettings);
        Toolbar();ImGui::Separator();
        const float workHeight=std::max(180.f,ImGui::GetContentRegionAvail().y-43);
        ImGui::BeginChild("assets-panel",ImVec2(285,workHeight),ImGuiChildFlags_Borders);AssetsPanel();ImGui::EndChild();ImGui::SameLine();
        const float center=std::max(260.f,ImGui::GetContentRegionAvail().x-374);
        ImGui::BeginChild("viewport-and-timeline",ImVec2(center,workHeight),ImGuiChildFlags_Borders);
        int pipeline=pbrEnabled_?1:0;ImGui::SetNextItemWidth(85);
        ImGui::BeginDisabled(strandHair_.loaded);
        if(ImGui::Combo("##preview-pipeline",&pipeline,"Toon\0PBR\0"))Try([&]{SelectPipeline(pipeline==1);});
        ImGui::EndDisabled();
        if(ImGui::IsItemHovered())ImGui::SetTooltip("PBR: studio / side / back lighting, ACES display transform.\nSkin response is applied only to exact names in --skin-profile; source textures stay unchanged.");
        ImGui::SameLine();
        if(displayPerformance_.frame>0) {
            ImGui::Text("%.1f FPS | frame %.2f ms | %s %.2f | skin %.2f | upload %.2f ms",1000./displayPerformance_.frame,displayPerformance_.frame,clothWorker_?"cloth submit":"cloth CPU",displayPerformance_.cloth,displayPerformance_.skin,displayPerformance_.upload);
            if(ImGui::IsItemHovered())ImGui::SetTooltip("Pose %.2f ms | cloth apply %.2f ms\nRender %.2f | UI %.2f | swap %.2f | render sync %.2f ms\nGPU scene span %.2f ms | %llu fixed steps in last frame\nRender sync overlaps render/UI/swap timings; upload includes its own wait.\nGPU results are polled without blocking; scene span includes dispatch gaps.\nOrbit: RMB / pan: MMB / zoom: wheel",displayPerformance_.pose,displayPerformance_.apply,displayPerformance_.render,displayPerformance_.ui,displayPerformance_.swap,displayPerformance_.sync,std::max(0.,displayPerformance_.gpu),static_cast<unsigned long long>(displayPerformance_.steps));
        } else ImGui::Text("Viewport / CPU skinning | %zu bones | orbit: RMB / pan: MMB / zoom: wheel",asset_->skeleton.bones.size());
        viewportSize_=ImGui::GetContentRegionAvail();viewportSize_.y=std::max(120.f,viewportSize_.y-118);viewportOrigin_=ImGui::GetCursorScreenPos();
        const auto referenceSlotOrigin=viewportOrigin_,referenceSlotSize=viewportSize_;
        if(referenceCameraEnabled_&&referenceCamera_.lockReferenceAspect){
            const auto fitted=EditorPreview::DianaReferenceViewport({viewportSize_.x,viewportSize_.y},referenceCamera_);
            viewportSize_={fitted.x,fitted.y};
            viewportOrigin_.x+=.5f*(referenceSlotSize.x-viewportSize_.x);viewportOrigin_.y+=.5f*(referenceSlotSize.y-viewportSize_.y);
            ImGui::SetCursorScreenPos(viewportOrigin_);
        }
        ImGui::Image(ImTextureID(previewTexture_),viewportSize_,ImVec2(0,1),ImVec2(1,0));
        const bool viewportHovered=ImGui::IsItemHovered();
        if(referenceCameraEnabled_&&referenceCamera_.lockReferenceAspect){
            ImGui::SetCursorScreenPos(ImVec2(referenceSlotOrigin.x,referenceSlotOrigin.y+referenceSlotSize.y));ImGui::Dummy(ImVec2(0,0));
        }
        auto& io=ImGui::GetIO();
        if(viewportHovered&&ImGui::IsMouseClicked(ImGuiMouseButton_Middle))cameraPanning_=true;
        if(!ImGui::IsMouseDown(ImGuiMouseButton_Middle))cameraPanning_=false;
        if(cameraPanning_&&(io.MouseDelta.x!=0||io.MouseDelta.y!=0)) {
            // Capture only drags that start in the viewport. Keep panning when
            // the cursor leaves it, using logical pixels to match ImGui's size.
            const glm::vec3 right(std::cos(yaw_),0,-std::sin(yaw_));
            const glm::vec3 up(-std::sin(pitch_)*std::sin(yaw_),std::cos(pitch_),-std::sin(pitch_)*std::cos(yaw_));
            const float unitsPerPixel=2.f*distance_*std::tan(glm::radians(cameraVerticalFovDegrees_)*.5f)/std::max(1.f,viewportSize_.y);
            cameraFocus_+=(-io.MouseDelta.x*right+io.MouseDelta.y*up)*unitsPerPixel;
            trackedCameraBone_=-1;
        }
        if(viewportHovered) {
            if(ImGui::IsMouseDragging(ImGuiMouseButton_Right)){yaw_-=io.MouseDelta.x*.006f;pitch_=std::clamp(pitch_+io.MouseDelta.y*.006f,-1.3f,1.3f);}
            const float referenceScale=referenceCameraEnabled_?float(glm::length(glm::dvec3(referenceCameraSnapshot_.placement[0]))):1.f;
            distance_=std::clamp(distance_*std::exp(-io.MouseWheel*.12f),referenceCameraEnabled_?.12f*referenceScale:1.f,referenceCameraEnabled_?5.f*referenceScale:20.f);
        }
        if(referenceCameraEnabled_){
            referenceCamera_.yawDegrees=glm::degrees(yaw_);referenceCamera_.pitchDegrees=glm::degrees(pitch_);
            referenceCamera_.distanceLocalMetres=distance_/float(glm::length(glm::dvec3(referenceCameraSnapshot_.placement[0])));
            referenceCamera_.focusLocal=glm::vec3(glm::inverse(placement_)*glm::dvec4(cameraFocus_,1));
        }
        if(AsyncPreview()){
            if(!clothSnapshot_)ImGui::TextColored(ImVec4(1,.78f,.35f,1),"Preparing physics / 正在准备物理预览");
            else ImGui::Text("Async physics | completed batch %.2f ms | physics lag %.1f ms",displayPerformance_.clothWorker,displayPerformance_.clothLag);
        }
        BoneOverlay();ClothOverlay();HairOverlay();Timeline();ImGui::EndChild();ImGui::SameLine();
        ImGui::BeginChild("details-panel",ImVec2(0,workHeight),ImGuiChildFlags_Borders);DetailsPanel();ImGui::EndChild();
        ImGui::Separator();ImGui::TextWrapped("%s",status_.c_str());
        if(!lastError_.empty())ImGui::TextColored(ImVec4(1,.4f,.35f,1),"%s",lastError_.c_str());
        ImGui::End();
    }
    void SaveScreenshot(const fs::path& path,int width,int height) {
        if(path.empty())return;
        if(!path.parent_path().empty())fs::create_directories(path.parent_path());
        std::vector<std::uint8_t> pixels(std::size_t(width)*height*4);
        OnRender([&]{glBindFramebuffer(GL_READ_FRAMEBUFFER,0);glReadBuffer(GL_BACK);glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());Check(glGetError()==GL_NO_ERROR,"Editor screenshot readback failed");});
        std::vector<std::uint8_t> top(pixels.size());
        for(int y=0;y<height;++y)std::copy_n(pixels.data()+std::size_t(height-1-y)*width*4,std::size_t(width)*4,top.data()+std::size_t(y)*width*4);
        Check(stbi_write_png(Utf8(path).c_str(),width,height,4,top.data(),width*4)!=0,"Could not save editor screenshot");
    }
    void SavePreviewCapture(const fs::path& path) {
        fs::create_directories(path.parent_path());const auto width=previewWidth_,height=previewHeight_;
        std::vector<std::uint8_t> pixels(std::size_t(width)*height*4),top(pixels.size());
        OnRender([&]{glGetTextureImage(previewTexture_,0,GL_RGBA,GL_UNSIGNED_BYTE,GLsizei(pixels.size()),pixels.data());Check(glGetError()==GL_NO_ERROR,"Cloth capture readback failed");});
        for(std::uint32_t y=0;y<height;++y)std::copy_n(pixels.data()+std::size_t(height-1-y)*width*4,std::size_t(width)*4,top.data()+std::size_t(y)*width*4);
        Check(stbi_write_png(Utf8(path).c_str(),int(width),int(height),4,top.data(),int(width)*4)!=0,"Could not save cloth preview frame");
    }
    void UpdatePerformanceDisplay() {
        const double blend=displayPerformance_.frame>0?.08:1.;
        for(const auto field:{&FramePerformance::frame,&FramePerformance::pose,&FramePerformance::skin,&FramePerformance::upload,&FramePerformance::cloth,&FramePerformance::hair,&FramePerformance::apply,&FramePerformance::render,&FramePerformance::ui,&FramePerformance::swap,&FramePerformance::sync,&FramePerformance::clothLag,&FramePerformance::limiterWait})
            displayPerformance_.*field+=(performance_.*field-displayPerformance_.*field)*blend;
        if(performance_.clothWorker>0)displayPerformance_.clothWorker=performance_.clothWorker;
        if(performance_.hairWorker>0)displayPerformance_.hairWorker=performance_.hairWorker;
        if(performance_.gpu>=0)displayPerformance_.gpu=displayPerformance_.gpu<0?performance_.gpu:displayPerformance_.gpu+(performance_.gpu-displayPerformance_.gpu)*blend;
        displayPerformance_.steps=performance_.steps;
    }
    Json::Value ShadowSkinCaptureState() const {
        Json::Value state;
        const auto& hair=strandHair_.material;const auto& shadows=pbrSettings_.shadows;
        const auto& skin=pbrSettings_.skinDiffusion;const auto& statistics=pbr_.Statistics();
        state["castDirectionalShadows"]=hair.castDirectionalShadows;
        state["strandShadowCoverage"]=shadows.strandCoverage==Render::StrandShadowCoverage::DeepOpacity?"deep-opacity":"ordered";
        state["hairShadowExtinctionScale"]=shadows.strandOpacityScale;
        state["hairSourceRoughnessMapping"]=hair.sourceRoughnessMapping;
        state["hairAuthoredLongitudinalRoughness"]=hair.longitudinalRoughness;
        state["hairEffectiveLongitudinalRoughness"]=Render::EffectiveStrandLongitudinalRoughness(hair);
        state["hairHostAmbientScale"]=pbrSettings_.strandIndirect.hostAmbientScale;
        state["receiveOpaqueShadows"]=hair.receiveOpaqueShadows;
        state["opaqueShadowPower"]=hair.opaqueShadowPower;
        state["volumeOpaqueBlockers"]=hair.volumeOpaqueBlockers;state["spatialVolume"]=hair.spatialVolume;
        state["directionalShadowsEnabled"]=shadows.enabled;state["sunShadowAtlasResolution"]=shadows.atlasResolution;
        state["sunShadowDistance"]=shadows.distance;state["sunShadowSplitLambda"]=shadows.splitLambda;
        state["sunShadowDepthBias"]=shadows.depthBias;state["sunShadowNormalBias"]=shadows.normalBias;
        state["sunShadowDepthBiasTexels"]=shadows.depthBiasTexels;state["sunShadowNormalBiasTexels"]=shadows.normalBiasTexels;
        state["sunShadowCascadeBlend"]=shadows.cascadeBlend;state["sunShadowDistanceFade"]=shadows.distanceFade;
        state["sunShadowReceiverPlaneClampTexels"]=shadows.receiverPlaneClampTexels;
        state["sunShadowCasterPadding"]=shadows.casterPadding;
        state["sunShadowFilter"]=shadows.filter==Render::DirectionalShadowFilter::SourcePoisson8?"source-poisson8":"tent";
        state["sunShadowRadius"]=shadows.sourcePoissonRadius;state["sunShadowRotationRadians"]=shadows.sourcePoissonRotationRadians;
        state["sunSurfaceShadowRadius"]=shadows.sourceSurfaceRadius;
        state["sunSurfaceShadowEffectiveRadius"]=shadows.sourceSurfaceRadius>0.f?shadows.sourceSurfaceRadius:shadows.sourcePoissonRadius;
        state["sunSurfaceShadowOverrideActive"]=shadows.filter==Render::DirectionalShadowFilter::SourcePoisson8&&shadows.sourceSurfaceRadius>0.f;
        state["sunSurfaceShadowRadiusMeaning"]="host opaque CSM receiver override only;0 inherits hair radius; original skin runtime law unconfirmed";
        state["sunShadowRadiusMeaning"]="first-cascade normalized UV; source Poisson8 study only; runtime writer unconfirmed";
        state["skinDiffusionEnabled"]=skin.enabled;state["skinDiffusionStrength"]=skin.strength;
        state["skinDiffusionKernel"]=skin.kernel==Render::SkinDiffusionKernel::SourceBurley?"source-burley":"gaussian";
        state["skinDiffusionRadiusMillimeters"]=skin.radiusMillimeters;state["skinDiffusionDiskSamples"]=skin.diskSamples;
        state["skinDiffusionWorldUnitScale"]=skin.worldUnitScale;state["skinDiffusionAlbedoPreservation"]=skin.albedoPreservation;
        for(int i=0;i<3;++i)state["skinDiffusionChannelRadii"].append(skin.channelRadii[i]);
        state["skinDiffusionNormalRejectCosine"]=skin.normalRejectCosine;state["skinDiffusionDepthRejectScale"]=skin.depthRejectScale;
        state["skinDiffusionMaxRadiusPixels"]=skin.maxRadiusPixels;
        state["postExposure"]=pbrSettings_.post.exposure;state["postGamma"]=pbrSettings_.post.gamma;
        state["postToneMap"]=int(pbrSettings_.post.toneMap);state["postBloomEnabled"]=pbrSettings_.post.bloomEnabled;
        state["postSsaoEnabled"]=pbrSettings_.post.ssaoEnabled;
        state["directionalShadowPasses"]=statistics.shadowPasses;state["strandShadowDraws"]=statistics.strandShadowDraws;
        state["strandOpacityShadowPasses"]=statistics.strandOpacityShadowPasses;
        state["strandOpacityShadowResolution"]=statistics.strandOpacityShadowResolution;
        state["skinDiffusionPasses"]=statistics.skinDiffusionPasses;state["skinDiffusionDraws"]=statistics.skinDiffusionDraws;
        return state;
    }
    void ReportBenchmark(const Options& options,const std::vector<FramePerformance>& samples,std::uint32_t warmupFrames,const char* waitBackend) const {
        Check(!samples.empty(),"Benchmark ended before the warmup completed");
        const auto sum=[&](double FramePerformance::*field){double total=0;for(const auto& sample:samples)total+=sample.*field;return total;};
        const auto mean=[&](double FramePerformance::*field){return sum(field)/double(samples.size());};
        std::vector<double> frameTimes;frameTimes.reserve(samples.size());
        std::uint64_t steps=0,constraintIterations=0,snapshots=0,hairSteps=0,hairSleepSteps=0;double gpuTotal=0;std::size_t gpuSamples=0;
        double hairMaximumPenetration=0,hairMaximumAttachmentDrift=0;std::size_t hairWorkerSamples=0;
        std::size_t cpuSamples=0,focusedFrames=0;double cpuWall=0;
        for(const auto& sample:samples){frameTimes.push_back(sample.frame);steps+=sample.steps;hairSteps+=sample.hairSteps;hairSleepSteps+=sample.hairSleepSteps;hairMaximumPenetration=std::max(hairMaximumPenetration,sample.hairPenetration);hairWorkerSamples+=sample.hairWorker>0;constraintIterations+=sample.constraintIterations;snapshots+=sample.clothSnapshots;focusedFrames+=sample.focused;if(sample.gpu>=0){gpuTotal+=sample.gpu;++gpuSamples;}if(sample.cpuTimeAvailable){++cpuSamples;cpuWall+=sample.frame/1000.;}}
        for(const auto& sample:samples)hairMaximumAttachmentDrift=std::max(hairMaximumAttachmentDrift,sample.hairAttachmentDrift);
        std::sort(frameTimes.begin(),frameTimes.end());
        const auto percentile=[&](double fraction){const double index=fraction*(frameTimes.size()-1);const auto lower=std::size_t(index),upper=std::min(lower+1,frameTimes.size()-1);return frameTimes[lower]+(frameTimes[upper]-frameTimes[lower])*(index-lower);};
        const double wall=sum(&FramePerformance::frame)/1000.,fps=double(samples.size())/std::max(wall,1e-9),gpu=gpuSamples?gpuTotal/gpuSamples:-1;
        const double processUser=sum(&FramePerformance::processUserCpu)/1000.,processKernel=sum(&FramePerformance::processKernelCpu)/1000.,mainUser=sum(&FramePerformance::mainUserCpu)/1000.,mainKernel=sum(&FramePerformance::mainKernelCpu)/1000.;
        std::uint32_t processId=0,logicalProcessors=std::thread::hardware_concurrency();std::string executable;
#ifdef _WIN32
        processId=GetCurrentProcessId();
        if(const auto count=GetActiveProcessorCount(ALL_PROCESSOR_GROUPS))logicalProcessors=count;
        std::array<wchar_t,32768> executablePath{};const auto pathLength=GetModuleFileNameW(nullptr,executablePath.data(),DWORD(executablePath.size()));
        if(pathLength&&pathLength<executablePath.size())executable=Utf8(fs::path(std::wstring(executablePath.data(),pathLength)));
#endif
        const double mainOneCorePercent=100.*(mainUser+mainKernel)/std::max(cpuWall,1e-9),processOneCorePercent=100.*(processUser+processKernel)/std::max(cpuWall,1e-9),processMachinePercent=processOneCorePercent/std::max(1u,logicalProcessors);
        std::cout<<std::fixed<<std::setprecision(3)<<"Realtime benchmark (initialization + "<<warmupFrames<<" warmup frames excluded, visible window, vsync "<<(options.noVsync?"off":"on")<<"): "<<samples.size()<<" frames / "<<wall<<" s / "<<fps<<" FPS\n"
            <<"Frame ms mean="<<mean(&FramePerformance::frame)<<" median="<<percentile(.5)<<" p95="<<percentile(.95)<<" max="<<frameTimes.back()<<"; CPU means cloth="<<mean(&FramePerformance::cloth)<<" skin="<<mean(&FramePerformance::skin)<<" upload="<<mean(&FramePerformance::upload)<<" render="<<mean(&FramePerformance::render)<<" UI="<<mean(&FramePerformance::ui)<<" swap="<<mean(&FramePerformance::swap)<<" render sync="<<mean(&FramePerformance::sync)<<"; GPU scene span="<<gpu<<" ms\n"
            <<"Animation="<<sum(&FramePerformance::animationSeconds)<<" s, simulated="<<sum(&FramePerformance::simulationSeconds)<<" s / wall="<<wall<<" s; fixed steps="<<steps<<" ("<<double(steps)/std::max(wall,1e-9)<<" / s); accepted delta="<<sum(&FramePerformance::acceptedDelta)<<" s / raw wall delta="<<sum(&FramePerformance::wallDelta)<<" s\n";
        std::cout<<"Process PID="<<processId<<" | executable="<<executable<<" | logical processors="<<logicalProcessors<<"\n";
        std::cout<<"OpenGL GPU="<<gpuRenderer_<<" | vendor="<<gpuVendor_<<"\n";
        std::cout<<"Preview scheduling QoS="<<options.previewQos<<"\n";
        std::cout<<"Frame limiter="<<options.maxFps<<" FPS (0=uncapped); wait backend="<<waitBackend<<"; sleep mean="<<mean(&FramePerformance::limiterWait)<<" ms, included in frame/wall time; focused/unfocused frames="<<focusedFrames<<" / "<<samples.size()-focusedFrames<<"; minimized wait="<<sum(&FramePerformance::minimizedWait)/1000.<<" s; interactive focus throttling disabled for benchmark\n";
        std::cout<<"Constraint iterations executed="<<constraintIterations<<"; limit="<<clothSettings_.iterations<<" per substep; convergence="<<clothSettings_.convergenceTolerance*1000<<" mm\n";
        std::cout<<"Cloth mode="<<(clothWorker_?"asynchronous snapshots":"synchronous")<<"; completed snapshots="<<snapshots<<" ("<<double(snapshots)/std::max(wall,1e-9)<<" / s); physics lag mean="<<mean(&FramePerformance::clothLag)<<" ms, final="<<samples.back().clothLag<<" ms\n";
        std::cout<<"Hair enabled="<<(hairAsset_&&hairEnabled_)<<"; fixed steps="<<hairSteps<<" ("<<double(hairSteps)/std::max(wall,1e-9)<<" / s); main-thread mean="<<mean(&FramePerformance::hair)<<" ms; observed worker-step mean="<<sum(&FramePerformance::hairWorker)/std::max<std::size_t>(1,hairWorkerSamples)<<" ms; observed surface penetration="<<hairMaximumPenetration*1000<<" mm\n";
        std::cout<<"Hair clock includes "<<hairSleepSteps<<" sleeping ticks; active solves="<<hairSteps-std::min(hairSteps,hairSleepSteps)<<" ("<<double(hairSteps-std::min(hairSteps,hairSleepSteps))/std::max(wall,1e-9)<<" / s)\n";
        if(cpuSamples)std::cout<<"CPU accounting ("<<cpuSamples<<" frames / "<<cpuWall<<" wall s): process user="<<processUser<<" kernel="<<processKernel<<" s; main thread user="<<mainUser<<" kernel="<<mainKernel<<" s; main thread="<<mainOneCorePercent<<"% of one logical processor; process="<<processOneCorePercent<<"% of one / "<<processMachinePercent<<"% of machine capacity\n";
        else std::cout<<"CPU accounting unavailable on this platform\n";
        if(options.benchmarkOutput.empty())return;
        if(!options.benchmarkOutput.parent_path().empty())fs::create_directories(options.benchmarkOutput.parent_path());
        std::ofstream output(options.benchmarkOutput);Check(bool(output),"Could not create benchmark report");
        output<<std::fixed<<std::setprecision(6)<<"{\n  \"mode\": \"real_time\",\n  \"initialization_excluded\": true,\n  \"warmup_frames\": "<<warmupFrames<<",\n  \"measured_frames\": "<<samples.size()<<",\n  \"visible_window\": true,\n  \"vsync\": "<<(options.noVsync?"false":"true")<<",\n  \"cloth_enabled\": "<<(clothEnabled_?"true":"false")<<",\n  \"capture_enabled\": "<<(!options.captureDirectory.empty()?"true":"false")<<",\n";
        output<<"  \"pipeline\": \""<<(pbrEnabled_?"pbr":"toon")<<"\",\n";
        output<<"  \"directSunScale\": "<<directSunScale_<<",\n  \"directPunctualScale\": "<<directPunctualScale_<<",\n";
        const auto shadowSkinState=ShadowSkinCaptureState();Json::StreamWriterBuilder stateWriter;stateWriter["indentation"]="";
        for(const auto& key:shadowSkinState.getMemberNames())output<<"  "<<JsonString(key)<<": "<<Json::writeString(stateWriter,shadowSkinState[key])<<",\n";
        output<<"  \"strand_hair_enabled\": "<<(strandHair_.loaded&&strandHair_.visible?"true":"false")<<",\n"
            <<"  \"strand_motion_active\": "<<(pbr_.Statistics().strandMotionActive?"true":"false")<<",\n"
            <<"  \"strand_history_used\": "<<(pbr_.Statistics().strandHairHistoryUsed?"true":"false")<<",\n"
            <<"  \"strand_dslut\": "<<JsonString(NativeHairMomentStatus(strandHair_.material))<<",\n"
            <<"  \"strand_volume_filter\": "<<JsonString(strandHair_.material.volumeHardwareFiltering?"linear":"legacy")<<",\n";
        output<<"  \"strand_ds_volume_mode\": "<<JsonString(strandHair_.material.sourceDSVolume?"native":"legacy")<<",\n"
            <<"  \"strand_ds_raster_trace_step\": "<<strandHair_.material.volumeRasterTraceStep<<",\n"
            <<"  \"strand_ds_trace_limit\": "<<strandHair_.material.volumeTraceLimit<<",\n"
            <<"  \"strand_ds_interpolation_samples\": "<<strandHair_.material.volumeInterpolationSamples<<",\n"
            <<"  \"volumeInjectionRate\": "<<strandHair_.material.volumeInjectionRate<<",\n"
            <<"  \"volumeInjectionLodFraction\": "<<strandHair_.material.volumeInjectionLodFraction<<",\n"
            <<"  \"volumeInjectionMinLodFactor\": "<<(strandHair_.material.volumeInjectionMinLodFactor?"true":"false")<<",\n"
            <<"  \"volumeInjectionQuality\": "<<strandHair_.material.volumeInjectionQuality<<",\n"
            <<"  \"nativeDsInputSegments\": "<<pbr_.Statistics().strandNativeDsInputSegments<<",\n"
            <<"  \"nativeDsInjectedSegments\": "<<pbr_.Statistics().strandNativeDsInjectedSegments<<",\n"
            <<"  \"nativeDsInjectionFraction\": "<<pbr_.Statistics().strandNativeDsInjectionFraction<<",\n"
            <<"  \"nativeDsRasterWidthScale\": "<<pbr_.Statistics().strandNativeDsRasterWidthScale<<",\n"
            <<"  \"nativeDsAverageWidth\": "<<pbr_.Statistics().strandNativeDsAverageWidth<<",\n"
            <<"  \"nativeDsInjectionPolicy\": "<<JsonString(strandHair_.material.sourceDSVolume?
                "incoming=floor(double(total)*double(float(lodFraction))); quality<2 or MinLod gates rate; count uses incoming cap and float truncation; RasterWidthScale compensation is paired with selected prefix; WidthAve stays at source header":
                "inactive: injection settings do not affect legacy density rendering")<<",\n";
        output<<"  \"strand_preserve_near_field\": "<<(strandHair_.material.volumePreserveNearField?"true":"false")<<",\n"
            <<"  \"strand_trace_exclusion_metres\": "<<pbr_.Statistics().strandVolumeBiasMetres/std::max(pbrSettings_.skinDiffusion.worldUnitScale,1e-6f)<<",\n";
        output<<"  \"strand_actual_tangents\": "<<(strandHair_.simulation.Settings().reconstructRenderTangents?"true":"false")<<",\n"
            <<"  \"strand_spatial_ibl_incident\": "<<(strandHair_.material.sourceSpatialIbl?"true":"false")<<",\n";
        output<<"  \"hair_front_depth_groups\": "<<pbrSettings_.strandFrontDepthGroups<<",\n  \"hair_volume_budget\": "<<strandHair_.material.volumeResolution<<",\n  \"hair_source_direct\": "<<(strandHair_.material.sourceDirectScattering?"true":"false")<<",\n  \"hair_opaque_blockers\": "<<(strandHair_.material.volumeOpaqueBlockers?"true":"false")<<",\n  \"hair_temporal_depth\": "<<(pbrSettings_.strandTemporalDepth?"true":"false")<<",\n  \"display_study\": "<<displayStudy_<<",\n";
        output<<"  \"process_id\": "<<processId<<",\n  \"executable\": "<<JsonString(executable)<<",\n  \"logical_processors\": "<<logicalProcessors<<",\n  \"cpu_time_available\": "<<(cpuSamples?"true":"false")<<",\n  \"cpu_time_samples\": "<<cpuSamples<<",\n  \"cpu_accounting_wall_seconds\": "<<cpuWall<<",\n  \"process_user_cpu_seconds\": "<<processUser<<",\n  \"process_kernel_cpu_seconds\": "<<processKernel<<",\n  \"process_cpu_seconds\": "<<processUser+processKernel<<",\n  \"main_thread_user_cpu_seconds\": "<<mainUser<<",\n  \"main_thread_kernel_cpu_seconds\": "<<mainKernel<<",\n  \"main_thread_cpu_seconds\": "<<mainUser+mainKernel<<",\n  \"main_thread_one_core_percent\": "<<mainOneCorePercent<<",\n  \"process_one_core_percent\": "<<processOneCorePercent<<",\n  \"process_machine_percent\": "<<processMachinePercent<<",\n";
        output<<"  \"hair_paired_transparency\": "<<(pbr_.Statistics().strandPairedTransparencyActive?"true":"false")<<",\n"
            <<"  \"hair_oit_mode\": "<<JsonString(HairOitMode(pbrSettings_))<<",\n"
            <<"  \"hair_native_oit_layers_requested\": "<<pbrSettings_.strandNativeOitLayers<<",\n"
            <<"  \"hair_native_oit_active\": "<<(pbr_.Statistics().strandNativeOitActive?"true":"false")<<",\n"
            <<"  \"hair_native_oit_layers\": "<<pbr_.Statistics().strandNativeOitLayers<<",\n"
            <<"  \"hair_native_oit_compute_passes\": "<<pbr_.Statistics().strandNativeOitComputePasses<<",\n"
            <<"  \"hair_native_oit_bytes\": "<<pbr_.Statistics().strandNativeOitBytes<<",\n"
            <<"  \"hair_transparency_passes\": "<<pbr_.Statistics().strandPasses<<",\n"
            <<"  \"hair_transparency_geometry_draws\": "<<pbr_.Statistics().strandTransparencyRasterPasses<<",\n"
            <<"  \"hair_exact_volume_integration\": "<<(strandHair_.material.volumeExactIntegration?"true":"false")<<",\n"
            <<"  \"hair_closed_head_proxies\": "<<(strandHair_.material.volumeProxyBlockers?"true":"false")<<",\n"
            <<"  \"hair_stable_opaque_cache\": "<<(strandHair_.material.volumeStableOpaqueCache?"true":"false")<<",\n"
            <<"  \"hair_point_ibl_paths\": "<<(strandHair_.material.volumePointIbl?"true":"false")<<",\n"
            <<"  \"hair_debug_mode\": "<<std::uint32_t(pbrSettings_.strandDebugMode)<<",\n"
            <<"  \"hair_volume_passes_final\": "<<pbr_.Statistics().strandVolumePasses<<",\n"
            <<"  \"hair_blocker_passes_final\": "<<pbr_.Statistics().strandBlockerPasses<<",\n"
            <<"  \"hair_adaptive_history\": "<<(pbrSettings_.temporal.hairCoverageHistoryRejection?"true":"false")<<",\n"
            <<"  \"hair_shutter_samples\": "<<pbr_.Statistics().strandShutterSamples<<",\n";
#ifdef NDEBUG
        output<<"  \"build\": \"Release\",\n";
#else
        output<<"  \"build\": \"Debug\",\n";
#endif
        output<<"  \"viewport\": ["<<previewWidth_<<", "<<previewHeight_<<"],\n  \"cloth_iterations\": "<<clothSettings_.iterations<<",\n  \"cloth_substeps\": "<<clothSettings_.substeps<<",\n  \"wall_seconds\": "<<wall<<",\n  \"animation_seconds\": "<<sum(&FramePerformance::animationSeconds)<<",\n  \"simulation_seconds\": "<<sum(&FramePerformance::simulationSeconds)<<",\n  \"accepted_delta_seconds\": "<<sum(&FramePerformance::acceptedDelta)<<",\n  \"raw_delta_seconds\": "<<sum(&FramePerformance::wallDelta)<<",\n  \"fixed_steps\": "<<steps<<",\n  \"fixed_steps_per_second\": "<<double(steps)/std::max(wall,1e-9)<<",\n  \"fps\": "<<fps<<",\n  \"frame_ms_mean\": "<<mean(&FramePerformance::frame)<<",\n  \"frame_ms_median\": "<<percentile(.5)<<",\n  \"frame_ms_p95\": "<<percentile(.95)<<",\n  \"frame_ms_max\": "<<frameTimes.back()<<",\n";
        output<<"  \"convergence_tolerance_m\": "<<clothSettings_.convergenceTolerance<<",\n";
        output<<"  \"gpu_vendor\": "<<JsonString(gpuVendor_)<<",\n  \"gpu_renderer\": "<<JsonString(gpuRenderer_)<<",\n";
        output<<"  \"preview_qos\": "<<JsonString(options.previewQos)<<",\n";
        output<<"  \"max_fps\": "<<options.maxFps<<",\n  \"limiter_wait_ms_mean\": "<<mean(&FramePerformance::limiterWait)<<",\n  \"limiter_wait_included_in_frame\": true,\n  \"focused_frames\": "<<focusedFrames<<",\n  \"unfocused_frames\": "<<samples.size()-focusedFrames<<",\n  \"background_fps_limit\": 15,\n  \"focus_throttle_applied\": false,\n  \"minimized_wait_seconds\": "<<sum(&FramePerformance::minimizedWait)/1000.<<",\n";
        output<<"  \"frame_wait_backend\": "<<JsonString(waitBackend)<<",\n";
        output<<"  \"cloth_async\": "<<(clothWorker_?"true":"false")<<",\n  \"cloth_snapshots\": "<<snapshots<<",\n  \"cloth_snapshots_per_second\": "<<double(snapshots)/std::max(wall,1e-9)<<",\n  \"physics_lag_ms_mean\": "<<mean(&FramePerformance::clothLag)<<",\n  \"physics_lag_ms_final\": "<<samples.back().clothLag<<",\n";
        output<<"  \"hair_enabled\": "<<(hairAsset_&&hairEnabled_?"true":"false")<<",\n  \"hair_steps\": "<<hairSteps<<",\n  \"hair_fixed_steps_per_second\": "<<double(hairSteps)/std::max(wall,1e-9)<<",\n  \"hair_main_thread_ms_mean\": "<<mean(&FramePerformance::hair)<<",\n  \"hair_worker_step_ms_mean\": "<<sum(&FramePerformance::hairWorker)/std::max<std::size_t>(1,hairWorkerSamples)<<",\n  \"hair_observed_surface_penetration_m\": "<<hairMaximumPenetration<<",\n";
        output<<"  \"hair_observed_attachment_drift_m\": "<<hairMaximumAttachmentDrift<<",\n";
        output<<"  \"hair_sleep_steps\": "<<hairSleepSteps<<",\n  \"hair_active_solves\": "<<hairSteps-std::min(hairSteps,hairSleepSteps)<<",\n  \"hair_active_solves_per_second\": "<<double(hairSteps-std::min(hairSteps,hairSleepSteps))/std::max(wall,1e-9)<<",\n";
        output<<"  \"constraint_iterations_executed\": "<<constraintIterations<<",\n";
        const std::array<std::pair<const char*,double FramePerformance::*>,9> fields{{{"pose",&FramePerformance::pose},{"skin",&FramePerformance::skin},{"upload",&FramePerformance::upload},{"cloth",&FramePerformance::cloth},{"cloth_apply",&FramePerformance::apply},{"render",&FramePerformance::render},{"ui",&FramePerformance::ui},{"swap",&FramePerformance::swap},{"render_sync",&FramePerformance::sync}}};
        for(const auto& [name,field]:fields)output<<"  \""<<name<<"_ms_mean\": "<<mean(field)<<",\n";
        output<<"  \"gpu_scene_span_ms_mean\": "<<gpu<<",\n  \"gpu_samples\": "<<gpuSamples<<",\n  \"sync_is_overlapping\": true,\n  \"gpu_scene_span_includes_dispatch_gaps\": true\n}\n";
        Check(bool(output),"Could not write benchmark report");
    }
public:
    Editor(GLFWwindow* window,const Render::OpenglBackendContext& context,const Options& options):window_(window),context_(context) {
        pbrSettings_.strandMotionVectors=true;
        lightingPreset_=options.lighting;
        sourceEnvironmentOverride_=options.sourceEnvironmentOverride;
        hairHostAmbientScaleOverride_=options.hairHostAmbientScale;
        sourceDirectOverride_=options.sourceDirectOverride;
        directSunScale_=options.directSunScale;directPunctualScale_=options.directPunctualScale;
        // Lighting/environment rebuilds edit sun input values, preserving the
        // explicit filter study and its normalized-UV radius independently.
        pbrSettings_.shadows.filter=options.sunShadowFilter;pbrSettings_.shadows.sourcePoissonRadius=options.sunShadowRadius;
        pbrSettings_.shadows.sourceSurfaceRadius=options.sunSurfaceShadowRadius;
        pbrSettings_.shadows.strandCoverage=options.hairShadowCoverage;
        pbrSettings_.shadows.strandOpacityScale=options.hairShadowExtinctionScale;
        hairDirectPath_=options.hairDirectProfile.empty()?Path(MODEL_SOURCE_DIR)/"Asset/Diana/st30-101-direct-lighting.json":options.hairDirectProfile;
        hairLightingPath_=options.hairLightingProfile.empty()?Path(MODEL_SOURCE_DIR)/"Asset/Diana/st30-101-lighting.json":options.hairLightingProfile;
        dianaPreview_=options.diana;
        disableSkinDiffusion_=options.noSkinDiffusion;disableEyeRefraction_=options.noEyeRefraction;
        pbrSettings_.eyeShell.enabled=!options.noEyeShell;
        pbrSettings_.skinDiffusion.kernel=options.skinKernel;
        if(!options.materialProfile.empty()){std::string error;characterMaterials_=CharacterMaterialProfile::Load(options.materialProfile,&error);Check(characterMaterials_.has_value(),error);}
        if(!options.skinProfile.empty())skinProfile_=EditorPreview::SkinPreviewProfile::Load(options.skinProfile);
        if(options.skin>=0)skinProfile_.settings.enabled=options.skin!=0;
        device_.Run(Render::BackendType::Opengl,context_);Check(models_.Startup(),models_.LastError());
        if(options.dianaTest)Model::TestData::RunModelGeometryRevisionGpuOracle(models_);
        if(!options.character.empty())LoadCharacter(options.character);
        else Load(options.package.empty()?options.source:options.package,!options.package.empty());
        if(!options.retargetSource.empty()){LoadRetargetSource(options.retargetSource);retargetPlaying_=!retargetSource_->animations.empty();selection_=Selection::Retarget;}
        SetText(animationName_,"VMD animation");
        for(const auto& animation:options.animations)OpenAnimation(animation);
        if(!options.vmds.empty())ImportMotion(options.vmds,"VMD motion");
        if(!options.animations.empty()||!options.vmds.empty())playing_=true;
        for(std::size_t i=0;i<asset_->skeleton.bones.size();++i)if(asset_->skeleton.bones[i].name==options.curveBone||asset_->skeleton.bones[i].englishName==options.curveBone)selectedBone_=int(i);
        time_=options.startTime;
        clothEnabled_=!options.noCloth;
        if(!options.hair.empty())LoadHair(options.hair);
        hairEnabled_=!options.noHair;
        if(!options.smoke&&!options.syncCloth)clothWorker_=std::make_unique<detail::ClothPreviewWorker>();
        if(options.motionTest){
            selection_=Selection::Animation;enableIk_=false;
            if(asset_->sourceFormat=="blend"&&options.animations.empty()&&options.vmds.empty()){selectedClip_=std::max(0,GeneratedClip(*asset_,"RigPreview"));bindPose_=false;playing_=true;}
        }
        if(options.hairTest){Check(bool(hairAsset_)&&hairEnabled_,"--hair-test needs a compatible enabled hair asset");selection_=Selection::Hair;showHairNodes_=!options.cleanClothView;showBones_=false;}
        if(options.clothTest){Check(modular_.has_value(),"--cloth-test requires --character");selection_=Selection::Cloth;showClothWire_=!options.cleanClothView;showClothColliders_=!options.cleanClothView;}
        if(options.meshRegions) {
            selection_=Selection::MeshRegions;showBones_=false;regionCategoryFilter_=options.regionCategory;
            std::size_t visible=0;for(std::uint32_t mi=0;mi<asset_->geometry->meshes.size();++mi)if(RegionVisible(mi)){if(!visible&&regionCategoryFilter_>=0)selectedRegionMesh_=int(mi);++visible;}
            auto category=regionCategoryFilter_<0?std::string("all"):std::string(MeshRegionKindName(static_cast<MeshRegionKind>(regionCategoryFilter_)));
            std::transform(category.begin(),category.end(),category.begin(),[](unsigned char c){return char(std::tolower(c));});
            std::cout<<"Mesh region preview: category="<<category<<"; expected visible sections="<<visible<<" / "<<asset_->geometry->meshes.size()<<'\n';
        }
        if(options.bindPose){selectedClip_=std::max(0,GeneratedClip(*asset_,"BindPose"));time_=0;playing_=false;retargetPlaying_=false;bindPose_=true;enableIk_=false;std::fill(manualMorphs_.begin(),manualMorphs_.end(),0);std::fill(morphOverrides_.begin(),morphOverrides_.end(),0);dirty_=true;clothReset_=true;hairReset_=true;showBones_=false;showNames_=false;}
        yaw_=float(options.yaw);distance_=float(options.distance);
        if(!options.strandHair.empty()) {
            Check(strandHair_.Load(options.strandHair,options.gravityMode),strandHair_.Error());
            selection_=Selection::StrandHair;showBones_=false;showNames_=false;playing_=false;bindPose_=true;
            const auto& bounds=strandHair_.simulation.Asset()->header;
            const glm::dvec3 lo(bounds.boundsMin[0],bounds.boundsMin[1],bounds.boundsMin[2]),hi(bounds.boundsMax[0],bounds.boundsMax[1],bounds.boundsMax[2]);
            // The imported head and extracted strands share metre coordinates.
            // Frame their combined hair bounds instead of scaling the head alone.
            if(!options.diana)placement_=glm::scale(glm::dmat4(1),glm::dvec3(2.7/(hi.y-lo.y)))*glm::translate(glm::dmat4(1),-(lo+hi)*.5);
            cameraFocus_={0,0,0};
            strandHair_.paused=options.diana&&!options.hairDynamic;strandHair_.visible=!options.noHair;
            if(characterMaterials_)ApplyStrandMaterialProfile(options.hairDsVolume);
            if(options.diana){
                auto capsules=std::make_shared<std::vector<Render::StrandOpaqueCapsule>>();
                // Source CLSP v3 capsules in the extracted Head bind pose.
                capsules->push_back({{1.95595e-8f,.96886315f,-.00821461f,.06687821f},{-1.68346e-7f,.92563673f,.01308093f,.05f}});
                capsules->push_back({{-4.58696e-7f,.90708285f,.04598658f,.04706905f},{-.00035261555f,.96938082f,.02377333f,.05984419f}});
                strandHair_.SetOpaqueCapsules(std::move(capsules));
            }
            if(options.noHairScattering){strandHair_.material.physicalScattering=false;strandHair_.material.spatialVolume=false;}
            if(options.noHairMultipleScattering)strandHair_.material.multipleScattering=0;
            if(options.noHairVolume)strandHair_.material.spatialVolume=false;
            if(options.noHairOpaqueShadows)strandHair_.material.receiveOpaqueShadows=false;
            if(options.hairOpaqueShadowPowerSet)strandHair_.material.opaqueShadowPower=options.hairOpaqueShadowPower;
            if(options.hairRoughnessInput>=0)strandHair_.material.sourceRoughnessMapping=options.hairRoughnessInput!=0;
            if(options.noHairSourceColor)strandHair_.material.sourceColor=false;
            if(options.noHairNtt)strandHair_.material.useNttLookup=false;
            if(options.noHairNativeLut)strandHair_.material.useNativeMomentLut=false;
            if(options.hairVolumeFilter>=0)strandHair_.material.volumeHardwareFiltering=options.hairVolumeFilter!=0;
            if(options.hairDsVolume>=0)strandHair_.material.sourceDSVolume=options.hairDsVolume!=0;
            ApplyNativeDSInjectionParameters(strandHair_.material,
                options.hairDsInjectionRateSet?options.hairDsInjectionRate:strandHair_.material.volumeInjectionRate,
                options.hairDsLodFractionSet?options.hairDsLodFraction:strandHair_.material.volumeInjectionLodFraction,
                options.hairDsInjectionQualitySet?options.hairDsInjectionQuality:float(strandHair_.material.volumeInjectionQuality),
                options.hairDsMinLod>=0?options.hairDsMinLod!=0:strandHair_.material.volumeInjectionMinLodFactor);
            if(options.hairCoverage>=0)strandHair_.material.areaPreservingCoverage=options.hairCoverage!=0;
            if(options.hairVolumeSky){strandHair_.material.volumeSkyTransmission=true;strandHair_.material.dualScatteringIbl=false;}
            if(options.hairIbl>=0){strandHair_.material.volumeSkyTransmission=options.hairIbl!=0;strandHair_.material.dualScatteringIbl=options.hairIbl==2;}
            pbrSettings_.strandFrontDepthGroups=options.hairQuality==0?2:options.hairQuality==1?3:7;
            strandHair_.material.volumeResolution=options.hairQuality==0?32:64;
            if(options.hairVolumeResolution>0)strandHair_.material.volumeResolution=std::uint32_t(options.hairVolumeResolution);
            Check(!strandHair_.material.sourceDSVolume||strandHair_.material.volumeResolution==32||strandHair_.material.volumeResolution==64||strandHair_.material.volumeResolution==128,
                "Native DSVolume requires volume budget32,64 or128; choose --hair-ds-volume legacy to use16 or48");
            if(options.hairVolumeIntegration>=0)strandHair_.material.volumeExactIntegration=options.hairVolumeIntegration!=0;
            if(options.hairProxyBlockers>=0)strandHair_.material.volumeProxyBlockers=options.hairProxyBlockers!=0;
            if(options.hairStableCache>=0)strandHair_.material.volumeStableOpaqueCache=options.hairStableCache!=0;
            if(options.hairPointIbl>=0)strandHair_.material.volumePointIbl=options.hairPointIbl!=0;
            pbrSettings_.strandDebugMode=Render::StrandDebugMode(options.hairDebug);
            if(options.hairShading>=0)strandHair_.material.sourceSegmentShading=options.hairShading!=0;
            if(options.hairGeometry>=0)strandHair_.material.sourceCurveGeometry=options.hairGeometry!=0;
            if(options.hairPairedTransparency>=0)pbrSettings_.strandPairedLayerTransparency=options.hairPairedTransparency!=0;
            if(options.hairAdaptiveHistory>=0)pbrSettings_.temporal.hairCoverageHistoryRejection=options.hairAdaptiveHistory!=0;
            if(options.hairFastMotionPixels>=0)pbrSettings_.temporal.hairFastMotionPixels=options.hairFastMotionPixels;
            pbrSettings_.temporal.hairMotionBlurEnabled=options.hairShutter;
            if(options.noHairNearShadow)strandHair_.material.volumePreserveNearField=false;
            if(options.hairTangents>=0)Check(strandHair_.SetRenderTangents(options.hairTangents!=0),strandHair_.Error());
            if(options.hairIblIncident>=0)strandHair_.material.sourceSpatialIbl=options.hairIblIncident!=0;
            pbrSettings_.strandTemporalDepth=!options.noHairTemporalDepth;
            if(options.noHairMotion)pbrSettings_.strandMotionVectors=false;
            if(options.noHairSourceDirect)strandHair_.material.sourceDirectScattering=false;
            if(options.noHairBlockers)strandHair_.material.volumeOpaqueBlockers=false;
            if(options.noHairCastShadows)strandHair_.material.castDirectionalShadows=false;
            if(options.noHairBackscatter)strandHair_.material.backscatterScale=0;
        }
        if(options.diana){selection_=Selection::CharacterMaterials;for(std::size_t i=0;i<characterMaterialEdits_.size();++i)if(characterMaterialEdits_[i].name=="Face_mat")selectedCharacterMaterial_=int(i);}
        if(options.frameRegion>=-1)FramePreviewRegions(options.frameRegion);
        if(options.dianaFace)FrameCharacter(true);
        ApplyDisplayStudy(options.displayStudy);
        if(!options.focusBone.empty()){for(std::size_t i=0;i<asset_->skeleton.bones.size();++i)if(asset_->skeleton.bones[i].name==options.focusBone||asset_->skeleton.bones[i].englishName==options.focusBone){trackedCameraBone_=int(i);break;}Check(trackedCameraBone_>=0,"Camera focus bone was not found");}
        settings_.backgroundColor=glm::vec4(.95f,.96f,.98f,1);settings_.groundHeight=-1.5f;
        // Prepare only the selected renderer; switching initializes the other
        // one lazily and never submits both pipelines for one preview frame.
        SelectPipeline(options.pbr);
        if(options.noHairSourceIndirect)pbrSettings_.strandIndirect.enabled=false;
        if(!options.cameraReferenceProfile.empty())referenceCamera_=EditorPreview::LoadDianaCameraReference(options.cameraReferenceProfile);
        captureCameraMetadata_=!options.cameraReferenceExport.empty()||!options.hairLinearCapture.empty();
        // Initialize lights at the existing default camera first. Applying a
        // reference view must not reorient or retint the selected light rig.
        if(options.cameraReference=="game-shot")ApplyReferenceCamera(true);
        pbrSettings_.strandFrontLayerTransparency=options.hairTransparency>=0?options.hairTransparency!=0:options.diana;
        if(options.hairOit>=0){
            pbrSettings_.strandNativeOit=options.hairOit>0;
            if(options.hairOit>0){pbrSettings_.strandNativeOitLayers=std::uint32_t(options.hairOit);strandHair_.material.sourceSegmentShading=true;}
            else pbrSettings_.strandFrontLayerTransparency=true;
        }
        IMGUI_CHECKVERSION();ImGui::CreateContext();guiReady_=true;
        auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
        RebuildFonts();
        ImGui::StyleColorsDark();auto& style=ImGui::GetStyle();style.WindowRounding=0;style.ChildRounding=5;style.FrameRounding=3;style.WindowPadding=ImVec2(12,10);
        style.Colors[ImGuiCol_WindowBg]=ImVec4(.07f,.085f,.11f,1);style.Colors[ImGuiCol_ChildBg]=ImVec4(.09f,.108f,.14f,1);
        Check(ImGui_ImplGlfw_InitForOpenGL(window_,true),"Could not initialize editor input backend");
        OnRender([this]{Check(ImGui_ImplOpenGL3_Init("#version 450 core"),"Could not initialize editor OpenGL UI");rendererReady_=true;Check(ImGui_ImplOpenGL3_CreateDeviceObjects(),"Could not create editor UI resources");for(auto& timer:gpuTimers_)glGenQueries(GLsizei(timer.query.size()),timer.query.data());if(const auto value=glGetString(GL_VENDOR))gpuVendor_=reinterpret_cast<const char*>(value);if(const auto value=glGetString(GL_RENDERER))gpuRenderer_=reinterpret_cast<const char*>(value);});
        std::cout<<"Skeletal asset editor: "<<asset_->name<<" | "<<asset_->skeleton.bones.size()<<" bones | "<<asset_->morphs.size()<<" morphs | "<<asset_->animations.size()<<" sequences | "<<asset_->ikRig.chains.size()<<" IK chains | pipeline="<<(pbrEnabled_?"pbr":"toon")<<" | lighting="<<std::array<const char*,6>{"studio","side","back","reference","source-cube","reference-neutral"}[lightingPreset_]
            <<" | hair-ibl="<<(!strandHair_.material.volumeSkyTransmission?"legacy":strandHair_.material.dualScatteringIbl?"source":"forward")<<" | TT/back gain="<<strandHair_.material.backscatterScale
            <<" | hair-dslut="<<NativeHairMomentStatus(strandHair_.material)<<" | volume-filter="<<(strandHair_.material.volumeHardwareFiltering?"linear":"legacy")
            <<" | hair-motion="<<pbrSettings_.strandMotionVectors<<" | hair-dynamic="<<(strandHair_.loaded&&!strandHair_.paused)<<'\n';
        std::cout<<"Hair OIT="<<HairOitMode(pbrSettings_)<<"; native study uses deterministic GPU scheduling; source runtime branch/atomic contention remains unobserved\n";
        std::cout<<"Hair shadow coverage="<<(pbrSettings_.shadows.strandCoverage==Render::StrandShadowCoverage::DeepOpacity?"deep-opacity":"ordered")
            <<"; extinction scale="<<pbrSettings_.shadows.strandOpacityScale
            <<"; HOST caster adapter, native shadow PSO unobserved; DS and hair receiving closure retained\n";
        std::cout<<"Hair longitudinal input="<<strandHair_.material.longitudinalRoughness
            <<"; source transfer="<<strandHair_.material.sourceRoughnessMapping
            <<"; shader hm_v="<<Render::EffectiveStrandLongitudinalRoughness(strandHair_.material)<<'\n';
        std::cout<<"Hair external sun CSM: receive="<<strandHair_.material.receiveOpaqueShadows<<"; opaqueShadowPower="<<strandHair_.material.opaqueShadowPower
            <<"; native constructor default.5 and CPU constant writer recovered; runtime value unobserved; DS/IBL and skin caster response unchanged\n";
        std::cout<<"Hair DSVolume="<<(strandHair_.material.sourceDSVolume?"native-texture":"legacy-density")
            <<"; raster step="<<strandHair_.material.volumeRasterTraceStep<<"; trace factor cap="<<strandHair_.material.volumeTraceLimit
            <<"; filtered cursor="<<strandHair_.material.volumeInterpolationSamples
            <<"; source CPU trace cap32 recovered; Texture allocation/sampler remain host adapters\n";
        std::cout<<"Hair DS injection: rate="<<strandHair_.material.volumeInjectionRate
            <<"; LOD fraction="<<strandHair_.material.volumeInjectionLodFraction
            <<"; MinLodFactor="<<strandHair_.material.volumeInjectionMinLodFactor
            <<"; quality="<<strandHair_.material.volumeInjectionQuality
            <<"; "<<(strandHair_.material.sourceDSVolume?"active":"inactive in legacy mode")
            <<"; incoming=floor(double(total)*double(float(LOD fraction))); rate requested only at quality<2 or MinLodFactor"
            <<"; selected prefix and RasterWidthScale compensation are paired; WidthAve stays at source header; submitted statistics available after rendering\n";
    }
    ~Editor() {
        clothWorker_.reset();
        strandHair_.Release();
        if(rendererReady_)OnRender([this]{for(auto& timer:gpuTimers_)glDeleteQueries(GLsizei(timer.query.size()),timer.query.data());ImGui_ImplOpenGL3_Shutdown();});
        if(guiReady_){ImGui_ImplGlfw_Shutdown();ImGui::DestroyContext();}
        garmentResource_.reset();resource_.reset();pbr_.Shutdown();toon_.Shutdown();models_.Shutdown();device_.StopAndRelease();
        while(!device_.returnSystem.callbacks.empty())device_.returnSystem.DrainCallbacks();
    }
    void Run(const Options& options) {
        auto previous=std::chrono::steady_clock::now();
        ScopedFrameWaiter frameWaiter;
        const bool interactive=!options.benchmark&&!options.smoke;
        bool workerSuspended=false;
        const auto suspendPreview=[&](bool suspend){
            if(suspend==workerSuspended)return;
            workerSuspended=suspend;
            if(clothWorker_)clothWorker_->SetPaused(suspend);
            if(!suspend){
                // The worker discards queued background time and resumes from
                // its retained completed snapshot, without resetting cloth.
                const bool activeCloth=AsyncPreview();
                const bool resumeExisting=activeCloth&&clothGeneration_!=0&&!clothReset_;
                const auto latest=resumeExisting?clothWorker_->Latest():nullptr;
                if(latest)time_=clothAnimationTimeline_.At(latest->simulatedSeconds,time_);
                clothSubmittedSeconds_=latest?latest->simulatedSeconds:0;
                clothResumePending_=resumeExisting;
                clothResumeSerial_=0;
                previousClothTime_=-1;dirty_=true;
                previous=PerfClock::now();
            }
        };
        std::vector<FramePerformance> benchmarkSamples;if(options.benchmark)benchmarkSamples.reserve(options.frames-options.benchmarkWarmup);
        bool benchmarkReady=!AsyncPreview();std::uint32_t benchmarkFirstMeasuredFrame=0;
        double smokeMotion=0;std::vector<glm::dvec3> baseline;
        std::vector<std::uint8_t> baselinePixels;
        std::size_t smokeChangedPixels=0;
        double maxPhysicalDifference=0;std::size_t maxClothContacts=0;
        double smokeHairMaxPenetration=0,smokeHairMaxAttachmentDrift=0;
        std::vector<glm::dvec3> heldCloth;double heldMovement=0;std::uint64_t heldStep=0;
        bool captured=false;std::uint32_t captureIndex=0;
        for(std::uint32_t frame=0;!glfwWindowShouldClose(window_)&&(!options.frames||frame<options.frames);++frame) {
            const auto frameStarted=PerfClock::now();performance_={};
            const auto cpuStarted=options.benchmark?ReadCpuTimes():CpuTimes{};
            glfwPollEvents();if(glfwGetKey(window_,GLFW_KEY_ESCAPE)==GLFW_PRESS)break;
            int w=0,h=0;glfwGetFramebufferSize(window_,&w,&h);
            while(!glfwWindowShouldClose(window_)&&(glfwGetWindowAttrib(window_,GLFW_ICONIFIED)||w<=0||h<=0)){
                suspendPreview(true);
                const auto waitStarted=PerfClock::now();glfwWaitEvents();
                performance_.minimizedWait+=Milliseconds(PerfClock::now()-waitStarted);
                previous=PerfClock::now();glfwGetFramebufferSize(window_,&w,&h);
            }
            if(glfwWindowShouldClose(window_))break;
            performance_.focused=glfwGetWindowAttrib(window_,GLFW_FOCUSED)==GLFW_TRUE;
            const bool background=interactive&&!performance_.focused;
            const bool physicsFrozen=AsyncPreview()&&clothFrozen_;
            const bool resuming=workerSuspended&&!background&&!physicsFrozen;
            suspendPreview(background||physicsFrozen);
            if(fontsDirty_)RebuildFonts();
            auto now=PerfClock::now();performance_.wallDelta=std::chrono::duration<double>(now-previous).count();double delta=background||resuming||physicsFrozen?0:std::clamp(performance_.wallDelta,0.,.1);previous=now;
            if(options.smoke)delta=options.clothTest||options.hairTest||options.strandTest?1.0/60.0:.2;
            if(options.hairOrbitStudy)delta=1./60.;
            performance_.acceptedDelta=delta;
            hairRenderTime_+=delta;
            if(options.hairOrbitStudy)yaw_=float(options.yaw+.18*std::sin(double(frame)*.19));
            performance_.frameLimit=background?(options.maxFps>0?std::min(15.,options.maxFps):15.):options.maxFps;
            if(options.holdAt>=0&&playing_&&time_>=options.holdAt){playing_=false;dirty_=true;}
            if(options.equipTest&&frame==30) {
                const auto bodyResource=resource_;const auto body=asset_;const auto garmentResource=garmentResource_;
                const auto clip=selectedClip_;const auto time=time_;const auto playing=playing_;
                const auto& slot=modular_->assembly.slots.front();const auto base=openedPath_.parent_path();
                EquipGarment(base/slot.package,base/slot.cloth);
                Check(resource_==bodyResource&&asset_==body&&selectedClip_==clip&&time_==time&&playing_==playing,"Equipping a garment interrupted the body or animation");
                Check(garmentResource_!=garmentResource,"Equipping did not replace the independent garment resource");
                std::cout<<"Compatible garment re-equipped; body resource and active animation preserved\n";
            }
            // Background interaction displays the retained pose at 15 FPS.
            // Both animation and physics clocks pause instead of queuing work.
            if(!background)Evaluate(delta);
            if(options.hairTest){smokeHairMaxPenetration=std::max(smokeHairMaxPenetration,HairStats().maxSurfacePenetration);smokeHairMaxAttachmentDrift=std::max(smokeHairMaxAttachmentDrift,HairStats().maxAttachmentDrift);}
            if(options.clothTest&&modular_) {
                for(std::size_t m=0;m<garmentDeformed_.meshes.size();++m)for(std::size_t v=0;v<garmentDeformed_.meshes[m].vertices.size();++v)maxPhysicalDifference=std::max(maxPhysicalDifference,glm::length(garmentDeformed_.meshes[m].vertices[v].position-garmentAnimated_.meshes[m].vertices[v].position));
                maxClothContacts=std::max(maxClothContacts,clothSimulator_.Stats().contacts);
                if(options.holdAt>=0&&!playing_) {
                    const auto& positions=clothSimulator_.Positions();
                    if(heldCloth.empty()){heldCloth=positions;heldStep=clothSimulator_.Stats().steps;}
                    else for(std::size_t i=0;i<positions.size();++i)heldMovement=std::max(heldMovement,glm::length(positions[i]-heldCloth[i]));
                }
            }
            if(options.smoke&&!options.strandTest&&!options.dianaTest){
                if(frame==0)for(const auto& mesh:deformed_.meshes)for(const auto& v:mesh.vertices)baseline.push_back(v.position);
                else {std::size_t i=0;for(const auto& mesh:deformed_.meshes)for(const auto& v:mesh.vertices)smokeMotion=std::max(smokeMotion,glm::length(v.position-baseline[i++]));}
                if(!options.motionTest&&frame==3){if(!options.meshRegions)selection_=Selection::Morphs;for(std::size_t i=0;i<asset_->morphs.size();++i)if(std::any_of(asset_->morphs[i].deltas.begin(),asset_->morphs[i].deltas.end(),[](const auto& delta){return glm::length2(delta.position)>1e-12;})){manualMorphs_[i]=.5f;morphOverrides_[i]=1;dirty_=true;break;}}
                if(!options.motionTest&&frame==4){for(std::size_t i=0;i<asset_->ikRig.chains.size();++i)if(!asset_->ikRig.chains[i].native&&asset_->ikRig.chains[i].name=="leftArm"){selectedChain_=int(i);ResetIkTarget();ikTarget_.y+=float(asset_->geometry->bounds.Extent().y*.025);enableIk_=true;dirty_=true;break;}}
                if(!options.motionTest&&!options.meshRegions&&frame==5){selection_=retargetSource_?Selection::Retarget:Selection::Ik;}
            }
            const auto scale=ImGui::GetIO().DisplayFramebufferScale;
            auto pw=std::uint32_t(std::clamp(viewportSize_.x*std::max(1.f,scale.x),128.f,2048.f));
            auto ph=std::uint32_t(std::clamp(viewportSize_.y*std::max(1.f,scale.y),128.f,2048.f));
            if(referenceCameraEnabled_&&referenceCamera_.lockReferenceAspect){
                const auto fitted=EditorPreview::DianaReferenceViewport({float(pw),float(ph)},referenceCamera_);
                pw=std::uint32_t(std::max(128.f,std::floor(fitted.x)));ph=std::uint32_t(std::max(128.f,std::floor(fitted.y)));
            }
            Measure(performance_.render,[&]{RenderPreview(pw,ph);});
            if(!options.captureDirectory.empty()&&frame>=2&&frame%options.captureEvery==0){char name[64];std::snprintf(name,sizeof(name),"frame_%05u.png",captureIndex++);SavePreviewCapture(options.captureDirectory/name);}
            if(options.smoke&&frame>=1) {
                std::vector<std::uint8_t> pixels(std::size_t(pw)*ph*4);
                OnRender([&]{glGetTextureImage(previewTexture_,0,GL_RGBA,GL_UNSIGNED_BYTE,GLsizei(pixels.size()),pixels.data());Check(glGetError()==GL_NO_ERROR,"Preview image readback failed");});
                if(frame==1) {
                    baselinePixels=pixels;
                    std::set<std::uint32_t> colors;
                    for(std::size_t p=0;p<pixels.size();p+=4)colors.insert(std::uint32_t(pixels[p])|(std::uint32_t(pixels[p+1])<<8)|(std::uint32_t(pixels[p+2])<<16));
                    Check(colors.size()>64,"Preview framebuffer contains no rendered model detail");
                } else if(pixels.size()==baselinePixels.size()) {
                    std::size_t changed=0;for(std::size_t p=0;p<pixels.size();p+=4)if(std::abs(int(pixels[p])-int(baselinePixels[p]))+std::abs(int(pixels[p+1])-int(baselinePixels[p+1]))+std::abs(int(pixels[p+2])-int(baselinePixels[p+2]))>12)++changed;
                    smokeChangedPixels=std::max(smokeChangedPixels,changed);
                }
            }
            Measure(performance_.ui,[&]{ImGui_ImplGlfw_NewFrame();ImGui::NewFrame();BuildUi();ImGui::Render();});
            const bool last=options.frames&&frame+1==options.frames;
            const bool screenshot=!captured&&!options.screenshot.empty()&&(last||(!options.frames&&frame>2));
            OnRender([this,w,h,screenshot]{
                Measure(performance_.ui,[&]{glBindFramebuffer(GL_FRAMEBUFFER,0);glViewport(0,0,w,h);glDisable(GL_SCISSOR_TEST);glClearColor(.07f,.085f,.11f,1);glClear(GL_COLOR_BUFFER_BIT);ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());});
                if(!screenshot)Measure(performance_.swap,[&]{context_.thread_Swap();});
            });
            if(screenshot){SaveScreenshot(options.screenshot,w,h);captured=true;OnRender([this]{Measure(performance_.swap,[&]{context_.thread_Swap();});});}
            if(performance_.frameLimit>0){
                // Anchor each budget to this frame's start. A slow frame never
                // creates catch-up frames or a spin wait. Include sleeping in
                // frame wall time and process CPU accounting for benchmarks.
                const auto period=std::chrono::duration_cast<PerfClock::duration>(std::chrono::duration<double>(1/performance_.frameLimit));
                const auto deadline=frameStarted+period;
                if(PerfClock::now()<deadline){const auto waitStarted=PerfClock::now();frameWaiter.WaitUntil(deadline);performance_.limiterWait=Milliseconds(PerfClock::now()-waitStarted);}
            }
            if(options.benchmark)RecordCpuTimes(performance_,cpuStarted,ReadCpuTimes());
            performance_.frame=Milliseconds(PerfClock::now()-frameStarted);UpdatePerformanceDisplay();
            if(options.benchmark){
                const bool wasReady=benchmarkReady;
                if(!benchmarkReady&&clothSnapshot_&&clothSnapshot_->totalFixedSteps>0)benchmarkReady=true;
                if(wasReady&&frame>=options.benchmarkWarmup){if(benchmarkSamples.empty())benchmarkFirstMeasuredFrame=frame;benchmarkSamples.push_back(performance_);}
            }
        }
        if(!options.cameraReferenceExport.empty())EditorPreview::SaveDianaCameraReferenceMetadata(options.cameraReferenceExport,CameraReferenceMetadata());
        if(options.benchmark)ReportBenchmark(options,benchmarkSamples,benchmarkFirstMeasuredFrame,options.maxFps>0?frameWaiter.Backend():"unused (uncapped)");
        if(!options.hairLinearCapture.empty()){
            const auto textures=pbr_.DebugTextures();
            Check(textures.strandResolvedColor.IsValid(),"Hair linear capture requires separate hair temporal resources");
            fs::create_directories(options.hairLinearCapture);
            Json::Value report;report["schema"]="model.hair-linear-capture.v1";
            report["camera"]=CameraReferenceMetadata();
            report["frameIndex"]=Json::UInt64(frameIndex_);report["diagnosticMode"]=int(pbrSettings_.strandDebugMode);
            report["temporalSequence"]=Json::UInt64(textures.completedTemporalSequence);
            report["jitterUV"].append(textures.completedJitterUV.x);report["jitterUV"].append(textures.completedJitterUV.y);
            for(int column=0;column<4;++column)for(int row=0;row<4;++row){
                report["jitteredViewProjectionColumnMajor"].append(textures.completedJitteredViewProjection[column][row]);
                report["unjitteredViewProjectionColumnMajor"].append(textures.completedUnjitteredViewProjection[column][row]);
            }
            report["bodyGeometryRevision"]=Json::UInt64(resource_->GeometryRevision());
            report["lightingPreset"]=lightingPreset_;report["displayStudy"]=displayStudy_;
            report["sourceEnvironmentOverride"]=sourceEnvironmentOverride_;
            report["sourceDirectOverride"]=sourceDirectOverride_;
            if(sourceDirectOverride_&&sourceHairDirect_){
                report["sourceDirectProfile"]=Utf8(hairDirectPath_);
                report["sourceDirectParameterOrigin"]=sourceHairDirect_->parameterOrigin;
                report["sourceDirectHostUnitEV"]=sourceHairDirect_->hostUnitEV;
                report["sourceDirectCount"]=pbrSettings_.strandSourceDirect.count;
                report["sourceDirectDistanceScale"]=pbrSettings_.strandSourceDirect.distanceToSourceMetres;
                report["opaqueProxyLuminanceCoverage"]=appliedSourceDirect_.proxyLuminanceCoverage;
                for(int i:appliedSourceDirect_.proxySourceInstances)report["opaqueProxySourceInstances"].append(i);
            }
            report["postExposure"]=pbrSettings_.post.exposure;report["postGamma"]=pbrSettings_.post.gamma;
            const auto shadowSkinState=ShadowSkinCaptureState();
            for(const auto& key:shadowSkinState.getMemberNames())report[key]=shadowSkinState[key];
            report["sourceSpatialIbl"]=strandHair_.material.sourceSpatialIbl;
            report["sourceSegmentShading"]=strandHair_.material.sourceSegmentShading;
            report["sourceCurveGeometry"]=strandHair_.material.sourceCurveGeometry;
            report["hairDsVolumeMode"]=strandHair_.material.sourceDSVolume?"native":"legacy";
            report["sourceDSVolume"]=strandHair_.material.sourceDSVolume;
            report["volumeRasterTraceStep"]=strandHair_.material.volumeRasterTraceStep;
            report["volumeTraceLimit"]=strandHair_.material.volumeTraceLimit;
            report["volumeInterpolationSamples"]=strandHair_.material.volumeInterpolationSamples;
            report["volumeInjectionRate"]=strandHair_.material.volumeInjectionRate;
            report["volumeInjectionLodFraction"]=strandHair_.material.volumeInjectionLodFraction;
            report["volumeInjectionMinLodFactor"]=strandHair_.material.volumeInjectionMinLodFactor;
            report["volumeInjectionQuality"]=strandHair_.material.volumeInjectionQuality;
            report["nativeDsInputSegments"]=Json::UInt64(pbr_.Statistics().strandNativeDsInputSegments);
            report["nativeDsInjectedSegments"]=Json::UInt64(pbr_.Statistics().strandNativeDsInjectedSegments);
            report["nativeDsInjectionFraction"]=pbr_.Statistics().strandNativeDsInjectionFraction;
            report["nativeDsRasterWidthScale"]=pbr_.Statistics().strandNativeDsRasterWidthScale;
            report["nativeDsAverageWidth"]=pbr_.Statistics().strandNativeDsAverageWidth;
            report["nativeDsInjectionPolicy"]=strandHair_.material.sourceDSVolume?
                "incoming=floor(double(total)*double(float(lodFraction))); quality<2 or MinLod gates rate; count uses incoming cap and float truncation; RasterWidthScale compensation is paired with selected prefix; WidthAve stays at source header":
                "inactive: injection settings do not affect legacy density rendering";
            report["nativeDsTextureAdapter"]="Texture cubeR32/64/128,6mips,manual trilinear RGBA32F; negativeLOD clamp0; source CPU trace cap32 and injection prefix/width policy recovered; actual frame list/format remain unobserved";
            report["hairOitMode"]=HairOitMode(pbrSettings_);
            report["nativeOitLayersRequested"]=pbrSettings_.strandNativeOitLayers;
            report["nativeOitActive"]=pbr_.Statistics().strandNativeOitActive;
            report["nativeOitLayers"]=pbr_.Statistics().strandNativeOitLayers;
            report["nativeOitComputePasses"]=pbr_.Statistics().strandNativeOitComputePasses;
            report["nativeOitBytes"]=Json::UInt64(pbr_.Statistics().strandNativeOitBytes);
            if(pbr_.Statistics().strandNativeOitActive){
                // Explicit final capture only: this API stalls for GPU readback.
                // Ordinary preview frames and benchmark statistics never call it.
                const auto counters=pbr_.CaptureStrandNativeOitCounters();
                report["nativeOitCandidateReferences"]=counters[0];
                report["nativeOitFallbackTiles"]=counters[1];
                report["nativeOitMaxTileCandidates"]=counters[2];
                report["nativeOitReservedReferences"]=counters[3];
                report["nativeOitCounterScope"]="last strand object, after completed final capture";
            }
            report["nativeOitScheduling"]="GPU projection/count/prefix/scatter and deterministic pixel ownership; source Wu/MLAB arithmetic study, not native atomic contention or captured runtime branch";
            report["curveOriginalSegments"]=Json::UInt64(pbr_.Statistics().strandCurveOriginalSegments);
            report["curveVisibleSegments"]=Json::UInt64(pbr_.Statistics().strandCurveVisibleSegments);
            report["curvePieces"]=Json::UInt64(pbr_.Statistics().strandCurvePieces);
            report["curveAtlasUploads"]=pbr_.Statistics().strandCurveUploads;
            report["curveShadowAtlasUploads"]=pbr_.Statistics().strandCurveShadowUploads;
            report["curveShadowPieces"]=Json::UInt64(pbr_.Statistics().strandCurveShadowPieces);
            report["curveScheduling"]="CPU source-formula compaction; RHI GPU curve evaluation and instanced drawing";
            report["segmentShadingPasses"]=pbr_.Statistics().strandShadingPasses;
            report["segmentShadingComputePasses"]=pbr_.Statistics().strandShadingComputePasses;
            report["segmentShadingRasterPasses"]=pbr_.Statistics().strandShadingRasterPasses;
            report["segmentShadingEvaluations"]=Json::UInt64(pbr_.Statistics().strandShadingEvaluations);
            report["segmentShadingOutputPoints"]=Json::UInt64(pbr_.Statistics().strandShadingOutputPoints);
            report["volumePointIbl"]=strandHair_.material.volumePointIbl;
            report["volumeResolution"]=strandHair_.material.volumeResolution;
            report["frontDepthGroups"]=pbrSettings_.strandFrontDepthGroups;
            report["pairedTransparency"]=pbrSettings_.strandPairedLayerTransparency;
            report["producerEnabled"]=pbrSettings_.strandIndirect.enabled;
            report["producerMultiplier"]=pbrSettings_.strandIndirect.multiplier;
            report["hostUnitEV"]=pbrSettings_.strandIndirect.exposureEV;
            report["probeRotationRadians"]=pbrSettings_.strandIndirect.rotation;
            report["realtimeGiEnabled"]=pbrSettings_.lumenGi.enabled||pbrSettings_.realtimeGi.enabled||pbrSettings_.surfaceCacheGi.enabled;
            auto vec=[](const auto& v){Json::Value a(Json::arrayValue);for(int i=0;i<3;++i)a.append(v[i]);return a;};
            report["cameraPosition"]=vec(camera_.position);report["cameraFocus"]=vec(cameraFocus_);
            report["sunColor"]=vec(pbrSettings_.shadows.sunColor);report["sunDirection"]=vec(pbrSettings_.shadows.sunDirection);
            report["sunIntensity"]=pbrSettings_.shadows.sunIntensity;
            report["directSunScale"]=directSunScale_;report["directPunctualScale"]=directPunctualScale_;
            report["ambient"]=vec(pbrSettings_.lighting.ambientAndExposure);
            report["skyIntensity"]=pbrSettings_.sky.intensity;report["skyRotation"]=pbrSettings_.sky.rotation;
            report["hairLightingProfile"]=Utf8(hairLightingPath_);
            for(unsigned i=0;i<4;++i){
                Json::Value p=vec(pbrSettings_.lighting.lightPositions[i]);p.append(pbrSettings_.lighting.lightPositions[i].w);
                Json::Value c=vec(pbrSettings_.lighting.lightColors[i]);c.append(pbrSettings_.lighting.lightColors[i].w);
                report["pointPositions"].append(p);report["pointColors"].append(c);
            }
            for(const auto& c:pbrSettings_.strandIndirect.coefficients)report["indirectCoefficients"].append(vec(c));
            const auto read=[&](const char* name,auto handle,const char* meaning,const char* alpha){
                if(!handle.IsValid())return;
                std::vector<float> values;OnRender([&]{values=device_.BackendDiagnostics()->ReadTexture(handle);});
                report["textures"][name]=EditorPreview::WriteHairLinearTexture(options.hairLinearCapture,name,meaning,alpha,previewWidth_,previewHeight_,values);
            };
            read("hair",textures.strandResolvedColor,"hair-only scene-linear after transparency resolve, before TAA; no display transform","straight coverage");
            if(pbrSettings_.strandDebugMode==Render::StrandDebugMode::Beauty){
                read("hair-temporal",textures.strandTemporalColor,"hair-only scene-linear after hair TAA, before display transform","premultiplied coverage");
                read("background",textures.strandBackgroundColor,"complete scene-linear background before hair and TAA","opaque");
                read("background-temporal",textures.strandTemporalBackgroundColor,"scene-linear background after opaque TAA","opaque");
                read("scene",textures.sceneColor,"scene-linear raw hair over background, before temporal AA","opaque");
            }
            read("display",textures.displayColor,"display encoded final internal-resolution preview","opaque");
            std::ofstream file(options.hairLinearCapture/"capture.json");file<<report;Check(bool(file),"Could not save linear capture metadata");
            std::cout<<"Opt-in linear Render/RHI hair capture: "<<Utf8(options.hairLinearCapture)<<'\n';
        }
        if(!options.hairVolumeReport.empty()){
            const auto textures=pbr_.DebugTextures();
            Check(textures.strandDensity.IsValid(),"Hair volume report requires an enabled spatial volume");
            std::vector<float> density,paths,extra,ambient;
            OnRender([&]{
                auto* diagnostics=device_.BackendDiagnostics();
                density=diagnostics->ReadTexture(textures.strandDensity);paths=diagnostics->ReadTexture(textures.strandPaths);
                extra=diagnostics->ReadTexture(textures.strandPathsExtra);ambient=diagnostics->ReadTexture(textures.strandPathsAmbient);
            });
            Check(!density.empty()&&paths.size()==density.size()&&extra.size()==density.size()&&ambient.size()==density.size(),"Hair volume diagnostic readback mismatch");
            std::ofstream report(options.hairVolumeReport);Check(bool(report),"Could not create hair volume report");
            const bool sourceIbl=strandHair_.material.dualScatteringIbl&&strandHair_.material.volumeSkyTransmission;
            report<<"atlas_texel,density_per_m,sun,point0,point1,point2,point3,";
            report<<(sourceIbl?"reserved_x,reserved_y,reserved_z,":"positive_x,positive_y,positive_z,");
            report<<(sourceIbl?"ibl_plus,ibl_minus,reserved\n":"negative_x,negative_y,negative_z\n");
            for(std::size_t i=0;i<density.size();i+=4)if(density[i]>0){
                report<<i/4<<','<<density[i];
                for(unsigned c=0;c<4;++c)report<<','<<paths[i+c];
                for(unsigned c=0;c<4;++c)report<<','<<extra[i+c];
                for(unsigned c=0;c<3;++c)report<<','<<ambient[i+c];
                report<<'\n';
            }
            Check(bool(report),"Could not write hair volume report");
            std::cout<<"Opt-in RHI volume readback: "<<Utf8(options.hairVolumeReport)<<" | atlas="<<textures.strandVolumeWidth<<'x'<<textures.strandVolumeHeight<<'\n';
        }
        if(options.asyncClothTest){
            Check(clothWorker_&&modular_&&clothSnapshot_,"Asynchronous cloth produced no completed snapshot");
            Check(asyncSnapshotsObserved_>=10&&asyncTotalSteps_>=30,"Asynchronous cloth did not advance while rendering");
            Check(asyncMaximumCollapsedTriangles_==0,"Asynchronous cloth collapsed a simulation triangle");
            Check(asyncMaximumResolvableSurfacePenetration_<.008,"Asynchronous cloth resolvable surface penetration exceeded 8 mm");
            Check(asyncMaximumAttachmentError_<1e-7,"Asynchronous cloth fixed attachments detached from the completed body pose");
            Check(clothSnapshot_->pose.global.size()==asset_->skeleton.bones.size(),"Completed body/cloth pose has a mismatched skeleton");
            std::cout<<"Async cloth preview passed; completed snapshots="<<asyncSnapshotsObserved_<<", fixed steps="<<asyncTotalSteps_<<", max resolvable/raw surface penetration="<<asyncMaximumResolvableSurfacePenetration_*1000<<" / "<<asyncMaximumSurfacePenetration_*1000<<" mm\n";
        }
        if(options.smoke){
            Check(frameIndex_>=3,"Editor smoke ended too early");
            const bool heldFromStart=(options.clothTest||options.hairTest)&&options.holdAt>=0&&options.startTime>=options.holdAt;
            if(options.dianaTest){
                OnRender([this]{Check(device_.BackendDiagnostics()->ValidationErrorCount()==0,"Diana preview reported RHI texture validation errors");});
                Check(characterMaterials_.has_value()&&characterMaterialReport_.matchedMaterials==17,"Diana material profile was not applied");
                Check(characterMaterialReport_.textureBindings>=79,"Diana enhanced material bindings are missing");
                Check(!pbrSettings_.lumenGi.enabled&&!pbrSettings_.realtimeGi.enabled&&!pbrSettings_.surfaceCacheGi.enabled,"Diana preview unexpectedly enabled GI");
                Check(strandHair_.loaded,"Diana strand resource missing");
                if(pbrSettings_.skinDiffusion.enabled)Check(pbr_.Statistics().skinDiffusionPasses==6&&pbr_.Statistics().skinDiffusionDraws>=3,"Diana spatial skin diffusion was not recorded");
                if(pbrSettings_.eyeShell.enabled)Check(pbr_.Statistics().eyeShellPasses>0&&pbr_.Statistics().eyeShellDraws>0,"Diana eye-shell postprocess was not recorded");
                if(strandHair_.visible&&strandHair_.material.spatialVolume)Check(pbr_.Statistics().strandVolumeResolution>0&&pbr_.Statistics().strandVolumeVoxelSize>0,"Diana spatial hair volume was not built");
                if(strandHair_.visible&&pbrSettings_.strandTemporalDepth&&pbrSettings_.strandMotionVectors&&pbrSettings_.temporal.antialiasing==Render::AntialiasingMode::TAA&&pbrSettings_.temporal.msaaSamples==1)
                    Check(pbr_.Statistics().strandMotionActive&&pbr_.Statistics().strandHairTemporalPasses>0,"Diana separate hair motion/history was not recorded");
                if(options.hairDynamic)Check(strandHair_.simulation.Stats().fixedSteps>0,"Requested dynamic Diana hair simulation did not advance");
                if(options.hairShutter&&strandHair_.visible&&pbrSettings_.strandTemporalDepth&&pbrSettings_.strandMotionVectors){
                    if(options.hairOrbitStudy||options.hairDynamic)Check(pbr_.Statistics().strandShutterSamples==4,"Requested moving geometry hair shutter was not recorded");
                    else Check(pbr_.Statistics().strandShutterSamples==0&&pbr_.Statistics().strandHairHistoryUsed,"Static hair shutter did not preserve sharp TAA without extra geometry samples");
                }
                std::cout<<"Diana hair reconstruction: dslut="<<NativeHairMomentStatus(strandHair_.material)<<", volume-filter="<<(strandHair_.material.volumeHardwareFiltering?"linear":"legacy")
                    <<", motion="<<pbr_.Statistics().strandMotionActive<<", motion passes="<<pbr_.Statistics().strandMotionPasses<<", hair temporal passes="<<pbr_.Statistics().strandHairTemporalPasses
                    <<", hair history="<<pbr_.Statistics().strandHairHistoryUsed<<", simulation steps="<<strandHair_.simulation.Stats().fixedSteps<<'\n';
                std::cout<<"Hair quality13: paired="<<pbr_.Statistics().strandPairedTransparencyActive<<", raster draws="<<pbr_.Statistics().strandTransparencyRasterPasses
                    <<", exact volume="<<strandHair_.material.volumeExactIntegration<<", head proxies="<<strandHair_.material.volumeProxyBlockers
                    <<", adaptive history="<<pbrSettings_.temporal.hairCoverageHistoryRejection<<", shutter samples="<<pbr_.Statistics().strandShutterSamples<<'\n';
                std::cout<<"Hair transport: stable opaque="<<strandHair_.material.volumeStableOpaqueCache<<", point IBL="<<strandHair_.material.volumePointIbl
                    <<", diagnostic="<<std::uint32_t(pbrSettings_.strandDebugMode)<<", volume passes="<<pbr_.Statistics().strandVolumePasses
                    <<", blocker passes="<<pbr_.Statistics().strandBlockerPasses<<'\n';
                std::cout<<"Hair internal shadow: near-field="<<strandHair_.material.volumePreserveNearField<<", trace exclusion="
                    <<pbr_.Statistics().strandVolumeBiasMetres*1000.f/std::max(pbrSettings_.skinDiffusion.worldUnitScale,1e-6f)<<" mm; density accumulation RGBA32F\n";
                std::cout<<"Hair direction/incident: tangents="<<(strandHair_.simulation.Settings().reconstructRenderTangents?"actual-render":"guide")
                    <<", IBL="<<(strandHair_.material.sourceSpatialIbl?"spatial-center-estimate":"angular-adapter")<<'\n';
                std::cout<<"Diana PBR preview passed; meshes="<<resource_->Source().meshes.size()<<", triangles="<<resource_->Source().TriangleCount()<<", matched materials="<<characterMaterialReport_.matchedMaterials<<", texture bindings="<<characterMaterialReport_.textureBindings<<", SSS passes="<<pbr_.Statistics().skinDiffusionPasses<<", eye-shell passes="<<pbr_.Statistics().eyeShellPasses<<", volume="<<pbr_.Statistics().strandVolumeResolution<<"^3 ("<<pbr_.Statistics().strandVolumeVoxelSize*1000.f/std::max(pbrSettings_.skinDiffusion.worldUnitScale,1e-6f)<<" mm), physical fibers="<<strandHair_.material.physicalScattering<<", multiple scattering="<<strandHair_.material.multipleScattering<<", coverage="<<(strandHair_.material.areaPreservingCoverage?"area":"legacy")<<", transparency="<<(pbr_.Statistics().strandNativeOitActive?HairOitMode(pbrSettings_):(pbr_.Statistics().strandFrontLayerTransparencyActive?"depth-groups":"weighted"))<<", groups="<<pbr_.Statistics().strandFrontDepthGroups<<", hair depth="<<pbr_.Statistics().strandTemporalDepthActive<<", hair shadow draws="<<pbr_.Statistics().strandShadowDraws<<", strand passes="<<pbr_.Statistics().strandPasses<<", Render RHI, RT/realtime GI disabled\n";
            }
            else if(options.strandTest){const auto& s=strandHair_.simulation.Stats();Check(s.gpuReady&&s.fixedSteps>=30,"Vulkan strand simulation did not advance");Check(s.nonFiniteNodes==0&&s.maxRootError<1e-5,"Invalid strand state or detached roots");Check(smokeChangedPixels>16,"Vulkan hair did not change the Model viewport");std::cout<<"Model Vulkan strand preview passed; steps="<<s.fixedSteps<<", displacement="<<s.maxGuideDisplacement<<", changed pixels="<<smokeChangedPixels<<", submit="<<s.submitMilliseconds<<" ms, readback="<<s.readbackMilliseconds<<" ms\n";}
            else if(!heldFromStart){Check(smokeMotion>1e-6,"Generated animation did not deform preview vertices");Check(smokeChangedPixels>16,"Animated geometry did not change the rendered preview");}
            if(!options.strandTest&&!options.dianaTest)std::cout<<"Editor GPU smoke passed; maximum animated vertex displacement="<<smokeMotion<<", changed viewport pixels="<<smokeChangedPixels<<"\n";
        }
        if(options.clothTest){Check(clothSimulator_.Stats().steps>=30,"Cloth did not advance fixed simulation steps");Check(clothSimulator_.Stats().pinned>0,"Cloth has no pinned attachments");Check(maxPhysicalDifference>1e-5,"Cloth output is identical to animated skinning");std::cout<<"Cloth GPU smoke passed; physical-vs-skinned displacement="<<maxPhysicalDifference<<", max contacts="<<maxClothContacts<<", solver steps="<<clothSimulator_.Stats().steps<<"\n";}
        if(options.clothTest&&!options.equipTest)Check(clothSimulator_.Stats().resets==1,"Continuous cloth preview unexpectedly reset its inertia");
        if(options.clothTest&&options.holdAt>=0){Check(!heldCloth.empty()&&clothSimulator_.Stats().steps>heldStep+15,"Pausing animation froze the cloth simulation");Check(heldMovement>modular_->cloth.unitsPerMeter*.001,"Cloth had no residual settling motion after animation stopped");std::cout<<"Animation held; cloth continued settling by "<<heldMovement/modular_->cloth.unitsPerMeter<<" m without resetting\n";}
        if(options.hairTest){const auto& stats=HairStats();Check(stats.steps>=30,"Hair simulation did not advance fixed ticks");Check(stats.surfaceVertices>0,"Hair mesh contact layer did not initialize");Check(stats.maxRootError<1e-6,"Hair roots detached from the animated head");Check(smokeHairMaxPenetration<.005,"Hair corrected mesh contact residual exceeds 5 mm");Check(smokeHairMaxAttachmentDrift<=.001501,"Authored scalp attachment drift exceeds 1.501 mm");std::cout<<"Hair GPU smoke passed; chains="<<stats.chains<<", bones="<<stats.bones<<", maximum contact residual="<<smokeHairMaxPenetration*1000<<" mm, attachment drift="<<smokeHairMaxAttachmentDrift*1000<<" mm, solver="<<stats.simulationMilliseconds<<" ms\n";}
    }
};
int Run(const std::vector<std::string>& args) {
    auto options=Parse(args);ScopedPreviewQos previewQos;options.previewQos=previewQos.Status();
    Check(glfwInit()==GLFW_TRUE,"GLFW initialization failed");
    struct GlfwEnd{~GlfwEnd(){glfwTerminate();}} cleanup;
    glfwWindowHint(GLFW_VISIBLE,options.smoke?GLFW_FALSE:GLFW_TRUE);
    ApplicationWindow::Window window({1600,1000,"Skeletal Asset Editor | Mesh / Skeleton / Animation / IK / Retarget",!options.noVsync});
    Check(window.Activate(),"Could not create asset editor window");auto context=window.GetRenderContextAsOpengl();Check(context.has_value(),"No OpenGL editor context");
    Editor editor(window.GetNativeWindow(),*context,options);editor.Run(options);return 0;
}
} // namespace
#ifdef _WIN32
int wmain(int argc,wchar_t** argv){try{std::vector<std::string> args;for(int i=0;i<argc;++i)args.push_back(Utf8(fs::path(argv[i])));return Run(args);}catch(const std::exception& e){std::cerr<<"Asset editor failed: "<<e.what()<<'\n';return 1;}}
#else
int main(int argc,char** argv){try{return Run(std::vector<std::string>(argv,argv+argc));}catch(const std::exception& e){std::cerr<<"Asset editor failed: "<<e.what()<<'\n';return 1;}}
#endif
