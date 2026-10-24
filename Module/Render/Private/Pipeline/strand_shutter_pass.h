#pragma once
#include "Render/Private/Pipeline/strand_render_pass.h"
#include "Render/Private/Pipeline/pipeline_resource_builder.h"
#include "Render/Private/Pipeline/post_process_shaders.h"
#include "Render/Private/Pipeline/temporal_effects_shaders.h"
#include <glm/gtc/matrix_inverse.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace Render::PipelineDetail {
struct StrandShutterStatistics {
    std::uint32_t samples=0,passes=0,rasterPasses=0,volumePasses=0,blockerPasses=0;
    bool historyUsed=false,stableOutput=false;
};
inline std::string StrandShutterAverageShader(){return R"GLSL(#version 450 core
layout(binding=0) uniform sampler2D shutter0;
layout(binding=1) uniform sampler2D shutter1;
layout(binding=2) uniform sampler2D shutter2;
layout(binding=3) uniform sampler2D shutter3;
layout(std140,binding=0) uniform ShutterResolve {vec4 shutterUV;};
in vec2 vUV;layout(location=0) out vec4 outColor;
vec4 ShutterTexel(sampler2D image,ivec2 p){
    vec4 value=texelFetch(image,clamp(p,ivec2(0),textureSize(image,0)-1),0);
    float alpha=isnan(value.a)||isinf(value.a)?0.0:clamp(value.a,0.0,1.0);
    vec3 color=mix(value.rgb,vec3(0.0),isnan(value.rgb));color=clamp(color,vec3(0.0),vec3(60000.0));
    return vec4(color*alpha,alpha);
}
vec4 ShutterSample(sampler2D image,vec2 uv){
    vec2 p=uv*vec2(textureSize(image,0))-.5;ivec2 base=ivec2(floor(p));vec2 f=fract(p);
    return mix(mix(ShutterTexel(image,base),ShutterTexel(image,base+ivec2(1,0)),f.x),
        mix(ShutterTexel(image,base+ivec2(0,1)),ShutterTexel(image,base+ivec2(1,1)),f.x),f.y);
}
void main(){
    vec2 uv=vUV+shutterUV.xy;
    // Each independent sample is STRAIGHT RGBA; premultiply before spatial
    // filtering, then average time. Alpha-over between times would thicken hair.
    outColor=.25*(ShutterSample(shutter0,uv)+ShutterSample(shutter1,uv)+
        ShutterSample(shutter2,uv)+ShutterSample(shutter3,uv));
}
)GLSL";}

