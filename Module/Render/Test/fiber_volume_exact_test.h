#pragma once
#include "Render/Private/Pipeline/fiber_volume.h"
#include <array>
#include <chrono>
#include <cmath>
#include <future>
#include <iostream>
#include <glm/gtc/matrix_transform.hpp>

namespace Render::TestData {
namespace FiberExactAuditDetail {
inline void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
template<class F> auto OnDevice(RHIDevice& device,F operation) {
    using T=std::invoke_result_t<F>;auto promise=std::make_shared<std::promise<T>>();auto result=promise->get_future();
    device.async_ExecuteCode([promise,operation=std::move(operation)]()mutable{
        try{if constexpr(std::is_void_v<T>){operation();promise->set_value();}else promise->set_value(operation());}
        catch(...){promise->set_exception(std::current_exception());}
    });return result.get();
}
inline void Submit(RHIDevice& device,RHIFrameEncoder& encoder) {
    Check(encoder.End(false),"End exact fiber-volume oracle");auto done=std::make_shared<bool>(false);
    Check(device.async_SubmitFrameCommands(encoder.GetCommandBuffer(),[done]{*done=true;}),"Submit exact fiber-volume oracle");
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
    while(!*done){device.returnSystem.DrainCallbacks();Check(std::chrono::steady_clock::now()<deadline,"Exact fiber-volume oracle timeout");
        if(!*done)device.returnSystem.WaitForCallbacks(std::chrono::milliseconds(2));}
}
inline std::vector<float> Read(RHIDevice& device,RenderResourceHandle<RHITextureSpec> image) {
    return OnDevice(device,[&device,image]{return device.BackendDiagnostics()->ReadTexture(image);});
}
struct Sample {
    const char* label="";glm::ivec3 size{8};float h=.02f;
    glm::vec3 p{.05f},direction{1,0,0};float maxDistance=.05f,bias=0;
    std::vector<glm::vec4> density,blocker;
    double expectedN=0,expectedG=1;
};
inline std::size_t Flat(glm::ivec3 size,int x,int y,int z){return (std::size_t(z)*size.y+y)*size.x+x;}
inline Sample Field(const char* label,glm::ivec3 size={8,8,8}) {
    Sample result;result.label=label;result.size=size;result.density.resize(std::size_t(size.x)*size.y*size.z);result.blocker=result.density;return result;
}
// A known global multi-affine field is reconstructed exactly by trilinear
// interpolation away from the zero border. Simpson's independent 3/8 rule
// integrates its ray-restricted cubic without using the GLSL coefficient code.
inline double Polynomial(glm::dvec3 q) {
    return .2+.11*q.x+.07*q.y+.19*q.z+.03*q.x*q.y+.04*q.x*q.z+.02*q.y*q.z+.013*q.x*q.y*q.z;
}
inline double Simpson38(const Sample& sample) {
    const glm::dvec3 origin=glm::dvec3(sample.p)/double(sample.h)-.5;
    const glm::dvec3 velocity=glm::normalize(glm::dvec3(sample.direction))/double(sample.h);
    const double first=double(sample.h)*sample.bias,last=sample.maxDistance,step=(last-first)/3.;
    return (last-first)/8.*(Polynomial(origin+velocity*first)+3.*Polynomial(origin+velocity*(first+step))+
        3.*Polynomial(origin+velocity*(first+2.*step))+Polynomial(origin+velocity*last));
}
} // namespace FiberExactAuditDetail

inline void RunFiberVolumeExactGpuOracle(RHIDevice& device) {
    using namespace FiberExactAuditDetail;
    Check(!StrandMaterial{}.volumeExactIntegration,"Generic exact-volume option must default off");
    std::vector<Sample> cases;
    for(const glm::vec3 direction:std::array{glm::vec3(1,.7f,.3f),glm::vec3(-.4f,1,-.6f),glm::vec3(.5f,-.8f,1)}) {
        auto c=Field("multi-affine oblique field");c.p=glm::vec3(3.1f,3.2f,3.4f)*c.h;
        c.direction=direction;c.maxDistance=2.1f*c.h;c.bias=.23f;
        for(int z=0;z<c.size.z;++z)for(int y=0;y<c.size.y;++y)for(int x=0;x<c.size.x;++x)
            c.density[Flat(c.size,x,y,z)].r=float(Polynomial({x,y,z}));
        c.expectedN=Simpson38(c);cases.push_back(std::move(c));
    }
    {
        auto c=Field("point-light clipping in uniform density");c.p=glm::vec3(2.5f)*c.h;c.maxDistance=2.25f*c.h;c.bias=.25f;
        for(auto& v:c.density)v.r=7.f;c.expectedN=7.*(double(c.maxDistance)-double(c.h)*c.bias);cases.push_back(std::move(c));
    }
    {
        auto c=Field("finite grid exit and zero-border ramp");c.p=glm::vec3(2.5f)*c.h;c.maxDistance=1e6f;c.bias=.25f;
        for(auto& v:c.density)v.r=7.f;c.expectedN=7.*double(c.h)*(8.-2.5-.25-.125);cases.push_back(std::move(c));
    }
    {
        auto c=Field("bilinear opaque maximum inside a cell");c.p=glm::vec3(2.5f)*c.h;c.direction={1,1,0};c.maxDistance=std::sqrt(2.f)*c.h;
        for(int z=0;z<c.size.z;++z)for(int y=0;y<c.size.y;++y)for(int x=0;x<c.size.x;++x)
            c.blocker[Flat(c.size,x,y,z)].r=float((x&1)^(y&1));
        // Along the diagonal of this cell, G(t)=2t(1-t), whose maximum is .5.
        c.expectedG=.5;cases.push_back(std::move(c));
    }
    {
        auto c=Field("cubic opaque maximum at one third");c.p=glm::vec3(2.5f)*c.h;c.direction={1,1,1};c.maxDistance=std::sqrt(3.f)*c.h;
        c.blocker[Flat(c.size,2,2,3)].r=1;
        // One upper-Z corner gives G(t)=t(1-t)^2, max4/27 at t=1/3.
        c.expectedG=23./27.;cases.push_back(std::move(c));
    }
    {
        auto c=Field("distant thin density peaks",{64,4,4});c.p={.5f*c.h,1.5f*c.h,1.5f*c.h};c.maxDistance=63*c.h;
        for(int z=0;z<c.size.z;++z)for(int y=0;y<c.size.y;++y)for(int x=0;x<c.size.x;++x)
            c.density[Flat(c.size,x,y,z)].r=x%7==3?25.f:0.f;
        // Each interior peak is a triangular tent of base2h and height25.
        c.expectedN=9.*25.*double(c.h);cases.push_back(std::move(c));
    }
    {
        auto c=Field("zero direction and empty interval");c.direction=glm::vec3(0);c.expectedN=0;cases.push_back(std::move(c));
        c=Field("light before physical bias");c.bias=2;c.maxDistance=.5f*c.h;
        for(auto& v:c.density)v.r=10;for(auto& v:c.blocker)v.r=1;cases.push_back(std::move(c));
        c=Field("bounded saturated N and opaque G");for(auto& v:c.density)v.r=10000;for(auto& v:c.blocker)v.r=1;
        c.expectedN=80;c.expectedG=0;cases.push_back(std::move(c));
    }
    PipelineDetail::ResourceBuilder resources(device);
    auto shader=PipelineDetail::FiberVolumePathsShader();shader.erase(shader.rfind("void main()"));
    const auto opaque=PipelineDetail::FiberVolumeOpaquePathsShader();const auto begin=opaque.find("float FiberTraceOpaqueVisibility("),end=opaque.rfind("void main()");
    Check(begin!=std::string::npos&&end>begin,"Locate production blocker trace for exact-volume fixture");
    shader+="\nlayout(binding=13) uniform sampler2D fiberOpaqueMask;\n";shader+=opaque.substr(begin,end-begin);
    shader+=R"GLSL(
layout(std140,binding=8) uniform ExactVolumeQuery {vec4 auditPosition,auditDirection;};
void main(){result=vec4(FiberTracePath(auditPosition.xyz,auditDirection.xyz,auditPosition.w),
    FiberTraceOpaqueVisibility(auditPosition.xyz,auditDirection.xyz,auditPosition.w),0,1);}
)GLSL";
    const auto pipeline=resources.FullscreenPipeline(FullscreenVertexShader(),shader);
    const auto quad=resources.FullscreenMesh();
    const auto uniform=resources.Uniform(sizeof(PipelineDetail::FiberVolumeConstants)),query=resources.Uniform(2*sizeof(glm::vec4));
    const auto output=resources.Texture(1,1,RHITextureFormat::RGBA32F);
    const auto target=resources.Target(output,{});
    std::vector<std::array<RenderResourceHandle<RHITextureSpec>,2>> images;
    for(const auto& c:cases) {
        const unsigned columns=unsigned(std::ceil(std::sqrt(double(c.size.z))));
        const unsigned width=unsigned(c.size.x)*columns,height=unsigned(c.size.y)*((unsigned(c.size.z)+columns-1)/columns);
        std::array<RenderResourceHandle<RHITextureSpec>,2> pair;
        for(unsigned kind=0;kind<2;++kind) {
            std::vector<glm::vec4> atlas(std::size_t(width)*height,glm::vec4(0));const auto& values=kind?c.blocker:c.density;
            for(int z=0;z<c.size.z;++z)for(int y=0;y<c.size.y;++y)for(int x=0;x<c.size.x;++x)
                atlas[(std::size_t(z/columns)*c.size.y+y)*width+(z%columns)*c.size.x+x]=values[Flat(c.size,x,y,z)];
            CreateRHITextureSpec desc;desc.width=width;desc.height=height;desc.mipmaps=false;desc.filterMode=RHIFilterMode::Linear;
            desc.textureDataStoreType=desc.textureUseType=RHITextureFormat::RGBA32F;desc.addressMode=RHIAddressMode::ClampToEdge;desc.data=atlas.data();
            pair[kind]=resources.Create<RHITextureSpec>([&](auto cb){device.async_CreateTexture(desc,cb);});
        }
        images.push_back(pair);
    }
    const auto owner=resources.Finish();
    const auto evaluate=[&](unsigned index,bool exact,bool hardware) {
        const auto& c=cases[index];const auto columns=unsigned(std::ceil(std::sqrt(double(c.size.z))));
        PipelineDetail::FiberVolumeConstants constants;constants.minimumStep={0,0,0,c.h};constants.size=glm::vec4(glm::vec3(c.size),float(columns));
        constants.atlas={float(c.size.x*columns),float(c.size.y*((unsigned(c.size.z)+columns-1)/columns)),1,1};
        constants.trace={2,c.bias,0,0};constants.quality.x=exact?1.f:0.f;constants.opaque.w=hardware?1.f:0.f;
        const std::array<glm::vec4,2> parameters{glm::vec4(c.p,c.maxDistance),glm::vec4(c.direction,0)};
        auto encoder=device.BeginFrame({0,1,1});Check(encoder.KeepAlive(owner),"Pin exact-volume fixture resources");
        Check(encoder.SetRenderTarget({target,1,1,RHICommand::ClearColor}),"Set exact-volume output");
        PipelineDetail::FiberVolumeUpload(encoder,uniform,constants);
        Check(encoder.UpdateUniformBufferBytes(query,parameters.data(),sizeof(parameters),0)&&encoder.BindUniformBuffer(query,8)&&
            encoder.BindTexture(images[index][0],12)&&encoder.BindTexture(images[index][1],13)&&encoder.BindPipeline(pipeline)&&
            encoder.BindMesh(quad)&&encoder.DrawIndexed({3}),"Draw actual N/G exact integration branches");
        Submit(device,encoder);const auto values=Read(device,output);Check(values.size()==4,"Read exact-volume result");
        for(float value:values)Check(std::isfinite(value),"Nonfinite exact-volume oracle");return glm::vec2(values[0],values[1]);
    };
    double maximumNError=0,maximumGError=0;unsigned negativeControls=0;
    for(unsigned i=0;i<cases.size();++i) {
        const auto exact=evaluate(i,true,false),filtered=evaluate(i,true,true),legacy=evaluate(i,false,false);
        maximumNError=std::max(maximumNError,std::abs(double(exact.x)-cases[i].expectedN));
        maximumGError=std::max(maximumGError,std::abs(double(exact.y)-cases[i].expectedG));
        if(std::abs(double(exact.x)-cases[i].expectedN)>2e-5||std::abs(double(exact.y)-cases[i].expectedG)>2e-5)
            std::cerr<<cases[i].label<<" exactN/G="<<exact.x<<'/'<<exact.y<<" expected="<<cases[i].expectedN<<'/'<<cases[i].expectedG<<'\n';
        Check(std::abs(double(exact.x)-cases[i].expectedN)<2e-5&&std::abs(double(exact.y)-cases[i].expectedG)<2e-5,"Exact cell N/G disagrees with independent field/analytic extrema oracle");
        Check(exact==filtered,"Exact field traversal must be independent of hardware atlas filtering precision");
        if(std::abs(double(legacy.x)-cases[i].expectedN)>.01||std::abs(double(legacy.y)-cases[i].expectedG)>.01)++negativeControls;
        Check(evaluate(i,false,false)==legacy,"Disabling exact integration did not restore legacy trace identity");
    }
    Check(negativeControls>=2,"Exact-volume fixtures failed to expose real growth-step N/G misses");
    // Exercise the production cache contract, not only shader-only uniforms.
    StrandMesh mesh(device);const std::array<StrandVertex,2> vertices{{{{-.1f,-.1f,-.1f,.0001f},{1,1,1,0}},{{.1f,.1f,.1f,.0001f},{1,1,1,1}}}};
    const std::array<std::uint32_t,2> line{0,1};Check(mesh.Update(vertices,line),"Create exact-volume cache geometry");
    StrandRenderItem item;item.geometry=mesh.Snapshot();item.material.spatialVolume=true;item.material.volumeResolution=8;item.material.volumeVoxelSize=.01f;
    PipelineDetail::StrandPassView view;view.sunIntensity=1;view.sunDirection={-1,0,0};view.lighting.lightColors.fill(glm::vec4(0));
    PipelineDetail::FiberVolumePass volume(device);
    const auto prepare=[&](){volume.Begin(1);auto encoder=device.BeginFrame({0,1,1});volume.Prepare(encoder,0,item,view);Submit(device,encoder);return volume.Statistics();};
    Check(prepare().passes==4,"Initial exact cache fixture must inject density and trace three paths");
    const auto density=Read(device,volume.DebugTextures().density),oldPaths=Read(device,volume.DebugTextures().paths);
    item.material.volumeExactIntegration=true;Check(prepare().passes==3,"Exact-quality change unnecessarily rebuilt density");
    Check(Read(device,volume.DebugTextures().density)==density,"Exact integration changed conserved physical density");
    Check(prepare().passes==0,"Paused exact-volume paths were not cached");
    item.material.volumeExactIntegration=false;Check(prepare().passes==3,"Disabled exact-quality change did not retrace paths");
    Check(Read(device,volume.DebugTextures().paths)==oldPaths,"Production cache failed exact-disabled legacy N identity");
    item.material.volumeExactIntegration=true;volume.Begin(1);auto cancelled=device.BeginFrame({0,1,1});volume.Prepare(cancelled,0,item,view);cancelled.Cancel();volume.Invalidate();
    Check(prepare().passes==4,"Cancelled exact-volume record was reused as valid GPU cache");volume.Reset();
    std::cout<<"Fiber exact trilinear cells: "<<cases.size()<<" analytic N/G cases, max error="<<maximumNError<<'/'<<maximumGError
        <<", growth-step negative controls="<<negativeControls<<"; filter independence, clipping, finite saturation, cache and cancel restoration passed\n";
}
} // namespace Render::TestData
