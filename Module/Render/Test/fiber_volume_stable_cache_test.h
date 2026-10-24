#pragma once
#include "Render/Private/Pipeline/fiber_volume.h"
#include <array>
#include <chrono>
#include <cmath>
#include <future>
#include <iostream>
#include <limits>

namespace Render::TestData {
namespace FiberStableCacheAuditDetail {
inline void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
template<class F> auto OnDevice(RHIDevice& device,F operation) {
    using T=std::invoke_result_t<F>;auto promise=std::make_shared<std::promise<T>>();auto result=promise->get_future();
    device.async_ExecuteCode([promise,operation=std::move(operation)]()mutable{
        try{if constexpr(std::is_void_v<T>){operation();promise->set_value();}else promise->set_value(operation());}
        catch(...){promise->set_exception(std::current_exception());}
    });return result.get();
}
inline void Submit(RHIDevice& device,RHIFrameEncoder& encoder) {
    Check(encoder.End(false),"End stable blocker-cache oracle");auto done=std::make_shared<bool>(false);
    Check(device.async_SubmitFrameCommands(encoder.GetCommandBuffer(),[done]{*done=true;}),"Submit stable blocker-cache oracle");
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
    while(!*done){device.returnSystem.DrainCallbacks();Check(std::chrono::steady_clock::now()<deadline,"Stable blocker-cache oracle timeout");
        if(!*done)device.returnSystem.WaitForCallbacks(std::chrono::milliseconds(2));}
}
inline std::vector<float> Read(RHIDevice& device,RenderResourceHandle<RHITextureSpec> image) {
    return OnDevice(device,[&device,image]{return device.BackendDiagnostics()->ReadTexture(image);});
}
} // namespace FiberStableCacheAuditDetail

// A static world depth discontinuity is actually rasterized at each jitter
// phase. The retained shell is compared with the first ordinary jittered-depth
// capture, not with a fabricated unjittered depth buffer. Later mode-off frames
// deliberately expose phase aliasing at that discontinuity.
inline void RunFiberVolumeStableCacheGpuOracle(RHIDevice& device) {
    using namespace FiberStableCacheAuditDetail;
    Check(!StrandMaterial{}.volumeStableOpaqueCache,"Generic stable blocker cache must default off");
    PipelineDetail::ResourceBuilder resources(device);
    const auto depth=resources.Texture(16,16,RHITextureFormat::Depth32F);
    const auto resizedDepth=resources.Texture(32,32,RHITextureFormat::Depth32F);
    const auto depthTarget=resources.Target({},depth),resizedDepthTarget=resources.Target({},resizedDepth);
    const auto parameters=resources.Uniform(sizeof(glm::vec4));
    const auto quad=resources.FullscreenMesh();
    const auto depthPipeline=resources.FullscreenPipeline(FullscreenVertexShader(),R"GLSL(#version 450 core
layout(std140,binding=8) uniform DepthEdgeAudit {vec4 query;};
void main(){float worldX=gl_FragCoord.x/query.y*2.0-1.0-query.x;
    gl_FragDepth=(worldX<0.0?.42:.58)+query.z;}
)GLSL",true);
    const auto probe=resources.Texture(1,1,RHITextureFormat::RGBA32F);
    const auto probeTarget=resources.Target(probe,{});
    const auto probePipeline=resources.FullscreenPipeline(FullscreenVertexShader(),
        std::string("#version 450 core\nlayout(binding=4) uniform sampler2D auditDensity;\nlayout(binding=5) uniform sampler2D auditMask;\nlayout(location=0) out vec4 result;\n")+
        PipelineDetail::FiberVolumeBlockGLSL()+
        "void main(){result=vec4(FiberVolumeSample(auditDensity,vec3(0)).r,FiberVolumeSample(auditMask,vec3(-.15,0,-.12)).r,FiberOpaqueDirectVisibility(vec3(-.15,0,-.12)).r,1);}\n");
    const auto owner=resources.Finish();
    StrandMesh mesh(device);std::array<StrandVertex,4> vertices{{
        {{-.4f,-.4f,-.4f,.00001f},{1,1,1,0}},{{.4f,.4f,.4f,.00001f},{1,1,1,1}},
        {{-.4f,.4f,-.4f,.00001f},{1,-1,1,0}},{{.4f,-.4f,.4f,.00001f},{1,-1,1,1}}}};
    const std::array<std::uint32_t,4> lines{0,1,2,3};Check(mesh.Update(vertices,lines),"Create stable-cache test groom");
    StrandRenderItem item;item.geometry=mesh.Snapshot();auto& material=item.material;
    material.spatialVolume=true;material.volumeResolution=16;material.volumeVoxelSize=.05f;
    material.volumeExactIntegration=true;material.volumeOpaqueBlockers=true;material.volumeDepthThreshold=.09f;
    material.volumeProxyBlockers=true;material.volumeSkyTransmission=true;
    const std::vector<StrandOpaqueCapsule> capsules{{{-.25f,.25f,0,.06f},{-.25f,.25f,0,.06f}}};
    item.opaqueCapsules=std::make_shared<const std::vector<StrandOpaqueCapsule>>(capsules);
    PipelineDetail::StrandPassView view;view.opaqueDepth=depth;view.opaqueContentRevision=1;view.sunIntensity=1;
    view.sunDirection={-1,0,0};view.cameraPosition={0,0,2};view.lighting.lightColors.fill(glm::vec4(0));
    view.lighting.ambientAndExposure={.1f,.1f,.1f,1};view.blockerStableViewValid=true;
    float physicalShift=0,jitter=.01f,depthShift=0;unsigned depthSize=16;
    const auto setView=[&](){view.blockerUnjitteredViewProjection=glm::mat4(1);view.blockerUnjitteredViewProjection[3].x=physicalShift;
        view.viewProjection=view.blockerUnjitteredViewProjection;view.viewProjection[3].x+=jitter;
        view.cameraPosition={-physicalShift,0,2};};
    const auto writeDepth=[&](){const glm::vec4 data(physicalShift+jitter,float(depthSize),depthShift,0);
        auto encoder=device.BeginFrame({0,depthSize,depthSize});Check(encoder.KeepAlive(owner),"Pin actual jittered depth producer");
        const auto target=depthSize==16?depthTarget:resizedDepthTarget;
        Check(encoder.SetRenderTarget({target,depthSize,depthSize,RHICommand::ClearDepth})&&
            encoder.UpdateUniformBufferBytes(parameters,&data,sizeof(data),0)&&encoder.BindUniformBuffer(parameters,8)&&
            encoder.BindPipeline(depthPipeline)&&encoder.BindMesh(quad)&&encoder.DrawIndexed({3}),"Rasterize jittered world depth edge");Submit(device,encoder);};
    PipelineDetail::FiberVolumePass volume(device),reference(device);
    const auto prepare=[&](PipelineDetail::FiberVolumePass& pass){setView();pass.Begin(1);auto encoder=device.BeginFrame({0,1,1});
        Check(encoder.KeepAlive(owner),"Pin stable cache depth");pass.Prepare(encoder,0,item,view);Submit(device,encoder);return pass.Statistics();};
    const auto snapshot=[&](PipelineDetail::FiberVolumePass& pass){const auto textures=pass.DebugTextures();
        return std::array{Read(device,textures.opaqueBlockers),Read(device,textures.directVisibility),Read(device,textures.extraVisibility)};};
    const auto sampleBindings=[&](){auto encoder=device.BeginFrame({0,1,1});Check(encoder.KeepAlive(owner),"Pin cached field receiver");
        Check(encoder.SetRenderTarget({probeTarget,1,1,RHICommand::ClearColor}),"Set cached field receiver target");volume.Bind(encoder,0,item);
        Check(encoder.BindPipeline(probePipeline)&&encoder.BindMesh(quad)&&encoder.DrawIndexed({3}),"Sample bound density/mask and cached G");Submit(device,encoder);return Read(device,probe);};
    writeDepth();Check(prepare(reference).passes==7,"First legacy jittered-depth reference capture");
    const auto original=snapshot(reference);
    material.volumeStableOpaqueCache=true;Check(prepare(volume).passes==7,"First stable world-occupancy capture");
    Check(snapshot(volume)==original,"Stable cache did not inject the first actual jittered VP/depth pair");
    const auto density=Read(device,volume.DebugTextures().density);
    const auto bound=sampleBindings();Check(bound[0]>0&&bound[1]>.1f,"Actual density/mask were not exposed at texture slots4/5");
    unsigned reused=0;
    for(float phase:{.06f,-.06f,.03f,-.04f,.05f,-.02f,.01f}) {
        jitter=phase;writeDepth();const auto stats=prepare(volume);
        Check(stats.passes==0&&stats.blockerPasses==0,"TAA jitter alone retraced cached opaque shell/G");
        Check(snapshot(volume)==original&&sampleBindings()==bound,"TAA phase changed retained world occupancy or G");++reused;
    }
    jitter=.06f;writeDepth();material.volumeStableOpaqueCache=false;
    Check(prepare(reference).blockerPasses==3,"Legacy negative-control did not regenerate phase-dependent shell");
    const auto currentLegacy=snapshot(reference);Check(currentLegacy[0]!=original[0],"Synthetic depth discontinuity did not expose real phase aliasing");
    Check(prepare(volume).blockerPasses==3&&snapshot(volume)==currentLegacy,"Turning stable cache off failed to restore the current legacy depth phase");
    jitter=-.06f;writeDepth();Check(prepare(volume).blockerPasses==3,"Disabled cache stopped following legacy jittered VP");
    material.volumeStableOpaqueCache=true;Check(prepare(volume).blockerPasses==3,"Reenabled stable cache failed to capture a fresh actual depth phase");
    Check(prepare(volume).passes==0,"Identical stable frame was not cached");
    // Physical camera motion must not hide behind a stable-view token. Source
    // IBL camera-derived paths also retrace, while density remains unchanged.
    physicalShift=.07f;writeDepth();const auto camera=prepare(volume);
    Check(camera.blockerPasses==3&&camera.updated==1,"Physical camera change retained stale shell or IBL paths");
    Check(Read(device,volume.DebugTextures().density)==density,"Camera motion rebuilt or changed physical density");
    const auto beforeContent=snapshot(volume);depthShift=.04f;writeDepth();++view.opaqueContentRevision;
    Check(prepare(volume).blockerPasses==3&&snapshot(volume)[0]!=beforeContent[0],"Opaque content revision failed to refresh the changed depth shell");
    auto updated=capsules;updated[0].startRadius.w=updated[0].endRadius.w=.1f;
    item.opaqueCapsules=std::make_shared<const std::vector<StrandOpaqueCapsule>>(updated);
    const auto nBeforeProxy=Read(device,volume.DebugTextures().paths);const auto proxy=prepare(volume);
    Check(proxy.passes==3&&proxy.blockerPasses==3&&Read(device,volume.DebugTextures().paths)==nBeforeProxy,"Proxy update invalidated density/N or retained stale G");
    item.model[3].z=.025f;const auto model=prepare(volume);Check(model.blockerPasses==3,"Model transform retained stale screen occupancy");
    const auto occupancy=snapshot(volume)[0];view.sunDirection={0,-1,0};const auto light=prepare(volume);
    Check(light.passes==5&&light.blockerPasses==2&&snapshot(volume)[0]==occupancy,"Light change must retrace N/G without reinjecting stable occupancy");
    depthSize=32;view.opaqueDepth=resizedDepth;writeDepth();
    Check(prepare(volume).blockerPasses==3,"Opaque depth resize/reallocation retained the old screen sample");
    view.blockerStableViewValid=false;Check(prepare(volume).blockerPasses==3,"Invalid stable-view token did not fall back to legacy capture");
    jitter=.02f;writeDepth();Check(prepare(volume).blockerPasses==3,"Missing stable-view token incorrectly suppressed jitter refresh");
    view.blockerStableViewValid=true;Check(prepare(volume).blockerPasses==3,"Valid-token transition failed fresh stable capture");
    vertices[0].positionRadius.y+=.005f;Check(mesh.Update(vertices,lines),"Update dynamic cache-test groom");item.geometry=mesh.Snapshot();
    const auto dynamic=prepare(volume);Check(dynamic.passes==7&&dynamic.blockerPasses==3,"Geometry revision retained stale grid occupancy");
    ++view.opaqueContentRevision;setView();volume.Begin(1);auto cancelled=device.BeginFrame({0,1,1});volume.Prepare(cancelled,0,item,view);
    Check(cancelled.Cancel(),"Cancel stable occupancy record");volume.Invalidate();
    Check(prepare(volume).passes==7,"Cancelled stable capture was treated as completed GPU occupancy");
    // A caller marking a stable view valid must supply finite invertible data.
    volume.Begin(1);auto invalid=device.BeginFrame({0,1,1});view.blockerUnjitteredViewProjection[0][0]=std::numeric_limits<float>::quiet_NaN();
    bool rejected=false;try{volume.Prepare(invalid,0,item,view);}catch(const std::invalid_argument&){rejected=true;}
    invalid.Cancel();volume.Invalidate();Check(rejected,"Invalid stable projection was accepted");
    volume.Reset();reference.Reset();
    std::cout<<"Fiber stable opaque cache: "<<reused<<" real jittered-depth phases reused with0passes; first-sample provenance, legacy alias negative control, camera/content/proxy/model/light/resize/dynamic/validity/cancel refresh and field bindings passed (host first-sample cache)\n";
}
} // namespace Render::TestData
