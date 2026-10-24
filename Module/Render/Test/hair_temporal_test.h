#pragma once
#include "Render/Private/Pipeline/pipeline_resource_builder.h"
#include "Render/Private/Pipeline/post_process_shaders.h"
#include "Render/Private/Pipeline/hair_temporal_shaders.h"
#include "Render/Public/Pipeline/temporal_effects.h"
#include <array>
#include <chrono>
#include <cmath>
#include <future>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace Render::TestData {
namespace HairTemporalAuditDetail {
inline void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F> auto OnDevice(RHIDevice& device,F operation) {
    using T=std::invoke_result_t<F>;auto promise=std::make_shared<std::promise<T>>();auto result=promise->get_future();
    device.async_ExecuteCode([promise,operation=std::move(operation)]()mutable {
        try{if constexpr(std::is_void_v<T>){operation();promise->set_value();}else promise->set_value(operation());}
        catch(...){promise->set_exception(std::current_exception());}
    });return result.get();
}
inline void Submit(RHIDevice& device,RHIFrameEncoder& encoder) {
    Check(encoder.End(false),"End separate-hair temporal oracle frame");auto complete=std::make_shared<bool>(false);
    device.async_SubmitFrameCommands(encoder.GetCommandBuffer(),[complete]{*complete=true;});
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
    while(!*complete){device.returnSystem.DrainCallbacks();Check(std::chrono::steady_clock::now()<deadline,"Hair temporal oracle timeout");
        if(!*complete)device.returnSystem.WaitForCallbacks(std::chrono::milliseconds(2));}
}
inline constexpr unsigned Width=8,Height=4,Count=Width*Height,SampleX=3,SampleY=1;
inline constexpr unsigned Sample=SampleY*Width+SampleX;
struct Case {
    const char* name="";
    TemporalConstants constants;
    std::array<glm::vec4,Count> current{},history{},motion{};
    std::array<float,Count> currentDepth{},oldDepth{};
    glm::vec4 expected{0};
};
inline glm::vec4 Premul(glm::vec3 straight,float alpha){return {straight*alpha,alpha};}
inline Case Base(const char* name) {
    Case result;result.name=name;result.constants.temporal={.8f,.001f,0,1};
    result.constants.screen={1.f/Width,1.f/Height,0,0};
    const glm::vec3 color(1,.25f,.125f);
    for(unsigned y=0;y<Height;++y)for(unsigned x=0;x<Width;++x) {
        const unsigned i=y*Width+x;const float alpha=x==2?0.f:x==4?1.f:.25f;
        result.current[i]={color,alpha};result.history[i]=Premul(color,.75f);
        result.motion[i]={(x+.5f)/Width,(y+.5f)/Height,.5f,1};
    }
    result.currentDepth.fill(.5f);result.oldDepth.fill(.5f);result.expected=Premul(color,.65f);return result;
}
} // namespace HairTemporalAuditDetail

