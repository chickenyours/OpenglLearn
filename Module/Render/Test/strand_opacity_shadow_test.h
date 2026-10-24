#pragma once
#include "Render/Private/Pipeline/strand_opacity_shadow_pass.h"
#include "Render/Public/Material/pbr_material.h"
#include "Render/Public/Compute/gpu_compute.h"
#include "Render/Test/fiber_near_shadow_test.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <utility>
#include <vector>

namespace Render::TestData {
namespace StrandOpacityShadowTest {
using FiberNearAuditDetail::Check;
inline constexpr unsigned Atlas=128,Opacity=Atlas/2,Tile=Opacity/2,OpaqueTile=Atlas/2;
inline constexpr std::array<double,8> Nodes{0,1./256,1./128,1./64,1./32,1./16,1./4,1};
struct Prefix {float nearest=1;std::array<float,8> tau{};};
using Pixels=std::vector<glm::vec4>;
inline Prefix Decode(const Pixels& pixels,unsigned x,unsigned y){
    const auto a=pixels[y*Atlas+x],b=pixels[y*Atlas+x+Opacity],c=pixels[(y+Opacity)*Atlas+x];
    return {a.g,{a.b,a.a,b.g,b.b,b.a,c.g,c.b,c.a}};
}
inline void Store(Pixels& pixels,unsigned x,unsigned y,const Prefix& p){
    auto& a=pixels[y*Atlas+x];auto& b=pixels[y*Atlas+x+Opacity];auto& c=pixels[(y+Opacity)*Atlas+x];
    a.g=p.nearest;a.b=p.tau[0];a.a=p.tau[1];b.g=p.tau[2];b.b=p.tau[3];b.a=p.tau[4];
    c.g=p.tau[5];c.b=p.tau[6];c.a=p.tau[7];
}
inline Pixels Read(RHIDevice& device,RenderResourceHandle<RHITextureSpec> texture){
    const auto values=FiberNearAuditDetail::Read(device,texture);
    Check(values.size()==Atlas*Atlas*4,"DOM8 packed texture readback dimensions");
    Pixels pixels(Atlas*Atlas);std::memcpy(pixels.data(),values.data(),values.size()*sizeof(float));return pixels;
}
inline unsigned OriginX(unsigned cascade,unsigned tile){return cascade%2*tile;}
inline unsigned OriginY(unsigned cascade,unsigned tile){return cascade/2*tile;}
inline double PrefixT(const Prefix& p,double span,double depth,double opacityScale=1){
    if(depth<p.nearest)return 1;
    const auto transmittance=[&](unsigned i){return std::exp(-std::clamp(double(p.tau[i])*std::max(opacityScale,0.),0.,80.));};
    const double z=(depth-p.nearest)/std::max(span,1e-8);
    if(z>=1)return transmittance(7);
    unsigned lo=0;while(lo<6&&z>Nodes[lo+1])++lo;
    const double phase=std::clamp((z-Nodes[lo])/(Nodes[lo+1]-Nodes[lo]),0.,1.);
    return transmittance(lo)*(1-phase)+transmittance(lo+1)*phase;
}
inline double BilinearT(const Pixels& pixels,const glm::vec4& spans,unsigned cascade,double u,double v,double depth,
                        double gradientX=0,double gradientY=0,double correctionLimit=0,double opacityScale=1){
    const double x=u*Tile-.5,y=v*Tile-.5;const int ix=int(std::floor(x)),iy=int(std::floor(y));
    const double fx=x-ix,fy=y-iy;double sum=0;
    for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx){
        const unsigned px=OriginX(cascade,Tile)+unsigned(std::clamp(ix+dx,0,int(Tile)-1));
        const unsigned py=OriginY(cascade,Tile)+unsigned(std::clamp(iy+dy,0,int(Tile)-1));
        const double correction=gradientX*((px-OriginX(cascade,Tile)+.5)/Tile-u)+gradientY*((py-OriginY(cascade,Tile)+.5)/Tile-v);
        const double sampleDepth=depth+std::clamp(correction,-correctionLimit,correctionLimit);
        sum+=PrefixT(Decode(pixels,px,py),spans[cascade],sampleDepth,opacityScale)*(dx?fx:1-fx)*(dy?fy:1-fy);
    }return sum;
}
// CPU reference uses a depth step and analytic bilinear weights, with no call
// to the production sampler. It preserves compare -> per-texel T -> filtering.
inline double Visibility(const Pixels& pixels,const glm::vec4& spans,unsigned cascade,double u,double v,double depth,
                         bool opacity,unsigned filter,double radius,double opacityScale=1){
    const auto tap=[&](int x,int y){
        x=std::clamp(x,0,int(OpaqueTile)-1);y=std::clamp(y,0,int(OpaqueTile)-1);
        const auto stored=pixels[(OriginY(cascade,OpaqueTile)+unsigned(y))*Atlas+OriginX(cascade,OpaqueTile)+unsigned(x)].r;
        if(depth>stored)return 0.;
        return opacity?BilinearT(pixels,spans,cascade,(x+.5)/OpaqueTile,(y+.5)/OpaqueTile,depth,0,0,0,opacityScale):1.;
    };
    if(filter==0){
        const double x=u*OpaqueTile-.5,y=v*OpaqueTile-.5;const int ix=int(std::floor(x))-1,iy=int(std::floor(y))-1;
        double sum=0,total=0;
        for(int dy=0;dy<4;++dy)for(int dx=0;dx<4;++dx){
            const double weight=std::max(2.-std::abs(ix+dx-x),0.)*std::max(2.-std::abs(iy+dy-y),0.);
            sum+=tap(ix+dx,iy+dy)*weight;total+=weight;
        }return sum/total;
    }
    constexpr std::array<std::array<double,2>,8> poisson{{
        {{-.6455659866333008,.0266201663762331}},{{-.4189446568489075,.8573530316352844}},
        {{.04460543394088745,.5316708087921143}},{{.5317319631576538,.3773360848426819}},
        {{.649772047996521,-.1614876240491867}},{{.12475734949111938,-.2891661524772644}},
        {{.2474198341369629,-.8544320464134216}},{{-.47650110721588135,-.6853070855140686}}}};
    const double footprint=std::max(radius,1.5/OpaqueTile);double sum=0;
    // Both surface and explicit-phase controls use zero rotation in this test.
    for(const auto& p:poisson){const double x=(u+p[1]*footprint)*OpaqueTile-.5,y=(v-p[0]*footprint)*OpaqueTile-.5;
        const int ix=int(std::floor(x)),iy=int(std::floor(y));const double fx=x-ix,fy=y-iy;
        for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx)
            sum+=tap(ix+dx,iy+dy)*(dx?fx:1-fx)*(dy?fy:1-fy)/8.;
    }return sum;
}
struct alignas(16) Probe {glm::vec4 local{},plane{};}; // uv,depth,cascade; depth-gradient xy,correction limit
struct alignas(16) Answer {glm::vec4 local{},directional{};};
static_assert(sizeof(Probe)==32&&sizeof(Answer)==32);
inline std::string Shader(){
    return std::string(R"GLSL(#version 450 core
layout(local_size_x=64) in;
layout(binding=12) uniform sampler2D shadowAtlas;
)GLSL")+Material::PbrSceneEffectsGLSL()+Material::PbrDirectionalShadowGLSL()+R"GLSL(
struct Probe {vec4 local,plane;};struct Answer {vec4 local,directional;};
layout(std430,set=2,binding=0) readonly buffer Probes {Probe probes[];};
layout(std430,set=2,binding=1) writeonly buffer Answers {Answer answers[];};
layout(push_constant) uniform Count {uvec4 dispatch;};
void main(){
    uint i=gl_GlobalInvocationID.x;if(i>=dispatch.x)return;
    vec4 q=probes[i].local;int cascade=int(q.w),opacitySize=textureSize(shadowAtlas,0).x/2,tile=opacitySize/2;
    ivec2 origin=ivec2(cascade%2,cascade/2)*tile;
    ivec2 p=origin+clamp(ivec2(floor(q.xy*float(tile))),ivec2(0),ivec2(tile-1));
    vec3 world=q.xyz*2.0-1.0,n=vec3(0,0,1),dx=vec3(2.0/64.0,0,0),dy=vec3(0,2.0/64.0,0);
    answers[i].local=vec4(DirectionalStrandOpacityTexel(cascade,p,q.z),
        DirectionalStrandShadowOpacity(cascade,q.xy,q.z),
        CascadeVisibility(cascade,world,n,dx,dy),CascadeVisibility(cascade,world,n,dx,dy,0.0));
    answers[i].directional=vec4(DirectionalVisibility(world,n,dx,dy),
        DirectionalVisibility(world,n,dx,dy,0.0),
        CascadeVisibility(cascade,world,n,dx,dy,0.0,sourceShadowFilter.y),
        DirectionalStrandShadowOpacityPlane(cascade,q.xy,q.z,probes[i].plane.xy,probes[i].plane.z));
}
)GLSL";
}
class Consumer {
    RHIDevice& device_;std::shared_ptr<Compute::Device> compute_;
    std::shared_ptr<const void> owner_;RenderResourceHandle<UniformBufferSpec> uniform_;Compute::PipelineHandle pipeline_;
public:
    std::size_t rows=0;double maximumError=0;
    explicit Consumer(RHIDevice& device):device_(device),compute_(Compute::Device::Borrow(device)){
        PipelineDetail::ResourceBuilder b(device);uniform_=b.Uniform(sizeof(SceneEffectsConstants));owner_=b.Finish();
        Compute::FramePipelineDesc desc;desc.source=Shader();desc.label="HOST DOM8 production packed/prefix/PCF oracle";
        desc.uniformMask=1u<<4;desc.sampledMask=1u<<12;desc.storageBuffers=2;desc.pushConstantBytes=16;
        pipeline_=compute_->CreateFramePipeline(desc);
    }
    ~Consumer(){compute_->Destroy(pipeline_);}
    std::vector<Answer> Run(RenderResourceHandle<RHITextureSpec> image,const Pixels& pixels,const glm::vec4& spans,
                            const std::vector<Probe>& probes,bool enabled=true,unsigned filter=0,float radius=.003f,float opacityScale=1.f){
        SceneEffectsConstants s;s.lightViewProjection.fill(glm::mat4(1));s.cascadeWorldTexelSize=glm::vec4(2.f/OpaqueTile);
        s.cascadeInverseDepthRange=glm::vec4(.5f);s.sunDirectionIntensity={0,0,-1,1};s.shadowParams=glm::vec4(0);
        s.shadowFilter=glm::vec4(0);s.sourceShadowFilter={float(filter),radius,0,0};s.strandShadowOpacity.x=enabled?1.f:0.f;
        s.strandShadowOpacity.y=opacityScale;
        s.strandShadowDepthSpan=spans;s.effects.x=1;s.mainView[3].z=-2;s.cascadeSplits={4,5,6,7};
        const auto input=compute_->CreateStorageBuffer(probes.size()*sizeof(Probe),probes.data(),"DOM8 independent geometric probes");
        const auto output=compute_->CreateStorageBuffer(probes.size()*sizeof(Answer),nullptr,"DOM8 production consumer results");
        auto encoder=device_.BeginFrame({0,1,1});Check(encoder.KeepAlive(owner_)&&encoder.KeepAlive(compute_),"Pin DOM8 oracle resources");
        Check(encoder.SetRenderTarget({{},1,1,0}),"Detach packed DOM8 producer attachment before sampling");
        Check(encoder.BindTexture(image,12),"Bind packed DOM8 oracle atlas");
        const auto* data=reinterpret_cast<const std::byte*>(&s);
        for(std::uint32_t at=0;at<sizeof(s);at+=RHICommand::MaxInlineUniformBytes)
            Check(encoder.UpdateUniformBufferBytes(uniform_,data+at,std::min<std::uint32_t>(RHICommand::MaxInlineUniformBytes,sizeof(s)-at),at),"Upload DOM8 scene snapshot");
        Check(encoder.BindUniformBuffer(uniform_,4),"Bind DOM8 scene snapshot");
        RHICommand::DispatchFrameCompute dispatch;dispatch.pipeline=pipeline_;dispatch.buffers[0]=input;dispatch.buffers[1]=output;
        dispatch.groupsX=std::uint32_t((probes.size()+63)/64);const glm::uvec4 count(std::uint32_t(probes.size()),0,0,0);
        dispatch.pushBytes=sizeof(count);std::memcpy(dispatch.pushConstants.data(),&count,sizeof(count));
        Check(encoder.DispatchCompute(dispatch),"Record actual DOM8 consumer");FiberNearAuditDetail::Submit(device_,encoder);
        const auto bytes=compute_->Readback(output);Check(bytes.size()==probes.size()*sizeof(Answer),"DOM8 consumer readback size");
        std::vector<Answer> got(probes.size());std::memcpy(got.data(),bytes.data(),bytes.size());
        for(std::size_t i=0;i<probes.size();++i){const auto q=probes[i].local;const unsigned c=unsigned(q.w);
            const unsigned x=OriginX(c,Tile)+unsigned(std::clamp(int(std::floor(q.x*Tile)),0,int(Tile)-1));
            const unsigned y=OriginY(c,Tile)+unsigned(std::clamp(int(std::floor(q.y*Tile)),0,int(Tile)-1));
            const double texel=PrefixT(Decode(pixels,x,y),spans[c],q.z,opacityScale);
            const double point=enabled?BilinearT(pixels,spans,c,q.x,q.y,q.z,0,0,0,opacityScale):1.;
            const double surface=Visibility(pixels,spans,c,q.x,q.y,q.z,enabled,filter,radius,opacityScale);
            const double hair=Visibility(pixels,spans,c,q.x,q.y,q.z,false,filter,radius);
            const double directional=Visibility(pixels,spans,0,q.x,q.y,q.z,enabled,filter,radius,opacityScale);
            const double hairDirectional=Visibility(pixels,spans,0,q.x,q.y,q.z,false,filter,radius);
            const auto plane=probes[i].plane;
            const double fitted=enabled?BilinearT(pixels,spans,c,q.x,q.y,q.z,plane.x,plane.y,plane.z,opacityScale):1.;
            const std::array<double,8> expected{texel,point,surface,hair,directional,hairDirectional,hair,fitted};
            std::array<float,8> actual{};std::memcpy(actual.data(),&got[i],sizeof(Answer));
            for(unsigned channel=0;channel<8;++channel){const double error=std::abs(double(actual[channel])-expected[channel]);
                maximumError=std::max(maximumError,error);
                if(!std::isfinite(actual[channel])||error>5e-5){
                    std::cerr<<"DOM8 consumer mismatch row="<<i<<" channel="<<channel<<" cascade="<<c<<" depth="<<q.z
                        <<" enabled="<<enabled<<" filter="<<filter<<" scale="<<opacityScale<<" actual="<<actual[channel]<<" expected="<<expected[channel]<<'\n';
                    throw std::runtime_error("HOST DOM8 packed prefix, T interpolation or same-tap PCF differs from CPU oracle");
                }
            }
            if(opacityScale==0)Check(got[i].local.y==1.f&&got[i].local.z==got[i].local.w&&
                got[i].directional.x==got[i].directional.y,"DOM8 scale0 did not preserve exact opaque-only visibility");
        }
        rows+=probes.size();Check(compute_->Diagnostics().validationErrors==0,"DOM8 consumer compute validation error");
        compute_->Destroy(input);compute_->Destroy(output);return got;
    }
};
inline RenderResourceHandle<RHITextureSpec> Texture(PipelineDetail::ResourceBuilder& b,RHIDevice& device,const Pixels& pixels){
    CreateRHITextureSpec d;d.width=d.height=Atlas;d.mipmaps=false;d.textureDataStoreType=d.textureUseType=RHITextureFormat::RGBA32F;
    d.filterMode=RHIFilterMode::Nearest;d.addressMode=RHIAddressMode::ClampToEdge;d.data=pixels.data();
    return b.Create<RHITextureSpec>([&](auto cb){device.async_CreateTexture(d,cb);});
}
inline void CheckSynthetic(RHIDevice& device,Consumer& consumer){
    Pixels nodes(Atlas*Atlas,glm::vec4(.9f,0,0,0));const glm::vec4 spans(.3f,.32f,.34f,.36f);
    for(unsigned c=0;c<4;++c)for(unsigned y=0;y<Tile;++y)for(unsigned x=0;x<Tile;++x){
        Prefix p;p.nearest=.15f+.06f*c;for(unsigned k=0;k<8;++k)p.tau[k]=.125f*float((k+1)*(c+1));
        Store(nodes,OriginX(c,Tile)+x,OriginY(c,Tile)+y,p);
    }
    std::vector<Probe> probes;
    for(unsigned c=0;c<4;++c){const auto p=Decode(nodes,OriginX(c,Tile),OriginY(c,Tile));
        std::vector<float> depths{p.nearest-.025f,p.nearest+spans[c]+.025f};
        // Query just after the first discontinuity: reconstructing world Z
        // and projecting it back can round one ULP across nearest-hair depth.
        // The remaining node boundaries are continuous in transmittance.
        for(unsigned k=0;k<8;++k)depths.push_back(p.nearest+float(k?Nodes[k]:1e-5)*spans[c]);
        for(unsigned k=0;k<7;++k)depths.push_back(p.nearest+float((Nodes[k]+Nodes[k+1])*.5)*spans[c]);
        for(float depth:depths)for(const glm::vec2 uv:{glm::vec2(.00001f),glm::vec2(.5f),glm::vec2(.99999f),glm::vec2(.00001f,.99999f)})
            probes.push_back({{uv,depth,float(c)}});
    }
    Pixels step(Atlas*Atlas,glm::vec4(.9f,0,0,0));
    for(unsigned c=0;c<4;++c)for(unsigned y=0;y<Tile;++y)for(unsigned x=0;x<Tile;++x){
        Prefix p;p.nearest=.2f;p.tau.fill(c==0?(x<Tile/2?0.f:float(std::log(4.))):float(std::log(double(c)+1)));
        Store(step,OriginX(c,Tile)+x,OriginY(c,Tile)+y,p);
    }
    // The independent opaque step deliberately cuts through the continuous
    // hair step, exposing a consumer that multiplies two separately filtered maps.
    Pixels joint=step;
    for(unsigned c=0;c<4;++c)for(unsigned y=0;y<OpaqueTile;++y)for(unsigned x=0;x<OpaqueTile;++x)
        joint[(OriginY(c,OpaqueTile)+y)*Atlas+OriginX(c,OpaqueTile)+x].r=x<OpaqueTile/2?.3f:.9f;
    std::vector<Probe> edge;
    for(unsigned c=0;c<4;++c)for(int k=-16;k<=16;++k)
        edge.push_back({{.5f+float(k)/(Tile*16.f),.5f,.7f,float(c)}});
    Pixels plane(Atlas*Atlas,glm::vec4(.9f,0,0,0));std::vector<Probe> fittedProbes;
    for(unsigned c=0;c<4;++c){
        for(unsigned y=0;y<Tile;++y)for(unsigned x=0;x<Tile;++x){Prefix p;
            p.nearest=.4f+.3f*((x+.5f)/Tile-.5f)+.2f*((y+.5f)/Tile-.5f);p.tau.fill(float(std::log(4.)));
            Store(plane,OriginX(c,Tile)+x,OriginY(c,Tile)+y,p);
        }
        for(float u:{.00001f,.27f,.5f,.72f,.99999f})for(float v:{.00001f,.51f,.99999f})
            fittedProbes.push_back({{u,v,.4f+.3f*(u-.5f)+.2f*(v-.5f)-.0001f,float(c)},{.3f,.2f,.05f,0}});
    }
    PipelineDetail::ResourceBuilder b(device);const auto nodeImage=Texture(b,device,nodes),stepImage=Texture(b,device,step),jointImage=Texture(b,device,joint),planeImage=Texture(b,device,plane);const auto owner=b.Finish();
    const auto nodeResult=consumer.Run(nodeImage,nodes,spans,probes);
    consumer.Run(nodeImage,nodes,spans,probes,false);
    consumer.Run(nodeImage,nodes,spans,probes,true,1,.02f);
    const auto stepResult=consumer.Run(stepImage,step,spans,edge);
    const auto center=stepResult[16].local;
    Check(std::abs(center.y-.625f)<2e-6f&&std::abs(center.y-.5f)>.1f,"DOM8 half-texel T average did not expose average-tau negative control");
    consumer.Run(stepImage,step,spans,edge,true,1,0);
    const auto jointResult=consumer.Run(jointImage,joint,spans,edge);
    consumer.Run(jointImage,joint,spans,edge,true,1,.02f);
    const auto disabled=consumer.Run(jointImage,joint,spans,edge,false);
    for(const auto& a:disabled)Check(a.local.z==a.local.w&&a.directional.x==a.directional.y,"Disabled DOM8 changed opaque-only CSM wrappers");
    unsigned wrongTau=0,wrongSeparate=0;
    for(const auto& q:edge){const unsigned c=unsigned(q.local.w);const double u=q.local.x,v=q.local.y,z=q.local.z;
        const double actual=Visibility(joint,spans,c,u,v,z,true,0,.003);
        const double separate=Visibility(joint,spans,c,u,v,z,false,0,.003)*BilinearT(joint,spans,c,u,v,z);
        wrongSeparate+=std::abs(actual-separate)>.001;
        if(c==0&&u>.484375&&u<.515625){const double phase=u*Tile-15.5;
            wrongTau+=std::abs(BilinearT(step,spans,0,u,v,z)-std::exp(-std::clamp(phase,0.,1.)*std::log(4.)))>.01;
        }
    }
    Check(wrongTau>0&&wrongSeparate>0,"DOM8 filtering negative controls were not exercised");
    const auto fitted=consumer.Run(planeImage,plane,spans,fittedProbes);unsigned falseSelfShadow=0;
    for(const auto& a:fitted){Check(std::abs(a.directional.w-1.f)<2e-6f,"DOM8 coarse-texel receiver-plane correction self-shadowed a front receiver");
        falseSelfShadow+=a.local.y<.99f;}
    Check(falseSelfShadow>0,"DOM8 receiver-plane test did not expose the flat-depth negative control");
    unsigned postMixScaleNegative=0;
    for(float scale:{.5f,.75f,0.f,1.f,2.f}){
        const auto scaledNodes=consumer.Run(nodeImage,nodes,spans,probes,true,0,.003f,scale);
        const auto scaledStep=consumer.Run(stepImage,step,spans,edge,true,0,.003f,scale);
        const auto scaledJoint=consumer.Run(jointImage,joint,spans,edge,true,0,.003f,scale);
        consumer.Run(jointImage,joint,spans,edge,true,1,.02f,scale);
        const double expectedCenter=.5*(1.+std::pow(.25,double(scale)));
        Check(std::abs(double(scaledStep[16].local.y)-expectedCenter)<2e-6,
            "DOM8 opacity scale was not applied to each prefix before spatial T interpolation");
        if(scale>0&&scale<1)Check(std::abs(double(scaledStep[16].local.y)-std::pow(.625,double(scale)))>.015,
            "DOM8 spatial scale test did not reject exponentiation of already-filtered T");
        for(std::size_t i=0;i<probes.size();++i){
            const auto& a=scaledNodes[i];const auto& base=nodeResult[i];
            Check(a.local.w==base.local.w&&a.directional.y==base.directional.y&&a.directional.z==base.directional.z,
                "DOM8 opacity scale leaked into an explicit-phase hair wrapper");
            if(scale==0)Check(std::abs(a.local.y-1.f)<2e-6f&&std::abs(a.local.z-a.local.w)<2e-6f&&
                std::abs(a.directional.x-a.directional.y)<2e-6f,"Zero DOM8 scale changed opaque-only CSM visibility");
            if(scale>0&&scale<1)postMixScaleNegative+=std::abs(double(a.local.x)-std::pow(double(base.local.x),double(scale)))>1e-4;
        }
        for(std::size_t i=0;i<edge.size();++i){const auto& a=scaledJoint[i];const auto& base=jointResult[i];
            Check(a.local.w==base.local.w&&a.directional.y==base.directional.y&&a.directional.z==base.directional.z,
                "DOM8 opacity scale changed the original opaque comparison/hair path");
            if(base.local.w==0)Check(a.local.z==0,"DOM8 opacity scaling lifted a completely opaque-blocked receiver");
            if(scale==0)Check(std::abs(a.local.z-a.local.w)<2e-6f&&std::abs(a.directional.x-a.directional.y)<2e-6f,
                "Zero DOM8 scale bypassed or altered the independent opaque comparison");
        }
        if(scale==1)Check(std::memcmp(scaledNodes.data(),nodeResult.data(),nodeResult.size()*sizeof(Answer))==0&&
            std::memcmp(scaledStep.data(),stepResult.data(),stepResult.size()*sizeof(Answer))==0&&
            std::memcmp(scaledJoint.data(),jointResult.data(),jointResult.size()*sizeof(Answer))==0,
            "DOM8 scale1 did not reproduce the original consumer snapshot exactly");
    }
    Check(postMixScaleNegative>0,"DOM8 depth-node scale order negative control was not exercised");
    std::cout<<"HOST DOM8 opacity scale:0/.5/.75/1/2, per-prefix then depth/spatial T interpolation; scale1 exact, scale0 exact opaque-only, unchanged hair wrappers; post-mix negative rows="
        <<postMixScaleNegative<<'\n';
}
inline void CheckProducer(RHIDevice& device,Consumer& consumer){
    PipelineDetail::ResourceBuilder b(device);const auto depth=b.Texture(Atlas,Atlas,RHITextureFormat::Depth32F);
    const auto target=b.Target({},depth);const auto owner=b.Finish();PipelineDetail::StrandOpacityShadowPass pass(device);
    StrandMesh mesh(device);const std::array<StrandVertex,2> vertices{{{{0,-.8f,-.4f,.125f},{0,1,0,0}},{{0,.8f,-.4f,.125f},{0,1,0,1}}}};
    const std::array<std::uint32_t,2> indices{0,1};Check(mesh.Update(vertices,indices),"Create DOM8 physical ribbon fixture");
    std::array<StrandRenderItem,2> items;for(auto& i:items){i.geometry=mesh.Snapshot();i.material.castDirectionalShadows=true;}
    items[0].material.opacity=.25f;items[1].material.opacity=.5f;items[1].model[3].z=.5f;
    std::array<glm::mat4,4> projection;projection.fill(glm::mat4(1));
    const auto run=[&](std::span<const StrandRenderItem> casters,const std::array<glm::mat4,4>& vp){
        auto encoder=device.BeginFrame({0,Atlas,Atlas});Check(encoder.KeepAlive(owner),"Pin DOM8 opaque source target");
        RHICommand::SetRenderTarget clear;clear.target=target;clear.width=clear.height=Atlas;clear.clearFlags=RHICommand::ClearDepth;clear.clearDepth=.9f;
        Check(encoder.SetRenderTarget(clear),"Clear DOM8 original opaque CSM");
        const auto result=pass.Record(encoder,casters,vp,{0,0,1},Atlas,depth);FiberNearAuditDetail::Submit(device,encoder);
        Check(result.combinedAtlas.IsValid(),"DOM8 producer did not return a packed atlas");
        auto pixels=Read(device,result.combinedAtlas);
        for(const auto& p:pixels)Check(p.r==.9f,"DOM8 packing changed opaque depth R");
        const auto original=FiberNearAuditDetail::Read(device,depth);for(float d:original)Check(d==.9f,"DOM8 caster wrote its original opaque depth attachment");
        return std::pair{result,std::move(pixels)};
    };
    const auto [forward,first]=run(items,projection);
    Check(forward.nearestDraws==8&&forward.tauDraws==8&&forward.packPasses==1,"DOM8 actual producer draw accounting");
    const double nearTau=-std::log(.75),allTau=-std::log(.375);std::vector<Probe> probes;
    for(unsigned c=0;c<4;++c){const auto p=Decode(first,OriginX(c,Tile)+Tile/2,OriginY(c,Tile)+Tile/2);
        Check(std::abs(p.nearest-.3f)<2e-6f,"DOM8 nearest pass did not retain the front physical ribbon depth");
        double lo=1e20,hi=-1e20;
        for(const auto& item:items)for(unsigned corner=0;corner<8;++corner){const auto mn=item.geometry->BoundsMin(),mx=item.geometry->BoundsMax();
            const glm::vec3 local((corner&1)?mx.x:mn.x,(corner&2)?mx.y:mn.y,(corner&4)?mx.z:mn.z);
            const auto clip=projection[c]*item.model*glm::vec4(local,1);const double z=.5*double(clip.z/clip.w)+.5;lo=std::min(lo,z);hi=std::max(hi,z);
        }
        // The production span conservatively expands the endpoint AABB by
        // one full physical width. Verify both grooms fit, while rejecting an
        // arbitrary scene/cascade depth range that would waste the near nodes.
        Check(forward.depthSpans[c]>=hi-lo-3e-6&&forward.depthSpans[c]<=hi-lo+.12501,
            "DOM8 depth span omitted a caster or escaped the groom/full-width envelope");
        for(unsigned k=0;k<8;++k){const double node=p.nearest+Nodes[k]*forward.depthSpans[c];
            const double expected=nearTau+(node>=.55-2e-7?-std::log(.5):0.);
            Check(std::abs(double(p.tau[k])-expected)<3e-5,"DOM8 producer cumulative-node opacity differs from independent ribbon depths/alphas");
            if(k)Check(p.tau[k]>=p.tau[k-1],"DOM8 cumulative optical depth decreased");
            probes.push_back({{.5f,.5f,float(node+(k?0.:1e-5*forward.depthSpans[c])),float(c)}});
        }
        Check(std::abs(std::exp(-p.tau[7])-.375)<3e-6&&std::abs(p.tau[7]-allTau)<3e-5,"DOM8 overlapping .25/.5 alpha did not produce T=.375");
        probes.push_back({{.5f,.5f,p.nearest-.01f,float(c)}});probes.push_back({{.5f,.5f,.8f,float(c)}});
    }
    consumer.Run(forward.combinedAtlas,first,forward.depthSpans,probes);
    for(float scale:{.5f,.75f,0.f,1.f,2.f})consumer.Run(forward.combinedAtlas,first,forward.depthSpans,probes,true,0,.003f,scale);
    const auto afterConsumer=Read(device,forward.combinedAtlas);
    Check(std::memcmp(first.data(),afterConsumer.data(),first.size()*sizeof(glm::vec4))==0,
        "DOM8 consumer opacity scale modified the producer nearest/prefix/opaque texture");
    std::reverse(items.begin(),items.end());const auto [reverse,reordered]=run(items,projection);
    double orderError=0;for(std::size_t i=0;i<first.size();++i)for(unsigned c=0;c<4;++c)
        orderError=std::max(orderError,std::abs(double(first[i][c])-reordered[i][c]));
    Check(orderError<3e-6,"DOM8 additive opacity depended on draw order");
    auto curved=items;for(auto& item:curved)item.material.sourceCurveGeometry=true;
    const auto [curveResult,curvePixels]=run(curved,projection);
    Check(curveResult.curveStatistics.curvePieces>0,"DOM8 source-curve caster branch was not exercised");
    for(unsigned c=0;c<4;++c){const auto p=Decode(curvePixels,OriginX(c,Tile)+Tile/2,OriginY(c,Tile)+Tile/2);
        Check(std::abs(p.nearest-.3f)<2e-6f&&std::abs(std::exp(-p.tau[7])-.375)<5e-6,
            "DOM8 source-curve subdivision changed a straight groom's continuous coverage");
    }
    auto tiny=items[1];tiny.material.opacity=1.f/512;const std::array<StrandRenderItem,1> thin{tiny};
    const auto [thinResult,thinPixels]=run(thin,projection);
    for(unsigned c=0;c<4;++c){const auto p=Decode(thinPixels,OriginX(c,Tile)+Tile/2,OriginY(c,Tile)+Tile/2);
        const double expected=-std::log(1.-1./512);
        Check(p.nearest<1&&p.tau[7]>0&&std::abs(double(p.tau[7])-expected)<2e-7,"DOM8 discarded alpha below the old1/128 binary threshold");
    }
    auto isolatedProjection=projection;for(unsigned c=1;c<4;++c)isolatedProjection[c][3].x=4.f;
    const auto [isolatedResult,isolated]=run(thin,isolatedProjection);
    for(unsigned c=1;c<4;++c)for(unsigned y=0;y<Tile;++y)for(unsigned x=0;x<Tile;++x){const auto p=Decode(isolated,OriginX(c,Tile)+x,OriginY(c,Tile)+y);
        Check(p.nearest==1&&std::all_of(p.tau.begin(),p.tau.end(),[](float t){return t==0;}),"DOM8 producer leaked between cascade tiles");}
    const std::span<const StrandRenderItem> empty;const auto [cleared,blank]=run(empty,projection);
    Check(cleared.nearestDraws==0&&cleared.tauDraws==0,"Empty DOM8 producer reported groom submissions");
    for(unsigned y=0;y<Opacity;++y)for(unsigned x=0;x<Opacity;++x){const auto p=Decode(blank,x,y);
        Check(p.nearest==1&&std::all_of(p.tau.begin(),p.tau.end(),[](float t){return t==0;}),"DOM8 reused stale nearest depth or optical depth on an empty frame");}
    consumer.Run(cleared.combinedAtlas,blank,cleared.depthSpans,probes);
    std::cout<<"HOST DOM8 producer: physical .25/.5 overlap T=.375, reversed maxError="<<orderError
        <<"; source-curve branch, alpha1/512 preserved, groom-bound node span, four-cascade isolation, empty-frame clear, opaque depth/producer prefixes unchanged by consumer scaling\n";
}
} // namespace StrandOpacityShadowTest

// Add this header to an existing RHI GPU test; call on both graphics backends.
// Actual production nearest/MRT/pack draws feed actual production GLSL consumers.
inline void RunStrandOpacityShadowGpuOracle(RHIDevice& device){
    static_assert(offsetof(SceneEffectsConstants,strandShadowOpacity)==560&&offsetof(SceneEffectsConstants,strandShadowDepthSpan)==576);
    static_assert(sizeof(SceneEffectsConstants)==592);
    StrandOpacityShadowTest::Check(SceneEffectsConstants{}.strandShadowOpacity.y==1.f,
        "Default DOM8 extinction no longer preserves the original optical-depth scale");
    StrandOpacityShadowTest::Consumer consumer(device);
    StrandOpacityShadowTest::CheckSynthetic(device,consumer);StrandOpacityShadowTest::CheckProducer(device,consumer);
    std::cout<<"HOST DOM8 consumer: "<<consumer.rows<<" actual packed/prefix/bilinear/Tent/Poisson8/wrapper rows, maxError="
        <<consumer.maximumError<<"; T-before-filter and joint-visibility negative controls, disabled/hair identities passed\n";
}
} // namespace Render::TestData
