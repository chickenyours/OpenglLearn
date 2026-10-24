#pragma once
#include "Render/Private/Pipeline/strand_shaders.h"
#include "Render/Public/Compute/gpu_compute.h"
#include "Render/Test/fiber_near_shadow_test.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace Render::TestData {
namespace StrandDirectShadowDetail {
using FiberNearAuditDetail::Check;
inline std::string Function(const std::string& text,const char* signature){
    const auto start=text.find(signature);Check(start!=std::string::npos,"Locate production direct-shadow function");
    auto end=text.find('{',start);Check(end!=std::string::npos,"Production direct-shadow function has no body");
    unsigned depth=0;for(;end<text.size();++end){if(text[end]=='{')++depth;else if(text[end]=='}'&&!--depth)return text.substr(start,end+1-start)+"\n";}
    throw std::runtime_error("Production direct-shadow function body is incomplete");
}
struct alignas(16) Input {glm::vec4 state{},mode{};}; // scale, external, DS, N; source, physical
struct alignas(16) Output {
    glm::vec4 beauty{},ballistic{},oldCombined{},oldCombinedBallistic{},unshadowed{},unshadowedBallistic{},gains{};
};
static_assert(sizeof(Input)==32&&sizeof(Output)==112);
inline std::string Shader(){
    using namespace PipelineDetail;
    const auto production=StrandFragmentShader(false,0,true,true);
    // These checks cover the actual sun callsites as well as the extracted
    // helpers: a future reversion of only beauty or diagnostics cannot hide
    // behind a correct, unused helper in this regression.
    Check(production.find("color+=StrandExternallyShadowedLight(")!=std::string::npos&&
          production.find("directColor+=StrandExternallyShadowedLight(")!=std::string::npos&&
          production.find("ballisticDirect+=StrandDebugExternallyShadowedBallisticLight(")!=std::string::npos,
        "Sun beauty/direct/ballistic do not share the separated visibility contract");
    const auto beauty=production.find("color+=StrandExternallyShadowedLight(");
    const auto direct=production.find("directColor+=StrandExternallyShadowedLight(");
    const auto ballistic=production.find("ballisticDirect+=StrandDebugExternallyShadowedBallisticLight(");
    for(auto at:{beauty,direct,ballistic}){
        const auto end=production.find(';',at);
        Check(production.substr(at,end-at).find("paths.x,directVisibility.x,opaqueVisibility")!=std::string::npos,
            "Sun source callsite merged or swapped DS visibility and external CSM");
    }
    std::string shader=R"GLSL(#version 450 core
layout(local_size_x=64) in;
layout(std140,binding=9) uniform DirectShadowMoments {vec4 strandMomentLut[256];};
vec4 strandSurface,strandVolume,strandFiber,strandScattering,strandColorVariation,strandIbl,fiberAtlas;
#define FIBER_MOMENT_LUT 1
)GLSL";
    shader+=FiberScatteringGLSL();
    shader+=Function(production,"vec3 StrandSafe(");
    shader+=Function(production,"float StrandLobe(");
    shader+=Function(production,"vec3 StrandLight(vec3 base,vec3 T,vec3 V,vec3 L,vec3 radiance,float pathCount,float visibility) {");
    shader+=Function(production,"vec3 StrandExternallyShadowedLight(");
    const auto debug=StrandDebugGLSL();
    shader+=Function(debug,"vec3 StrandDebugBallisticLight(");
    shader+=Function(debug,"vec3 StrandDebugExternallyShadowedBallisticLight(");
    shader+=R"GLSL(
struct DirectShadowInput {vec4 state,mode;};
struct DirectShadowOutput {vec4 beauty,ballistic,oldCombined,oldCombinedBallistic,unshadowed,unshadowedBallistic,gains;};
layout(std430,set=2,binding=0) readonly buffer DirectShadowInputs {DirectShadowInput inputs[];};
layout(std430,set=2,binding=1) writeonly buffer DirectShadowOutputs {DirectShadowOutput outputs[];};
layout(push_constant) uniform DirectShadowCount {uvec4 count;};
void main(){
    uint index=gl_GlobalInvocationID.x;if(index>=count.x)return;
    vec4 state=inputs[index].state;
    float scale=state.x,externalVisibility=state.y,dsVisibility=state.z,N=state.w;
    strandSurface=vec4(.25,.5,.25,.45);strandVolume=vec4(1,0,1,.02);
    strandFiber=vec4(1.55,.5,.8,1);strandScattering=vec4(inputs[index].mode.y,1,1,1);
    strandColorVariation=vec4(0,0,0,inputs[index].mode.x);strandIbl=vec4(scale,1,1,0);fiberAtlas=vec4(0,0,0,1);
    vec3 base=vec3(.3,.18,.06),T=vec3(0,1,0),V=vec3(0,0,1),L=normalize(vec3(1,0,1));
    vec3 energy=vec3(1.1,.75,1.4);float combined=externalVisibility*dsVisibility;
    outputs[index].beauty=vec4(StrandExternallyShadowedLight(base,T,V,L,energy,N,dsVisibility,externalVisibility),1);
    outputs[index].ballistic=vec4(StrandDebugExternallyShadowedBallisticLight(base,T,V,L,energy,N,dsVisibility,externalVisibility,vec3(1)),1);
    // Old merged-input behavior is an intentional negative control for s!=1.
    outputs[index].oldCombined=vec4(StrandLight(base,T,V,L,energy,N,combined),1);
    outputs[index].oldCombinedBallistic=vec4(StrandDebugBallisticLight(base,T,V,L,energy,N,combined,vec3(1)),1);
    outputs[index].unshadowed=vec4(StrandLight(base,T,V,L,energy,N,dsVisibility),1);
    outputs[index].unshadowedBallistic=vec4(StrandDebugBallisticLight(base,T,V,L,energy,N,dsVisibility,vec3(1)),1);
    outputs[index].gains=vec4(FiberDirectGain(N,scale,dsVisibility),FiberDirectGain(N,scale,combined),dsVisibility,externalVisibility);
}
)GLSL";
    return shader;
}
} // namespace StrandDirectShadowDetail