// Small real-GPU geometric/coverage oracles, independent of the production
// shader's GLSL algebra. Motion is previous RAW UV/depth/valid, history is
// premultiplied on the previous STABLE grid, and current input is straight RGBA.
// No Model dependency, native graphics API, new build target or game execution.
inline void RunHairTemporalGpuOracle(RHIDevice& device) {
    using namespace HairTemporalAuditDetail;
    const glm::vec3 hairColor(1,.25f,.125f),background(.1f,.4f,.9f);
    std::vector<Case> cases;
    cases.push_back(Base("static coverage history"));
    {
        auto c=Base("current/previous jitter use different raw/stable grids");
        constexpr float currentJitter=.25f/Width,previousJitter=-.25f/Width;
        c.constants.currentInverseViewProjection[3][0]=-2*currentJitter;
        c.constants.previousInverseViewProjection[3][0]=-2*previousJitter;
        c.constants.previousViewProjection[3][0]=2*previousJitter;
        for(unsigned y=0;y<Height;++y)for(unsigned x=0;x<Width;++x){const auto i=y*Width+x;
            c.motion[i].x=(x+.5f)/Width+previousJitter;c.history[i]=Premul(hairColor,std::min(1.f,.4f+.1f*x));}
        // Current 1/4-pixel jitter gives coverage .25*.75+1*.25=.4375.
        // Stable old texel3 has .7 coverage: .2*.4375+.8*.7=.6475.
        c.expected=Premul(hairColor,.6475f);cases.push_back(c);
    }
    {
        auto c=Base("two-pixel object motion samples old fiber location");
        for(unsigned y=0;y<Height;++y)for(unsigned x=0;x<Width;++x){const auto i=y*Width+x;
            c.motion[i].x=(float(x)+.5f-2)/Width;c.history[i]=Premul(hairColor,x==1?.75f:.1f);}
        // Velocity is 2px. Effective old weight=.8*(1-.35*2/32)=.7825.
        c.expected=Premul(hairColor,.64125f);cases.push_back(c);
    }
    {
        auto c=Base("history premultiplied interpolation preserves straight hue");
        c.current.fill(glm::vec4(1,0,0,.25f));
        for(unsigned y=0;y<Height;++y){c.current[y*Width+2].a=0;c.current[y*Width+4].a=1;}
        c.history.fill(glm::vec4(0));
        for(unsigned y=0;y<Height;++y){c.history[y*Width+1]={1,0,0,1};
            for(unsigned x=0;x<Width;++x)c.motion[y*Width+x].x=2.f/Width;}
        // Between opaque red and zero premultiplied history: (.5,0,0,.5).
        // Speed1.5px => weight .786875; .25+(.5-.25)*weight=.44671875.
        c.expected={.44671875f,0,0,.44671875f};cases.push_back(c);
    }
    {
        auto c=Base("premultiply current before fractional jitter interpolation");
        c.constants.temporal.w=0;c.constants.currentInverseViewProjection[3][0]=-1.f/Width;
        c.current.fill(glm::vec4(0,0,17,0));
        for(unsigned y=0;y<Height;++y)c.current[y*Width+3]={1,0,0,1};
        // Half-pixel sample between opaque red and transparent blue. This must
        // be (.5,0,0,.5), never (.25,0,4.25,.5) from straight interpolation.
        c.expected={.5f,0,0,.5f};cases.push_back(c);
    }
    {
        auto c=Base("reset rejects valid old hair");c.constants.temporal.w=0;c.expected=Premul(hairColor,.25f);cases.push_back(c);
        c=Base("zero history weight is current premultiplied identity");c.constants.temporal.x=0;c.expected=Premul(hairColor,.25f);cases.push_back(c);
        c=Base("invalid motion rejects history");for(auto& m:c.motion)m.w=0;c.expected=Premul(hairColor,.25f);cases.push_back(c);
        c=Base("previous UV outside image rejects history");for(auto& m:c.motion)m.x=-.125f;c.expected=Premul(hairColor,.25f);cases.push_back(c);
        c=Base("old nearest-depth mismatch rejects history");c.oldDepth.fill(.9f);c.expected=Premul(hairColor,.25f);cases.push_back(c);
        c=Base("clear old nearest-depth rejects history");c.oldDepth.fill(1);c.expected=Premul(hairColor,.25f);cases.push_back(c);
        c=Base("empty history carries no coverage");c.history.fill(glm::vec4(0));c.expected=Premul(hairColor,.25f);cases.push_back(c);
    }
    {
        auto c=Base("A0 edge borrows neighboring fiber motion without face color");
        // Deliberately poisonous RGB under alpha0 represents an arbitrary
        // transparent texel, not a usable opaque/face sample.
        c.current.fill(glm::vec4(0,100,0,0));c.history.fill(glm::vec4(0));c.motion.fill(glm::vec4(0));
        c.currentDepth.fill(1);c.oldDepth.fill(1);
        for(unsigned y=0;y<Height;++y){const unsigned neighbor=y*Width+4,old=y*Width+3;
            c.current[neighbor]={hairColor,1};c.currentDepth[neighbor]=.5f;
            c.motion[neighbor]={4.5f/Width,(y+.5f)/Height,.5f,1};
            c.oldDepth[old]=.5f;c.history[old]=Premul(hairColor,.5f);}
        // Dilation translates neighbor oldUV by exactly -1px, addressing the
        // selected stable old texel3. Current A0 + .8*old A.5 => A.4.
        c.expected=Premul(hairColor,.4f);cases.push_back(c);
    }
    {
        auto c=Base("no nearby fiber cannot create a history halo");
        c.current.fill(glm::vec4(30,40,50,0));c.history.fill(glm::vec4(4,0,4,1));c.motion.fill(glm::vec4(0));
        c.currentDepth.fill(1);c.expected=glm::vec4(0);cases.push_back(c);
        c=Base("empty current neighborhood clamps valid old coverage to zero");
        c.current.fill(glm::vec4(0,99,0,0));c.history.fill(glm::vec4(1,.25f,.125f,1));c.expected=glm::vec4(0);cases.push_back(c);
        c=Base("clear/reset yields transparent black");c.current.fill(glm::vec4(10,9,8,0));c.constants.temporal.w=0;c.expected=glm::vec4(0);cases.push_back(c);
        c=Base("HDR current opacity is multiplied only once");c.current.fill(glm::vec4(4,2,1,.2f));c.constants.temporal.w=0;c.expected={.8f,.4f,.2f,.2f};cases.push_back(c);
        c=Base("fully covered HDR hair keeps complete radiance");c.current.fill(glm::vec4(4,2,1,1));c.constants.temporal.w=0;c.expected={4,2,1,1};cases.push_back(c);
    }
    PipelineDetail::ResourceBuilder programs(device);
    const auto output=programs.Texture(Width,Height,RHITextureFormat::RGBA32F),composed=programs.Texture(Width,Height,RHITextureFormat::RGBA32F);
    const auto target=programs.Target(output,{}),compositeTarget=programs.Target(composed,{});
    const auto pipeline=programs.FullscreenPipeline(Render::FullscreenVertexShader(),PipelineDetail::HairTemporalShader());
    const auto composite=programs.FullscreenPipeline(Render::FullscreenVertexShader(),PipelineDetail::HairTemporalCompositeShader());
    const auto buffer=programs.Uniform(sizeof(TemporalConstants));const auto mesh=programs.FullscreenMesh();
    const auto programOwner=programs.Finish();float maximumError=0;
    for(const auto& c:cases) {
        PipelineDetail::ResourceBuilder resources(device);
        const auto texture=[&](const void* data,RHITextureFormat format,RHIFilterMode filter,unsigned w=Width,unsigned h=Height) {
            CreateRHITextureSpec desc;desc.width=w;desc.height=h;desc.data=data;desc.mipmaps=false;
            desc.textureDataStoreType=desc.textureUseType=format;desc.filterMode=filter;desc.addressMode=RHIAddressMode::ClampToEdge;
            return resources.Create<RHITextureSpec>([&](auto cb){device.async_CreateTexture(desc,cb);});
        };
        const auto current=texture(c.current.data(),RHITextureFormat::RGBA32F,RHIFilterMode::Linear);
        const auto history=texture(c.history.data(),RHITextureFormat::RGBA32F,RHIFilterMode::Linear);
        const auto depth=texture(c.currentDepth.data(),RHITextureFormat::Depth32F,RHIFilterMode::Nearest);
        const auto oldDepth=texture(c.oldDepth.data(),RHITextureFormat::Depth32F,RHIFilterMode::Nearest);
        const auto motion=texture(c.motion.data(),RHITextureFormat::RGBA32F,RHIFilterMode::Nearest);
        const glm::vec4 backgroundPixel(background,1);const auto back=texture(&backgroundPixel,RHITextureFormat::RGBA32F,RHIFilterMode::Nearest,1,1);
        const auto owner=resources.Finish();auto encoder=device.BeginFrame({0,Width,Height});
        Check(encoder.KeepAlive(programOwner)&&encoder.KeepAlive(owner),"Pin hair temporal oracle resources");
        Check(encoder.SetRenderTarget({target,Width,Height,RHICommand::ClearColor}),"Select independent hair history output");
        const auto* bytes=reinterpret_cast<const std::byte*>(&c.constants);
        for(std::uint32_t offset=0;offset<sizeof(TemporalConstants);offset+=256)
            Check(encoder.UpdateUniformBufferBytes(buffer,bytes+offset,std::min(256u,std::uint32_t(sizeof(TemporalConstants))-offset),offset),"Upload hair temporal constants");
        Check(encoder.BindUniformBuffer(buffer,TemporalEffectsBinding)&&encoder.BindPipeline(pipeline)&&encoder.BindMesh(mesh)&&
            encoder.BindTexture(current,0)&&encoder.BindTexture(history,1)&&encoder.BindTexture(depth,2)&&encoder.BindTexture(oldDepth,3)&&
            encoder.BindTexture(motion,4)&&encoder.DrawIndexed({3}),"Draw independent premultiplied hair temporal oracle");
        Check(encoder.SetRenderTarget({compositeTarget,Width,Height,RHICommand::ClearColor})&&encoder.BindPipeline(composite)&&encoder.BindMesh(mesh)&&
            encoder.BindTexture(back,0)&&encoder.BindTexture(output,1)&&encoder.DrawIndexed({3}),"Draw analytic premultiplied hair-over oracle");
        Submit(device,encoder);
        const auto pixels=OnDevice(device,[&]{return device.BackendDiagnostics()->ReadTexture(output);});
        const auto over=OnDevice(device,[&]{return device.BackendDiagnostics()->ReadTexture(composed);});
        Check(pixels.size()==Count*4&&over.size()==Count*4,"Read hair temporal oracle RGBA32F outputs");
        for(float value:pixels)Check(std::isfinite(value)&&value>=0,"Nonfinite/negative independent hair history");
        const auto expectedOver=glm::vec4(glm::vec3(c.expected)+(1-c.expected.a)*background,1);
        for(int channel=0;channel<4;++channel) {
            const float error=std::abs(pixels[Sample*4+channel]-c.expected[channel]);maximumError=std::max(maximumError,error);
            const float overError=std::abs(over[Sample*4+channel]-expectedOver[channel]);maximumError=std::max(maximumError,overError);
            if(error>3e-5f||overError>3e-5f)std::cerr<<"Hair temporal "<<c.name<<" channel"<<channel<<" actual="<<pixels[Sample*4+channel]
                <<" expected="<<c.expected[channel]<<" composed="<<over[Sample*4+channel]<<" expectedOver="<<expectedOver[channel]<<'\n';
            Check(error<=3e-5f,"Hair temporal geometric/coverage oracle mismatch");Check(overError<=3e-5f,"Hair temporal composition differs from premultiplied over");
        }
    }
    std::cout<<"Separate hair temporal: "<<cases.size()<<" jitter/moving-history/premultiply-first/depth-reject/reset/dilated-A0/halo/HDR-opacity and analytic-over oracles passed; maxAbs="<<maximumError<<'\n';
}
} // namespace Render::TestData
