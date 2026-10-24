#pragma once
#include "Render/Private/Pipeline/fiber_volume.h"
#include <array>
#include <future>
#include <iostream>
#include <vector>

namespace Render::TestData {
namespace FiberNearAuditDetail {
inline void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
template<class F> auto OnDevice(RHIDevice& device,F operation) {
    using T=std::invoke_result_t<F>;auto promise=std::make_shared<std::promise<T>>();auto future=promise->get_future();
    device.async_ExecuteCode([promise,operation=std::move(operation)]()mutable{
        try{if constexpr(std::is_void_v<T>){operation();promise->set_value();}else promise->set_value(operation());}
        catch(...){promise->set_exception(std::current_exception());}
    });return future.get();
}
inline void Submit(RHIDevice& device,RHIFrameEncoder& encoder) {
    Check(encoder.End(false),"End fiber near-shadow oracle");auto done=std::make_shared<bool>(false);
    Check(device.async_SubmitFrameCommands(encoder.GetCommandBuffer(),[done]{*done=true;}),"Submit fiber near-shadow oracle");
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
    while(!*done){device.returnSystem.DrainCallbacks();Check(std::chrono::steady_clock::now()<deadline,"Fiber near-shadow oracle timeout");if(!*done)device.returnSystem.WaitForCallbacks(std::chrono::milliseconds(2));}
}
inline std::vector<float> Read(RHIDevice& device,RenderResourceHandle<RHITextureSpec> image) {
    return OnDevice(device,[&device,image]{return device.BackendDiagnostics()->ReadTexture(image);});
}
inline double Area(const std::vector<float>& density,double h) {
    double result=0;for(std::size_t i=0;i<density.size();i+=4){Check(std::isfinite(density[i])&&density[i]>=0,"Invalid injected fiber density");result+=density[i];}
    return result*h*h*h;
}
inline void Run128SliceOracle(RHIDevice& device) {
    // A single segment reaches the full 128-slice grid. Splitting it into
    // short segments would hide GS output truncation beyond the old 64 budget.
    constexpr float radius=.00001f;
    // Snapshot bounds include endpoint radii: the resulting .63m extent is
    // exactly 126 requested cells plus the existing two-cell border.
    const std::array<StrandVertex,2> points{{
        {{0,0,-.31499f,radius},{0,0,1,0}},{{0,0,.31499f,radius},{0,0,1,1}}}};
    const std::array<std::uint32_t,2> lines{0,1};
    StrandMesh geometry(device);Check(geometry.Update(points,lines),"Create 128-slice long-segment fixture");
    StrandRenderItem item;item.geometry=geometry.Snapshot();item.material.spatialVolume=true;
    item.material.volumeResolution=128;item.material.volumeVoxelSize=.005f;
    item.material.volumeRasterWidthScale=1;item.material.volumePreserveNearField=true;
    item.material.volumeHardwareFiltering=true;
    PipelineDetail::StrandPassView view;view.sunDirection={0,0,-1};view.sunIntensity=1;
    view.lighting.lightColors.fill(glm::vec4(0));view.lighting.ambientAndExposure=glm::vec4(0);
    PipelineDetail::FiberVolumePass volume(device);volume.Begin(1);
    auto encoder=device.BeginFrame({0,1,1});volume.Prepare(encoder,0,item,view);Submit(device,encoder);
    const auto statistics=volume.Statistics();const auto textures=volume.DebugTextures();
    const float h=statistics.effectiveVoxelSize;
    Check(statistics.resolution==128&&std::abs(h-.005f)<1e-7f,"128 budget did not retain the requested 5mm physical grid");
    Check(std::abs(statistics.effectiveBiasMetres-.005f)<1e-7f,"128 budget changed the requested physical trace exclusion");
    const auto extent=glm::max(item.geometry->BoundsMax()-item.geometry->BoundsMin(),glm::vec3(1e-6f));
    const auto grid=glm::min(glm::uvec3(glm::ceil(extent/h))+glm::uvec3(2),glm::uvec3(128));
    const unsigned columns=unsigned(std::ceil(std::sqrt(float(grid.z))));
    const auto density=Read(device,textures.density),paths=Read(device,textures.paths);
    Check(density.size()==std::size_t(textures.width)*textures.height*4&&paths.size()==density.size(),"128-slice atlas dimensions differ from readback");
    const double expected=double(glm::length(glm::vec3(points[1].positionRadius-points[0].positionRadius)))*(2.*double(radius));
    const double relativeError=std::abs(Area(density,h)/expected-1.);
    Check(relativeError<.0005,"128-slice GS truncated long-segment projected area");
    unsigned fullSlices=0,blankTexels=0;float maximumPath=0;
    const unsigned tiles=unsigned(textures.width/grid.x)*unsigned(textures.height/grid.y);
    for(unsigned z=0;z<tiles;++z) {
        double sliceDensity=0.;
        for(unsigned y=0;y<grid.y;++y)for(unsigned x=0;x<grid.x;++x) {
            const std::size_t pixel=std::size_t(z/columns*grid.y+y)*textures.width+(z%columns*grid.x+x);
            for(unsigned channel=0;channel<4;++channel) {
                Check(std::isfinite(density[pixel*4+channel])&&std::isfinite(paths[pixel*4+channel]),"128-slice atlas contains nonfinite density or optical paths");
                if(z>=grid.z)Check(density[pixel*4+channel]==0.f&&paths[pixel*4+channel]==0.f,"Unused 128-slice atlas tile acquired density or optical paths");
            }
            if(z>=grid.z){++blankTexels;continue;}
            sliceDensity+=density[pixel*4];maximumPath=std::max(maximumPath,paths[pixel*4]);
        }
        // Avoid endpoint roundoff at the first/last occupied physical cells;
        // every interior slice, including the old GS's omitted tail, is full.
        if(z>=2&&z+2<grid.z){Check(sliceDensity>0.,"128-slice long segment lost an interior voxel slice");++fullSlices;}
    }
    Check(fullSlices>64&&blankTexels>0&&maximumPath>0.f,"128-slice fixture did not exercise long GS output, blank atlas tiles and active tracing");
    volume.Reset();
    std::cout<<"Fiber 128-slice budget: "<<fullSlices<<" interior slices, "<<blankTexels
        <<" blank atlas texels, projected-area relative error="<<relativeError<<"; requested 5mm grid/bias and active optical paths passed\n";
}
inline void RunPhaseOracle(RHIDevice& device) {
    // Exercise the actual N and G functions together on a nonuniform field.
    // This deliberately includes the previous whole-march shift as a negative
    // control: a uniform slab cannot expose its distant-peak aliasing.
    PipelineDetail::ResourceBuilder resources(device);
    auto shader=PipelineDetail::FiberVolumePathsShader();shader.erase(shader.rfind("void main()"));
    const auto opaque=PipelineDetail::FiberVolumeOpaquePathsShader();
    const auto opaqueBegin=opaque.find("float FiberTraceOpaqueVisibility("),opaqueEnd=opaque.rfind("void main()");
    Check(opaqueBegin!=std::string::npos&&opaqueEnd>opaqueBegin,"Locate production opaque trace helper");
    shader+="\nlayout(binding=13) uniform sampler2D fiberOpaqueMask;\n";
    shader+=opaque.substr(opaqueBegin,opaqueEnd-opaqueBegin);
    shader+=R"GLSL(
void main(){
    vec3 p=fiberGridMinStep.xyz+vec3(gl_FragCoord.x,1.5,1.5)*fiberGridMinStep.w;
    result=vec4(FiberTracePath(p,vec3(1.0,0.0,0.0),1e6),
        FiberTraceOpaqueVisibility(p,vec3(1.0,0.0,0.0),1e6),
        FiberTracePath(p,vec3(-1.0,0.0,0.0),1e6),
        FiberTraceOpaqueVisibility(p,vec3(-1.0,0.0,0.0),1e6));
}
)GLSL";
    const auto pipeline=resources.FullscreenPipeline(FullscreenVertexShader(),shader);
    const auto mesh=resources.FullscreenMesh();
    const auto output=resources.Texture(64,1,RHITextureFormat::RGBA32F);
    const auto target=resources.Target(output,{});
    const auto buffer=resources.Uniform(sizeof(PipelineDetail::FiberVolumeConstants));
    std::array<float,64> peaks{};
    for(unsigned x=0;x<64;++x)peaks[x]=x%11==2?70.f:x%7==4?20.f:0.f;
    std::vector<glm::vec4> density(128*8,glm::vec4(0)),blockers=density;
    for(unsigned z=0;z<4;++z)for(unsigned y=0;y<4;++y)for(unsigned x=0;x<64;++x){
        const auto pixel=(z/2*4+y)*128+(z%2*64+x);
        density[pixel].r=peaks[x];blockers[pixel].r=peaks[x]*.01f;
    }
    CreateRHITextureSpec desc;desc.width=128;desc.height=8;desc.mipmaps=false;
    desc.textureDataStoreType=desc.textureUseType=RHITextureFormat::RGBA32F;
    desc.filterMode=RHIFilterMode::Linear;desc.addressMode=RHIAddressMode::ClampToEdge;
    desc.data=density.data();const auto densityImage=resources.Create<RHITextureSpec>([&](auto cb){device.async_CreateTexture(desc,cb);});
    desc.data=blockers.data();const auto blockerImage=resources.Create<RHITextureSpec>([&](auto cb){device.async_CreateTexture(desc,cb);});
    const auto owner=resources.Finish();
    PipelineDetail::FiberVolumeConstants constants;constexpr float h=.01617090031504631f;
    constants.minimumStep={0,0,0,h};constants.size={64,4,4,2};constants.atlas={128,8,1,1};
    constants.trace={1.1f,1,0,0};
    const auto sample=[&](float bias,float anchor,bool filtering){
        constants.trace.y=bias;constants.cameraLocal.w=anchor;constants.opaque.w=filtering?1.f:0.f;
        auto encoder=device.BeginFrame({0,64,1});Check(encoder.KeepAlive(owner),"Pin nonuniform near-field oracle");
        Check(encoder.SetRenderTarget({target,64,1,RHICommand::ClearColor}),"Bind nonuniform near-field output");
        PipelineDetail::FiberVolumeUpload(encoder,buffer,constants);
        Check(encoder.BindTexture(densityImage,12)&&encoder.BindTexture(blockerImage,13)&&encoder.BindPipeline(pipeline)&&
            encoder.BindMesh(mesh)&&encoder.DrawIndexed({3}),"Draw nonuniform near-field oracle");
        Submit(device,encoder);auto values=Read(device,output);Check(values.size()==64*4,"Nonuniform near-field output dimensions");
        for(float v:values)Check(std::isfinite(v),"Nonfinite nonuniform near-field output");return values;
    };
    unsigned comparisons=0,restoredDepth=0,restoredBlockers=0,oldShiftDepthFailures=0,oldShiftBlockerFailures=0;
    double maximumNearIntegralError=0;
    for(bool filtering:{false,true})for(float bias:{1.f,8.f}){
        const auto legacy=sample(bias,0,filtering);
        const auto nearValues=sample(bias*.005f/h,bias,filtering);
        const auto shifted=sample(bias*.005f/h,0,filtering);
        for(std::size_t i=0;i<legacy.size();i+=2){
            Check(nearValues[i]>=legacy[i],"Anchored near interval lost distant nonnegative optical depth");
            Check(nearValues[i+1]<=legacy[i+1],"Anchored near interval weakened a distant opaque blocker");
            Check(nearValues[i]>=0.f&&nearValues[i]<=80.f&&nearValues[i+1]>=0.f&&nearValues[i+1]<=1.f,"Near-field N/G outside physical bounds");
            restoredDepth+=nearValues[i]>legacy[i]+1e-5f;restoredBlockers+=nearValues[i+1]<legacy[i+1]-1e-5f;
            oldShiftDepthFailures+=shifted[i]<legacy[i]-1e-4f;oldShiftBlockerFailures+=shifted[i+1]>legacy[i+1]+1e-4f;++comparisons;
        }
        Check(sample(bias,0,filtering)==legacy,"Nonuniform near-field disable did not restore legacy bit identity");
        if(bias==8.f){
            // Synthetic zero actual bias reaches the full 32 quarter-voxel
            // segments. Each starts at a cell center; trapezoids integrate the
            // independent piecewise-linear field exactly, including exit clip.
            const auto fullNear=sample(0.f,8.f,filtering);
            const auto rho=[&](double x){const auto a=int(std::floor(x-.5));const double f=x-.5-a;
                const double first=a>=0&&a<64?peaks[std::size_t(a)]:0.,second=a+1>=0&&a+1<64?peaks[std::size_t(a+1)]:0.;return first*(1.-f)+second*f;};
            for(unsigned x=0;x<64;++x)for(unsigned side=0;side<2;++side){
                const double p=double(x)+.5,sign=side?-1.:1.,length=std::min(8.,side?p:64.-p);
                double integral=0.;for(double d=0.;d<length;){const double next=std::min(d+.25,length);
                    integral+=(rho(p+sign*d)+rho(p+sign*next))*.5*(next-d)*h;d=next;}
                const auto channel=std::size_t(x)*4+side*2;
                const double error=std::abs(double(fullNear[channel])-double(legacy[channel])-integral);
                maximumNearIntegralError=std::max(maximumNearIntegralError,error);
                Check(error<(filtering?.005:.0003),"Maximum-bias near interval omitted part of its independent integral");
                Check(fullNear[channel]>=legacy[channel]&&fullNear[channel+1]<=legacy[channel+1],"Maximum-bias near interval broke N/G monotonicity");
            }
        }
    }
    Check(restoredDepth&&restoredBlockers,"Nonuniform oracle did not exercise added N and G near-field effects");
    Check(oldShiftDepthFailures&&oldShiftBlockerFailures,"Nonuniform negative control failed to expose the old shifted march");
    std::cout<<"Fiber near phase: "<<comparisons<<" N/G comparisons, restored depth/blockers="<<restoredDepth<<"/"<<restoredBlockers
        <<", old shifted-march failures="<<oldShiftDepthFailures<<"/"<<oldShiftBlockerFailures
        <<", max-bias32-segment integral error="<<maximumNearIntegralError<<"; legacy/manual and hardware filtering restore passed\n";
}
}
inline void RunFiberNearShadowGpuOracle(RHIDevice& device) {
    using namespace FiberNearAuditDetail;
    Check(!StrandMaterial{}.volumePreserveNearField,"Generic near-field adapter must default off");
    PipelineDetail::ResourceBuilder resources(device);
    const auto output=resources.Texture(1,1,RHITextureFormat::RGBA32F);
    const auto outputTarget=resources.Target(output,{});
    const auto fullscreen=resources.FullscreenMesh();
    const auto opaqueDepth=resources.Texture(16,16,RHITextureFormat::Depth32F);
    const auto opaqueTarget=resources.Target({},opaqueDepth);
    // Keep the exact production march and sample helpers, replacing only its
    // multi-light main. Bind() supplies the real private production constants.
    auto fragment=PipelineDetail::FiberVolumePathsShader();const auto main=fragment.rfind("void main()");
    Check(main!=std::string::npos,"Locate production fiber path main");fragment.erase(main);
    fragment+=R"GLSL(
void main(){result=vec4(FiberTracePath(vec3(0.0),vec3(1.0,0.0,0.0),1e6));}
)GLSL";
    const auto march=resources.FullscreenPipeline(FullscreenVertexShader(),fragment);
    const auto owner=resources.Finish();
    {auto encoder=device.BeginFrame({0,16,16});Check(encoder.KeepAlive(owner),"Pin near-shadow audit targets");
        RHICommand::SetRenderTarget clear;clear.target=opaqueTarget;clear.width=clear.height=16;
        clear.clearFlags=RHICommand::ClearDepth;clear.clearDepth=.5f;Check(encoder.SetRenderTarget(clear),"Clear near-shadow plane depth");Submit(device,encoder);}
    StrandMesh geometry(device);const std::array<StrandVertex,4> points{{
        {{-.2f,-.2f,-.2f,.000001f},{1,1,1,0}},{{.2f,.2f,.2f,.000001f},{1,1,1,1}},
        {{-.2f,.2f,-.2f,.000001f},{1,-1,1,0}},{{.2f,-.2f,.2f,.000001f},{1,-1,1,1}}}};
    const std::array<std::uint32_t,4> lines{0,1,2,3};Check(geometry.Update(points,lines),"Create near-shadow audit geometry");
    StrandRenderItem item;item.geometry=geometry.Snapshot();item.material.spatialVolume=true;
    item.material.volumeVoxelSize=.005f;item.material.volumeBiasVoxels=1;item.material.volumeTraceStep=1;
    item.material.volumeRasterWidthScale=1;item.material.volumeHardwareFiltering=true;
    PipelineDetail::StrandPassView view;view.cameraPosition={0,0,2};view.sunIntensity=0;
    view.lighting.lightColors.fill(glm::vec4(0));view.lighting.ambientAndExposure=glm::vec4(0);
    PipelineDetail::FiberVolumePass volume(device);
    const auto prepare=[&]{volume.Begin(1);auto encoder=device.BeginFrame({0,1,1});Check(encoder.KeepAlive(owner),"Pin near-shadow fixture inputs");volume.Prepare(encoder,0,item,view);Submit(device,encoder);return volume.Statistics();};
    double maximumSlabError=0,maximumAreaError=0;std::array<float,2> physicalBias{},actualVoxel{};
    for(unsigned caseIndex=0;caseIndex<2;++caseIndex) {
        item.material.volumeResolution=caseIndex?16:8;item.material.volumePreserveNearField=false;
        const auto legacyStats=prepare();const auto textures=volume.DebugTextures();
        const float h=legacyStats.effectiveVoxelSize;actualVoxel[caseIndex]=h;
        Check(h>item.material.volumeVoxelSize*2,"Coarse-budget fixture did not enlarge physical voxels");
        Check(std::abs(legacyStats.effectiveBiasMetres-h)<1e-7f,"Legacy bias no longer follows actual voxel size");
        const auto density=Read(device,textures.density);const auto low=item.geometry->BoundsMin()-glm::vec3(h);
        const auto extent=glm::max(item.geometry->BoundsMax()-item.geometry->BoundsMin(),glm::vec3(1e-6f));
        const auto grid=glm::min(glm::uvec3(glm::ceil(extent/h))+glm::uvec3(2),glm::uvec3(item.material.volumeResolution));
        const double targetArea=2.*double(glm::length(glm::vec3(points[1].positionRadius-points[0].positionRadius)))*2.*points[0].positionRadius.w;
        const double massError=std::abs(Area(density,h)/targetArea-1.);maximumAreaError=std::max(maximumAreaError,massError);
        Check(massError<.0005,"FP32 density failed independent segment projected-area conservation");
        const auto formats=OnDevice(device,[&device,textures]{
            return std::array{device.GetResourcePool().TextureTable.Get(textures.density)->textureDataStoreType,
                device.GetResourcePool().TextureTable.Get(textures.paths)->textureDataStoreType};});
        Check(formats[0]==RHITextureFormat::RGBA32F&&formats[1]==RHITextureFormat::RGBA16F,"Density/path storage precision contract changed");
        PipelineDetail::ResourceBuilder uniformResources(device);
        constexpr float rho=10.f;std::vector<glm::vec4> uniformCells(std::size_t(textures.width)*textures.height,glm::vec4(rho,0,0,0));
        CreateRHITextureSpec desc;desc.width=textures.width;desc.height=textures.height;desc.mipmaps=false;
        desc.textureDataStoreType=desc.textureUseType=RHITextureFormat::RGBA32F;desc.filterMode=RHIFilterMode::Linear;
        desc.addressMode=RHIAddressMode::ClampToEdge;desc.data=uniformCells.data();
        const auto uniformDensity=uniformResources.Create<RHITextureSpec>([&](auto cb){device.async_CreateTexture(desc,cb);});
        const auto uniformOwner=uniformResources.Finish();
        const auto sample=[&]{auto encoder=device.BeginFrame({0,1,1});Check(encoder.KeepAlive(owner)&&encoder.KeepAlive(uniformOwner),"Pin homogeneous slab oracle");
            Check(encoder.SetRenderTarget({outputTarget,1,1,RHICommand::ClearColor}),"Bind homogeneous slab output");
            volume.Bind(encoder,0,item);
            Check(encoder.BindTexture(uniformDensity,12)&&encoder.BindPipeline(march)&&encoder.BindMesh(fullscreen)&&encoder.DrawIndexed({3}),"Draw homogeneous slab oracle");
            Submit(device,encoder);const auto values=Read(device,output);Check(values.size()==4&&std::isfinite(values[0]),"Invalid homogeneous slab result");return values[0];};
        const float legacy=sample();
        // A constant cell field has one half-voxel linear ramp at grid exit.
        // Its exact lost area is rho*h/8. The production midpoint march has
        // only a possible cusp-crossing error <=rho*h/32 with step=h/2.
        const double exit=double(low.x)+double(grid.x)*h;
        const auto expected=[&](double bias){return double(rho)*(exit-bias-double(h)*.125);};
        const double integrationTolerance=double(rho)*h/32.+2e-5;
        const double legacyError=std::abs(double(legacy)-expected(h));maximumSlabError=std::max(maximumSlabError,legacyError);
        Check(legacyError<=integrationTolerance,"Legacy homogeneous slab differs from independent analytic integral");
        item.material.volumePreserveNearField=true;const auto nearStats=prepare();physicalBias[caseIndex]=nearStats.effectiveBiasMetres;
        Check(nearStats.passes==3&&nearStats.updated==1,"Physical bias switch must retrace N without injecting density");
        Check(std::abs(nearStats.effectiveBiasMetres-.005f)<1e-7f,"Physical near-field bias grew with coarse grid");
        Check(density==Read(device,volume.DebugTextures().density),"Near-field adapter changed physical density or mean tangent");
        const float nearCount=sample();const double nearError=std::abs(double(nearCount)-expected(.005));maximumSlabError=std::max(maximumSlabError,nearError);
        Check(nearError<=integrationTolerance,"Preserved near field differs from independent analytic slab integral");
        Check(nearCount>legacy&&std::abs(double(nearCount-legacy)-double(rho)*(h-.005))<=2.*integrationTolerance,
            "Preserved near field did not restore the omitted neighboring optical depth");
        Check(std::exp(-.25f*nearCount)<std::exp(-.25f*legacy),"Restored near optical depth did not lower slab transmission");
        item.material.volumePreserveNearField=false;Check(prepare().passes==3,"Disabling near-field adapter did not retrace N");
        Check(sample()==legacy&&density==Read(device,volume.DebugTextures().density),"Near-field disable did not restore legacy identity");
    }
    Check(actualVoxel[0]!=actualVoxel[1]&&physicalBias[0]==physicalBias[1],"Different volume budgets did not retain the same physical bias");
    Run128SliceOracle(device);
    RunPhaseOracle(device);
    // Ambient RGB is the same byte128 PbrPassConstants data consumed as
    // strandAmbient. It must enable IBL N/G even with Sky and cube disabled.
    item.material.volumeSkyTransmission=true;item.material.dualScatteringIbl=true;
    item.material.volumeOpaqueBlockers=true;item.material.volumeDepthThreshold=.12f;
    view.opaqueDepth=opaqueDepth;view.opaqueContentRevision=1;view.sky.options.x=0;view.indirect={};
    view.lighting.ambientAndExposure=glm::vec4(0);prepare();
    for(float value:Read(device,volume.DebugTextures().pathsAmbient))Check(value==0.f,"No incident environment should have inactive N paths");
    for(float value:Read(device,volume.DebugTextures().extraVisibility))Check(value==1.f,"Inactive environment should have unoccluded extra visibility");
    view.lighting.ambientAndExposure={.15f,.1f,.05f,1};const auto ambientStats=prepare();
    Check(ambientStats.passes==5&&ambientStats.blockerPasses==2,"Ambient-only enable did not trace independent N and G");
    const auto ambientN=Read(device,volume.DebugTextures().pathsAmbient),ambientG=Read(device,volume.DebugTextures().extraVisibility);
    float maximumAmbientN=0,minimumAmbientG=1;
    for(std::size_t i=0;i<ambientN.size();i+=4)maximumAmbientN=std::max({maximumAmbientN,ambientN[i],ambientN[i+1]});
    for(std::size_t i=0;i<ambientG.size();i+=4)minimumAmbientG=std::min({minimumAmbientG,ambientG[i+1],ambientG[i+2]});
    Check(maximumAmbientN>1e-6f&&minimumAmbientG<.95f,"Ambient-only IBL lost real near optical depth or opaque blockers");
    volume.Reset();
    // 3,280 individually tiny injections into a single physical cell exercise
    // actual framebuffer accumulation, rather than merely uploading FP32 data.
    constexpr unsigned injectionCount=3280;constexpr float radius=.00000125f;
    std::vector<StrandVertex> finePoints;std::vector<std::uint32_t> fineLines;
    finePoints.reserve(injectionCount*2);fineLines.reserve(injectionCount*2);
    for(unsigned i=0;i<injectionCount;++i){finePoints.push_back({{0,-.02f,0,radius},{0,1,0,0}});finePoints.push_back({{0,.02f,0,radius},{0,1,0,1}});fineLines.push_back(2*i);fineLines.push_back(2*i+1);}
    StrandMesh fineGeometry(device);Check(fineGeometry.Update(finePoints,fineLines),"Create fine density injection fixture");
    item=StrandRenderItem{};item.geometry=fineGeometry.Snapshot();item.material.spatialVolume=true;
    item.material.volumeVoxelSize=.1f;item.material.volumeResolution=8;item.material.volumeRasterWidthScale=1;
    view=PipelineDetail::StrandPassView{};view.lighting.lightColors.fill(glm::vec4(0));view.lighting.ambientAndExposure=glm::vec4(0);
    const auto precisionStats=prepare();const auto precisionDensity=Read(device,volume.DebugTextures().density);
    const double precisionExpected=double(injectionCount)*double(.04f)*(2.*double(radius));
    const double precisionError=std::abs(Area(precisionDensity,precisionStats.effectiveVoxelSize)/precisionExpected-1.);
    unsigned occupied=0;for(std::size_t i=0;i<precisionDensity.size();i+=4)occupied+=precisionDensity[i]>0.f;
    Check(occupied==1&&precisionError<.0005,"FP32 framebuffer lost tiny additive density contributions");
    volume.Reset();
    std::cout<<"Fiber near-shadow: requested physical bias="<<physicalBias[0]<<"m at actual h="<<actualVoxel[0]<<"/"<<actualVoxel[1]
        <<"m; analytic slab max error="<<maximumSlabError<<", projected-area max relative error="<<maximumAreaError
        <<", 3280 tiny injections relative error="<<precisionError<<", ambient-only max N="<<maximumAmbientN<<", min G visibility="<<minimumAmbientG
        <<"; legacy identity, invariant density, FP32 accumulation/FP16 paths and independent ambient N/G passed\n";
}
} // namespace Render::TestData