// Optional four-midpoint, trailing-shutter host reconstruction. The caller
// supplies exposureFraction=shutterSeconds/(CURRENT-LAST COMPLETED timestamp),
// in [0,1], and rejects cuts/topology/width changes through the existing prior
// geometry/model validity. No temporal state is inferred from a recorded frame.
// A private scratch protects the main pass's depth/motion/debug textures. All
// four samples share ONE current-pose density/G/lighting cache; this does not
// reconstruct time-varying self shadows or the native engine's motion blur.
class StrandShutterPass {
    RHIDevice& device_;
    StrandRenderPass scratch_;
    std::shared_ptr<const void> programs_,targets_;
    RenderResourceHandle<VertexBufferSpec> fullscreen_;
    RenderResourceHandle<PipelineSpec> depthCopy_,average_;
    RenderResourceHandle<UniformBufferSpec> constants_;
    std::array<RenderResourceHandle<RHITextureSpec>,4> samples_{};
    std::array<RenderResourceHandle<RenderTargetSpec>,4> sampleTargets_{};
    RenderResourceHandle<RHITextureSpec> opaqueDepth_,averaged_;
    RenderResourceHandle<RenderTargetSpec> depthTarget_,averageTarget_;
    std::uint32_t width_=0,height_=0;
    StrandShutterStatistics stats_;
    static void Require(bool okay,const char* message){if(!okay)throw std::runtime_error(message);}
    void Prepare(std::uint32_t width,std::uint32_t height){
        if(!programs_){ResourceBuilder builder(device_);fullscreen_=builder.FullscreenMesh();constants_=builder.Uniform(sizeof(glm::vec4));
            depthCopy_=builder.FullscreenPipeline(FullscreenVertexShader(),DepthHistoryCopyShader(),true);
            average_=builder.FullscreenPipeline(FullscreenVertexShader(),StrandShutterAverageShader());programs_=builder.Finish();}
        if(targets_&&width_==width&&height_==height)return;
        ResourceBuilder builder(device_);opaqueDepth_=builder.Texture(width,height,RHITextureFormat::Depth32F);
        depthTarget_=builder.Target({},opaqueDepth_);
        for(unsigned i=0;i<4;++i){samples_[i]=builder.Texture(width,height,RHITextureFormat::RGBA16F);sampleTargets_[i]=builder.Target(samples_[i],opaqueDepth_);}
        averaged_=builder.Texture(width,height,RHITextureFormat::RGBA32F);averageTarget_=builder.Target(averaged_,{});
        targets_=builder.Finish();width_=width;height_=height;
    }
public:
    explicit StrandShutterPass(RHIDevice& device):device_(device),scratch_(device){}
    void Record(RHIFrameEncoder& encoder,std::span<const StrandRenderItem> items,const StrandPassTarget& target,
        const StrandPassView& view,float exposureFraction,bool stableOutput=true){
        stats_={};
        if(!std::isfinite(exposureFraction)||exposureFraction<0.f||exposureFraction>1.f||!target.width||!target.height||target.samples!=1||!view.opaqueDepth.IsValid())
            throw std::invalid_argument("Geometry shutter requires fraction[0,1], single-sample output and separate opaque depth");
        Prepare(target.width,target.height);
        Require(encoder.KeepAlive(programs_)&&encoder.KeepAlive(targets_),"Pin strand shutter resources");
        Require(encoder.SetRenderTarget({depthTarget_,width_,height_,RHICommand::ClearDepth}),"Bind shutter independent opaque-depth copy");
        Require(encoder.BindPipeline(depthCopy_)&&encoder.BindMesh(fullscreen_)&&encoder.BindTexture(view.opaqueDepth,2)&&encoder.DrawIndexed({3}),"Copy opaque depth for shutter samples");
        for(unsigned i=0;i<4;++i){
            Require(encoder.SetRenderTarget({sampleTargets_[i],width_,height_,RHICommand::ClearColor}),"Clear independent shutter layer");
            auto sampleView=view;sampleView.outputTemporalData=sampleView.outputMotionData=false;sampleView.outputStraightColor=true;
            sampleView.shutterSample=1.f-exposureFraction*(1.f-(float(i)+.5f)*.25f);
            scratch_.Record(encoder,items,{sampleTargets_[i],samples_[i],opaqueDepth_,width_,height_,1},sampleView);
            stats_.passes+=scratch_.PassCount();stats_.rasterPasses+=scratch_.TransparencyRasterPassCount();
            stats_.volumePasses+=scratch_.VolumeStatistics().passes;stats_.blockerPasses+=scratch_.VolumeStatistics().blockerPasses;
        }
        glm::vec4 resolve(0);
        if(stableOutput){
            const float determinant=glm::determinant(view.currentUnjitteredViewProjection);
            if(!std::isfinite(determinant)||std::abs(determinant)<1e-20f)throw std::invalid_argument("Shutter stable projection must be invertible");
            const auto jitter=view.viewProjection*glm::inverse(view.currentUnjitteredViewProjection)*glm::vec4(0,0,0,1);
            if(!std::isfinite(jitter.x)||!std::isfinite(jitter.y)||!std::isfinite(jitter.w)||std::abs(jitter.w)<1e-8f)throw std::invalid_argument("Shutter stable jitter is invalid");
            resolve={.5f*jitter.x/jitter.w,.5f*jitter.y/jitter.w,0,0};
        }
        Require(encoder.SetRenderTarget({averageTarget_,width_,height_,RHICommand::ClearColor}),"Bind premultiplied shutter average");
        Require(encoder.UpdateUniformBufferBytes(constants_,&resolve,sizeof(resolve),0)&&encoder.BindUniformBuffer(constants_,0)&&
            encoder.BindPipeline(average_)&&encoder.BindMesh(fullscreen_),"Bind shutter averaging program");
        for(unsigned i=0;i<4;++i)Require(encoder.BindTexture(samples_[i],i),"Bind independent shutter sample");
        Require(encoder.DrawIndexed({3}),"Average premultiplied shutter samples");
        stats_.samples=4;stats_.passes+=2;stats_.stableOutput=stableOutput;
        stats_.historyUsed=view.motionHistoryValid&&exposureFraction>0.f&&std::any_of(items.begin(),items.end(),[](const auto& item){
            return item.geometry&&item.previousTransformValid&&item.geometry->PreviousInputMatched();});
    }
    RenderResourceHandle<RHITextureSpec> PremultipliedColor()const{return averaged_;}
    const std::array<RenderResourceHandle<RHITextureSpec>,4>& SampleTextures()const{return samples_;}
    const StrandShutterStatistics& Statistics()const{return stats_;}
    void InvalidateVolume(){scratch_.InvalidateVolume();}
    void Shutdown(){scratch_.Shutdown();targets_.reset();programs_.reset();samples_={};sampleTargets_={};opaqueDepth_={};averaged_={};
        depthTarget_={};averageTarget_={};constants_={};fullscreen_={};depthCopy_={};average_={};width_=height_=0;stats_={};}
};
} // namespace Render::PipelineDetail
