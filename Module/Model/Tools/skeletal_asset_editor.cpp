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
#endif
#include <glad/glad.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "ApplicationWindow/public/window.h"
#include "Model/Public/model_module.h"
#include "Model/Public/skeletal_asset.h"
#include "Model/Public/vmd_animation.h"
#include "Model/Public/cloth_asset.h"
#include "Model/Public/hair_asset.h"
#include "Model/Private/cloth_preview_worker.h"
#include "Model/Private/preview_animation_clock.h"
#include "Model/Private/garment_recognition.h"
#include "Render/Private/rhi_device.h"
#include "Render/Public/Pipeline/toon_render_pipeline.h"
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
    fs::path benchmarkOutput;
    std::string previewQos="unavailable";
    std::vector<fs::path> animations, vmds;
    bool smoke=false, motionTest=false,clothTest=false,equipTest=false,cleanClothView=false,meshRegions=false;
    bool benchmark=false,noCloth=false,noHair=false,hairTest=false,noVsync=false,vsyncExplicit=false,syncCloth=false,asyncClothTest=false;
    double startTime=0;
    double holdAt=-1,yaw=.12,distance=4.7;
    double maxFps=-1; // Automatic: interactive 60; benchmark/smoke uncapped.
    std::uint32_t captureEvery=6;
    std::string curveBone,focusBone;
    int regionCategory=-1;
    std::uint32_t frames=0;
    std::uint32_t benchmarkWarmup=30;
};
Options Parse(const std::vector<std::string>& args) {
    Options result;
    for (std::size_t i=1;i<args.size();++i) {
        const auto& a=args[i];
        if (a=="--smoke-test") result.smoke=true;
        else if (a=="--motion-test") {result.smoke=true;result.motionTest=true;}
        else if (a=="--cloth-test") {result.smoke=true;result.motionTest=true;result.clothTest=true;}
        else if (a=="--cloth-equip-test") {result.smoke=true;result.motionTest=true;result.clothTest=true;result.equipTest=true;}
        else if (a=="--clean-cloth-view")result.cleanClothView=true;
        else if (a=="--mesh-regions")result.meshRegions=true;
        else if (a=="--benchmark")result.benchmark=true;
        else if (a=="--no-cloth")result.noCloth=true;
        else if (a=="--no-hair")result.noHair=true;
        else if (a=="--hair-test"){result.smoke=true;result.motionTest=true;result.hairTest=true;}
        else if (a=="--no-vsync"){result.noVsync=true;result.vsyncExplicit=true;}
        else if (a=="--vsync"){result.noVsync=false;result.vsyncExplicit=true;}
        else if (a=="--sync-cloth")result.syncCloth=true;
        else if (a=="--async-cloth-test"){result.asyncClothTest=true;result.benchmark=true;}
        else if (a=="--source"||a=="--package"||a=="--screenshot"||a=="--frames"||a=="--retarget-source"||a=="--animation"||a=="--vmd"||a=="--time"||a=="--curve-bone"||a=="--focus-bone"||a=="--character"||a=="--capture-dir"||a=="--capture-every"||a=="--cloth-hold-at"||a=="--yaw"||a=="--distance"||a=="--benchmark-frames"||a=="--benchmark-warmup"||a=="--benchmark-output"||a=="--max-fps"||a=="--hair"||a=="--region-category") {
            Check(++i<args.size(),"Missing value for "+a);
            if(a=="--source")result.source=Path(args[i]);
            else if(a=="--package")result.package=Path(args[i]);
            else if(a=="--screenshot")result.screenshot=Path(args[i]);
            else if(a=="--retarget-source")result.retargetSource=Path(args[i]);
            else if(a=="--character")result.character=Path(args[i]);
            else if(a=="--hair")result.hair=Path(args[i]);
            else if(a=="--capture-dir")result.captureDirectory=Path(args[i]);
            else if(a=="--benchmark-output")result.benchmarkOutput=Path(args[i]);
            else if(a=="--animation")result.animations.push_back(Path(args[i]));
            else if(a=="--vmd")result.vmds.push_back(Path(args[i]));
            else if(a=="--curve-bone")result.curveBone=args[i];
            else if(a=="--focus-bone")result.focusBone=args[i];
            else if(a=="--region-category") {
                auto category=args[i];std::transform(category.begin(),category.end(),category.begin(),[](unsigned char c){return char(std::tolower(c));});
                const std::array<std::string,6> names{"all","unknown","body","hair","garment","accessory"};
                const auto found=std::find(names.begin(),names.end(),category);
                Check(found!=names.end(),"Invalid region category (use all, unknown, body, hair, garment or accessory)");
                result.regionCategory=int(found-names.begin())-1;result.meshRegions=true;
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
    if(result.source.empty()&&result.package.empty()&&result.character.empty()) {
        auto outfit=Path(MODEL_SOURCE_DIR)/"Asset/Generated/MahiroCloth/Character.asset.json";
        auto generated=Path(MODEL_SOURCE_DIR)/"Asset/Generated/MahiroMaid/Package.asset.json";
        if(fs::exists(outfit))result.character=outfit;
        else if(fs::exists(generated))result.package=generated;
        else result.source=Path(MODEL_SOURCE_DIR)/"Asset/Model/Rikka_Uniform/Rikka_Uniform.pmx";
    }
    Check(int(!result.source.empty())+int(!result.package.empty())+int(!result.character.empty())==1,"Choose --source, --package or --character");
    if(result.smoke&&!result.frames)result.frames=result.clothTest||result.hairTest?180:7;
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
enum class Selection { Mesh, Skeleton, Animation, Ik, Morphs, Retarget, Diagnostics, Cloth, Hair, MeshRegions };
class Editor {
    Render::RHIDevice device_;
    ModelModule models_{device_};
    Render::ToonRenderPipeline toon_{device_};
    std::shared_ptr<ModelResource> resource_;
    std::shared_ptr<ModelResource> garmentResource_;
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
    float yaw_=0.12f,pitch_=0.05f,distance_=4.7f;
    glm::vec3 cameraFocus_{0};
    double time_=0;
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
        auto uploaded=models_.Upload(candidate->geometry);
        Check(bool(uploaded),models_.LastError());
        garmentResource_.reset();modular_.reset();garmentAnimated_={};garmentDeformed_={};clothReset_=true;previousClothTime_=-1;
        hairAsset_.reset();hairSimulator_=HairSimulator{};hairPose_={};hairPath_.clear();hairReset_=true;hairSettingsPending_=false;previousHairTime_=-1;uploadedBodyPoseValid_=false;
        ClearClothPreview();
        resource_=std::move(uploaded);asset_=std::move(candidate);openedPath_=path;trackedCameraBone_=-1;
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
        // A Blender source with no authored clip opens as a static asset
        // inspection. Do not skin/upload a high-resolution mesh continuously
        // just to play the generated diagnostic sequence.
        if(asset_->sourceFormat=="blend"&&std::all_of(asset_->animations.begin(),asset_->animations.end(),[](const auto& clip){return clip.generated;})) {
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
    void RenderPreview(std::uint32_t width,std::uint32_t height) {
        Check(toon_.Resize(width,height,1024,4),toon_.LastError());
        if(trackedCameraBone_>=0&&std::size_t(trackedCameraBone_)<DisplayPose().global.size())cameraFocus_=glm::vec3(placement_*glm::dvec4(glm::dvec3(DisplayPose().global[trackedCameraBone_][3]),1));
        camera_.position=cameraFocus_+glm::vec3(distance_*std::cos(pitch_)*std::sin(yaw_),distance_*std::sin(pitch_),distance_*std::cos(pitch_)*std::cos(yaw_));
        camera_.view=glm::lookAt(camera_.position,cameraFocus_,glm::vec3(0,1,0));
        camera_.nearPlane=.03f;camera_.farPlane=100;
        camera_.projection=glm::perspective(glm::radians(43.f),float(width)/height,camera_.nearPlane,camera_.farPlane);
        Render::RenderFrame frame;frame.frameIndex=++frameIndex_;
        std::string error;if(showBody_||!modular_)AppendPreviewBody(frame,error);
        if(modular_&&showGarment_&&garmentResource_&&!RegionPreviewFilter())Check(garmentResource_->AppendTo(frame,placement_,glm::dvec3(0),camera_.view,&error),error);
        Render::RHICommand::BeginFrame begin;begin.frameIndex=frame.frameIndex;begin.framebufferWidth=width;begin.framebufferHeight=height;
        auto encoder=device_.BeginFrame(begin);
        if(!resource_->KeepAlive(encoder)||(garmentResource_&&!garmentResource_->KeepAlive(encoder))||!toon_.Record(encoder,frame,camera_,settings_)||!encoder.End(false)) {
            encoder.Cancel();if(auto token=toon_.RecordedFrameToken())toon_.DiscardFrame(token);
            throw std::runtime_error(toon_.LastError());
        }
        const auto token=toon_.RecordedFrameToken();
        // The UI backend and GPU timer share this synchronization point. There
        // is no framebuffer readback in the ordinary interactive preview.
        OnRender([this]{ImGui_ImplOpenGL3_NewFrame();BeginGpuTimer();});
        auto done=std::make_shared<bool>(false);
        Check(device_.async_SubmitFrameCommands(encoder.GetCommandBuffer(),[done]{*done=true;}),"Could not submit editor preview frame");
        auto deadline=std::chrono::steady_clock::now()+30s;
        const auto waitStarted=PerfClock::now();
        while(!*done){device_.returnSystem.DrainCallbacks();Check(std::chrono::steady_clock::now()<deadline,"Preview frame timed out");if(!*done)device_.returnSystem.WaitForCallbacks(2ms);}
        performance_.sync+=Milliseconds(PerfClock::now()-waitStarted);
        Check(toon_.CompleteFrame(token),toon_.LastError());
        auto handle=toon_.DebugTextures().sceneColor;
        OnRender([this,handle]{EndGpuTimer();auto texture=device_.GetResourcePool().TextureTable.Get(handle);Check(texture!=nullptr,"Missing preview texture");previewTexture_=texture->rhi_id;});
        previewWidth_=width;previewHeight_=height;
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
        ImGui::BeginChild("region-sections",ImVec2(0,190),ImGuiChildFlags_Borders);
        for(const auto& region:asset_->meshRegions) {
            if(region.mesh>=asset_->geometry->meshes.size())continue;
            if(regionCategoryFilter_>=0&&int(region.kind)!=regionCategoryFilter_)continue;
            const auto& mesh=asset_->geometry->meshes[region.mesh];
            const auto& material=region.sourceMaterial;
            const auto name=material.empty()?mesh.name:material;
            if(regionFilter_[0]&&name.find(regionFilter_.data())==std::string::npos&&
                mesh.name.find(regionFilter_.data())==std::string::npos&&region.sourceObject.find(regionFilter_.data())==std::string::npos)continue;
            ImGui::PushID(int(region.mesh));
            const auto label="#"+std::to_string(region.mesh)+" "+name;
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
    void DetailsPanel() {
        ImGui::TextColored(ImVec4(.3f,.75f,1,1),"DETAILS / INSPECTOR");
        ImGui::BeginChild("inspector-scroll",ImVec2(0,0));
        switch(selection_) {
            case Selection::Mesh:MeshDetails();break;
            case Selection::Skeleton:SkeletonDetails();break;
            case Selection::Morphs:MorphDetails();break;
            case Selection::Animation:AnimationDetails();break;
            case Selection::Ik:IkDetails();break;
            case Selection::Retarget:RetargetDetails();break;
            case Selection::Cloth:ClothDetails();break;
            case Selection::Hair:HairDetails();break;
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
        ImGui::SameLine();if(ImGui::Button("Frame model")){cameraFocus_={0,0,0};distance_=4.7f;}
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
        if(displayPerformance_.frame>0) {
            ImGui::Text("Toon | %.1f FPS | frame %.2f ms | %s %.2f | skin %.2f | upload %.2f ms",1000./displayPerformance_.frame,displayPerformance_.frame,clothWorker_?"cloth submit":"cloth CPU",displayPerformance_.cloth,displayPerformance_.skin,displayPerformance_.upload);
            if(ImGui::IsItemHovered())ImGui::SetTooltip("Pose %.2f ms | cloth apply %.2f ms\nRender %.2f | UI %.2f | swap %.2f | render sync %.2f ms\nGPU scene span %.2f ms | %llu fixed steps in last frame\nRender sync overlaps render/UI/swap timings; upload includes its own wait.\nGPU results are polled without blocking; scene span includes dispatch gaps.\nOrbit: RMB / zoom: wheel",displayPerformance_.pose,displayPerformance_.apply,displayPerformance_.render,displayPerformance_.ui,displayPerformance_.swap,displayPerformance_.sync,std::max(0.,displayPerformance_.gpu),static_cast<unsigned long long>(displayPerformance_.steps));
        } else ImGui::Text("Toon viewport / CPU skinning | %zu bones | orbit: RMB / zoom: wheel",asset_->skeleton.bones.size());
        viewportSize_=ImGui::GetContentRegionAvail();viewportSize_.y=std::max(120.f,viewportSize_.y-118);viewportOrigin_=ImGui::GetCursorScreenPos();
        ImGui::Image(ImTextureID(previewTexture_),viewportSize_,ImVec2(0,1),ImVec2(1,0));
        if(ImGui::IsItemHovered()) {
            auto& io=ImGui::GetIO();
            if(ImGui::IsMouseDragging(ImGuiMouseButton_Right)){yaw_-=io.MouseDelta.x*.006f;pitch_=std::clamp(pitch_+io.MouseDelta.y*.006f,-1.3f,1.3f);}
            distance_=std::clamp(distance_*std::exp(-io.MouseWheel*.12f),1.f,20.f);
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
        output<<"  \"process_id\": "<<processId<<",\n  \"executable\": "<<JsonString(executable)<<",\n  \"logical_processors\": "<<logicalProcessors<<",\n  \"cpu_time_available\": "<<(cpuSamples?"true":"false")<<",\n  \"cpu_time_samples\": "<<cpuSamples<<",\n  \"cpu_accounting_wall_seconds\": "<<cpuWall<<",\n  \"process_user_cpu_seconds\": "<<processUser<<",\n  \"process_kernel_cpu_seconds\": "<<processKernel<<",\n  \"process_cpu_seconds\": "<<processUser+processKernel<<",\n  \"main_thread_user_cpu_seconds\": "<<mainUser<<",\n  \"main_thread_kernel_cpu_seconds\": "<<mainKernel<<",\n  \"main_thread_cpu_seconds\": "<<mainUser+mainKernel<<",\n  \"main_thread_one_core_percent\": "<<mainOneCorePercent<<",\n  \"process_one_core_percent\": "<<processOneCorePercent<<",\n  \"process_machine_percent\": "<<processMachinePercent<<",\n";
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
        device_.Run(Render::BackendType::Opengl,context_);Check(models_.Startup(),models_.LastError());
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
        yaw_=float(options.yaw);distance_=float(options.distance);
        if(!options.focusBone.empty()){for(std::size_t i=0;i<asset_->skeleton.bones.size();++i)if(asset_->skeleton.bones[i].name==options.focusBone||asset_->skeleton.bones[i].englishName==options.focusBone){trackedCameraBone_=int(i);break;}Check(trackedCameraBone_>=0,"Camera focus bone was not found");}
        Check(toon_.Initialize(),toon_.LastError());settings_.backgroundColor=glm::vec4(.95f,.96f,.98f,1);settings_.groundHeight=-1.5f;
        IMGUI_CHECKVERSION();ImGui::CreateContext();guiReady_=true;
        auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
        RebuildFonts();
        ImGui::StyleColorsDark();auto& style=ImGui::GetStyle();style.WindowRounding=0;style.ChildRounding=5;style.FrameRounding=3;style.WindowPadding=ImVec2(12,10);
        style.Colors[ImGuiCol_WindowBg]=ImVec4(.07f,.085f,.11f,1);style.Colors[ImGuiCol_ChildBg]=ImVec4(.09f,.108f,.14f,1);
        Check(ImGui_ImplGlfw_InitForOpenGL(window_,true),"Could not initialize editor input backend");
        OnRender([this]{Check(ImGui_ImplOpenGL3_Init("#version 450 core"),"Could not initialize editor OpenGL UI");rendererReady_=true;Check(ImGui_ImplOpenGL3_CreateDeviceObjects(),"Could not create editor UI resources");for(auto& timer:gpuTimers_)glGenQueries(GLsizei(timer.query.size()),timer.query.data());if(const auto value=glGetString(GL_VENDOR))gpuVendor_=reinterpret_cast<const char*>(value);if(const auto value=glGetString(GL_RENDERER))gpuRenderer_=reinterpret_cast<const char*>(value);});
        std::cout<<"Skeletal asset editor: "<<asset_->name<<" | "<<asset_->skeleton.bones.size()<<" bones | "<<asset_->morphs.size()<<" morphs | "<<asset_->animations.size()<<" sequences | "<<asset_->ikRig.chains.size()<<" IK chains\n";
    }
    ~Editor() {
        clothWorker_.reset();
        if(rendererReady_)OnRender([this]{for(auto& timer:gpuTimers_)glDeleteQueries(GLsizei(timer.query.size()),timer.query.data());ImGui_ImplOpenGL3_Shutdown();});
        if(guiReady_){ImGui_ImplGlfw_Shutdown();ImGui::DestroyContext();}
        garmentResource_.reset();resource_.reset();toon_.Shutdown();models_.Shutdown();device_.StopAndRelease();
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
            if(options.smoke)delta=options.clothTest||options.hairTest?1.0/60.0:.2;
            performance_.acceptedDelta=delta;
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
            if(options.smoke){
                if(frame==0)for(const auto& mesh:deformed_.meshes)for(const auto& v:mesh.vertices)baseline.push_back(v.position);
                else {std::size_t i=0;for(const auto& mesh:deformed_.meshes)for(const auto& v:mesh.vertices)smokeMotion=std::max(smokeMotion,glm::length(v.position-baseline[i++]));}
                if(!options.motionTest&&frame==3){if(!options.meshRegions)selection_=Selection::Morphs;for(std::size_t i=0;i<asset_->morphs.size();++i)if(std::any_of(asset_->morphs[i].deltas.begin(),asset_->morphs[i].deltas.end(),[](const auto& delta){return glm::length2(delta.position)>1e-12;})){manualMorphs_[i]=.5f;morphOverrides_[i]=1;dirty_=true;break;}}
                if(!options.motionTest&&frame==4){for(std::size_t i=0;i<asset_->ikRig.chains.size();++i)if(!asset_->ikRig.chains[i].native&&asset_->ikRig.chains[i].name=="leftArm"){selectedChain_=int(i);ResetIkTarget();ikTarget_.y+=float(asset_->geometry->bounds.Extent().y*.025);enableIk_=true;dirty_=true;break;}}
                if(!options.motionTest&&!options.meshRegions&&frame==5){selection_=retargetSource_?Selection::Retarget:Selection::Ik;}
            }
            const auto scale=ImGui::GetIO().DisplayFramebufferScale;
            const auto pw=std::uint32_t(std::clamp(viewportSize_.x*std::max(1.f,scale.x),128.f,2048.f));
            const auto ph=std::uint32_t(std::clamp(viewportSize_.y*std::max(1.f,scale.y),128.f,2048.f));
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
        if(options.benchmark)ReportBenchmark(options,benchmarkSamples,benchmarkFirstMeasuredFrame,options.maxFps>0?frameWaiter.Backend():"unused (uncapped)");
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
            if(!heldFromStart){Check(smokeMotion>1e-6,"Generated animation did not deform preview vertices");Check(smokeChangedPixels>16,"Animated geometry did not change the rendered preview");}
            std::cout<<"Editor GPU smoke passed; maximum animated vertex displacement="<<smokeMotion<<", changed viewport pixels="<<smokeChangedPixels<<"\n";
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
