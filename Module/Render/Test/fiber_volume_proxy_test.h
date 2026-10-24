#pragma once
#include "Render/Private/Pipeline/fiber_volume.h"
#include <array>
#include <chrono>
#include <cmath>
#include <future>
#include <iostream>
#include <limits>

namespace Render::TestData {
namespace FiberProxyAuditDetail {
inline void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
template<class F> auto OnDevice(RHIDevice& device,F operation) {
    using T=std::invoke_result_t<F>;auto promise=std::make_shared<std::promise<T>>();auto result=promise->get_future();
    device.async_ExecuteCode([promise,operation=std::move(operation)]()mutable{
        try{if constexpr(std::is_void_v<T>){operation();promise->set_value();}else promise->set_value(operation());}
        catch(...){promise->set_exception(std::current_exception());}
    });return result.get();
}
inline void Submit(RHIDevice& device,RHIFrameEncoder& encoder) {
    Check(encoder.End(false),"End opaque proxy oracle");auto done=std::make_shared<bool>(false);
    Check(device.async_SubmitFrameCommands(encoder.GetCommandBuffer(),[done]{*done=true;}),"Submit opaque proxy oracle");
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
    while(!*done){device.returnSystem.DrainCallbacks();Check(std::chrono::steady_clock::now()<deadline,"Opaque proxy oracle timeout");
        if(!*done)device.returnSystem.WaitForCallbacks(std::chrono::milliseconds(2));}
}
inline std::vector<float> Read(RHIDevice& device,RenderResourceHandle<RHITextureSpec> image) {
    return OnDevice(device,[&device,image]{return device.BackendDiagnostics()->ReadTexture(image);});
}
// Independent occupancy oracle: existence of a containing sphere is the
// minimum of a scalar quadratic, unlike the GLSL closest-swept-sphere solve.
inline bool Contains(const StrandOpaqueCapsule& capsule,glm::dvec3 p) {
    if(std::max(capsule.startRadius.w,capsule.endRadius.w)<=0)return false;
    const glm::dvec3 a(capsule.startRadius),axis=glm::dvec3(capsule.endRadius)-a,v=p-a;
    const double radius=capsule.startRadius.w,delta=double(capsule.endRadius.w)-radius;
    const double A=glm::dot(axis,axis)-delta*delta,B=-2.*(glm::dot(v,axis)+radius*delta),C=glm::dot(v,v)-radius*radius;
    const auto f=[&](double t){return (A*t+B)*t+C;};
    double result=std::min(f(0),f(1));if(A>0)result=std::min(result,f(std::clamp(-B/(2.*A),0.,1.)));
    return result<=0;
}
} // namespace FiberProxyAuditDetail

inline void RunFiberVolumeProxyGpuOracle(RHIDevice& device) {
    using namespace FiberProxyAuditDetail;
    Check(!StrandMaterial{}.volumeProxyBlockers,"Generic opaque capsule proxy must default off");
    PipelineDetail::ResourceBuilder resources(device);
    const auto image=resources.Texture(64,64,RHITextureFormat::RGBA16F);
    const auto target=resources.Target(image,{});
    const auto depth=resources.Texture(16,16,RHITextureFormat::Depth32F);
    const auto depthTarget=resources.Target({},depth);
    const auto pipeline=resources.FullscreenPipeline(FullscreenVertexShader(),PipelineDetail::FiberVolumeOpaqueShellShader());
    const auto quad=resources.FullscreenMesh();
    const auto uniform=resources.Uniform(sizeof(PipelineDetail::FiberVolumeConstants));
    const auto visibilityImage=resources.Texture(1,1,RHITextureFormat::RGBA32F);
    const auto visibilityTarget=resources.Target(visibilityImage,{});
    const auto visibilityPipeline=resources.FullscreenPipeline(FullscreenVertexShader(),
        std::string("#version 450 core\nlayout(location=0) out vec4 result;\n")+PipelineDetail::FiberVolumeBlockGLSL()+
        "void main(){result=FiberOpaqueDirectVisibility(vec3(0));}\n");
    const auto owner=resources.Finish();
    {auto encoder=device.BeginFrame({0,16,16});Check(encoder.KeepAlive(owner),"Pin proxy screen-depth fixture");
        RHICommand::SetRenderTarget clear;clear.target=depthTarget;clear.width=clear.height=16;clear.clearFlags=RHICommand::ClearDepth;clear.clearDepth=.5f;
        Check(encoder.SetRenderTarget(clear),"Clear independent proxy screen depth");Submit(device,encoder);}
    const std::array<StrandOpaqueCapsule,4> capsules{{
        {{-.21f,-.18f,.03f,.08f},{.17f,.23f,-.04f,.15f}},
        {{-.23f,.13f,-.2f,.09f},{-.23f,.13f,-.2f,.12f}},
        {{-.1f,0,0,.01f},{0,0,0,.2f}},
        {{-.2f,0,0,0},{.2f,0,0,0}}
    }};
    PipelineDetail::FiberVolumeConstants constants;constants.minimumStep={-.4f,-.4f,-.4f,.05f};constants.size={16,16,16,4};
    constants.atlas={64,64,1,1};constants.quality={0,0,1,1};constants.opaque={1,.11f,0,0};constants.cameraForward={0,0,-1,0};
    unsigned occupied=0,comparisons=0;
    const auto mask=[&](const std::vector<StrandOpaqueCapsule>& values,bool screen,bool proxy) {
        constants.quality.y=screen?1.f:0.f;constants.quality.z=proxy?1.f:0.f;constants.quality.w=float(values.size());constants.capsules={};
        for(std::size_t i=0;i<values.size();++i){constants.capsules[2*i]=values[i].startRadius;constants.capsules[2*i+1]=values[i].endRadius;}
        auto encoder=device.BeginFrame({0,64,64});Check(encoder.KeepAlive(owner),"Pin proxy occupancy target");
        Check(encoder.SetRenderTarget({target,64,64,RHICommand::ClearColor}),"Set proxy occupancy target");
        PipelineDetail::FiberVolumeUpload(encoder,uniform,constants);
        Check(encoder.BindTexture(depth,12)&&encoder.BindPipeline(pipeline)&&encoder.BindMesh(quad)&&encoder.DrawIndexed({3}),"Draw real proxy/screen union producer");
        Submit(device,encoder);return Read(device,image);
    };
    const auto verify=[&](const std::vector<StrandOpaqueCapsule>& values,bool screen,bool proxy) {
        const auto output=mask(values,screen,proxy);Check(output.size()==64*64*4,"Read proxy atlas");
        for(int z=0;z<16;++z)for(int y=0;y<16;++y)for(int x=0;x<16;++x) {
            const glm::dvec3 p=glm::dvec3(constants.minimumStep)+
                (glm::dvec3(x,y,z)+.5)*double(constants.minimumStep.w);
            bool inside=false;if(proxy)for(const auto& capsule:values)inside|=Contains(capsule,p);
            const bool shell=screen&&p.z>0&&p.z<constants.opaque.y;
            const auto pixel=(std::size_t(z/4)*16+y)*64+(z%4)*16+x;
            const float expected=inside||shell?1.f:0.f;
            Check(output[pixel*4]==expected,"Tapered/degenerate capsule occupancy or screen-union disagrees with independent quadratic oracle");
            for(int c=1;c<4;++c)Check(output[pixel*4+c]==0.f,"Proxy blocker wrote density/tangent channels");
            occupied+=unsigned(inside);++comparisons;
        }
    };
    for(const auto& capsule:capsules)verify({capsule},false,true);
    const std::vector<StrandOpaqueCapsule> pair{capsules[0],capsules[1]};verify(pair,true,true);verify(pair,true,false);verify(pair,false,false);
    Check(occupied>0,"Proxy occupancy oracle never observed a containing capsule");
    // Real volume cache: use no resolved depth at all, and move the camera
    // behind the head/outside its old viewport. Proxy geometry remains local.
    StrandMesh mesh(device);const std::array<StrandVertex,4> vertices{{
        {{-.4f,-.4f,-.4f,.00001f},{1,1,1,0}},{{.4f,.4f,.4f,.00001f},{1,1,1,1}},
        {{-.4f,.4f,-.4f,.00001f},{1,-1,1,0}},{{.4f,-.4f,.4f,.00001f},{1,-1,1,1}}}};
    const std::array<std::uint32_t,4> lines{0,1,2,3};Check(mesh.Update(vertices,lines),"Create proxy volume bounds");
    StrandRenderItem item;item.geometry=mesh.Snapshot();item.material.spatialVolume=true;item.material.volumeResolution=16;item.material.volumeVoxelSize=.05f;
    item.material.volumeExactIntegration=true;item.material.volumeProxyBlockers=true;item.material.volumeDepthThreshold=0;
    item.opaqueCapsules=std::make_shared<const std::vector<StrandOpaqueCapsule>>(pair);
    PipelineDetail::StrandPassView view;view.sunIntensity=1;view.sunDirection={-1,0,0};view.cameraPosition={0,0,3};
    view.lighting.lightColors.fill(glm::vec4(0));view.lighting.ambientAndExposure=glm::vec4(0);
    PipelineDetail::FiberVolumePass volume(device);
    const auto prepare=[&](){volume.Begin(1);auto encoder=device.BeginFrame({0,1,1});volume.Prepare(encoder,0,item,view);Submit(device,encoder);return volume.Statistics();};
    const auto shaderVisibility=[&](){auto encoder=device.BeginFrame({0,1,1});Check(encoder.KeepAlive(owner),"Pin proxy receiver oracle");
        Check(encoder.SetRenderTarget({visibilityTarget,1,1,RHICommand::ClearColor}),"Set proxy receiver output");volume.Bind(encoder,0,item);
        Check(encoder.BindPipeline(visibilityPipeline)&&encoder.BindMesh(quad)&&encoder.DrawIndexed({3}),"Draw actual proxy visibility receiver");
        Submit(device,encoder);return Read(device,visibilityImage);};
    const auto first=prepare();Check(first.passes==7&&first.blockerPasses==3,"Proxy-only pipeline failed without independent opaque depth");
    const auto textures=volume.DebugTextures();const auto density=Read(device,textures.density),paths=Read(device,textures.paths),blockers=Read(device,textures.opaqueBlockers),visibility=Read(device,textures.directVisibility);
    float minimum=1,maximum=0;for(std::size_t i=0;i<visibility.size();i+=4){minimum=std::min(minimum,visibility[i]);maximum=std::max(maximum,visibility[i]);}
    Check(minimum<.01f&&maximum>.99f,"Head proxy did not retain both blocked and open sun paths");
    view.cameraPosition={5,2,-4};view.cameraForward={0,0,1};view.viewProjection[3]={10,10,10,1};view.opaqueContentRevision=77;
    Check(prepare().passes==0,"Proxy-only occupancy acquired a screen-camera dependency");
    Check(Read(device,volume.DebugTextures().opaqueBlockers)==blockers&&Read(device,volume.DebugTextures().directVisibility)==visibility,"Offscreen/behind-head camera changed local proxy G");
    auto changed=pair;for(auto& c:changed){c.startRadius.x+=.13f;c.endRadius.x+=.13f;}
    item.opaqueCapsules=std::make_shared<const std::vector<StrandOpaqueCapsule>>(changed);
    const auto moved=prepare();Check(moved.passes==3&&moved.blockerPasses==3,"Proxy content update invalidated the wrong volume stages");
    Check(Read(device,volume.DebugTextures().density)==density&&Read(device,volume.DebugTextures().paths)==paths,"Proxy G update changed density or N extinction");
    Check(Read(device,volume.DebugTextures().opaqueBlockers)!=blockers,"Moving proxy did not update local occupancy");
    const auto rejects=[&](std::vector<StrandOpaqueCapsule> invalid) {
        item.opaqueCapsules=std::make_shared<const std::vector<StrandOpaqueCapsule>>(std::move(invalid));volume.Begin(1);
        auto encoder=device.BeginFrame({0,1,1});bool rejected=false;
        try{volume.Prepare(encoder,0,item,view);}catch(const std::invalid_argument&){rejected=true;}encoder.Cancel();volume.Invalidate();return rejected;
    };
    auto bad=pair;bad[0].startRadius.w=-.1f;Check(rejects(bad),"Negative proxy radius was accepted");
    bad=pair;bad[0].endRadius.x=std::numeric_limits<float>::quiet_NaN();Check(rejects(bad),"Nonfinite proxy coordinate was accepted");
    Check(rejects(std::vector<StrandOpaqueCapsule>(17,capsules[0])),"Over-budget proxy capsules were accepted");
    item.opaqueCapsules=std::make_shared<const std::vector<StrandOpaqueCapsule>>(pair);prepare();item.material.volumeProxyBlockers=false;
    Check(prepare().passes==0&&!volume.DebugTextures().directVisibility.IsValid(),"Disabled proxy retained active G resources");
    for(float v:shaderVisibility())Check(v==1.f,"Disabled proxy visibility receiver did not restore unoccluded identity");
    item.material.volumeProxyBlockers=true;Check(prepare().blockerPasses==3,"Reenabled proxy reused disabled occupancy state");
    item.opaqueCapsules.reset();Check(prepare().passes==0&&!volume.DebugTextures().opaqueBlockers.IsValid(),"Empty proxy input was not identity");
    for(float v:shaderVisibility())Check(v==1.f,"Empty proxy visibility receiver was not unoccluded");
    volume.Reset();
    std::cout<<"Fiber opaque collision proxies: "<<comparisons<<" quadratic occupancy comparisons, min/max sun G="<<minimum<<'/'<<maximum
        <<"; tapered/degenerate/contained/zero-radius, screen union, no-depth/offscreen stability, independent N and cache/validation passed (not render-mesh visibility)\n";
}
} // namespace Render::TestData
