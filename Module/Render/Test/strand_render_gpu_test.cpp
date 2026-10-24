#include <fstream>
#include <filesystem>
#include "Render/Private/Pipeline/source_display_pass.h"
#include "Render/Test/native_direct_fixture.h"
#include "Render/Test/native_moment_fixture.h"
#include "Render/Test/fiber_volume_sampling_test.h"
#include "Render/Test/fiber_near_shadow_test.h"
#include "Render/Test/strand_spatial_ibl_test.h"
#include "Render/Test/hair_temporal_test.h"
#include "Render/Test/hair_temporal_rejection_test.h"
#include "Render/Test/fiber_volume_exact_test.h"
#include "Render/Test/fiber_volume_proxy_test.h"
#include "Render/Test/fiber_volume_stable_cache_test.h"
#include "Render/Test/fiber_point_paths_test.h"
#include "Render/Test/strand_shutter_test.h"
#include "Render/Private/Pipeline/strand_shadow_pass.h"
#include "Render/Private/Pipeline/strand_motion_shaders.h"
#include "ApplicationWindow/public/window.h"
#include "Render/Private/Pipeline/pipeline_resource_builder.h"
#include "Render/Private/Pipeline/strand_render_pass.h"
#include "Render/Private/Pipeline/fiber_scattering_shaders.h"
#include "Render/Private/Pipeline/strand_ntt_shaders.h"
#include "Render/Private/Pipeline/strand_indirect_shaders.h"
#include "Render/Test/native_ntt_fixture_rows.h"
#include "Render/Private/Pipeline/post_process_shaders.h"
#include "Render/Public/Pipeline/material_render_pipeline.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <future>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/ext/matrix_clip_space.hpp>

#if defined(_WIN32)
// Match the editor's adapter preference; user/application GPU policy keeps
// priority. This also exercises NVIDIA's stricter desktop GLSL overload rules.
extern "C" { __declspec(dllexport) unsigned long NvOptimusEnablement=1; }
#endif

