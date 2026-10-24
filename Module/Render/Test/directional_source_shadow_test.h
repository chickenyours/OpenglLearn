#pragma once
#include "Render/Public/Material/pbr_material.h"
#include "Render/Private/Pipeline/strand_segment_shaders.h"
#include "Render/Public/Compute/gpu_compute.h"
#include "Render/Test/fiber_near_shadow_test.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>

namespace Render::TestData {
namespace SourceDirectionalShadowTest {
using FiberNearAuditDetail::Check;
struct alignas(16) Input {glm::vec4 local{},policy{};}; // uv,z,cascade; slope,use END,angle,END
struct alignas(16) Output {glm::vec4 cascade{},directional{};};
static_assert(sizeof(Output)==32);
inline std::string Shader(){
    const auto compute=PipelineDetail::StrandSegmentComputeShader(false,false);
    const auto raster=PipelineDetail::StrandSegmentFragmentShader(false,false);
    for(const auto* source:{&compute,&raster})
        Check(source->find("strandSourceShadowAngle=DirectionalSourceShadowAngle(record.y);")!=std::string::npos,
            "Native segment source-END shadow rotation is missing from a backend producer");
    const auto fragment=PipelineDetail::StrandFragmentShader(false,0,false,false);
    Check(fragment.find("strandSourceShadowAngle=sourceShadowFilter.z;")!=std::string::npos&&
          fragment.find("shadowDx,shadowDy,strandSourceShadowAngle)")!=std::string::npos,
        "Fragment/segment lighting no longer shares the shadow phase contract");
    return std::string(R"GLSL(#version 450 core
layout(local_size_x=64) in;
layout(binding=12) uniform sampler2D shadowAtlas;
)GLSL")+Material::PbrSceneEffectsGLSL()+Material::PbrDirectionalShadowGLSL()+R"GLSL(
struct Input {vec4 local,policy;};
struct Output {vec4 cascade,directional;};
layout(std430,set=2,binding=0) readonly buffer Inputs {Input inputs[];};
layout(std430,set=2,binding=1) writeonly buffer Outputs {Output outputs[];};
layout(push_constant) uniform Count {uvec4 dispatch;};
void main(){
    uint index=gl_GlobalInvocationID.x;if(index>=dispatch.x)return;
    vec4 p=inputs[index].local,policy=inputs[index].policy;
    float angle=policy.y>.5?DirectionalSourceShadowAngle(uint(policy.w)):policy.z;
    vec3 position=p.xyz*2.0-1.0;
    vec3 dx=vec3(2.0/128.0,0,2.0*policy.x/128.0),dy=vec3(0,2.0/128.0,0);
    vec3 normal=normalize(vec3(-policy.x,0,1));
    outputs[dispatch.y+index].cascade=vec4(
        CascadeVisibility(int(p.w),position,normal,dx,dy,angle),
        CascadeVisibility(int(p.w),position,normal,dx,dy),
        angle,CascadeVisibility(int(p.w),position,normal,dx,dy,0.0));
    // The fixture's main-view depths lie inside cascade0 for every row.
    // This separately exercises the actual directional wrappers, including
    // their forwarding of phase and per-consumer radius to each cascade.
    outputs[dispatch.y+index].directional=vec4(
        DirectionalVisibility(position,normal,dx,dy,angle),
        DirectionalVisibility(position,normal,dx,dy),
        angle,DirectionalVisibility(position,normal,dx,dy,0.0));
}
)GLSL";
}
inline double ExpectedAngle(const Input& input){
    if(input.policy.y<.5f)return input.policy.z;
    // Independent source f32 arithmetic, not a call to the production helper.
    const float turns=float(std::uint32_t(input.policy.w))*0.0027777778450399637f;
    return float((turns-std::floor(turns))*6.2831854820251465f);
}
// Analytic visibility of a vertical binary depth step: four-neighbor compare
// then interpolation is exactly a one-texel linear coverage ramp. No depth
// atlas access or production implementation is used by this CPU oracle.
inline double ExpectedStep(const Input& in,const SceneEffectsConstants& scene,double angle,bool depthFirst=false,double radiusUv=-1){
    constexpr std::array<std::array<double,2>,8> source{{
        {{-.6455659866333008,.0266201663762331}},{{-.4189446568489075,.8573530316352844}},
        {{.04460543394088745,.5316708087921143}},{{.5317319631576538,.3773360848426819}},
        {{.649772047996521,-.1614876240491867}},{{.12475734949111938,-.2891661524772644}},
        {{.2474198341369629,-.8544320464134216}},{{-.47650110721588135,-.6853070855140686}}}};
    const double center=double(in.local.x)*128.0-.5;
    if(scene.sourceShadowFilter.x<.5f){
        double sum=0,weight=0;const auto start=int(std::floor(center))-1;
        for(int x=0;x<4;++x){const int texel=start+x;const double w=std::max(2.0-std::abs(texel-center),0.0);
            sum+=(texel>=64?1.0:0.0)*w;weight+=w;}
        return sum/weight;
    }
    const double radius=std::max((radiusUv>=0?radiusUv:double(scene.sourceShadowFilter.y))*128.0,1.5);
    double sum=0;
    for(const auto& point:source){const double x=center+radius*(point[0]*std::sin(angle)+point[1]*std::cos(angle));
        sum+=depthFirst?(x>=63.5?1.0:0.0):std::clamp(x-63.0,0.0,1.0);}
    return sum/8.0;
}
} // namespace SourceDirectionalShadowTest

// Include in an existing RHI GPU test and invoke on each backend. No native
// graphics calls: real production receiver GLSL, a sampled RHI texture and
// immutable SceneEffects UBO snapshots, FrameCompute, then SSBO readback.
inline void RunDirectionalSourceShadowGpuOracle(RHIDevice& device){
    using namespace SourceDirectionalShadowTest;
    static_assert(offsetof(SceneEffectsConstants,shadowFilter)==528&&offsetof(SceneEffectsConstants,sourceShadowFilter)==544);
    std::vector<Input> rows;
    for(unsigned angle=0;angle<3;++angle)for(int x=-96;x<=96;++x){
        const float u=.5f+float(x)/(128.f*8.f);
        rows.push_back({{u,.47f,.5f,0},{0,angle==2?1.f:0.f,angle==1?.731f:0.f,419475}});
    }
    for(unsigned cascade=1;cascade<4;++cascade)for(float u:{.00001f,.001f,.1f,.47f,.9f,.999f,.99999f})
        for(float v:{.00001f,.4f,.99999f})for(float angle:{0.f,.731f}){
            const float slope=cascade==3?.2f:0.f;
            rows.push_back({{u,v,.5f+slope*(u-.5f),float(cascade)},{slope,0,angle,0}});
        }
    // First tile is a sharp step; adjacent tiles are completely lit, completely
    // dark, and a sloping self-receiving plane. Near-edge taps must not leak.
    std::vector<glm::vec4> atlas(256u*256u,glm::vec4(0));
    for(unsigned y=0;y<256;++y)for(unsigned x=0;x<256;++x){const unsigned c=x/128+2*(y/128),ix=x%128;
        const float depth=c==0?(ix<64?.25f:.75f):(c==1?.75f:(c==2?.25f:.5f+.2f*((float(ix)+.5f)/128.f-.5f)));
        atlas[y*256+x]=glm::vec4(depth);}
    PipelineDetail::ResourceBuilder resources(device);
    CreateRHITextureSpec textureDesc;textureDesc.width=textureDesc.height=256;textureDesc.mipmaps=false;
    textureDesc.textureDataStoreType=textureDesc.textureUseType=RHITextureFormat::RGBA32F;
    textureDesc.filterMode=RHIFilterMode::Nearest;textureDesc.addressMode=RHIAddressMode::ClampToEdge;textureDesc.data=atlas.data();
    const auto texture=resources.Create<RHITextureSpec>([&](auto cb){device.async_CreateTexture(textureDesc,cb);});
    const auto uniform=resources.Uniform(sizeof(SceneEffectsConstants));const auto owner=resources.Finish();
    auto compute=std::shared_ptr<Compute::Device>(Compute::Device::Borrow(device));
    Compute::FramePipelineDesc desc;desc.source=Shader();desc.label="directional source Poisson8 analytic step/plane/tile regression";
    desc.uniformMask=1u<<4;desc.sampledMask=1u<<12;desc.storageBuffers=2;desc.pushConstantBytes=16;
    const auto pipeline=compute->CreateFramePipeline(desc);
    std::vector<SceneEffectsConstants> scenes;
    // Cases6/7 override only the surface radius. Case8 explicitly equals the
    // inherited radius; case9 proves Tent ignores the override; case10 proves
    // a surface override may also be narrower than the strand radius.
    const std::array<glm::vec4,11> states{{
        {0,.003f,0,0},{0,.05f,.731f,0},{1,0,0,0},{1,.003f,.731f,0},{1,.02f,0,0},{1,.05f,.731f,0},
        {1,.003f,.731f,.012f},{1,.003f,.731f,.05f},{1,.003f,.731f,.003f},{0,.003f,0,.05f},{1,.02f,0,.003f}}};
    for(const auto& state:states){
        SceneEffectsConstants s;s.lightViewProjection.fill(glm::mat4(1));s.cascadeWorldTexelSize=glm::vec4(2.f/128.f);
        s.cascadeInverseDepthRange=glm::vec4(.5f);s.sunDirectionIntensity={0,0,-1,1};
        s.shadowParams={.00001f,0,0,0};s.shadowFilter={0,0,0,4};s.sourceShadowFilter=state;
        s.effects.x=1;s.mainView[3].z=-.75f;s.cascadeSplits={1,2,3,4};scenes.push_back(s);
    }
    const auto inputs=compute->CreateStorageBuffer(rows.size()*sizeof(Input),rows.data(),"directional shadow geometric samples");
    const auto outputs=compute->CreateStorageBuffer(rows.size()*scenes.size()*sizeof(Output),nullptr,"directional shadow actual visibility");
    auto encoder=device.BeginFrame({0,1,1});Check(encoder.KeepAlive(owner)&&encoder.KeepAlive(compute),"Retain source shadow oracle resources");
    Check(encoder.BindTexture(texture,12),"Bind source shadow synthetic atlas");
    for(unsigned caseIndex=0;caseIndex<scenes.size();++caseIndex){const auto* bytes=reinterpret_cast<const std::byte*>(&scenes[caseIndex]);
        for(std::uint32_t at=0;at<sizeof(SceneEffectsConstants);at+=RHICommand::MaxInlineUniformBytes){
            const auto count=std::min<std::uint32_t>(RHICommand::MaxInlineUniformBytes,sizeof(SceneEffectsConstants)-at);
            Check(encoder.UpdateUniformBufferBytes(uniform,bytes+at,count,at),"Upload source shadow policy snapshot");}
        Check(encoder.BindUniformBuffer(uniform,4),"Bind source shadow policy snapshot");
        RHICommand::DispatchFrameCompute command;command.pipeline=pipeline;command.buffers[0]=inputs;command.buffers[1]=outputs;
        command.groupsX=std::uint32_t((rows.size()+63)/64);const glm::uvec4 count(std::uint32_t(rows.size()),caseIndex*std::uint32_t(rows.size()),0,0);
        command.pushBytes=sizeof(count);std::memcpy(command.pushConstants.data(),&count,sizeof(count));
        Check(encoder.DispatchCompute(command),"Dispatch production directional shadow receiver");
    }
    FiberNearAuditDetail::Submit(device,encoder);const auto bytes=compute->Readback(outputs);
    Check(bytes.size()==rows.size()*scenes.size()*sizeof(Output),"Read source shadow oracle outputs");
    std::vector<Output> got(rows.size()*scenes.size());std::memcpy(got.data(),bytes.data(),bytes.size());
    double maxError=0;unsigned partial=0,depthFirstExposed=0,phaseExposed=0,surfaceStudyChanged=0;
    std::array<unsigned,11> surfacePartial{};
    for(unsigned c=0;c<scenes.size();++c)for(unsigned i=0;i<rows.size();++i){const auto& row=rows[i];const auto& actual=got[c*rows.size()+i].cascade;
        const double angle=ExpectedAngle(row);const int cascade=int(row.local.w);
        const double surfaceRadius=scenes[c].sourceShadowFilter.w>0?scenes[c].sourceShadowFilter.w:scenes[c].sourceShadowFilter.y;
        const double expected=cascade==0?ExpectedStep(row,scenes[c],angle):(cascade==2?0.:1.);
        const double fixedExpected=cascade==0?ExpectedStep(row,scenes[c],scenes[c].sourceShadowFilter.z,false,surfaceRadius):(cascade==2?0.:1.);
        const double zeroExpected=cascade==0?ExpectedStep(row,scenes[c],0):(cascade==2?0.:1.);
        const double error=std::max({std::abs(actual.x-expected),std::abs(actual.y-fixedExpected),std::abs(actual.w-zeroExpected)});
        maxError=std::max(maxError,error);
        Check(std::isfinite(actual.x)&&actual.x>=0&&actual.x<=1.000001f&&error<4e-5,"Directional shadow differs from analytic step/constant/plane/tile oracle");
        Check(std::abs(actual.z-angle)<1e-6,"Source shadow rotation differs from original END-index f32 DAG");
        if(cascade>0)Check(std::abs(actual.x-expected)<1e-6,"Poisson taps leaked across cascade tiles or self-shadowed a fitted receiver plane");
        if(scenes[c].sourceShadowFilter.x<.5f)Check(actual.x==actual.w,"Legacy tent unexpectedly depends on source phase");
        else if(cascade==0){partial+=actual.x>0&&actual.x<1;
            depthFirstExposed+=std::abs(expected-ExpectedStep(row,scenes[c],angle,true))>.01;
            phaseExposed+=std::abs(expected-zeroExpected)>.01;}
        const auto& directional=got[c*rows.size()+i].directional;
        const double directionalExpected=ExpectedStep(row,scenes[c],angle);
        const double directionalFixed=ExpectedStep(row,scenes[c],scenes[c].sourceShadowFilter.z,false,surfaceRadius);
        const double directionalZero=ExpectedStep(row,scenes[c],0);
        const double directionalError=std::max({std::abs(directional.x-directionalExpected),std::abs(directional.y-directionalFixed),std::abs(directional.w-directionalZero)});
        maxError=std::max(maxError,directionalError);
        Check(directionalError<4e-5&&std::abs(directional.z-angle)<1e-6,"Directional wrappers dropped surface radius or source phase");
        if(cascade==0&&actual.y>1e-7f&&actual.y<.999999f)++surfacePartial[c];
        if(c>=6&&c<=8){const auto& original=got[3*rows.size()+i];
            Check(actual.x==original.cascade.x&&actual.w==original.cascade.w&&
                  directional.x==original.directional.x&&directional.w==original.directional.w,
                "HOST surface radius leaked into an explicit-phase strand lookup at fixed projections");
            if(c==6)surfaceStudyChanged+=std::abs(actual.y-original.cascade.y)>1e-6f;
            if(c==8)Check(actual==original.cascade&&directional==original.directional,
                "Zero-inherit and explicit equal surface radius differ");
        }
        if(c==9){const auto& original=got[i];Check(actual==original.cascade&&directional==original.directional,
            "Default Tent changed when only HOST surface radius was set");}
    }
    Check(partial>100&&depthFirstExposed>50&&phaseExposed>50,"Poisson penumbra, compare-before-interpolation or phase negative control was not exercised");
    Check(surfaceStudyChanged>0&&surfacePartial[7]>surfacePartial[3]+30,
        "Surface-only overrides did not expose the .012 study change and wider penumbra");
    Check(compute->Diagnostics().validationErrors==0,"Source directional shadow regression produced validation errors");
    compute->Destroy(inputs);compute->Destroy(outputs);compute->Destroy(pipeline);
    std::cout<<"Directional source Poisson8: "<<rows.size()*scenes.size()<<" rows, analytic step/constant/tile-edge/receiver-plane; default Tent phase identity; maxError="
        <<maxError<<" partial="<<partial<<" depthFirstNegative="<<depthFirstExposed<<" phaseNegative="<<phaseExposed
        <<"; HOST surface radius .012 changed="<<surfaceStudyChanged<<" wider-surface partial="<<surfacePartial[7]
        <<" inherited="<<surfacePartial[3]<<", fixed-projection strand identity; 0-inherit/equal override/Tent identities\n";
}
} // namespace Render::TestData
