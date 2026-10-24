#pragma once
#include "Render/Test/strand_direct_shadow_test.h"
#include "Render/Private/Pipeline/strand_segment_shaders.h"
#include "Render/Private/Pipeline/strand_render_pass.h"
#include <limits>

namespace Render::TestData {
namespace StrandOpaqueShadowPowerTest {
using FiberNearAuditDetail::Check;
struct alignas(16) Input {glm::vec4 state{};}; // raw CSM, DS visibility, N
struct alignas(16) Output {glm::vec4 visibility{},beauty{},unshadowed{},oldRaw{},gains{};};
// Mirrors the production prefix plus LUT. The GLSL block is extracted from
// the real shader, so an offset/table-layout mismatch is visible on the GPU.
struct alignas(16) MaterialSnapshot {
    std::array<glm::vec4,15> prefix{};
    std::array<glm::vec4,256> moments{};
};
static_assert(sizeof(Input)==16&&sizeof(Output)==80);
static_assert(offsetof(MaterialSnapshot,moments)==240&&sizeof(MaterialSnapshot)==4336);
inline std::string Shader(){
    using namespace PipelineDetail;
    using StrandDirectShadowDetail::Function;
    const auto production=StrandFragmentShader(false,0,true,true);
    const auto block=production.find("layout(std140,binding=2) uniform StrandMaterial {");
    const auto end=production.find("\n};",block);
    Check(block!=std::string::npos&&end!=std::string::npos,"Locate production strand material UBO");
    const auto helper=production.find("float StrandOpaqueShadowVisibility(");
    const auto call=production.find("float opaqueVisibility=StrandOpaqueShadowVisibility(rawOpaqueVisibility,strandOpaqueShadowParameters.x);");
    const auto sun=production.find("    if(strandSunDirection.w>0.0) {");
    const auto locals=production.find("    if(strandSourceDirectOptions.x>.5) {",sun);
    Check(helper!=std::string::npos&&call>sun&&call<locals&&
          production.find("StrandOpaqueShadowVisibility(",call+70)==std::string::npos,
        "External shadow power escaped its single sun callsite into DS, IBL or local lights");
    const auto compute=StrandSegmentComputeShader(false,false);
    const auto raster=StrandSegmentFragmentShader(false,false);
    for(const auto* producer:{&compute,&raster})
        Check(producer->find("float opaqueVisibility=StrandOpaqueShadowVisibility(rawOpaqueVisibility,strandOpaqueShadowParameters.x);")!=std::string::npos,
            "Segment lighting omitted the per-material external shadow power");
    std::string result=R"GLSL(#version 450 core
layout(local_size_x=64) in;
)GLSL";
    result+=production.substr(block,end+3-block)+"\n";
    result+="#define FIBER_MOMENT_LUT 1\nvec4 fiberAtlas=vec4(0,0,0,1);\n";
    result+=FiberScatteringGLSL();
    result+=Function(production,"vec3 StrandSafe(");
    result+=Function(production,"float StrandLobe(");
    result+=Function(production,"vec3 StrandLight(vec3 base,vec3 T,vec3 V,vec3 L,vec3 radiance,float pathCount,float visibility) {");
    result+=Function(production,"float StrandOpaqueShadowVisibility(");
    result+=Function(production,"vec3 StrandExternallyShadowedLight(");
    result+=R"GLSL(
struct Input {vec4 state;};
struct Output {vec4 visibility,beauty,unshadowed,oldRaw,gains;};
layout(std430,set=2,binding=0) readonly buffer Inputs {Input inputs[];};
layout(std430,set=2,binding=1) writeonly buffer Outputs {Output outputs[];};
layout(push_constant) uniform Count {uvec4 dispatch;};
void main(){
    uint i=gl_GlobalInvocationID.x;if(i>=dispatch.x)return;
    vec3 state=inputs[i].state.xyz;float raw=state.x,ds=state.y,N=state.z;
    float externalVisibility=StrandOpaqueShadowVisibility(raw,strandOpaqueShadowParameters.x);
    vec3 base=vec3(.3,.18,.06),T=vec3(0,1,0),V=vec3(0,0,1),L=normalize(vec3(1,0,1));
    vec3 energy=vec3(1.1,.75,1.4);uint at=dispatch.y+i;
    outputs[at].visibility=vec4(externalVisibility,raw,strandOpaqueShadowParameters.x,strandIbl.x);
    outputs[at].beauty=vec4(StrandExternallyShadowedLight(base,T,V,L,energy,N,ds,externalVisibility),1);
    outputs[at].unshadowed=vec4(StrandLight(base,T,V,L,energy,N,ds),1);
    outputs[at].oldRaw=vec4(StrandExternallyShadowedLight(base,T,V,L,energy,N,ds,raw),1);
    outputs[at].gains=vec4(FiberDirectGain(N,strandIbl.x,ds),
        FiberDirectGain(N,strandIbl.x,ds*externalVisibility),ds,N);
}
)GLSL";
    return result;
}
} // namespace StrandOpaqueShadowPowerTest