using namespace Render;
namespace {
constexpr uint32_t Size=64;
void Require(bool value,const std::string& message) { if(!value)throw std::runtime_error(message); }
template<class F> auto OnThread(RHIDevice& device,F&& operation) {
    using T=std::invoke_result_t<F>;
    auto completion=std::make_shared<std::promise<T>>(); auto future=completion->get_future();
    device.async_ExecuteCode([completion,operation=std::forward<F>(operation)]() mutable {
        try {
            if constexpr(std::is_void_v<T>) { operation();completion->set_value(); }
            else completion->set_value(operation());
        }catch(...){completion->set_exception(std::current_exception());}
    });
    return future.get();
}
void Submit(RHIDevice& device,RHIFrameEncoder& encoder) {
    Require(encoder.End(false),"End strand test frame");
    auto completed=std::make_shared<bool>(false);
    device.async_SubmitFrameCommands(encoder.GetCommandBuffer(),[completed]{*completed=true;});
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
    while(!*completed) {
        device.returnSystem.DrainCallbacks();
        Require(std::chrono::steady_clock::now()<deadline,"Strand GPU frame timeout");
        if(!*completed)device.returnSystem.WaitForCallbacks(std::chrono::milliseconds(2));
    }
}
float MaximumDifference(const std::vector<float>& a,const std::vector<float>& b) {
    Require(a.size()==b.size(),"Strand readback dimensions");float difference=0;
    for(size_t i=0;i<a.size();++i)difference=std::max(difference,std::abs(a[i]-b[i]));
    return difference;
}
} // namespace
int main(int argc,char** argv) {
    try {
        bool vulkan=false,validation=false,curveOnly=false;
        for(int i=1;i<argc;++i){vulkan|=std::string(argv[i])=="--vulkan";validation|=std::string(argv[i])=="--validation";curveOnly|=std::string(argv[i])=="--curve-only";}
        if(vulkan)Require(RegisterVulkanBackend(),"Vulkan backend unavailable");
        Require(glfwInit(),"GLFW initialization");glfwWindowHint(GLFW_VISIBLE,GLFW_FALSE);
        ApplicationWindow::Window window({Size,Size,"RHI strand ribbon contract",false,vulkan?BackendType::Vulkan:BackendType::Opengl});
        Require(window.Activate(),"Strand test window");
        RHIDevice device;
        if(vulkan){auto context=*window.GetRenderContextAsVulkan();context.validation=validation;context.hardwareRayTracing=false;device.Run(BackendType::Vulkan,context);}
        else device.Run(BackendType::Opengl,*window.GetRenderContextAsOpengl());
        {
            #include "Render/Test/strand_curve_test.inl"
            if(!curveOnly){
            TestData::RunSourceDirectGpuOracle(device);
            TestData::RunNativeMomentGpuOracle(device);
            TestData::RunFiberVolumeSamplingGpuOracle(device);
            TestData::RunFiberNearShadowGpuOracle(device);
            TestData::RunStrandSpatialIblGpuOracle(device);
            TestData::RunHairTemporalGpuOracle(device);
            TestData::RunHairTemporalRejectionGpuOracle(device);
            TestData::RunFiberVolumeExactGpuOracle(device);
            TestData::RunFiberVolumeProxyGpuOracle(device);
            TestData::RunFiberVolumeStableCacheGpuOracle(device);
            TestData::RunFiberPointPathsGpuOracle(device);
            TestData::RunStrandShutterGpuOracle(device);
            #include "Render/Test/strand_paired_transparency_test.inl"
            #include "Render/Test/strand_debug_test.inl"
            #include "Render/Test/strand_segment_test.inl"
            #include "Render/Test/strand_source_light_test.inl"
            #include "Render/Test/source_display_test.inl"
            PipelineDetail::ResourceBuilder builder(device);
            auto color=builder.Texture(Size,Size,RHITextureFormat::RGBA16F);
            auto depth=builder.Texture(Size,Size,RHITextureFormat::Depth32F);
            auto target=builder.Target(color,depth);
            auto planeMesh=builder.FullscreenMesh();
            auto plane=builder.FullscreenPipeline(R"GLSL(#version 450 core
layout(location=0) in vec2 p;void main(){gl_Position=vec4(p,0,1);}
)GLSL",R"GLSL(#version 450 core
layout(location=0) out vec4 color;void main(){color=vec4(.025,.025,.025,1);}
)GLSL",true);
            const auto opaqueDepth=builder.Texture(Size,Size,RHITextureFormat::Depth32F);
            const auto opaqueDepthTarget=builder.Target({},opaqueDepth);
            const auto copyOpaqueDepth=builder.FullscreenPipeline(FullscreenVertexShader(),R"GLSL(#version 450 core
layout(binding=0) uniform sampler2D sourceDepth;
void main(){gl_FragDepth=texelFetch(sourceDepth,ivec2(gl_FragCoord.xy),0).r;}
)GLSL",true);
            auto owner=builder.Finish();
            StrandMesh hair(device),second(device);
            std::array<StrandVertex,4> points{{
                {{-.3f,-.8f,.4f,.06f},{0,1,0,0}},{{-.3f,.8f,.4f,.06f},{0,1,0,1}},
                {{.3f,-.8f,-.4f,.06f},{0,1,0,0}},{{.3f,.8f,-.4f,.06f},{0,1,0,1}}
            }};
            const std::array<uint32_t,4> lines{0,1,2,3};
            Require(hair.Update(points,lines),hair.LastError());
            auto snapshot=hair.Snapshot();
            Require(snapshot&&snapshot->PointCount()==4&&snapshot->IndexCount()==12,"Line to triangle topology conversion");
            OnThread(device,[&]{auto* mesh=device.GetResourcePool().vertexBufferTable.Get(snapshot->Mesh());
                Require(mesh&&mesh->vertexCount==8&&mesh->indexCount==12,"RHI ribbon vertex/index layout");});
            auto moved=points;moved[0].positionRadius.y+=.01f;
            Require(hair.Update(moved,lines),hair.LastError());
            Require(hair.Snapshot()->Mesh()!=snapshot->Mesh(),"In-flight geometry snapshot was mutated");
            Require(!hair.Update(points,std::span<const uint32_t>(lines.data(),3)),"Odd line-list indices accepted");
            PipelineDetail::StrandRenderPass pass(device);
            PipelineDetail::StrandPassTarget passTarget{target,color,depth,Size,Size,1};
            PipelineDetail::StrandPassView view;view.cameraPosition={0,0,2};
            const auto draw=[&](std::vector<StrandRenderItem> items) {
                auto encoder=device.BeginFrame({0,Size,Size});
                Require(encoder.KeepAlive(owner),"Pin strand test target");
                Require(encoder.SetRenderTarget({target,Size,Size,RHICommand::ClearColor|RHICommand::ClearDepth,glm::vec4(.025,.025,.025,1)}),"Clear strand target");
                Require(encoder.BindPipeline(plane)&&encoder.BindMesh(planeMesh)&&encoder.DrawIndexed({3}),"Opaque depth occluder");
                view.opaqueDepth={};
                if(view.frontLayerTransparency||view.outputTemporalData||view.outputMotionData||std::any_of(items.begin(),items.end(),[](const auto& v){return v.material.volumeOpaqueBlockers;})) {
                    // Bind the independent destination before sampling the
                    // scene depth: it is still an active attachment above.
                    Require(encoder.SetRenderTarget({opaqueDepthTarget,Size,Size,RHICommand::ClearDepth})&&
                        encoder.BindPipeline(copyOpaqueDepth)&&encoder.BindTexture(depth,0)&&
                        encoder.BindMesh(planeMesh)&&encoder.DrawIndexed({3}),"Capture independent opaque depth for front hair layers");
                    view.opaqueDepth=opaqueDepth;
                }
                pass.Record(encoder,items,passTarget,view);Submit(device,encoder);
                return OnThread(device,[&]{return device.BackendDiagnostics()->ReadTexture(color);});
            };
            {
                PipelineDetail::StrandShadowPass caster(device);
                StrandMesh shadowMesh(device);
                const std::array<StrandVertex,2> shadowPoints{{{{0,-.8f,-.4f,.08f},{0,1,0,0}},{{0,.8f,-.4f,.08f},{0,1,0,1}}}};
                const std::array<std::uint32_t,2> shadowSegment{0,1};Require(shadowMesh.Update(shadowPoints,shadowSegment),shadowMesh.LastError());
                StrandRenderItem shadowItem;shadowItem.geometry=shadowMesh.Snapshot();shadowItem.material.castDirectionalShadows=true;
                const auto shadowCoverage=[&](bool enabled,float opacity){
                    shadowItem.material.castDirectionalShadows=enabled;shadowItem.material.opacity=opacity;
                    auto encoder=device.BeginFrame({0,Size,Size});Require(encoder.KeepAlive(owner),"Pin caster target");
                    Require(encoder.SetRenderTarget({target,Size,Size,RHICommand::ClearDepth}),"Clear strand shadow depth");
                    const std::array<StrandRenderItem,1> casters{shadowItem};
                    const auto draws=caster.Record(encoder,casters,glm::mat4(1),{0,0,1},Size);
                    Require(draws==std::uint32_t(enabled),"Strand shadow draw accounting");Submit(device,encoder);
                    return OnThread(device,[&]{return device.BackendDiagnostics()->ReadTexture(depth);});
                };
                const auto disabledShadow=shadowCoverage(false,1),partialShadow=shadowCoverage(true,.25f),fullShadow=shadowCoverage(true,1);
                std::uint32_t disabledPixels=0,partialPixels=0,fullPixels=0;
                for(std::size_t i=0;i<fullShadow.size();++i){disabledPixels+=disabledShadow[i]<.9f;partialPixels+=partialShadow[i]<.9f;fullPixels+=fullShadow[i]<.9f;}
                Require(disabledPixels==0&&partialPixels>0&&fullPixels>partialPixels*2,"Strand CSM caster did not preserve coverage ordering or disable identity");
                std::cout<<"Strand CSM coverage adapter: partial="<<partialPixels<<", full="<<fullPixels<<", disabled=0 passed; ordered 8x8 coverage, not deep opacity\n";
            }
            #include "Render/Test/strand_blocker_test.inl"
            #include "Render/Test/strand_motion_test.inl"
            StrandRenderItem item;item.geometry=snapshot;item.guides=true;
            item.material.rootColor=item.material.tipColor={.8f,.12f,.035f};item.material.opacity=.8f;
            const auto baseline=draw({});const auto visible=draw({item});
            Require(visible.size()==Size*Size*4,"Strand float texture readback");
            float hiddenDifference=0,visibleDifference=0;
            for(uint32_t y=12;y<52;++y)for(uint32_t x=17;x<26;++x)
                hiddenDifference=std::max(hiddenDifference,std::abs(visible[(y*Size+x)*4]-baseline[(y*Size+x)*4]));
            for(uint32_t y=12;y<52;++y)for(uint32_t x=38;x<47;++x)
                visibleDifference=std::max(visibleDifference,std::abs(visible[(y*Size+x)*4]-baseline[(y*Size+x)*4]));
            Require(hiddenDifference<.001f,"Behind-skin strands ignored opaque depth");
            Require(visibleDifference>.05f,"Front strands did not cover the HDR target");
            std::array<StrandVertex,2> overlap{{{{0,-.8f,-.4f,.08f},{0,1,0,0}},{{0,.8f,-.4f,.08f},{0,1,0,1}}}};
            const std::array<uint32_t,2> segment{0,1};
            Require(second.Update(overlap,segment),second.LastError());
            auto red=item;red.geometry=second.Snapshot();red.material.rootColor=red.material.tipColor={.7f,.03f,.02f};
            auto green=red;green.material.rootColor=green.material.tipColor={.02f,.6f,.04f};green.model[3].z=.1f;
            const auto forward=draw({red,green}),reverse=draw({green,red});
            Require(MaximumDifference(forward,reverse)<.003f,"Weighted strand transparency depends on draw order");
            {
                // Independent geometric oracle: intersect a physical stripe
                // with each unit pixel interval, rather than restating the
                // fragment shader's side/derivative coverage expression.
                StrandMesh coverageMesh(device);StrandRenderItem probe;
                probe.guides=true;probe.material.rootColor=probe.material.tipColor={1,0,0};
                probe.material.opacity=.6f;probe.material.maxPixelWidth=12;
                probe.material.areaPreservingCoverage=true;
                constexpr std::uint32_t row=Size/2;
                const auto stripe=[&](float width,float phase,float opacity) {
                    std::array<float,Size> result{};
                    const double center=.5*Size+phase,first=center-.5*width,last=center+.5*width;
                    for(std::uint32_t x=0;x<Size;++x)
                        result[x]=float(std::max(0.0,std::min(double(x+1),last)-std::max(double(x),first))*opacity);
                    return result;
                };
                const auto extract=[&](const std::vector<float>& image) {
                    std::array<float,Size> result{};
                    for(std::uint32_t x=0;x<Size;++x) {
                        const auto i=(row*Size+x)*4;
                        result[x]=(image[i]-baseline[i])/(1.f-baseline[i]);
                        Require(std::isfinite(result[x])&&result[x]>=-.001f&&result[x]<=1.001f,"Invalid geometric fiber coverage readback");
                    }
                    return result;
                };
                const auto total=[](const auto& values) {double sum=0;for(float value:values)sum+=value;return float(sum);};
                const auto configure=[&](float width,float phase,float opacity,float minimum=1.f,float scale=1.f,bool area=true,float z=-.4f) {
                    auto line=overlap;
                    for(auto& point:line){point.positionRadius.x=2.f*phase/float(Size);point.positionRadius.z=z;point.positionRadius.w=width/float(Size);}
                    Require(coverageMesh.Update(line,segment),coverageMesh.LastError());probe.geometry=coverageMesh.Snapshot();
                    probe.material.opacity=opacity;probe.material.minPixelWidth=minimum;probe.material.widthScale=scale;
                    probe.material.areaPreservingCoverage=area;probe.material.minWidthMetres=probe.material.maxWidthMetres=0;
                    probe.strandCountFraction=1;
                };
                const auto nearArea=[&](float actual,float expected,const char* message) {
                    if(std::abs(actual-expected)>std::max(.01f,.03f*expected))
                        std::cerr<<message<<" actual="<<actual<<" expected="<<expected<<'\n';
                    Require(std::abs(actual-expected)<=std::max(.01f,.03f*expected),message);
                };
                float maximumAreaError=0,maximumPixelError=0;
                for(float width:{.125f,.25f,.5f,.8f,1.f,2.f,4.f})for(float phase:{0.f,.125f,.25f,.375f,.5f,.625f,.75f,.875f}) {
                    configure(width,phase,.6f);
                    const auto measured=extract(draw({probe})),expected=stripe(width,phase,.6f);
                    const float sum=total(measured);maximumAreaError=std::max(maximumAreaError,std::abs(sum-width*.6f));
                    nearArea(sum,width*.6f,"Subpixel fiber coverage failed physical-width * opacity area conservation");
                    for(std::uint32_t x=0;x<Size;++x) {
                        maximumPixelError=std::max(maximumPixelError,std::abs(measured[x]-expected[x]));
                        Require(std::abs(measured[x]-expected[x])<=.006f,"Fiber coverage disagrees with physical stripe/pixel intersection");
                    }
                }
                for(float minimum:{1.f,2.f,4.f})for(float phase:{0.f,.25f,.5f,.75f}) {
                    configure(.25f,phase,.6f,minimum);
                    nearArea(total(extract(draw({probe}))),.15f,"Minimum raster footprint changed physical fiber coverage area");
                }
                for(float opacity:{.25f,.5f,1.f}) {
                    configure(.5f,.375f,opacity);
                    nearArea(total(extract(draw({probe}))),.5f*opacity,"Fiber opacity was applied more than once");
                }
                for(float scale:{.5f,1.f,2.f}) {
                    configure(.5f,.25f,.6f,1,scale);
                    nearArea(total(extract(draw({probe}))),.3f*scale,"Fiber widthScale did not linearly scale integrated coverage");
                }
                for(float fraction:{.5f,.25f}) {
                    configure(.5f,.25f,.6f);probe.strandCountFraction=fraction;
                    nearArea(total(extract(draw({probe}))),.3f/fraction,"Native reciprocal LOD width compensation did not conserve the selected-fiber area");
                }
                // With an identity projection, one local metre spans Size/2
                // pixels. These tests exercise authored full-width bounds in
                // metres, independently of the screen raster footprint.
                configure(.125f,.25f,.6f);probe.material.minWidthMetres=2.f*.5f/float(Size);
                nearArea(total(extract(draw({probe}))),.3f,"Physical minimum full width was not converted from metres before projection");
                configure(2.f,.25f,.6f);probe.material.maxWidthMetres=2.f*.5f/float(Size);
                nearArea(total(extract(draw({probe}))),.3f,"Physical maximum full width was not converted from metres before projection");
                configure(.125f,.25f,.6f);probe.material.minWidthMetres=2.f*.5f/float(Size);
                probe.material.maxWidthMetres=2.f/float(Size);probe.strandCountFraction=.5f;
                nearArea(total(extract(draw({probe}))),.6f,"Native reciprocal LOD compensation did not also scale physical width bounds");
                configure(.5f,.25f,.6f);
                for(int invalid=0;invalid<7;++invalid) {
                    auto invalidProbe=probe;
                    if(invalid==0)invalidProbe.strandCountFraction=-.01f;
                    if(invalid==1)invalidProbe.strandCountFraction=1.01f;
                    if(invalid==2)invalidProbe.strandCountFraction=std::numeric_limits<float>::quiet_NaN();
                    if(invalid==3)invalidProbe.material.minWidthMetres=-.001f;
                    if(invalid==4)invalidProbe.material.maxWidthMetres=-.001f;
                    if(invalid==5){invalidProbe.material.minWidthMetres=.02f;invalidProbe.material.maxWidthMetres=.01f;}
                    if(invalid==6)invalidProbe.material.maxWidthMetres=std::numeric_limits<float>::infinity();
                    auto rejected=device.BeginFrame({0,Size,Size});bool failed=false;
                    try{pass.Record(rejected,std::span<const StrandRenderItem>(&invalidProbe,1),passTarget,view);}catch(const std::invalid_argument&){failed=true;}
                    Require(failed&&rejected.Cancel(),"Invalid native LOD fraction or physical full-width bounds were accepted");
                }
                float legacyMean=0;
                for(float phase:{0.f,.125f,.25f,.375f,.5f,.625f,.75f,.875f}) {
                    configure(.25f,phase,.6f,1,1,false);legacyMean+=total(extract(draw({probe})))/8.f;
                }
                Require(legacyMean<.15f*.4f,"Legacy one-pixel inward feather counterexample no longer reproduces its known area loss");
                configure(0,.5f,.6f);
                Require(total(extract(draw({probe})))<.001f,"Zero-radius area coverage produced a visible fiber");
                configure(.5f,.375f,.6f,1,1,true,.4f);
                Require(MaximumDifference(draw({probe}),baseline)<.001f,"Area-preserving hair ignored opaque scene depth");
                configure(.8f,.25f,.5f);
                auto secondLayer=probe;secondLayer.model[3].z=.1f;
                const auto product=extract(draw({probe,secondLayer})),oneLayer=stripe(.8f,.25f,.5f);
                float productArea=0;
                for(std::uint32_t x=0;x<Size;++x) {
                    const float expected=1.f-(1.f-oneLayer[x])*(1.f-oneLayer[x]);productArea+=expected;
                    Require(std::abs(product[x]-expected)<.006f,"Overlapping physical stripes lost the independent transmittance product");
                }
                nearArea(total(product),productArea,"Overlapping physical fiber coverage changed union area");
                std::cout<<"Physical fiber coverage: widths=.125/.25/.5/.8/1/2/4px, 8 pixel phases, maxAreaError="<<maximumAreaError
                    <<", maxPixelError="<<maximumPixelError<<", legacy .25px opacity.6 meanArea="<<legacyMean
                    <<" vs physical .15; minimum widths, opacity once, widthScale, native LOD/metre bounds, invalid inputs, opaque depth and overlap product passed\n";
                {
                    // Independent 2D oracle: clip the actual unit pixel square
                    // against both physical strip half-planes and measure the
                    // polygon area. This does not copy the shader's CDF kernel.
                    const auto squareStripArea=[](std::uint32_t x,std::uint32_t y,glm::dvec2 normal,glm::dvec2 center,double width) {
                        const double left=double(x),bottom=double(y);
                        std::vector<glm::dvec2> polygon{{left,bottom},{left+1,bottom},{left+1,bottom+1},{left,bottom+1}};
                        for(double sign:{-1.0,1.0}) {
                            std::vector<glm::dvec2> clipped;
                            if(polygon.empty())break;
                            auto previous=polygon.back();double previousDistance=sign*glm::dot(normal,previous-center)-.5*width;
                            for(auto current:polygon) {
                                const double distance=sign*glm::dot(normal,current-center)-.5*width;
                                const bool wasInside=previousDistance<=0,isInside=distance<=0;
                                if(wasInside!=isInside)clipped.push_back(previous+(current-previous)*(previousDistance/(previousDistance-distance)));
                                if(isInside)clipped.push_back(current);
                                previous=current;previousDistance=distance;
                            }
                            polygon=std::move(clipped);
                        }
                        double twiceArea=0;
                        for(std::size_t i=0;i<polygon.size();++i) {
                            const auto& a=polygon[i];const auto& b=polygon[(i+1)%polygon.size()];twiceArea+=a.x*b.y-a.y*b.x;
                        }
                        return std::clamp(.5*std::abs(twiceArea),0.0,1.0);
                    };
                    StrandMesh diagonalMesh(device);StrandRenderItem diagonal;
                    diagonal.guides=true;diagonal.material.rootColor=diagonal.material.tipColor={1,0,0};
                    diagonal.material.opacity=.6f;diagonal.material.areaPreservingCoverage=true;
                    const auto savedCamera=view.cameraPosition;
                    float largestPixelError=0;double largestAreaError=0;
                    constexpr std::uint32_t roiFirst=16,roiLast=48;
                    for(float slope:{1.f,.5f}) {
                        const glm::vec2 direction=glm::normalize(glm::vec2(1,slope)),normal(-direction.y,direction.x);
                        const auto normalDouble=glm::normalize(glm::dvec2(normal));
                        for(float width:{.125f,.25f,.5f,.8f,1.f,2.f,4.f})
                            for(float phase:{0.f,.125f,.2236068f,.25f,.375f,.5f,.75f}) {
                                // Endpoints lie well outside the middle ROI,
                                // so the oracle can use an infinite strip.
                                const glm::vec2 offset=normal*(2.f*phase/float(Size));
                                std::array<StrandVertex,2> line{};
                                for(int endpoint=0;endpoint<2;++endpoint) {
                                    const auto p=offset+direction*(endpoint?1.5f:-1.5f);
                                    line[endpoint].positionRadius={p.x,p.y,-.4f,width/float(Size)};
                                    line[endpoint].tangentArc={direction.x,direction.y,0,float(endpoint)};
                                }
                                Require(diagonalMesh.Update(line,segment),diagonalMesh.LastError());diagonal.geometry=diagonalMesh.Snapshot();
                                // Match the camera's transverse translation to
                                // keep billboard width exactly in the XY plane.
                                view.cameraPosition={offset.x,offset.y,savedCamera.z};
                                const auto image=draw({diagonal});double actualArea=0,expectedArea=0;
                                const glm::dvec2 center=glm::dvec2(.5*Size)+normalDouble*double(phase);
                                for(std::uint32_t y=roiFirst;y<roiLast;++y)for(std::uint32_t x=roiFirst;x<roiLast;++x) {
                                    const auto i=(y*Size+x)*4;
                                    const float actual=(image[i]-baseline[i])/(1.f-baseline[i]);
                                    const float expected=float(squareStripArea(x,y,normalDouble,center,width)*.6);
                                    const float error=std::abs(actual-expected);largestPixelError=std::max(largestPixelError,error);
                                    if(!std::isfinite(actual)||error>.006f)
                                        std::cerr<<"Oblique pixel coverage slope="<<slope<<" width="<<width<<" phase="<<phase<<" pixel="<<x<<','<<y
                                            <<" actual="<<actual<<" polygon="<<expected<<'\n';
                                    Require(std::isfinite(actual)&&error<=.006f,"Oblique hair alpha disagrees with pixel-square/physical-strip polygon area");
                                    actualArea+=actual;expectedArea+=expected;
                                }
                                const double error=std::abs(actualArea-expectedArea);largestAreaError=std::max(largestAreaError,error);
                                Require(error<=std::max(.015,.02*expectedArea),"Oblique hair ROI lost integrated pixel-square coverage area");
                            }
                    }
                    view.cameraPosition=savedCamera;
                    std::cout<<"Oblique physical coverage: slopes 1 and .5, 7 widths x 7 normal phases, independent polygon-square clipping ROI [16,48), maxPixelError="
                        <<largestPixelError<<", maxAreaError="<<largestAreaError<<" passed; host pixel-square adapter, not native Wu equivalence\n";
                }
            }
            {
                // Sorted-color oracle is independent of the peel shader. At
                // the center of a wide guide ribbon, coverage is one and its
                // RGB is exactly the authored constant, isolating compositing
                // from both strand lighting and antialiasing.
                const auto pixel=[&](const std::vector<float>& image) {
                    const auto i=((Size/2)*Size+Size/2)*4;
                    return glm::vec3(image[i],image[i+1],image[i+2]);
                };
                const auto compare=[&](glm::vec3 actual,glm::vec3 expected,const char* message) {
                    if(glm::any(glm::greaterThan(glm::abs(actual-expected),glm::vec3(.004f))))
                        std::cerr<<message<<" actual="<<actual.x<<','<<actual.y<<','<<actual.z
                            <<" expected="<<expected.x<<','<<expected.y<<','<<expected.z<<'\n';
                    Require(glm::all(glm::lessThanEqual(glm::abs(actual-expected),glm::vec3(.004f))),message);
                };
                const auto layer=[&](glm::vec3 color,float opacity,float translationZ) {
                    auto result=red;result.material.rootColor=result.material.tipColor=color;
                    result.material.opacity=opacity;result.model=glm::mat4(1);result.model[3].z=translationZ;
                    result.material.areaPreservingCoverage=true;return result;
                };
                const auto sourceColor=[](const StrandRenderItem& value){return value.material.rootColor;};
                const glm::vec3 background=pixel(baseline);
                const auto sorted=[&](const std::vector<StrandRenderItem>& layers) {
                    glm::vec3 result(0);float transmission=1;
                    for(const auto& value:layers){result+=transmission*value.material.opacity*sourceColor(value);transmission*=1-value.material.opacity;}
                    return result+transmission*background;
                };
                const auto weight=[](float alpha){return std::clamp(4.f*alpha+.01f,.01f,4.f);};
                const auto weighted=[&](const std::vector<StrandRenderItem>& layers) {
                    glm::vec3 color(0);float denominator=0,transmission=1;
                    // All chosen NDC depths are in front of z=0, where the
                    // production depth weight is clamped to one on GL and VK.
                    for(const auto& value:layers) {
                        const float alpha=value.material.opacity,w=alpha*weight(alpha);
                        color+=sourceColor(value)*w;denominator+=w;transmission*=1-alpha;
                    }
                    return (denominator>0?color/denominator:glm::vec3(0))*(1-transmission)+background*transmission;
                };
                const auto dark=layer({.1f,.08f,.04f},.95f,-.35f),bright=layer({1,1,1},.95f,.15f);
                view.frontLayerTransparency=false;
                const auto original=draw({dark,bright});compare(pixel(original),weighted({dark,bright}),"Original WBOIT color baseline changed");
                view.frontLayerTransparency=true;
                const auto peeled=draw({dark,bright});compare(pixel(peeled),sorted({dark,bright}),"Front dark strand did not occlude the bright rear strand with ordered alpha");
                Require(pixel(peeled).r<pixel(original).r*.5f,"Front layers did not remove the WBOIT bright-rear color washout");
                Require(MaximumDifference(peeled,draw({bright,dark}))<.004f,"Front-layer transparency depends on submission order");
                const auto onlyFront=draw({dark});compare(pixel(onlyFront),sorted({dark}),"Single front layer changed its alpha/color convention");
                Require(MaximumDifference(draw({}),baseline)<.001f,"Empty front-layer draw changed the opaque target");
                auto transparent=bright;transparent.material.opacity=0;
                Require(MaximumDifference(draw({transparent}),baseline)<.001f,"Zero-alpha front layer changed the opaque target");
                Require(MaximumDifference(draw({transparent,dark}),onlyFront)<.004f,"Empty peel layer changed a visible front layer");
                auto hidden=bright;hidden.model[3].z=1;
                Require(MaximumDifference(draw({hidden}),baseline)<.001f,"Front-layer path ignored the captured opaque depth");
                Require(MaximumDifference(draw({hidden,dark}),onlyFront)<.004f,"Opaque-occluded tail changed visible front hair");
                const auto first=layer({.12f,.02f,.01f},.35f,-.35f),secondLayer=layer({.05f,.4f,.1f},.4f,-.05f);
                const auto third=layer({.02f,.1f,1},.8f,.15f),fourth=layer({1,.6f,.01f},.55f,.35f);
                const auto three=draw({first,secondLayer,third});compare(pixel(three),sorted({first,secondLayer,third}),"Two front layers plus a single tail layer were not exactly ordered");
                Require(MaximumDifference(three,draw({third,first,secondLayer}))<.004f,"Three-layer peel/tail color depends on submission order");
                const auto four=draw({fourth,secondLayer,first,third});
                const float t0=1-first.material.opacity,t1=1-secondLayer.material.opacity;
                const auto hybrid=sourceColor(first)*first.material.opacity+t0*sourceColor(secondLayer)*secondLayer.material.opacity+
                    t0*t1*weighted({third,fourth});
                compare(pixel(four),hybrid,"Two sorted front layers did not preserve the weighted blended tail");
                Require(MaximumDifference(four,draw({third,first,fourth,secondLayer}))<.004f,"Four-layer hybrid transparency depends on submission order");
                // Coplanar fragments form a weighted-color group, preserving
                // its product transmittance without inventing a front/back
                // order among fragments whose depth is identical.
                for(std::size_t count:{std::size_t(2),std::size_t(5)}) {
                    const auto equal=layer({.7f,.2f,.1f},.2f,-.35f);
                    std::vector<StrandRenderItem> ties(count,equal);
                    const auto image=draw(ties);const float expectedAlpha=1.f-std::pow(.8f,float(count));
                    compare(pixel(image),sorted(ties),"Coplanar group lost same-color product-transmittance coverage");
                    const float actualAlpha=(pixel(image).r-background.r)/(.7f-background.r);
                    Require(std::abs(actualAlpha-expectedAlpha)<.003f,"Coplanar group alpha did not equal 1-(1-alpha)^count");
                    std::reverse(ties.begin(),ties.end());
                    Require(MaximumDifference(image,draw(ties))<.004f,"Same-color coplanar group depends on submission order");
                    if(count==5) {
                        ties.push_back(bright);const auto groupedThenRear=draw(ties);
                        compare(pixel(groupedThenRear),sorted(ties),"Five coplanar front fibers did not occlude a separate rear depth group");
                        std::reverse(ties.begin(),ties.end());
                        Require(MaximumDifference(groupedThenRear,draw(ties))<.004f,"Two distinct depth groups depend on submission order");
                    }
                }
                auto tiedDark=dark,tiedBright=bright;tiedDark.material.opacity=.2f;tiedBright.material.opacity=.35f;tiedBright.model=tiedDark.model;
                const auto mixed=draw({tiedDark,tiedBright});const auto mixedOracle=weighted({tiedDark,tiedBright});
                compare(pixel(mixed),mixedOracle,"Mixed-color coplanar group did not retain weighted color and product transmittance");
                const float mixedAlpha=1.f-(1.f-tiedDark.material.opacity)*(1.f-tiedBright.material.opacity);
                const auto groupColor=(mixedOracle-background*(1-mixedAlpha))/mixedAlpha;
                Require(std::abs((pixel(mixed).r-background.r)/(groupColor.r-background.r)-mixedAlpha)<.003f,
                    "Mixed-color coplanar group lost union coverage");
                Require(MaximumDifference(mixed,draw({tiedBright,tiedDark}))<.004f,"Mixed-color coplanar group depends on submission order");
                // Additional depth groups are tested against front-to-back over,
                // using distinct full-coverage constant-color fibers.
                for(std::uint32_t groups:{3u,7u}) {
                    view.frontDepthGroups=groups;
                    std::vector<StrandRenderItem> ordered;
                    for(std::uint32_t i=0;i<=groups;++i)
                        ordered.push_back(layer(glm::vec3(.09f+.07f*i,.4f-.025f*i,.05f+.03f*i),.35f,-.48f+.045f*i));
                    compare(pixel(draw(ordered)),sorted(ordered),"Extended depth groups disagree with analytic over");
                    Require(pass.PassCount()==4*groups+3&&pass.FrontDepthGroups()==groups,"Depth-group cost reporting");
                    std::reverse(ordered.begin(),ordered.end());
                    const auto reversed=draw(ordered);std::reverse(ordered.begin(),ordered.end());
                    Require(MaximumDifference(reversed,draw(ordered))<.004f,"Extended groups depend on draw order");
                }
                view.frontDepthGroups=3;view.outputTemporalData=true;
                auto depthProbe=dark;depthProbe.material.opacity=.90f;
                const auto low=draw({depthProbe});
                const auto readTemporal=[&]{
                    const auto outputs=pass.TemporalTextures();Require(outputs.coverage.IsValid()&&outputs.compositeDepth.IsValid(),"Missing independent hair temporal outputs");
                    return OnThread(device,[&]{return std::pair{device.BackendDiagnostics()->ReadTexture(outputs.coverage),device.BackendDiagnostics()->ReadTexture(outputs.compositeDepth)};});
                };
                auto [lowCoverage,lowDepth]=readTemporal();const auto center=(Size/2)*Size+Size/2;
                Require(std::abs(lowCoverage[4*center]-.90f)<.003f&&lowCoverage[4*center+1]<.01f&&std::abs(lowDepth[center]-.5f)<1e-5f,"Low hair coverage incorrectly merged depth");
                depthProbe.material.opacity=.98f;draw({depthProbe});auto [highCoverage,highDepth]=readTemporal();
                Require(highCoverage[4*center+1]>.99f&&std::abs(highDepth[center]-.125f)<1e-5f,"High hair coverage did not merge nearest fiber depth");
                view.outputTemporalData=false;const auto noTemporal=draw({depthProbe});
                view.outputTemporalData=true;Require(MaximumDifference(noTemporal,draw({depthProbe}))<.001f,"Temporal depth changed hair color");
                view.frontLayerTransparency=false;
                const auto weightedDepth=draw({depthProbe});readTemporal();Require(pass.TemporalPassCount()==2,"Weighted hair temporal discovery budget");
                view.outputTemporalData=false;Require(MaximumDifference(weightedDepth,draw({depthProbe}))<.001f,"Weighted temporal depth changed coverage");
                view.frontDepthGroups=2;
                std::cout<<"Extended 3/7 depth groups and .95 high-coverage temporal depth: analytic over, reverse order, both threshold branches, weighted identity passed\n";
                Require(MaximumDifference(draw({dark,bright}),original)<.001f,"Disabling front layers did not restore the original WBOIT path");
                {
                    // The first implementation deliberately peels only
                    // single-sample targets. An actual 4x target must keep the
                    // old multisample path, even without a peel-depth snapshot.
                    PipelineDetail::ResourceBuilder multisample(device);
                    const auto msColor=multisample.Texture(Size,Size,RHITextureFormat::RGBA16F,4);
                    const auto msDepth=multisample.Texture(Size,Size,RHITextureFormat::Depth32F,4);
                    const auto msTarget=multisample.Target(msColor,msDepth);
                    const auto resolved=multisample.Texture(Size,Size,RHITextureFormat::RGBA16F);
                    const auto resolveTarget=multisample.Target(resolved,{});
                    const auto msOwner=multisample.Finish();
                    const PipelineDetail::StrandPassTarget msPassTarget{msTarget,msColor,msDepth,Size,Size,4};
                    const std::array<StrandRenderItem,2> layers{dark,bright};
                    const auto drawMultisample=[&](bool enabled) {
                        view.frontLayerTransparency=enabled;view.opaqueDepth={};
                        auto encoder=device.BeginFrame({0,Size,Size});
                        Require(encoder.KeepAlive(owner)&&encoder.KeepAlive(msOwner),"Pin multisample front-layer fallback targets");
                        Require(encoder.SetRenderTarget({msTarget,Size,Size,RHICommand::ClearColor|RHICommand::ClearDepth,glm::vec4(.025,.025,.025,1)})&&
                            encoder.BindPipeline(plane)&&encoder.BindMesh(planeMesh)&&encoder.DrawIndexed({3}),"Draw multisample opaque baseline");
                        pass.Record(encoder,layers,msPassTarget,view);
                        Require(encoder.ResolveRenderTarget({msTarget,resolveTarget,true,false}),"Resolve multisample front-layer fallback");
                        Submit(device,encoder);return OnThread(device,[&]{return device.BackendDiagnostics()->ReadTexture(resolved);});
                    };
                    const auto msLegacy=drawMultisample(false),msFallback=drawMultisample(true);
                    Require(MaximumDifference(msLegacy,msFallback)<.001f,"Multisample front layers did not fall back to the original WBOIT path");
                    view.frontLayerTransparency=false;view.opaqueDepth={};
                }
                std::cout<<"Front hair layers: dark/bright ordered RGB="<<pixel(peeled).r<<','<<pixel(peeled).g<<','<<pixel(peeled).b
                    <<" vs WBOIT="<<pixel(original).r<<','<<pixel(original).g<<','<<pixel(original).b
                    <<"; single/empty/opaque depth, 3-layer exact and 4-layer weighted tail, coplanar 2/5-fiber coverage and mixed weighted groups, submission order, disabled identity and 4x MSAA fallback passed\n";
            }
            auto nativeNttTable=std::make_shared<StrandTextureTable>();nativeNttTable->width=1;nativeNttTable->height=128;
            for(const auto& rg:TestData::NativeNttRows)nativeNttTable->texels.push_back({rg[0],rg[1],0,1});
            {
                struct alignas(16) CubeAudit {
                    std::array<glm::vec4,6> coefficients{{{1,0,0,0},{0,2,0,0},{0,0,3,0},{4,4,0,0},{5,0,5,0},{0,6,6,0}}};
                    glm::vec4 options{1,1,1,0};
                } cube;
                glm::vec4 direction(1,0,0,0);
                PipelineDetail::ResourceBuilder resources(device);
                const auto output=resources.Texture(4,4,RHITextureFormat::RGBA32F);
                const auto target=resources.Target(output,{});
                const auto cubeBuffer=resources.Uniform(sizeof(cube)),directionBuffer=resources.Uniform(sizeof(direction));
                std::string fragment=R"GLSL(#version 450 core
layout(std140,binding=8) uniform CubeDirection {vec4 auditDirection;};
layout(location=0) out vec4 color;
)GLSL";
                fragment+=PipelineDetail::StrandIndirectGLSL();fragment+=R"GLSL(
void main(){color=vec4(auditDirection.w>.5?StrandIndirectRing(auditDirection.xyz):StrandIndirectDirection(auditDirection.xyz),1.0);}
)GLSL";
                const auto pipeline=resources.FullscreenPipeline(FullscreenVertexShader(),fragment);
                const auto cubeOwner=resources.Finish();
                const auto evaluate=[&](glm::vec4 input) {
                    direction=input;auto encoder=device.BeginFrame({0,4,4});Require(encoder.KeepAlive(cubeOwner),"Pin six-coefficient strand indirect audit");
                    Require(encoder.SetRenderTarget({target,4,4,RHICommand::ClearColor})&&
                        encoder.UpdateUniformBufferBytes(cubeBuffer,&cube,sizeof(cube),0)&&encoder.BindUniformBuffer(cubeBuffer,7)&&
                        encoder.UpdateUniformBufferBytes(directionBuffer,&direction,sizeof(direction),0)&&encoder.BindUniformBuffer(directionBuffer,8)&&
                        encoder.BindPipeline(pipeline)&&encoder.BindMesh(planeMesh)&&encoder.DrawIndexed({3}),"Draw native strand indirect cube audit");
                    Submit(device,encoder);const auto pixels=OnThread(device,[&]{return device.BackendDiagnostics()->ReadTexture(output);});
                    Require(pixels.size()==64,"Strand indirect cube audit readback");for(float value:pixels)Require(std::isfinite(value)&&value>=0,"Nonfinite native strand indirect cube");
                    return glm::vec3(pixels[0],pixels[1],pixels[2]);
                };
                const auto compare=[&](glm::vec3 actual,glm::vec3 expected,const char* message) {
                    Require(glm::all(glm::lessThanEqual(glm::abs(actual-expected),glm::vec3(.0001f))),message);
                };
                for(int axis=0;axis<3;++axis)for(int sign=0;sign<2;++sign) {
                    glm::vec4 d(0);d[axis]=sign?-1.f:1.f;
                    compare(evaluate(d),glm::vec3(cube.coefficients[axis*2+sign]),"Native strand cube lost an axis or signed directional coefficient");
                }
                compare(evaluate({1,2,3,0}),{3.2857142857f,0,4.0714285714f},"Native strand cube source L-squared directional reference");
                compare(evaluate({-1,-2,-3,0}),{1.1428571429f,5.1428571429f,3.8571428571f},"Native strand cube discarded signed/L1 directional asymmetry");
                compare(evaluate({0,1,0,1}),{1.5f,2,2.75f},"Host cylinder ring adapter disagrees with independent hemisphere circle integration");
                compare(evaluate({1,2,3,1}),{1.3928571429f,1.7142857143f,1.5178571429f},"Host cylinder ring second-moment reference");
                cube.options.z=0;cube.options.w=1;
                compare(evaluate({1,0,0,0}),{0,6,6},"Strand cube Y rotation disagrees with world-to-environment direction");
                compare(evaluate({0,0,1,0}),{1,0,0},"Strand cube Y rotation swapped signed axes");
                cube.options.y=2;
                compare(evaluate({0,0,1,0}),{2,0,0},"Strand cube applied an extra pi or material multiplier");
                cube.options.y=0;compare(evaluate({1,2,3,0}),{0,0,0},"Zero strand indirect producer gain failed");
                std::cout<<"Source strand indirect cube: six signed axes, L-squared lookup, rotation, gain and independent cylinder-ring references passed\n";
            }
            {
                // Original embedded native rows are constant across X. This
                // compact 1x128 fixture samples the exact original half values;
                // the Model asset keeps the full original 128x128 table.
                struct alignas(16) NttAudit {glm::vec4 ibl{0,0,0,1},arguments{0,-.5f,.25f,0};} audit;
                PipelineDetail::ResourceBuilder resources(device);
                auto texture=[&](const void* data,uint32_t height) {
                    CreateRHITextureSpec desc;desc.width=1;desc.height=height;
                    desc.textureDataStoreType=desc.textureUseType=RHITextureFormat::RGBA32F;
                    desc.filterMode=RHIFilterMode::Linear;desc.addressMode=RHIAddressMode::ClampToEdge;desc.mipmaps=false;desc.data=data;
                    return resources.Create<RHITextureSpec>([&](auto cb){device.async_CreateTexture(desc,cb);});
                };
                const auto nativeTexture=texture(nativeNttTable->texels.data(),128);
                const glm::vec4 signedValue(.3f,-.5f,0,1);const auto signedTexture=texture(&signedValue,1);
                const auto output=resources.Texture(4,4,RHITextureFormat::RGBA32F);
                const auto outputTarget=resources.Target(output,{});
                const auto buffer=resources.Uniform(sizeof(audit));
                std::string fragment=R"GLSL(#version 450 core
layout(std140,binding=7) uniform NttAuditBlock {vec4 strandIbl,lookupArguments;};
layout(location=0) out vec4 color;
)GLSL";
                fragment+=PipelineDetail::StrandNttGLSL();fragment+=R"GLSL(
void main() {
    vec2 uv=vec2(.5-.5*lookupArguments.x,sqrt(max(lookupArguments.z,.02)));
    vec2 rg=textureLod(strandNttSampler,uv,0.0).rg;
    color=vec4(FiberNttAzimuth(lookupArguments.x,lookupArguments.y,lookupArguments.z),rg,1.0);
}
)GLSL";
                const auto pipeline=resources.FullscreenPipeline(FullscreenVertexShader(),fragment);
                const auto tableOwner=resources.Finish();
                const auto evaluate=[&](RenderResourceHandle<RHITextureSpec> input) {
                    auto encoder=device.BeginFrame({0,4,4});Require(encoder.KeepAlive(tableOwner),"Pin original Ntt lookup audit");
                    Require(encoder.SetRenderTarget({outputTarget,4,4,RHICommand::ClearColor})&&
                        encoder.UpdateUniformBufferBytes(buffer,&audit,sizeof(audit),0)&&encoder.BindUniformBuffer(buffer,7)&&
                        encoder.BindPipeline(pipeline)&&encoder.BindTexture(input,0)&&encoder.BindMesh(planeMesh)&&encoder.DrawIndexed({3}),
                        "Draw original RG Ntt lookup audit");Submit(device,encoder);
                    const auto pixels=OnThread(device,[&]{return device.BackendDiagnostics()->ReadTexture(output);});
                    Require(pixels.size()==64,"Ntt audit readback");for(float value:pixels)Require(std::isfinite(value),"Nonfinite Ntt lookup response");
                    return glm::vec3(pixels[0],pixels[1],pixels[2]);
                };
                const auto compare=[&](glm::vec3 actual,glm::vec3 expected,const char* message) {
                    const auto tolerance=glm::max(glm::abs(expected)*.003f,glm::vec3(.003f));
                    if(glm::any(glm::greaterThan(glm::abs(actual-expected),tolerance)))
                        std::cerr<<message<<" actual="<<actual.x<<","<<actual.y<<","<<actual.z<<" expected="<<expected.x<<","<<expected.y<<","<<expected.z<<'\n';
                    Require(glm::all(glm::lessThanEqual(glm::abs(actual-expected),tolerance)),message);
                };
                compare(evaluate(nativeTexture),{.2481209395f,.328857421875f,1.03076171875f},"Original Ntt half-row / shifted G Gaussian reference");
                audit.arguments={-.4f,-1,.8f,0};
                compare(evaluate(nativeTexture),{.2398647318f,.2617285055f,.7046483769f},"Original Ntt sqrt azimuthal roughness / two-channel reference");
                audit.arguments={0,-1,.02f,0};
                compare(evaluate(nativeTexture),{.6501852450f,3.6204188875f,2.3103769298f},"Original Ntt minimum roughness / linear non-SRGB reference");
                audit.arguments={-1,.5f,1,0};
                compare(evaluate(nativeTexture),{.0673019358f,.25537109375f,.65478515625f},"Original Ntt clamped upper edge reference");
                compare(evaluate(signedTexture),{.3f,.3f,-.5f},"Ntt float G shift was clamped or gamma converted");
                audit.ibl.w=0;
                Require(std::abs(evaluate(signedTexture).x-std::exp2(-5.741926193237305f-5.2658371925354f*.5f))<.0001f,
                    "Absent native Ntt table changed the generator-fit compatibility path");
                std::cout<<"Native Ntt RG lookup: original half rows, sqrt roughness, clamp edges, shifted Gaussian, signed float G and absent-table fallback passed\n";
            }
            {
                // Independent goldens execute original DXIL %419..553 in
                // hair_color/gpu_color_vectors.py. This production-pass test
                // additionally covers loc2 coordinates and the RHI LUT cache.
                auto table=std::make_shared<StrandTextureTable>();table->width=2;table->height=3;
                table->texels={{.05f,.10f,.20f,1},{.35f,.50f,.80f,1},
                    {.40f,.20f,.10f,1},{.90f,.70f,.30f,1},{.10f,.30f,.20f,1},{.60f,.90f,.70f,1}};
                StrandMesh colored(device);auto source=red;source.guides=true;source.material.opacity=1;
                source.material.sourceColor=true;source.material.gradientTable=table;
                source.material.gradientWeights={1,.4f,.2f};source.material.colorRandomOffsets={.3f,0,0,0};
                source.material.hsvVariation={.02f,.3f,.03f};
                const auto sample=[&](int arcByte,int randomByte) {
                    const glm::vec2 coordinate(float(arcByte)/255.f,float(randomByte)/255.f);
                    const std::array<StrandColorCoordinate,2> coordinates{{{coordinate},{coordinate}}};
                    Require(colored.Update(overlap,segment,coordinates),colored.LastError());source.geometry=colored.Snapshot();
                    const auto pixels=draw({source});const auto center=(32*Size+32)*4;
                    return glm::vec3(pixels[center],pixels[center+1],pixels[center+2]);
                };
                const auto check=[&](glm::vec3 actual,glm::vec3 expected,const char* label) {
                    // Center coverage is nearly one; allow the residual .005
                    // transparent background and RGBA16F target quantization.
                    if(glm::any(glm::greaterThan(glm::abs(actual-expected),glm::vec3(.006f))))
                        std::cerr<<label<<" actual="<<actual.x<<","<<actual.y<<","<<actual.z<<" expected="<<expected.x<<","<<expected.y<<","<<expected.z<<'\n';
                    Require(glm::all(glm::lessThanEqual(glm::abs(actual-expected),glm::vec3(.006f))),label);
                };
                check(sample(0,64),{.0805524918f,.1280513235f,.2015294122f},"Source color A root / authored arc / HSV SSA reference");
                check(sample(128,128),{.6474608141f,.4655938163f,.2363171812f},"Source color B bilinear / linear RGB HSV SSA reference");
                check(sample(255,160),{.5158586996f,.8918235303f,.6528573854f},"Source color C tip / correlated source random SSA reference");
                check(sample(255,255),{.4704500055f,.6088221444f,.7760000005f},"Source color wrapped byte255 random / HSV SSA reference");
                source.material.sourceColor=false;const auto disabled=draw({source});
                auto noTable=source;noTable.material.gradientTable={};
                Require(MaximumDifference(disabled,draw({noTable}))<.001f,"Disabled source color changed generic strand material");
                source.material.sourceColor=true;source.geometry=second.Snapshot();
                Require(MaximumDifference(disabled,draw({source}))<.001f,"Missing optional source coordinates did not preserve generic endpoint color");
                auto replacement=std::make_shared<StrandTextureTable>(*table);
                for(auto& texel:replacement->texels)texel={.2f,.3f,.4f,1};
                source.material.gradientTable=replacement;source.material.hsvVariation={0,0,0};
                check(sample(128,128),{.2f,.3f,.4f},"Immutable replacement gradient table did not update RHI material resource");
                const auto paused=draw({source});
                Require(MaximumDifference(paused,draw({source}))<.001f,"Cached immutable source color table changed paused rendering");
                std::cout<<"Source hair color: authored arc/random, A/B/C selection, linear bilinear samples, HSV SSA goldens, disabled identity and immutable table replacement passed\n";
            }
            {
                // Exercise the production physical path with real RHI target
                // readback. These are transport invariants, not a duplicate of
                // the shader's implementation or a generated reference image.
                auto fiber=red;fiber.guides=false;fiber.material.physicalScattering=true;
                fiber.material.rootColor=fiber.material.tipColor={.7f,.5f,.25f};
                fiber.material.specularStrength=1.f;fiber.material.secondarySpecular=1.f;
                fiber.material.transmission=1.f;fiber.material.multipleScattering=0.f;
                fiber.material.opacity=1.f;fiber.material.cuticleShift=.02f;
                view.sunIntensity=1.f;view.sunColor={1,1,1};view.sunDirection={0,0,-1};
                view.lighting.ambientAndExposure={0,0,0,1};
                view.lighting.lightColors.fill(glm::vec4(0));
                const auto energy=[&](const std::vector<float>& pixels) {
                    double result=0;
                    for(uint32_t y=26;y<38;++y)for(uint32_t x=29;x<35;++x)for(int c=0;c<3;++c)
                        result+=std::max(pixels[(y*Size+x)*4+c]-.025f,0.f);
                    return result;
                };
                const auto finite=[&](const std::vector<float>& pixels) {
                    for(float value:pixels)Require(std::isfinite(value)&&value>=-.001f&&value<32.f,
                        "Physical hair produced unbounded/non-finite radiance");
                };
                const auto front=draw({fiber});finite(front);
                view.sunDirection={0,0,1};
                const auto back=draw({fiber});finite(back);
                auto noTransmission=fiber;noTransmission.material.transmission=0;
                const auto blocked=draw({noTransmission});finite(blocked);
                Require(energy(back)>energy(blocked)+.1,"R/TT/TRT transmission did not respond to back lighting");
                auto nativeFiber=fiber;nativeFiber.material.nttTable=nativeNttTable;
                const auto nativeBack=draw({nativeFiber});finite(nativeBack);
                Require(MaximumDifference(nativeBack,back)>.003f,"Production physical hair ignored the native Ntt material texture");
                nativeFiber.material.useNttLookup=false;
                Require(MaximumDifference(draw({nativeFiber}),back)<.001f,"Disabled native Ntt lookup with retained asset failed to restore generic compatibility");
                nativeFiber.material.useNttLookup=true;
                nativeFiber.material.nttTable={};
                Require(MaximumDifference(draw({nativeFiber}),back)<.001f,"Removing native Ntt table failed to restore generic compatibility");
                auto dense=fiber;dense.material.multipleScattering=1;dense.material.scatteringDensity=4;
                view.sunDirection={0,0,-1};
                const auto blonde=draw({dense});finite(blonde);
                auto dark=dense;dark.material.rootColor=dark.material.tipColor={.003f,.002f,.001f};
                const auto pigmented=draw({dark});finite(pigmented);
                Require(energy(blonde)>energy(pigmented)+.1,"Absorption did not suppress pigmented multi-fiber scattering");
                Require(MaximumDifference(front,blonde)>.003f,"Multi-fiber scattering had no visible effect");
                auto white=dense;white.material.rootColor=white.material.tipColor={1,1,1};white.material.scatteringDensity=16;
                finite(draw({white}));
                view.sunDirection={0,-1,0};finite(draw({white}));
                view.sunDirection={0,0,-1};
                auto noIor=fiber;noIor.material.ior=1.001f;noIor.material.transmission=0;
                noIor.material.secondarySpecular=0;noIor.material.multipleScattering=0;
                auto highIor=noIor;highIor.material.ior=1.8f;
                Require(energy(draw({highIor}))>energy(draw({noIor}))+.1,"Fiber IOR did not control dielectric reflection");
                auto narrow=fiber;narrow.material.longitudinalRoughness=.15f;
                auto broad=fiber;broad.material.longitudinalRoughness=.8f;
                Require(MaximumDifference(draw({narrow}),draw({broad}))>.003f,
                    "Independent longitudinal roughness did not change lobe width");
                auto smoothAzimuth=fiber;smoothAzimuth.material.specularStrength=0;
                smoothAzimuth.material.transmission=0;smoothAzimuth.material.azimuthalRoughness=.05f;
                auto roughAzimuth=smoothAzimuth;roughAzimuth.material.azimuthalRoughness=.95f;
                Require(MaximumDifference(draw({smoothAzimuth}),draw({roughAzimuth}))>.001f,
                    "Recovered azimuthal roughness did not control the TRT glint");
                red.material.physicalScattering=green.material.physicalScattering=true;
                red.guides=green.guides=false;
                const auto pForward=draw({red,green}),pReverse=draw({green,red});
                Require(MaximumDifference(pForward,pReverse)<.003f,"Physical hair lost transparency order independence");
                view.sunIntensity=0;
                auto skyFiber=fiber;skyFiber.material.multipleScattering=1;
                const auto unlitSky=draw({skyFiber});
                view.sky.options={1,1,1,0};
                view.sky.diffuseSH[0]=glm::vec4(glm::vec3(.4f/.282095f),0);
                const auto litSky=draw({skyFiber});finite(litSky);
                Require(energy(litSky)>energy(unlitSky)+1,"Physical hair omitted the opaque PBR precomputed sky");
                const auto center=(32*Size+32)*4;
                const glm::vec3 expected=.4f*skyFiber.material.indirectLightingMultiplier*
                    glm::mix(glm::vec3(1),skyFiber.material.rootColor,skyFiber.material.albedoBlendRate);
                for(int c=0;c<3;++c)Require(std::abs(litSky[center+c]-expected[c])<.025f,
                    "Hair sky illumination did not preserve source indirect/albedo mapping");
                view.sky.options.x=0;
                Require(MaximumDifference(draw({skyFiber}),unlitSky)<.001f,"Disabled sky altered physical hair lighting");
                view.indirect.coefficients.fill(glm::vec3(.4f));view.indirect.multiplier=2;view.indirect.exposureEV=1;
                view.indirect.rotation=.5f;view.indirect.enabled=false;
                Require(MaximumDifference(draw({skyFiber}),unlitSky)<.001f,"Disabled native strand cube altered the existing SH/ambient path");
                view.indirect.enabled=true;view.indirect.multiplier=1;view.indirect.exposureEV=0;view.indirect.rotation=0;
                const auto cubeOnly=draw({skyFiber});finite(cubeOnly);
                Require(MaximumDifference(cubeOnly,litSky)<.002f,"Cube-only strand environment added a pi factor or double material .8 multiplier");
                view.indirect.multiplier=2;view.indirect.exposureEV=-1;
                Require(MaximumDifference(draw({skyFiber}),cubeOnly)<.001f,"Strand producer gain and host unit EV were not independent multiplicative factors");
                view.indirect.multiplier=1;view.indirect.exposureEV=1;
                const auto doubleCube=draw({skyFiber});finite(doubleCube);
                for(int c=0;c<3;++c)Require(std::abs(doubleCube[center+c]-2*expected[c])<.035f,"Strand unit exposure adapter did not scale linear environment once");
                view.indirect.exposureEV=0;view.indirect.coefficients={{{1,0,0},{0,2,0},{0,0,3},{4,4,0},{5,0,5},{0,6,6}}};
                skyFiber.material.volumeSkyTransmission=true;skyFiber.material.dualScatteringIbl=true;
                const auto directionalCube=draw({skyFiber});finite(directionalCube);
                view.indirect.rotation=glm::radians(90.f);const auto rotatedCube=draw({skyFiber});finite(rotatedCube);
                Require(MaximumDifference(directionalCube,rotatedCube)>.001f,"Production two-direction source IBL ignored the signed/rotated cube producer");
                {
                    // A behavioral input equivalence through the real pass:
                    // scaling its retained HOST ambient must match explicitly
                    // supplying that RGB, while the source cube stays fixed.
                    // No scattering/color/shader formulas are duplicated here.
                    Require(StrandIndirectLightingSettings{}.hostAmbientScale==1.f,
                        "Default source HOST ambient policy changed existing callers");
                    const auto savedIndirect=view.indirect;
                    const auto savedAmbient=view.lighting.ambientAndExposure;
                    const glm::vec4 hostAmbient(.24f,.12f,.08f,.8125f);
                    auto ambientFiber=skyFiber;
                    unsigned ambientCaptures=0;
                    float minimumAmbientDifference=std::numeric_limits<float>::max();
                    for(bool spatial:{false,true})for(bool segmentShading:{false,true}) {
                        ambientFiber.material.sourceSpatialIbl=spatial;
                        ambientFiber.material.sourceSegmentShading=segmentShading;
                        std::vector<float> scaleZero,scaleOne;
                        for(float scale:{0.f,.5f,1.f}) {
                            view.lighting.ambientAndExposure=hostAmbient;
                            view.indirect.hostAmbientScale=scale;
                            const auto scaled=draw({ambientFiber});++ambientCaptures;finite(scaled);
                            Require(glm::all(glm::equal(view.lighting.ambientAndExposure,hostAmbient)),
                                "Hair ambient policy mutated caller/opaque lighting inputs");
                            if(segmentShading)Require(pass.SegmentShadingStatistics().passes>0,
                                "HOST ambient regression did not execute the segment producer");
                            view.indirect.hostAmbientScale=1.f;
                            view.lighting.ambientAndExposure=glm::vec4(glm::vec3(hostAmbient)*scale,hostAmbient.w);
                            const auto explicitRgb=draw({ambientFiber});++ambientCaptures;
                            Require(MaximumDifference(scaled,explicitRgb)==0.f,
                                "Source HOST ambient scale differs from explicit RGB at the real shader input");
                            // Alpha/coverage and the uncovered opaque plane
                            // must not be scaled with the hair's illumination.
                            for(std::size_t i=3;i<scaled.size();i+=4)
                                Require(scaled[i]==directionalCube[i],"HOST ambient changed hair coverage or opaque alpha");
                            for(unsigned c=0;c<4;++c)Require(scaled[c]==directionalCube[c],
                                "Hair HOST ambient scale changed the opaque/background pixel");
                            if(scale==0.f)scaleZero=scaled;
                            if(scale==1.f)scaleOne=scaled;
                        }
                        minimumAmbientDifference=std::min(minimumAmbientDifference,MaximumDifference(scaleZero,scaleOne));
                    }
                    Require(minimumAmbientDifference>.001f,
                        "HOST ambient fixture did not expose a nonzero input effect in every consumer");
                    view.indirect.enabled=false;
                    view.lighting.ambientAndExposure=hostAmbient;
                    view.indirect.hostAmbientScale=1.f;
                    const auto disabled=draw({ambientFiber});++ambientCaptures;
                    for(float scale:{0.f,.5f}) {
                        view.indirect.hostAmbientScale=scale;
                        Require(MaximumDifference(disabled,draw({ambientFiber}))==0.f,
                            "Disabled source cube unexpectedly applied its HOST ambient policy");++ambientCaptures;
                    }
                    view.indirect=savedIndirect;view.lighting.ambientAndExposure=savedAmbient;
                    std::cout<<"Source HOST ambient: "<<ambientCaptures
                        <<" production captures; scale0/.5/1 exact explicit-RGB equivalence, angular/spatial fragment/segment, disabled identity, caller/opaque/coverage isolation; minimum endpoint difference="
                        <<minimumAmbientDifference<<'\n';
                }
                view.indirect={};
                {
                    const auto valid=view.indirect;
                    for(int invalid=0;invalid<7;++invalid) {
                        if(invalid==0)view.indirect.coefficients[0].x=std::numeric_limits<float>::quiet_NaN();
                        if(invalid==1)view.indirect.coefficients[0].x=-1;
                        if(invalid==2)view.indirect.exposureEV=33;
                        if(invalid==3)view.indirect.hostAmbientScale=-.01f;
                        if(invalid==4)view.indirect.hostAmbientScale=1.01f;
                        if(invalid==5)view.indirect.hostAmbientScale=std::numeric_limits<float>::quiet_NaN();
                        if(invalid==6)view.indirect.hostAmbientScale=std::numeric_limits<float>::infinity();
                        auto rejected=device.BeginFrame({0,Size,Size});bool failed=false;
                        try{pass.Record(rejected,std::span<const StrandRenderItem>(&skyFiber,1),passTarget,view);}catch(const std::invalid_argument&){failed=true;}
                        Require(failed&&rejected.Cancel(),"Invalid strand ambient cube parameters were accepted");view.indirect=valid;
                    }
                }
                std::cout<<"Production strand cube: disabled identity, independent Sky-off environment, single material .8, gain/host EV and directional rotation passed\n";
                {
                    // Production scattering helper, rendered through RHI.
                    // A controlled four-channel LUT isolates the transcribed
                    // source arithmetic from LUT quadrature/asset uncertainty.
                    struct alignas(16) AuditConstants {
                        glm::vec4 base{.077424f,.061931f,.048708f,.02f};
                        glm::vec4 configuration{0,1,1,.5f}; // N,MS,source gain,v
                        glm::vec4 shape{0,0,.25f,.8f}; // sin(thetaO),mode,primary,azimuth
                        std::array<glm::vec4,256> moments;
                    } audit;
                    audit.moments.fill(glm::vec4(.7f,.25f,.03f,.6f));
                    PipelineDetail::ResourceBuilder resources(device);
                    const auto auditTexture=resources.Texture(4,4,RHITextureFormat::RGBA16F);
                    const auto auditTarget=resources.Target(auditTexture,{});
                    const auto auditBuffer=resources.Uniform(sizeof(audit));
                    std::string auditFragment=R"GLSL(#version 450 core
layout(std140,binding=7) uniform AuditBlock {
    vec4 auditBase,auditConfiguration,auditShape;
    vec4 strandMomentLut[256];
};
layout(location=0) out vec4 outColor;
#define FIBER_MOMENT_LUT 1
)GLSL";
                    auditFragment+=PipelineDetail::FiberScatteringGLSL();
                    auditFragment+=R"GLSL(
void main() {
    float so=clamp(auditShape.x,-.9999,.9999);
    vec3 T=vec3(0.0,1.0,0.0),V=vec3(0.0,so,sqrt(1.0-so*so)),L=vec3(.70710678118,0.0,.70710678118);
    vec4 fiber=vec4(1.55,auditConfiguration.w,auditShape.w,1.0);
    vec3 response=FiberIblDirection(auditBase.rgb,T,V,L,fiber,auditShape.z,auditBase.w,vec3(1.0),
        auditConfiguration.y,auditConfiguration.x,1.0,auditConfiguration.z,1.0);
    if(auditShape.y>1.5) {
        float gain=FiberIblBackscatterGain(auditConfiguration.x,auditConfiguration.z,1.0);
        response=FiberIblSingle(auditBase.rgb,T,V,L,fiber,auditShape.z,auditBase.w,vec3(1.0),gain)*
            pow(vec3(strandMomentLut[0].r),vec3(auditConfiguration.x));
    }
    outColor=vec4(response,1.0);
}
)GLSL";
                    const auto auditPipeline=resources.FullscreenPipeline(FullscreenVertexShader(),auditFragment);
                    const auto auditOwner=resources.Finish();
                    const auto evaluate=[&] {
                        auto encoder=device.BeginFrame({0,4,4});Require(encoder.KeepAlive(auditOwner),"Pin native hair IBL audit");
                        Require(encoder.SetRenderTarget({auditTarget,4,4,RHICommand::ClearColor}),"Bind native hair IBL audit");
                        const auto* bytes=reinterpret_cast<const std::byte*>(&audit);
                        for(uint32_t offset=0;offset<sizeof(audit);offset+=256)
                            Require(encoder.UpdateUniformBufferBytes(auditBuffer,bytes+offset,std::min(256u,uint32_t(sizeof(audit))-offset),offset),"Upload native hair IBL audit");
                        Require(encoder.BindUniformBuffer(auditBuffer,7)&&encoder.BindPipeline(auditPipeline)&&encoder.BindMesh(planeMesh)&&encoder.DrawIndexed({3}),"Draw native hair IBL audit");
                        Submit(device,encoder);const auto pixels=OnThread(device,[&]{return device.BackendDiagnostics()->ReadTexture(auditTexture);});
                        Require(pixels.size()==4*4*4,"Native hair IBL audit readback");
                        for(float value:pixels)Require(std::isfinite(value)&&value>=0&&value<32,"Native hair IBL produced nonfinite/unbounded energy");
                        return glm::vec3(pixels[0],pixels[1],pixels[2]);
                    };
                    const auto luminance=[](glm::vec3 v){return glm::dot(v,glm::vec3(.2126f,.7152f,.0722f));};
                    const auto golden=[&](glm::vec3 actual,glm::vec3 expected,const char* label) {
                        // Golden values come from an independent Python
                        // transcription verified against the original DXIL SSA
                        // instruction graph, not from this GLSL implementation.
                        const auto error=glm::abs(actual-expected),tolerance=glm::max(glm::abs(expected)*.003f,glm::vec3(2e-7f));
                        if(!glm::all(glm::lessThanEqual(error,tolerance)))
                            std::cerr<<label<<" actual="<<actual.x<<","<<actual.y<<","<<actual.z<<" expected="<<expected.x<<","<<expected.y<<","<<expected.z<<'\n';
                        Require(glm::all(glm::lessThanEqual(error,tolerance)),label);
                    };
                    const auto zeroFull=evaluate();audit.configuration.y=0;const auto zeroSingle=evaluate();
                    golden(zeroFull,{.149198299161f,.148991073453f,.148782173724f},"N0 source SSA multiple-scattering reference");
                    golden(zeroSingle,{.0164998594745f,.0162926337660f,.0160837340369f},"N0 source SSA single-scattering reference");
                    Require(luminance(zeroFull)>luminance(zeroSingle)+.001f,"N=0 native IBL lost its local backward-scattering term");
                    audit.configuration={5,1,1,.5f};const auto deepFull=evaluate();
                    golden(deepFull,{.0235103358255f,.0235089395495f,.0235075319941f},"N5 source SSA propagated phase/variance reference");
                    audit.shape.y=2;const auto forwardOnly=evaluate();audit.shape.y=0;
                    Require(luminance(deepFull)>luminance(forwardOnly)+.001f,"Native IBL failed to restore deep-fiber energy lost by forward-only attenuation");
                    audit.configuration.y=0;const auto deepSingle=evaluate();
                    golden(deepSingle,{.000111175178631f,.000109778902791f,.000108371347488f},"N5 source SSA uncollided reference");
                    Require(luminance(deepFull)>luminance(deepSingle)+.001f,"Native IBL multiple-scattering switch lost its additive backward response");
                    audit.configuration={5,1,1,.2f};audit.shape.x=.45f;const auto narrowVariance=evaluate();
                    audit.configuration.w=.6f;const auto broadVariance=evaluate();
                    Require(glm::length(narrowVariance-broadVariance)>.001f,"Native propagated sine-domain variance did not alter angular response");
                    audit.configuration={20,1,1,.5f};audit.shape.x=0;
                    golden(evaluate(),{.000130293717662f,.000130293717235f,.000130293716805f},"N20 source SSA deep-volume reference");
                    audit.configuration={20,1,1,.5f};audit.base=glm::vec4(1,1,1,.02f);audit.shape.x=.999f;evaluate();
                    audit.configuration.z=0;evaluate();
                    std::cout<<"Source dual IBL: N0 local-back="<<luminance(zeroFull-zeroSingle)<<", N5 full="<<luminance(deepFull)
                        <<" vs forward-only="<<luminance(forwardOnly)<<"; local/global backward phase, variance and white/grazing bounds passed\n";
                }
                {
                    PipelineDetail::ResourceBuilder shadows(device);
                    const auto shadowDepth=shadows.Texture(16,16,RHITextureFormat::Depth32F);
                    const auto shadowTarget=shadows.Target({},shadowDepth);
                    const auto shadowPlane=shadows.FullscreenPipeline(R"GLSL(#version 450 core
layout(location=0) in vec2 p;void main(){gl_Position=vec4(p,0,1);}
)GLSL",R"GLSL(#version 450 core
void main(){}
)GLSL",true);
                    const auto shadowOwner=shadows.Finish();
                    auto encoder=device.BeginFrame({0,Size,Size});Require(encoder.KeepAlive(shadowOwner),"Pin physical opaque shadow fixture");
                    Require(encoder.SetRenderTarget({shadowTarget,16,16,RHICommand::ClearDepth})&&encoder.BindPipeline(shadowPlane)&&
                        encoder.BindMesh(planeMesh)&&encoder.DrawIndexed({3}),"Render opaque body shadow depth fixture");Submit(device,encoder);
                    // Lift the reflected fiber signal above the .025 fixture
                    // background before comparing the measured shadow energy.
                    view.sunIntensity=4;view.sunDirection={0,0,-1};view.shadowAtlas=shadowDepth;
                    view.effects.effects={1,0,0,16};view.effects.mainView=glm::mat4(1);
                    view.effects.cascadeSplits={1,2,3,4};view.effects.shadowFilter=glm::vec4(0);
                    view.effects.cascadeWorldTexelSize=glm::vec4(.01f);view.effects.cascadeInverseDepthRange=glm::vec4(1);
                    for(auto& matrix:view.effects.lightViewProjection){matrix=glm::mat4(1);matrix[2][2]=-1;}
                    auto receiver=fiber;receiver.material.receiveOpaqueShadows=true;
                    const auto bodyShadow=draw({receiver});finite(bodyShadow);
                    receiver.material.receiveOpaqueShadows=false;const auto disabledShadow=draw({receiver});finite(disabledShadow);
                    const auto blockerDepth=OnThread(device,[&]{return device.BackendDiagnostics()->ReadTexture(shadowDepth);});
                    std::cout<<"Opaque fiber CSM diagnostic: shadowEnergy="<<energy(bodyShadow)<<", disabledEnergy="<<energy(disabledShadow)
                        <<", imageDiff="<<MaximumDifference(bodyShadow,disabledShadow)<<", atlasDepth=["
                        <<(blockerDepth.empty()?-1:*std::min_element(blockerDepth.begin(),blockerDepth.end()))<<","
                        <<(blockerDepth.empty()?-1:*std::max_element(blockerDepth.begin(),blockerDepth.end()))<<"]\n";
                    Require(energy(bodyShadow)<energy(disabledShadow)*.1,"Opaque depth atlas failed to shadow the physical hair sun");
                    view.effects.effects.x=0;receiver.material.receiveOpaqueShadows=true;
                    Require(MaximumDifference(draw({receiver}),disabledShadow)<.001f,"Disabled opaque CSM changed physical hair lighting");
                    view.shadowAtlas={};view.effects={};
                    std::cout<<"Physical hair opaque sun CSM: rendered blocker atlas, material disabled identity and scene disabled identity passed\n";
                }
                {
                    // Volume density is measured in inverse metres. Integrate
                    // it over cells to detect duplicate ribbon-triangle
                    // injection and screen-width-dependent fake density.
                    auto spatial=fiber;spatial.material.spatialVolume=true;
                    spatial.material.volumeResolution=16;spatial.material.volumeVoxelSize=.02f;
                    spatial.material.multipleScattering=0;spatial.material.scatteringDensity=1;
                    view.sunIntensity=4;view.sunDirection={-1,0,-.3f};
                    {
                        auto cancelled=device.BeginFrame({0,Size,Size});
                        pass.Record(cancelled,std::span<const StrandRenderItem>(&spatial,1),passTarget,view);
                        Require(cancelled.Cancel(),"Cancel recorded fiber volume");pass.InvalidateVolume();
                    }
                    const auto isolated=draw({spatial});finite(isolated);
                    const auto statistics=pass.VolumeStatistics();const auto textures=pass.VolumeDebugTextures();
                    Require(statistics.passes==4&&statistics.updated==1&&statistics.resolution==16&&statistics.effectiveVoxelSize>.02f,
                        "Spatial fiber volume did not report actual adaptive voxel size/update budget");
                    const auto density=OnThread(device,[&]{return device.BackendDiagnostics()->ReadTexture(textures.density);});
                    double integral=0;for(size_t i=0;i<density.size();i+=4){Require(std::isfinite(density[i])&&density[i]>=0,"Invalid spatial density");integral+=density[i];}
                    const double h=statistics.effectiveVoxelSize;
                    integral*=h*h*h;
                    const double expectedArea=1.6*.16*spatial.material.widthScale*spatial.material.volumeRasterWidthScale;
                    Require(std::abs(integral-expectedArea)<expectedArea*.015,
                        "Spatial density did not conserve physical segment projected area (duplicate or missing voxel injection)");
                    const auto paths=OnThread(device,[&]{return device.BackendDiagnostics()->ReadTexture(textures.paths);});
                    for(float value:paths)Require(std::isfinite(value)&&value>=0&&value<=80,"Invalid spatial optical path");
                    const auto cached=draw({spatial});
                    Require(pass.VolumeStatistics().passes==0&&pass.VolumeStatistics().updated==0&&MaximumDifference(isolated,cached)<.001f,
                        "Paused geometry did not reuse its spatial optical-path cache");
                    spatial.material.minPixelWidth=2;
                    draw({spatial});Require(pass.VolumeStatistics().passes==0,"Screen-space coverage incorrectly invalidated physical volume");
                    spatial.material.minPixelWidth=1;
                    spatial.material.areaPreservingCoverage=false;
                    draw({spatial});
                    Require(pass.VolumeStatistics().passes==0&&pass.VolumeStatistics().updated==0,
                        "Legacy coverage mode incorrectly rebuilt physical density or optical paths");
                    const auto coverageDensity=OnThread(device,[&]{return device.BackendDiagnostics()->ReadTexture(pass.VolumeDebugTextures().density);});
                    const auto coveragePaths=OnThread(device,[&]{return device.BackendDiagnostics()->ReadTexture(pass.VolumeDebugTextures().paths);});
                    Require(MaximumDifference(density,coverageDensity)==0&&MaximumDifference(paths,coveragePaths)==0,
                        "Screen-space coverage mode changed physical density or optical paths");
                    spatial.material.areaPreservingCoverage=true;
                    Require(MaximumDifference(draw({spatial}),isolated)<.001f&&pass.VolumeStatistics().passes==0,
                        "Restoring area coverage did not retain the physical volume cache and visible result");
                    {
                        auto normalized=spatial;normalized.material.normalizedVolumeWidth=true;
                        draw({normalized});
                        const auto normalizedStats=pass.VolumeStatistics();const auto normalizedTextures=pass.VolumeDebugTextures();
                        const auto pixels=OnThread(device,[&]{return device.BackendDiagnostics()->ReadTexture(normalizedTextures.density);});
                        double area=0;for(size_t i=0;i<pixels.size();i+=4)area+=pixels[i];
                        area*=std::pow(double(normalizedStats.effectiveVoxelSize),3);
                        const double referenceArea=1.6*.00005*normalized.material.volumeRasterWidthScale;
                        Require(std::abs(area-referenceArea)<referenceArea*.015,"Source normalized-width volume did not use the 50-micrometre reference full width");
                        Require(std::abs(normalized.geometry->MeanRadius()-.08f)<1e-7f,"Snapshot did not retain the raw strand-width sample mean");
                        normalized.material.volumeAverageWidth=.32f; // twice the known full diameter
                        draw({normalized});const auto explicitStats=pass.VolumeStatistics();const auto explicitTextures=pass.VolumeDebugTextures();
                        const auto explicitPixels=OnThread(device,[&]{return device.BackendDiagnostics()->ReadTexture(explicitTextures.density);});
                        double explicitArea=0;for(size_t i=0;i<explicitPixels.size();i+=4)explicitArea+=explicitPixels[i];
                        explicitArea*=std::pow(double(explicitStats.effectiveVoxelSize),3);
                        Require(std::abs(explicitArea-.5*referenceArea)<referenceArea*.015,"Recovered source widthAve was ignored or treated as a half width");
                        normalized.material.volumeAverageWidth=0;
                        StrandMesh doubledWidth(device);auto doubled=overlap;
                        for(auto& point:doubled)point.positionRadius.w*=2;
                        Require(doubledWidth.Update(doubled,segment),doubledWidth.LastError());normalized.geometry=doubledWidth.Snapshot();
                        draw({normalized});const auto doubledStats=pass.VolumeStatistics();const auto doubledTextures=pass.VolumeDebugTextures();
                        const auto doubledPixels=OnThread(device,[&]{return device.BackendDiagnostics()->ReadTexture(doubledTextures.density);});
                        double doubledArea=0;for(size_t i=0;i<doubledPixels.size();i+=4)doubledArea+=doubledPixels[i];
                        doubledArea*=std::pow(double(doubledStats.effectiveVoxelSize),3);
                        Require(std::abs(doubledArea-area)<referenceArea*.02,"Source width/mean normalization changed under a full-width/radius factor-two reinterpretation");
                        StrandMesh zeroWidth(device);auto zeros=overlap;for(auto& point:zeros)point.positionRadius.w=0;
                        Require(zeroWidth.Update(zeros,segment),zeroWidth.LastError());normalized.geometry=zeroWidth.Snapshot();draw({normalized});
                        const auto zeroTextures=pass.VolumeDebugTextures();const auto zeroPixels=OnThread(device,[&]{return device.BackendDiagnostics()->ReadTexture(zeroTextures.density);});
                        for(float value:zeroPixels)Require(std::isfinite(value)&&value==0,"Zero-width source normalization produced nonzero/nonfinite density");
                        std::cout<<"Source DS normalized width: projectedArea="<<area<<", doubledWidthArea="<<doubledArea<<", referenceDiameter=50um\n";
                    }
                    StrandMesh occluding(device);
                    std::array<StrandVertex,4> obstructed{{overlap[0],overlap[1],overlap[0],overlap[1]}};
                    obstructed[2].positionRadius.x=.4f;obstructed[3].positionRadius.x=.4f;
                    Require(occluding.Update(obstructed,lines),occluding.LastError());spatial.geometry=occluding.Snapshot();
                    const auto shadowed=draw({spatial});finite(shadowed);
                    Require(energy(shadowed)<energy(isolated)*.98,"Off-screen-neighbor physical fibers did not cast spatial self shadows");
                    view.sky.options={1,1,1,0};view.sunIntensity=0;
                    spatial.material.volumeSkyTransmission=false;
                    const auto unattenuatedSky=draw({spatial});
                    auto noSpatial=spatial;noSpatial.material.spatialVolume=false;
                    Require(MaximumDifference(unattenuatedSky,draw({noSpatial}))<.001f,
                        "Disabled experimental volume Sky transmission altered environment illumination");
                    spatial.material.volumeSkyTransmission=true;
                    spatial.material.dualScatteringIbl=false;
                    const auto experimentalSky=draw({spatial});finite(experimentalSky);
                    Require(MaximumDifference(unattenuatedSky,experimentalSky)>.001f&&energy(experimentalSky)<=energy(unattenuatedSky)+.001,
                        "Enabled experimental volume Sky transmission had no bounded visibility effect");
                    const auto environmentTextures=pass.VolumeDebugTextures();
                    const auto environmentPaths=OnThread(device,[&]{return device.BackendDiagnostics()->ReadTexture(environmentTextures.pathsExtra);});
                    float environmentCount=0;for(size_t i=0;i<environmentPaths.size();i+=4)for(int c=1;c<4;++c)environmentCount=std::max(environmentCount,environmentPaths[i+c]);
                    Require(environmentCount>.01f,"Precomputed Sky had no spatial six-direction fiber visibility");
                    spatial.material.dualScatteringIbl=true;spatial.material.multipleScattering=0;
                    const auto sourceSingleSky=draw({spatial});finite(sourceSingleSky);
                    spatial.material.multipleScattering=1;const auto sourceMultipleSky=draw({spatial});finite(sourceMultipleSky);
                    Require(energy(sourceMultipleSky)>energy(sourceSingleSky)+.01,
                        "Production source IBL failed to add local/global backward scattering");
                    const auto sourceTextures=pass.VolumeDebugTextures();
                    const auto sourceDensity=OnThread(device,[&]{return device.BackendDiagnostics()->ReadTexture(sourceTextures.density);});
                    for(size_t i=0;i<sourceDensity.size();i+=4)if(sourceDensity[i]>.001f)
                        Require(std::abs(sourceDensity[i+1])<.001f&&std::abs(sourceDensity[i+2]-sourceDensity[i])<.001f&&std::abs(sourceDensity[i+3])<.001f,
                            "Density volume did not preserve the physical segment's weighted tangent");
                    const auto sourcePaths=OnThread(device,[&]{return device.BackendDiagnostics()->ReadTexture(sourceTextures.pathsAmbient);});
                    float sourceCount=0;
                    for(size_t i=0;i<sourcePaths.size();i+=4) {
                        Require(std::isfinite(sourcePaths[i])&&std::isfinite(sourcePaths[i+1])&&sourcePaths[i]>=0&&sourcePaths[i+1]>=0&&
                            sourcePaths[i]<=80&&sourcePaths[i+1]<=80&&sourcePaths[i+2]==0&&sourcePaths[i+3]==0,
                            "Source two-path atlas violated its independent RG count / zero BA contract");
                        sourceCount=std::max({sourceCount,sourcePaths[i],sourcePaths[i+1]});
                    }
                    Require(sourceCount>.01f,"Source two-path atlas had no real spatial optical depth");
                    const auto cameraBefore=view.cameraPosition;
                    view.cameraPosition.x+=1;
                    draw({spatial});Require(pass.VolumeStatistics().passes==3&&pass.VolumeStatistics().updated==1,
                        "Source camera motion rebuilt geometry or failed to update its three path passes");
                    const auto movedTextures=pass.VolumeDebugTextures();
                    const auto movedDensity=OnThread(device,[&]{return device.BackendDiagnostics()->ReadTexture(movedTextures.density);});
                    const auto movedPaths=OnThread(device,[&]{return device.BackendDiagnostics()->ReadTexture(movedTextures.pathsAmbient);});
                    Require(MaximumDifference(sourceDensity,movedDensity)==0,"Source camera motion changed physical density");
                    Require(MaximumDifference(sourcePaths,movedPaths)>.001f,"Source oblique optical paths did not follow the view-relative two directions");
                    draw({spatial});Require(pass.VolumeStatistics().passes==0,"Paused source paths did not reuse their camera cache");
                    {
                        view.cameraPosition.x+=.25f;auto cancelled=device.BeginFrame({0,Size,Size});
                        pass.Record(cancelled,std::span<const StrandRenderItem>(&spatial,1),passTarget,view);
                        Require(pass.VolumeStatistics().passes==3&&cancelled.Cancel(),"Cancel camera-only source path recording");
                        pass.InvalidateVolume();draw({spatial});
                        Require(pass.VolumeStatistics().passes==4,"Cancelled source paths incorrectly committed density/path cache state");
                    }
                    spatial.material.dualScatteringIbl=false;draw({spatial});
                    Require(pass.VolumeStatistics().passes==3,"Source to forward switch did not update paths independently of density");
                    view.cameraPosition.x+=1;draw({spatial});
                    Require(pass.VolumeStatistics().passes==0,"Camera-independent forward paths were unnecessarily invalidated");
                    view.cameraPosition=cameraBefore;spatial.material.dualScatteringIbl=true;
                    draw({spatial});Require(pass.VolumeStatistics().passes==3,"Forward to source switch did not restore oblique paths");
                    view.sky.diffuseSH[2]={0,0,.15f/.488603f,0};
                    const auto directionalSky=draw({spatial});finite(directionalSky);
                    Require(MaximumDifference(directionalSky,sourceMultipleSky)>.001f,
                        "Source two-direction Sky adapter discarded the incident L1 directional band");
                    view.sky.diffuseSH[2]=glm::vec4(0);
                    view.sky.options.x=0;view.indirect={};const auto noCubeVolume=draw({spatial});
                    view.indirect.enabled=true;view.indirect.coefficients.fill(glm::vec3(.4f));
                    const auto cubeVolume=draw({spatial});finite(cubeVolume);
                    Require(MaximumDifference(cubeVolume,noCubeVolume)>.001f,"Cube-only source volume had optical paths but no environment illumination");
                    Require(pass.VolumeStatistics().passes==3&&pass.VolumeStatistics().updated==1,
                        "Cube-only source environment did not enable camera-relative volume paths while SH Sky was disabled");
                    const auto cubeTextures=pass.VolumeDebugTextures();
                    const auto cubePaths=OnThread(device,[&]{return device.BackendDiagnostics()->ReadTexture(cubeTextures.pathsAmbient);});
                    float cubeCount=0;for(size_t i=0;i<cubePaths.size();i+=4)cubeCount=std::max({cubeCount,cubePaths[i],cubePaths[i+1]});
                    Require(cubeCount>.01f,"Cube-only source IBL had no real spatial optical depths");
                    view.indirect={};
                    view.sky.options.x=0;view.sunIntensity=4;
                    auto shifted=obstructed;shifted[2].positionRadius.x=-.4f;shifted[3].positionRadius.x=-.4f;
                    Require(occluding.Update(shifted,lines),occluding.LastError());spatial.geometry=occluding.Snapshot();
                    finite(draw({spatial}));Require(pass.VolumeStatistics().updated==1,"Dynamic strand revision did not rebuild its optical paths");
                    spatial.material.scatteringDensity=16;spatial.material.multipleScattering=1;
                    spatial.material.rootColor=spatial.material.tipColor={1,1,1};finite(draw({spatial}));
                    // A uniform model scale changes world voxel size while
                    // preserving dimensionless local optical path counts.
                    const auto local=draw({spatial});const auto localTextures=pass.VolumeDebugTextures();
                    const auto localPaths=OnThread(device,[&]{return device.BackendDiagnostics()->ReadTexture(localTextures.paths);});
                    spatial.model=glm::scale(glm::mat4(1),glm::vec3(2));draw({spatial});
                    const auto scaledTextures=pass.VolumeDebugTextures();
                    const auto scaledPaths=OnThread(device,[&]{return device.BackendDiagnostics()->ReadTexture(scaledTextures.paths);});
                    // Sun is directional; finite point positions legitimately
                    // move relative to local geometry under a model scale.
                    float sunDifference=0;for(size_t i=0;i<localPaths.size();i+=4)sunDifference=std::max(sunDifference,std::abs(localPaths[i]-scaledPaths[i]));
                    Require(sunDifference<.01f&&std::abs(pass.VolumeStatistics().effectiveVoxelSize-float(2*h))<.001f,
                        "Uniform model scale changed fiber encounter count or hid actual world voxel size");
                    std::cout<<"Spatial fiber volume: area="<<integral<<", actualVoxel="<<h<<", cells="<<statistics.voxelCount
                        <<"; conservation, cached pause, dynamic revision, physical self shadow and uniform scale passed\n";
                }
                std::cout<<"Physical hair: source hemisphere LUT, backlit TT, absorption, bounded white/grazing multiple scattering, IOR and independent longitudinal/azimuthal roughness passed\n";
            }
            {
                MaterialRenderPipeline pipeline(device);Require(pipeline.Initialize(),pipeline.LastError());
                Require(pipeline.Resize(Size,Size,128),pipeline.LastError());
                RenderFrame frame;frame.strands={red,green};
                PipelineCamera camera;camera.position={0,0,2};camera.nearPlane=.1f;camera.farPlane=10.f;
                camera.view=glm::lookAtRH(camera.position,glm::vec3(0),glm::vec3(0,1,0));
                camera.projection=glm::perspectiveRH_NO(glm::radians(45.f),1.f,camera.nearPlane,camera.farPlane);
                MaterialPipelineSettings settings;settings.shadows.enabled=false;settings.shadows.atlasResolution=128;settings.reflection.enabled=false;
                settings.post.ssaoEnabled=false;settings.post.bloomEnabled=false;
                settings.sky.enabled=false;settings.indirect.enabled=false;settings.probes.enabled=false;
                settings.realtimeGi.enabled=false;settings.lumenGi.enabled=false;settings.surfaceCacheGi.enabled=false;
                auto encoder=device.BeginFrame({1,Size,Size});
                Require(pipeline.Record(encoder,frame,camera,settings),pipeline.LastError());
                const auto token=pipeline.RecordedFrameToken();Submit(device,encoder);
                Require(pipeline.CompleteFrame(token),pipeline.LastError());
                Require(pipeline.Statistics().strandPasses==3&&pipeline.Statistics().strandSegments==2,
                    "Material pipeline did not record the strand HDR passes");
                const auto textures=pipeline.DebugTextures();Require(textures.displayColor.IsValid(),"No completed display texture export");
                const auto display=OnThread(device,[&]{return device.BackendDiagnostics()->ReadTexture(textures.displayColor);});
                Require(display.size()==Size*Size*4,"PBR display texture dimensions");
                for(float value:display)Require(std::isfinite(value),"Non-finite strand display output");
                // Exercise actual material-pipeline ping-pong and transaction
                // boundaries, in addition to the independent shader oracles.
                settings.temporal.antialiasing=AntialiasingMode::TAA;
                settings.strandMotionVectors=true;settings.strandTemporalDepth=true;
                StrandMesh temporalHair(device);
                std::array<StrandVertex,2> temporalPoints{{
                    {{-.2f,-.6f,0,.02f},{0,1,0,0}},{{-.2f,.6f,0,.02f},{0,1,0,1}}
                }};
                const std::array<std::uint32_t,2> temporalLines{0,1};
                Require(temporalHair.Update(temporalPoints,temporalLines),temporalHair.LastError());
                StrandRenderItem temporalItem;temporalItem.guides=true;
                temporalItem.material.rootColor=temporalItem.material.tipColor={1.5f,.6f,.1f};
                temporalItem.material.opacity=.6f;temporalItem.previousTransformValid=true;
                const auto temporalRecord=[&](bool submit){
                    temporalItem.geometry=temporalHair.Snapshot();frame.strands={temporalItem};
                    auto next=device.BeginFrame({1,Size,Size});
                    Require(pipeline.Record(next,frame,camera,settings),pipeline.LastError());
                    const auto nextToken=pipeline.RecordedFrameToken();
                    if(submit){Submit(device,next);Require(pipeline.CompleteFrame(nextToken),pipeline.LastError());}
                    else{Require(next.Cancel(),"Cancel recorded hair temporal frame");Require(pipeline.DiscardFrame(nextToken),pipeline.LastError());}
                };
                temporalRecord(true);
                Require(!pipeline.Statistics().strandHairHistoryUsed&&pipeline.Statistics().strandMotionActive,
                    "First separate hair frame accepted history or missed motion output");
                Require(temporalHair.Update(temporalPoints,temporalLines,{},temporalPoints),temporalHair.LastError());
                temporalRecord(true);temporalRecord(true);
                Require(pipeline.Statistics().strandHairHistoryUsed,"Static explicit hair history did not converge");
                const auto completedHair=pipeline.DebugTextures().strandTemporalColor;
                auto translated=temporalPoints;for(auto& p:translated)p.positionRadius.x+=.08f;
                Require(temporalHair.Update(translated,temporalLines,{},temporalPoints),temporalHair.LastError());
                temporalRecord(false);
                Require(pipeline.DebugTextures().strandTemporalColor==completedHair,
                    "Discard advanced the completed hair history image");
                temporalRecord(true);
                Require(pipeline.Statistics().strandHairHistoryUsed,
                    "Valid dynamic geometry revision unnecessarily reset hair history");
                const auto movedHair=pipeline.DebugTextures();
                Require(movedHair.strandMotion.IsValid()&&movedHair.strandTemporalColor.IsValid(),"Missing completed hair temporal debug export");
                const auto resolvedHair=OnThread(device,[&]{return device.BackendDiagnostics()->ReadTexture(movedHair.strandTemporalColor);});
                std::uint32_t coveredHair=0;
                for(std::size_t i=0;i<resolvedHair.size();i+=4){
                    for(int c=0;c<4;++c)Require(std::isfinite(resolvedHair[i+c]),"Nonfinite temporal hair history");
                    Require(resolvedHair[i+3]>=0&&resolvedHair[i+3]<=1,"Invalid temporal hair coverage");
                    if(resolvedHair[i+3]>.05f)++coveredHair;
                }
                Require(coveredHair>10,"Moving hair temporal layer is empty");
                pipeline.ResetHistory();temporalRecord(true);
                Require(!pipeline.Statistics().strandHairHistoryUsed,"Explicit reset reused old hair history");
                settings.strandMotionVectors=false;temporalRecord(true);
                Require(!pipeline.Statistics().strandMotionActive&&pipeline.Statistics().strandHairTemporalPasses==0,
                    "Disabled strand motion still added separate temporal work");
                settings.strandMotionVectors=true;settings.strandTemporalDepth=false;
                temporalRecord(true);temporalRecord(true);
                Require(pipeline.Statistics().historyUsed,"Camera-only fallback failed to retain unchanged content");
                auto fallbackMoved=translated;for(auto& p:fallbackMoved)p.positionRadius.x+=.08f;
                Require(temporalHair.Update(fallbackMoved,temporalLines,{},translated),temporalHair.LastError());
                temporalRecord(true);
                Require(!pipeline.Statistics().historyUsed&&!pipeline.Statistics().strandMotionActive,
                    "Camera-only fallback retained moving strand geometry without motion output");
                std::cout<<"Material hair temporal: moving snapshot, ping-pong, completed/discarded frame, reset and opt-out PASS\n";
                settings.strandTemporalDepth=true;settings.temporal.hairMotionBlurEnabled=true;
                settings.temporal.motionBlurEnabled=true;settings.strandTimeSeconds=0.;
                Require(temporalHair.Update(fallbackMoved,temporalLines,{},fallbackMoved),temporalHair.LastError());
                Require(!temporalHair.Snapshot()->HasDeformationMotion(),"Identical supplied vertices marked as moving");
                pipeline.ResetHistory();temporalRecord(true);
                Require(pipeline.Statistics().strandShutterSamples==0,"First hair frame used absent shutter history");
                settings.strandTimeSeconds=.016;temporalRecord(true);
                Require(pipeline.Statistics().strandShutterSamples==0&&pipeline.Statistics().strandHairHistoryUsed,
                    "Static shutter bypass lost sharp TAA accumulation or recorded unnecessary geometry samples");
                camera.position.x+=.005f;camera.view=glm::lookAtRH(camera.position,glm::vec3(0),glm::vec3(0,1,0));
                settings.strandTimeSeconds=.032;temporalRecord(true);
                Require(pipeline.Statistics().strandShutterSamples==4&&pipeline.Statistics().motionBlurPasses==1,
                    "Independent geometry shutter/background camera blur was not recorded");
                camera.position.x+=.005f;camera.view=glm::lookAtRH(camera.position,glm::vec3(0),glm::vec3(0,1,0));
                settings.strandTimeSeconds=.048;temporalRecord(false);
                temporalRecord(true);
                Require(pipeline.Statistics().strandShutterSamples==4,"Discard advanced shutter timeline");
                camera.position.x+=.005f;camera.view=glm::lookAtRH(camera.position,glm::vec3(0),glm::vec3(0,1,0));
                temporalRecord(true);
                Require(pipeline.Statistics().strandShutterSamples==0,"Zero elapsed timeline accepted shutter motion");
                camera.position.x+=.005f;camera.view=glm::lookAtRH(camera.position,glm::vec3(0),glm::vec3(0,1,0));
                settings.strandTimeSeconds=.01;temporalRecord(true);
                Require(pipeline.Statistics().strandShutterSamples==0,"Backward timeline accepted shutter motion");
                camera.position.x+=.005f;camera.view=glm::lookAtRH(camera.position,glm::vec3(0),glm::vec3(0,1,0));
                settings.strandTimeSeconds=.5;temporalRecord(true);
                Require(pipeline.Statistics().strandShutterSamples==0,"Long missing interval extrapolated shutter motion");
                settings.strandTimeSeconds.reset();temporalRecord(true);
                Require(pipeline.Statistics().strandShutterSamples==0,"Absent timeline accepted shutter motion");
                std::cout<<"Material hair shutter: completed timeline, discard, zero/backward/gap/missing timestamp and separate background camera blur PASS\n";
                // Deformation provenance is independent of upload revisions;
                // arc-only changes alter appearance coordinates, not geometry.
                auto motionCheck=fallbackMoved;motionCheck[0].tangentArc.w+=.01f;
                Require(temporalHair.Update(motionCheck,temporalLines,{},fallbackMoved),temporalHair.LastError());
                Require(!temporalHair.Snapshot()->HasDeformationMotion(),"Arc-only change marked as physical motion");
                motionCheck[0].positionRadius.x+=.001f;
                Require(temporalHair.Update(motionCheck,temporalLines,{},fallbackMoved),temporalHair.LastError());
                Require(temporalHair.Snapshot()->HasDeformationMotion(),"Actual strand movement was ignored");
                Require(temporalHair.Update(motionCheck,temporalLines),temporalHair.LastError());
                Require(!temporalHair.Snapshot()->HasDeformationMotion(),"Missing history fabricated deformation motion");
                pipeline.Shutdown();
                Require(!pipeline.DebugTextures().displayColor.IsValid(),"Display lease not invalidated on shutdown");
            }
            Require(OnThread(device,[&]{return device.BackendDiagnostics()->ValidationErrorCount();})==0,"Native validation error");
            pass.Shutdown();hair.Clear();second.Clear();
            }
            Require(OnThread(device,[&]{return device.BackendDiagnostics()->ValidationErrorCount();})==0,"Native validation error");
        }
        device.StopAndRelease();
        if(curveOnly)std::cout<<"RHI native curve isolated suite passed\n";
        else std::cout<<"RHI strands: triangle ribbons, immutable snapshots, opaque-depth occlusion, order-independent alpha, physical scattering, HDR integration and display export passed\n";
        return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
