#pragma once
#include "Render/Test/hair_temporal_test.h"
#include <limits>

namespace Render::TestData {
namespace HairTemporalRejectionDetail {
using HairTemporalAuditDetail::Check;
using HairTemporalAuditDetail::OnDevice;
using HairTemporalAuditDetail::Submit;
inline constexpr unsigned Width=64,Height=8,Count=Width*Height,SampleX=32,SampleY=3,Sample=SampleY*Width+SampleX;
inline const glm::vec3 HairColor{1,.25f,.125f};
inline glm::vec4 Premul(glm::vec3 color,float alpha){return {color*alpha,alpha};}
struct Frame {
    TemporalConstants constants;
    std::array<glm::vec4,Count> current{},history{},motion{};
    std::array<float,Count> depth{},oldDepth{};
    Frame(){
        constants.temporal={.8f,.001f,0,1};constants.screen={1.f/Width,1.f/Height,0,0};
        constants.reactive.w=8.f;depth.fill(.5f);oldDepth.fill(.5f);
        for(unsigned y=0;y<Height;++y)for(unsigned x=0;x<Width;++x){
            const auto i=y*Width+x;current[i]={HairColor,x==SampleX-1?0.f:x==SampleX+1?1.f:.25f};
            history[i]=Premul(HairColor,.75f);motion[i]={(x+.5f)/Width,(y+.5f)/Height,.5f,1};
        }
    }
    void Move(float pixels){for(unsigned y=0;y<Height;++y)for(unsigned x=0;x<Width;++x)motion[y*Width+x].x=(float(x)+.5f-pixels)/Width;}
};
inline glm::vec4 Pixel(const std::vector<float>& values){return {values[Sample*4],values[Sample*4+1],values[Sample*4+2],values[Sample*4+3]};}
inline void Equal(glm::vec4 actual,glm::vec4 expected,const char* message,float tolerance=3e-5f){
    const auto error=glm::abs(actual-expected);const bool match=glm::all(glm::lessThanEqual(error,glm::vec4(tolerance)));
    if(!match)std::cerr<<message<<": actual="<<actual.r<<","<<actual.g<<","<<actual.b<<","<<actual.a
        <<" expected="<<expected.r<<","<<expected.g<<","<<expected.b<<","<<expected.a<<'\n';
    Check(match,message);
}
class Fixture {
    RHIDevice& device_;
    RenderResourceHandle<RHITextureSpec> output_;
    RenderResourceHandle<RenderTargetSpec> target_;
    RenderResourceHandle<PipelineSpec> pipeline_;
    RenderResourceHandle<VertexBufferSpec> mesh_;
    RenderResourceHandle<UniformBufferSpec> constants_;
    std::shared_ptr<const void> owner_;
public:
    unsigned draws=0;
    explicit Fixture(RHIDevice& device):device_(device){
        PipelineDetail::ResourceBuilder builder(device);output_=builder.Texture(Width,Height,RHITextureFormat::RGBA32F);
        target_=builder.Target(output_,{});mesh_=builder.FullscreenMesh();constants_=builder.Uniform(sizeof(TemporalConstants));
        pipeline_=builder.FullscreenPipeline(FullscreenVertexShader(),PipelineDetail::HairTemporalShader());owner_=builder.Finish();
    }
    std::vector<float> Run(const Frame& frame,bool enabled){
        PipelineDetail::ResourceBuilder builder(device_);
        const auto texture=[&](const void* data,RHITextureFormat format,RHIFilterMode filter){
            CreateRHITextureSpec desc;desc.width=Width;desc.height=Height;desc.data=data;desc.mipmaps=false;
            desc.textureDataStoreType=desc.textureUseType=format;desc.filterMode=filter;desc.addressMode=RHIAddressMode::ClampToEdge;
            return builder.Create<RHITextureSpec>([&](auto cb){device_.async_CreateTexture(desc,cb);});
        };
        const auto current=texture(frame.current.data(),RHITextureFormat::RGBA32F,RHIFilterMode::Linear);
        const auto history=texture(frame.history.data(),RHITextureFormat::RGBA32F,RHIFilterMode::Linear);
        const auto depth=texture(frame.depth.data(),RHITextureFormat::Depth32F,RHIFilterMode::Nearest);
        const auto oldDepth=texture(frame.oldDepth.data(),RHITextureFormat::Depth32F,RHIFilterMode::Nearest);
        const auto motionImage=texture(frame.motion.data(),RHITextureFormat::RGBA32F,RHIFilterMode::Nearest);
        const auto texturesOwner=builder.Finish();auto encoder=device_.BeginFrame({0,Width,Height});
        Check(encoder.KeepAlive(owner_)&&encoder.KeepAlive(texturesOwner),"Pin coverage-aware hair temporal fixture");
        Check(encoder.SetRenderTarget({target_,Width,Height,RHICommand::ClearColor}),"Bind coverage-aware history output");
        auto values=frame.constants;values.reactive.z=enabled?1.f:0.f;
        const auto* bytes=reinterpret_cast<const std::byte*>(&values);
        for(std::uint32_t offset=0;offset<sizeof(values);offset+=256)
            Check(encoder.UpdateUniformBufferBytes(constants_,bytes+offset,std::min(256u,std::uint32_t(sizeof(values))-offset),offset),"Upload coverage-aware history constants");
        Check(encoder.BindUniformBuffer(constants_,TemporalEffectsBinding)&&encoder.BindPipeline(pipeline_)&&encoder.BindMesh(mesh_)&&
            encoder.BindTexture(current,0)&&encoder.BindTexture(history,1)&&encoder.BindTexture(depth,2)&&
            encoder.BindTexture(oldDepth,3)&&encoder.BindTexture(motionImage,4)&&encoder.DrawIndexed({3}),"Draw coverage-aware hair temporal oracle");
        Submit(device_,encoder);++draws;
        auto pixels=OnDevice(device_,[&]{return device_.BackendDiagnostics()->ReadTexture(output_);});
        Check(pixels.size()==Count*4,"Read coverage-aware history dimensions");
        for(std::size_t i=0;i<pixels.size();i+=4){
            const float alpha=pixels[i+3];Check(std::isfinite(alpha)&&alpha>=0.f&&alpha<=1.f,"Temporal guard produced invalid coverage");
            for(unsigned channel=0;channel<3;++channel)Check(std::isfinite(pixels[i+channel])&&pixels[i+channel]>=0.f&&
                pixels[i+channel]<=60000.f*alpha+.01f,"Temporal guard violated finite premultiplied HDR bounds");
        }
        return pixels;
    }
};
} // namespace HairTemporalRejectionDetail

// Independent A/B scenes for the production shader. The old path remains a
// control; expectations are known coverage/over invariants, not a CPU copy of
// its limiter implementation. No new pass, device-native call or Model state.
inline void RunHairTemporalRejectionGpuOracle(RHIDevice& device){
    using namespace HairTemporalRejectionDetail;Fixture fixture(device);
    for(float speed:{0.f,.5f}){
        Frame frame;frame.Move(speed);const auto legacy=fixture.Run(frame,false),guarded=fixture.Run(frame,true);
        Check(legacy==guarded,"Static/subpixel-motion hair history was changed by the fast-motion guard");
        Check(Pixel(guarded).a>.6f,"Static/subpixel history lost its anti-aliasing coverage benefit");
    }
    {
        Frame frame;constexpr float jitter=.25f/Width,previousJitter=-.25f/Width;
        frame.constants.currentInverseViewProjection[3][0]=-2.f*jitter;
        frame.constants.previousInverseViewProjection[3][0]=-2.f*previousJitter;
        frame.constants.previousViewProjection[3][0]=2.f*previousJitter;
        for(unsigned y=0;y<Height;++y)for(unsigned x=0;x<Width;++x)frame.motion[y*Width+x].x=(x+.5f)/Width+previousJitter;
        const auto legacy=fixture.Run(frame,false),guarded=fixture.Run(frame,true);
        Check(legacy==guarded,"Projection jitter incorrectly activated the stable-grid motion limiter");
        Equal(Pixel(guarded),Premul(HairColor,.6875f),"Jitter-only accumulation changed coverage");
    }
    float fastLegacyAlpha=0.f;
    {
        Frame frame;frame.Move(8.f);const auto legacy=Pixel(fixture.Run(frame,false)),guarded=Pixel(fixture.Run(frame,true));
        Equal(legacy,Premul(HairColor,.615f),"Disabled guard changed known legacy fast-motion opacity");
        Equal(guarded,Premul(HairColor,.25f),"Fast moving partial coverage retained its old silhouette");fastLegacyAlpha=legacy.a;
        frame.Move(4.f);const auto middle=Pixel(fixture.Run(frame,true));
        Check(middle.a>.25f&&middle.a<.615f,"Intermediate motion response failed to preserve bounded history");
        frame.Move(8.f);frame.current.fill(glm::vec4(HairColor,1));
        for(unsigned y=0;y<Height;++y)frame.current[y*Width+SampleX-1].a=0.f;
        frame.history.fill(Premul(HairColor,.25f));
        Equal(Pixel(fixture.Run(frame,true)),Premul(HairColor,1.f),"Fast newly covered hair inherited stale low opacity");
        frame.current.fill(glm::vec4(HairColor,.25f));frame.history.fill(Premul(HairColor,.25f));
        Check(fixture.Run(frame,false)==fixture.Run(frame,true),"Coherent fast motion lost exact color/coverage identity");
    }
    float oldTail=0.f;
    {
        Frame frame;frame.current.fill(glm::vec4(0,60000,0,0));frame.history.fill(Premul(HairColor,.5f));frame.motion.fill(glm::vec4(0));
        for(unsigned y=0;y<Height;++y){const auto i=y*Width+SampleX+1;frame.current[i]={HairColor,1};
            frame.motion[i]={(SampleX+1.5f-8.f)/Width,(y+.5f)/Height,.5f,1};}
        const auto legacy=Pixel(fixture.Run(frame,false)),guarded=Pixel(fixture.Run(frame,true));
        Check(legacy.a>.3f,"Fast disappearing-edge negative control did not create the old tail");
        Equal(guarded,glm::vec4(0),"Fast disappearing subpixel hair left opacity/color behind");oldTail=legacy.a;
        for(unsigned y=0;y<Height;++y)frame.motion[y*Width+SampleX+1].x=(SampleX+1.5f)/Width;
        const auto stationary=Pixel(fixture.Run(frame,true));Equal(stationary,Premul(HairColor,.4f),"Static A0 dilation lost subpixel reconstruction");
    }
    float oldChroma=0.f;
    {
        Frame frame;frame.Move(8.f);const glm::vec3 green(0,.2126f/.7152f,0);
        frame.current.fill(glm::vec4(green,1));frame.current[Sample]={1,0,0,1};frame.history.fill(Premul(green,1));
        const auto legacy=Pixel(fixture.Run(frame,false)),guarded=Pixel(fixture.Run(frame,true));
        Check(legacy.g>.2f&&legacy.r<.3f,"Equal-luminance negative control did not retain stale chroma");
        Equal(guarded,{1,0,0,1},"Fast equal-luminance chroma change was not rejected");oldChroma=legacy.g;
        // Low coverage does not imply low energy: exercise the same rejection
        // with transparent HDR and poisonous RGB under alpha zero.
        const glm::vec3 brightRed(1000,0,0),brightGreen=green*1000.f;
        frame.current.fill(glm::vec4(brightGreen,.005f));frame.current[Sample]={brightRed,.005f};
        frame.current[Sample-1]={0,60000,60000,0};frame.history.fill(Premul(brightGreen,.005f));
        Equal(Pixel(fixture.Run(frame,true)),Premul(brightRed,.005f),"Transparent HDR retained stale equal-luma chroma",.0002f);
    }
    {
        Frame frame;frame.Move(8.f);for(auto& value:frame.motion)value.x=-.1f;
        const auto guarded=fixture.Run(frame,true);Equal(Pixel(guarded),Premul(HairColor,.25f),"Outside-image motion accepted hair history");
        Check(guarded==fixture.Run(frame,false),"Outside-image rejection depended on guard mode");
        frame=Frame{};frame.history.fill(glm::vec4(1,0,0,std::numeric_limits<float>::quiet_NaN()));
        Equal(Pixel(fixture.Run(frame,true)),Premul(HairColor,.25f),"NaN history alpha contaminated coverage");
        frame.history.fill(glm::vec4(std::numeric_limits<float>::infinity(),0,0,.75f));
        Equal(Pixel(fixture.Run(frame,true)),Premul(HairColor,.25f),"Infinite HDR history contaminated hair");
    }
    std::cout<<"Hair coverage-aware history: "<<fixture.draws<<" GPU scenes; fast partial legacy alpha="<<fastLegacyAlpha
        <<" -> .25, disappearing tail="<<oldTail<<" -> 0, equal-luma stale green="<<oldChroma
        <<" -> 0; static/slow/jitter/coherent-motion/transparent-HDR/finite/outside and disabled-control passed\n";
}
} // namespace Render::TestData