// Root integration: include this header in strand_native_ds_gpu_test.cpp and
// call RunStrandOpaqueShadowPowerGpuOracle(device) on both RHI backends.
inline void RunStrandOpaqueShadowPowerGpuOracle(RHIDevice& device){
    using namespace StrandOpaqueShadowPowerTest;
    Check(StrandMaterial{}.opaqueShadowPower==1.f,"Default external shadow power changed legacy material behavior");
    std::vector<Input> rows;
    for(float raw:{0.f,1e-8f,.0001f,.01f,.125f,.25f,.5f,.75f,.9f,.99999f,1.f})
        for(float ds:{0.f,.4f,1.f})for(float n:{0.f,.5f,2.f})rows.push_back({{raw,ds,n,0}});
    std::vector<glm::vec2> policies;
    for(float power:{.05f,.5f,1.f,2.f,4.f})for(float scale:{.5f,1.f,2.f})policies.push_back({power,scale});
    PipelineDetail::ResourceBuilder resources(device);
    const auto uniform=resources.Uniform(sizeof(MaterialSnapshot));
    const auto color=resources.Texture(4,4,RHITextureFormat::RGBA16F);
    const auto depth=resources.Texture(4,4,RHITextureFormat::Depth32F);
    const auto target=resources.Target(color,depth);
    const auto owner=resources.Finish();
    auto compute=std::shared_ptr<Compute::Device>(Compute::Device::Borrow(device));
    Compute::FramePipelineDesc desc;desc.source=Shader();desc.label="per-material external CSM ShadowPow, independent DS gain";
    desc.uniformMask=1u<<2;desc.storageBuffers=2;desc.pushConstantBytes=16;
    const auto pipeline=compute->CreateFramePipeline(desc);
    const auto input=compute->CreateStorageBuffer(rows.size()*sizeof(Input),rows.data(),"external shadow raw visibility/DS inputs");
    const auto output=compute->CreateStorageBuffer(rows.size()*policies.size()*sizeof(Output),nullptr,"external shadow material snapshot outputs");
    auto encoder=device.BeginFrame({0,4,4});
    Check(encoder.KeepAlive(owner)&&encoder.KeepAlive(compute),"Retain external shadow oracle resources");
    for(unsigned c=0;c<policies.size();++c){
        MaterialSnapshot material;
        material.prefix[2]={.25f,.5f,.25f,.45f}; // surface
        material.prefix[3]={1,0,1,.02f}; // volume
        material.prefix[5]={1.55f,.5f,.8f,1}; // fiber
        material.prefix[6]={1,1,1,1}; // physical/source closure
        material.prefix[8]={policies[c].y,1,1,0}; // backscatter scale
        material.prefix[11].w=1; // source direct
        material.prefix[14]={policies[c].x,0,0,0}; // appended opaque shadow parameters
        material.moments.fill({.7f,.25f,.03f,.6f});
        const auto* data=reinterpret_cast<const std::byte*>(&material);
        for(std::uint32_t at=0;at<sizeof(material);at+=RHICommand::MaxInlineUniformBytes)
            Check(encoder.UpdateUniformBufferBytes(uniform,data+at,
                std::min<std::uint32_t>(RHICommand::MaxInlineUniformBytes,sizeof(material)-at),at),"Upload complete production material layout");
        Check(encoder.BindUniformBuffer(uniform,2),"Bind per-dispatch material snapshot");
        RHICommand::DispatchFrameCompute command;command.pipeline=pipeline;command.buffers[0]=input;command.buffers[1]=output;
        command.groupsX=std::uint32_t((rows.size()+63)/64);
        const glm::uvec4 count(std::uint32_t(rows.size()),c*std::uint32_t(rows.size()),0,0);
        command.pushBytes=sizeof(count);std::memcpy(command.pushConstants.data(),&count,sizeof(count));
        Check(encoder.DispatchCompute(command),"Record actual production shadow-power helpers");
    }
    FiberNearAuditDetail::Submit(device,encoder);
    const auto bytes=compute->Readback(output);Check(bytes.size()==rows.size()*policies.size()*sizeof(Output),"Read shadow-power oracle");
    std::vector<Output> got(rows.size()*policies.size());std::memcpy(got.data(),bytes.data(),bytes.size());
    double maxPowerError=0,maxEnergyError=0;unsigned checks=0,nonidentity=0,gainControl=0;
    for(unsigned c=0;c<policies.size();++c)for(unsigned i=0;i<rows.size();++i){
        const auto& v=got[c*rows.size()+i];const auto s=rows[i].state;const float power=policies[c].x,scale=policies[c].y;
        const double expected=s.x==0?0:(s.x==1?1:std::pow(double(s.x),double(power)));
        const double error=std::abs(v.visibility.x-expected);maxPowerError=std::max(maxPowerError,error);
        Check(error<=1e-7+2e-6*expected,"External CSM power differs from independent CPU pow oracle");++checks;
        Check(v.visibility.y==s.x&&v.visibility.z==power&&v.visibility.w==scale&&v.gains.z==s.y&&v.gains.w==s.z,
            "Material UBO snapshots or independent DS inputs were changed");++checks;
        if(power==1||s.x==0||s.x==1){Check(v.visibility.x==s.x,"External shadow power lost an exact identity/endpoint");++checks;}
        const double root=std::sqrt(double(scale));
        const double expectedGain=root+(double(scale)*scale-root)*s.y*std::exp(-std::max(double(s.z)-1.,0.));
        Check(std::abs(v.gains.x-expectedGain)<2e-6,"Shadow power modified source DS gain K");++checks;
        for(unsigned channel=0;channel<3;++channel){
            const double energy=double(v.unshadowed[channel])*v.visibility.x;
            const double e=std::abs(v.beauty[channel]-energy);maxEnergyError=std::max(maxEnergyError,e);
            Check(std::isfinite(v.beauty[channel])&&v.beauty[channel]>=0&&e<=1e-7+2e-6*std::abs(energy),
                "Shadow power changed DS closure instead of only external direct energy");++checks;
            if(power==1){Check(v.beauty[channel]==v.oldRaw[channel],"Default power1 changed old direct-light output");++checks;}
            if(power!=1&&s.x>0&&s.x<1&&s.y>0)nonidentity+=std::abs(v.beauty[channel]-v.oldRaw[channel])>1e-7f;
        }
        gainControl+=scale!=1&&s.x>0&&s.x<1&&s.y>0&&std::abs(v.gains.x-v.gains.y)>1e-7f;
    }
    Check(nonidentity>0&&gainControl>0,"Shadow-power and CSM-dependent-K negative controls did not expose changes");
    // Record must reject invalid per-material values before drawing. Use real
    // geometry/target handles so another invalid precondition cannot mask it.
    StrandMesh mesh(device);
    const std::array<StrandVertex,2> points{{{{0,-.5f,0,.01f},{0,1,0,0}},{{0,.5f,0,.01f},{0,1,0,1}}}};
    const std::array<std::uint32_t,2> indices{0,1};Check(mesh.Update(points,indices),mesh.LastError().c_str());
    PipelineDetail::StrandRenderPass pass(device);
    const PipelineDetail::StrandPassTarget passTarget{target,color,depth,4,4,1};PipelineDetail::StrandPassView view;
    for(float invalid:{-.1f,0.f,.049f,4.01f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}){
        StrandRenderItem item;item.geometry=mesh.Snapshot();item.material.opaqueShadowPower=invalid;
        auto rejected=device.BeginFrame({0,4,4});bool failed=false;
        try{pass.Record(rejected,std::span<const StrandRenderItem>(&item,1),passTarget,view);}
        catch(const std::invalid_argument&){failed=true;}
        Check(failed&&rejected.Cancel(),"Invalid external shadow power was accepted by Record");++checks;
    }
    Check(compute->Diagnostics().validationErrors==0,"Shadow-power oracle generated RHI compute validation errors");
    compute->Destroy(input);compute->Destroy(output);compute->Destroy(pipeline);
    std::cout<<"External CSM ShadowPow: "<<rows.size()*policies.size()<<" rows / "<<checks
        <<" checks, power1 identity, raw0/1 exact, independent DS K, complete material UBO, invalid Record; maxPowerError="
        <<maxPowerError<<", maxEnergyError="<<maxEnergyError<<'\n';
}
} // namespace Render::TestData