// Root integration: include this header in strand_native_ds_gpu_test.cpp and
// invoke RunStrandDirectShadowGpuOracle(device) before the suite's final
// backend-validation check. One RHI compute dispatch, no native graphics API.
inline void RunStrandDirectShadowGpuOracle(RHIDevice& device){
    using namespace StrandDirectShadowDetail;
    std::vector<Input> inputs;
    for(unsigned mode=0;mode<3;++mode)for(float scale:{.5f,1.f,2.f})
        for(float n:{0.f,.5f,2.f})for(float ds:{0.f,.4f,.8f,1.f})for(float external:{0.f,.25f,.5f,1.f})
            inputs.push_back({{scale,external,ds,n},{mode==0?1.f:0.f,mode==2?0.f:1.f,0,0}});
    auto compute=std::shared_ptr<Compute::Device>(Compute::Device::Borrow(device));
    Check(compute->Diagnostics().frameAvailable,"Direct-shadow regression requires RHI FrameCompute");
    Compute::FramePipelineDesc description;description.source=Shader();description.label="source direct DS gain versus external CSM separation";
    description.uniformMask=1u<<9;description.storageBuffers=2;description.pushConstantBytes=16;
    const auto pipeline=compute->CreateFramePipeline(description);
    const auto input=compute->CreateStorageBuffer(inputs.size()*sizeof(Input),inputs.data(),"direct DS/CSM regression inputs");
    const auto output=compute->CreateStorageBuffer(inputs.size()*sizeof(Output),nullptr,"direct DS/CSM regression outputs");
    std::array<glm::vec4,256> moments;moments.fill({.7f,.25f,.03f,.6f});
    PipelineDetail::ResourceBuilder resources(device);const auto uniform=resources.Uniform(sizeof(moments));const auto owner=resources.Finish();
    auto encoder=device.BeginFrame({0,1,1});Check(encoder.KeepAlive(compute)&&encoder.KeepAlive(owner),"Pin direct-shadow regression resources");
    const auto* bytes=reinterpret_cast<const std::byte*>(moments.data());
    for(std::uint32_t at=0;at<sizeof(moments);at+=256)
        Check(encoder.UpdateUniformBufferBytes(uniform,bytes+at,256,at),"Upload direct-shadow fixed moment field");
    Check(encoder.BindUniformBuffer(uniform,9),"Bind direct-shadow fixed moment field");
    RHICommand::DispatchFrameCompute command;command.pipeline=pipeline;command.buffers[0]=input;command.buffers[1]=output;
    command.groupsX=std::uint32_t((inputs.size()+63)/64);const glm::uvec4 count(std::uint32_t(inputs.size()),0,0,0);
    command.pushBytes=sizeof(count);std::memcpy(command.pushConstants.data(),&count,sizeof(count));
    Check(encoder.DispatchCompute(command),"Record production direct-shadow regression helpers");
    FiberNearAuditDetail::Submit(device,encoder);const auto result=compute->Readback(output);
    Check(result.size()==inputs.size()*sizeof(Output),"Read direct-shadow regression results");
    std::vector<Output> actual(inputs.size());std::memcpy(actual.data(),result.data(),result.size());
    unsigned checks=0;std::array<unsigned,2> exposedFull{},exposedBallistic{};double maximumLinearError=0;
    for(std::size_t i=0;i<inputs.size();++i){
        const auto in=inputs[i];const auto& got=actual[i];const float external=in.state.y,ds=in.state.z,s=in.state.x,n=in.state.w;
        const bool source=in.mode.x>.5f;
        const double expectedGain=std::sqrt(double(s))+(double(s)*s-std::sqrt(double(s)))*ds*std::exp(-std::max(double(n)-1.,0.));
        Check(std::abs(double(got.gains.x)-expectedGain)<2e-6,"Source gain does not consume the fixed independent DS visibility");++checks;
        for(unsigned channel=0;channel<3;++channel){
            for(auto value:{got.beauty[channel],got.ballistic[channel],got.oldCombined[channel],got.oldCombinedBallistic[channel]})
                Check(std::isfinite(value)&&value>=0,"Direct-shadow regression produced invalid energy");
            const double fullExpected=double(got.unshadowed[channel])*external;
            const double ballisticExpected=double(got.unshadowedBallistic[channel])*external;
            const double fullError=std::abs(double(got.beauty[channel])-fullExpected);
            const double ballisticError=std::abs(double(got.ballistic[channel])-ballisticExpected);
            maximumLinearError=std::max({maximumLinearError,fullError,ballisticError});
            Check(fullError<=1e-7+2e-6*std::abs(fullExpected),"External CSM changed source direct gain instead of scaling final energy linearly");
            Check(ballisticError<=1e-7+2e-6*std::abs(ballisticExpected),"Ballistic diagnostics use a different external CSM gain contract");checks+=2;
            if(external==0||ds==0){Check(got.beauty[channel]==0&&got.ballistic[channel]==0,"Complete DS or CSM occlusion must give zero energy");++checks;}
            if(!source||s==1||external==1){
                Check(got.beauty[channel]==got.oldCombined[channel]&&got.ballistic[channel]==got.oldCombinedBallistic[channel],
                    "Generic/non-source or source backscale1/unshadowed behavior changed");++checks;
            }
            if(source&&s!=1&&external>0&&external<1&&ds>0){
                const unsigned index=s<1?0u:1u;
                exposedFull[index]+=std::abs(got.beauty[channel]-got.oldCombined[channel])>1e-7f;
                exposedBallistic[index]+=std::abs(got.ballistic[channel]-got.oldCombinedBallistic[channel])>1e-7f;
            }
        }
    }
    for(unsigned i=0;i<2;++i)Check(exposedFull[i]>0&&exposedBallistic[i]>0,
        "The s=.5/2 negative control did not expose the old CSM-dependent source gain");
    Check(compute->Diagnostics().validationErrors==0,"Direct-shadow regression generated compute validation errors");
    compute->Destroy(input);compute->Destroy(output);compute->Destroy(pipeline);
    std::cout<<"Source direct external-shadow separation: "<<inputs.size()<<" rows / "<<checks
        <<" checks; s=.5/2 CSM-linear beauty+ballistic, independent DS K, s1 identity, generic/tangent identities; maxLinearError="
        <<maximumLinearError<<"; changed negative-control RGB full="<<exposedFull[0]<<','<<exposedFull[1]
        <<" ballistic="<<exposedBallistic[0]<<','<<exposedBallistic[1]<<'\n';
}
} // namespace Render::TestData
