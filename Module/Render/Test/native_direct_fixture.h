#pragma once
#include "Render/Private/Pipeline/pipeline_resource_builder.h"
#include "Render/Private/Pipeline/post_process_shaders.h"
#include "Render/Private/Pipeline/fiber_scattering_shaders.h"
#include "Render/Private/Pipeline/strand_ntt_shaders.h"
#include "Render/Test/native_direct_golden.h"
#include "Render/Test/native_ntt_fixture_rows.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <future>
#include <iostream>
#include <stdexcept>

namespace Render::TestData {
namespace SourceDirectAuditDetail {
inline void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F> auto OnDevice(RHIDevice& device,F operation) {
    using T=std::invoke_result_t<F>;auto promise=std::make_shared<std::promise<T>>();auto result=promise->get_future();
    device.async_ExecuteCode([promise,operation=std::move(operation)]()mutable {
        try{if constexpr(std::is_void_v<T>){operation();promise->set_value();}else promise->set_value(operation());}
        catch(...){promise->set_exception(std::current_exception());}
    });return result.get();
}
inline void Submit(RHIDevice& device,RHIFrameEncoder& encoder) {
    Check(encoder.End(false),"End source-direct audit frame");auto complete=std::make_shared<bool>(false);
    device.async_SubmitFrameCommands(encoder.GetCommandBuffer(),[complete]{*complete=true;});
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
    while(!*complete){device.returnSystem.DrainCallbacks();Check(std::chrono::steady_clock::now()<deadline,"Source-direct GPU timeout");
        if(!*complete)device.returnSystem.WaitForCallbacks(std::chrono::milliseconds(2));}
}
inline glm::vec3 Vector(const std::array<float,3>& a){return {a[0],a[1],a[2]};}
} // namespace SourceDirectAuditDetail

// Call from an existing started RHI test device on either backend. No native
// graphics entry points, application window, CMake target or runtime asset IO.
inline void RunSourceDirectGpuOracle(RHIDevice& device) {
    using namespace SourceDirectAuditDetail;
    struct alignas(16) Constants {
        glm::vec4 base{0},tangent{0},view{0},light{0},fiber{1.55f,.5f,.8f,1};
        glm::vec4 controls{.25f,.02f,1,1},optics{0,1,1,1};
    } data;
    static_assert(sizeof(Constants)==112);
    std::array<glm::vec4,256> moments{};
    std::array<glm::vec4,128> ntt{};
    for(std::size_t i=0;i<ntt.size();++i)ntt[i]={NativeNttRows[i][0],NativeNttRows[i][1],0,1};
    PipelineDetail::ResourceBuilder builder(device);
    const auto output=builder.Texture(4,4,RHITextureFormat::RGBA32F);
    const auto target=builder.Target(output,{});
    const auto mesh=builder.FullscreenMesh();
    const auto constants=builder.Uniform(sizeof(data)),momentBuffer=builder.Uniform(sizeof(moments));
    CreateRHITextureSpec table;table.width=1;table.height=128;table.data=ntt.data();
    table.textureDataStoreType=table.textureUseType=RHITextureFormat::RGBA32F;
    table.mipmaps=false;table.filterMode=RHIFilterMode::Linear;table.addressMode=RHIAddressMode::ClampToEdge;
    const auto texture=builder.Create<RHITextureSpec>([&](auto cb){device.async_CreateTexture(table,cb);});
    std::string fragment=R"GLSL(#version 450 core
layout(std140,binding=8) uniform SourceDirectAudit {
    vec4 auditBase,auditTangent,auditView,auditLight,auditFiber,auditControls,auditOptics;
};
layout(std140,binding=9) uniform SourceDirectMoments {vec4 strandMomentLut[256];};
const vec4 strandIbl=vec4(0.0,0.0,0.0,1.0);
layout(location=0) out vec4 outColor;
)GLSL";
    fragment+=PipelineDetail::StrandNttGLSL();fragment+="\n#define FIBER_MOMENT_LUT 1\n";
    fragment+=PipelineDetail::FiberScatteringGLSL();fragment+=R"GLSL(
void main() {
    vec3 result;
    if(auditBase.w>.5)result=FiberScatter(auditBase.rgb,auditTangent.xyz,auditView.xyz,auditLight.xyz,auditFiber,
        auditControls.x,auditControls.y,vec3(1.0),auditControls.z,auditControls.w,auditOptics.x,auditOptics.y>.5);
    else result=FiberSourceDirect(auditBase.rgb,auditTangent.xyz,auditView.xyz,auditLight.xyz,auditFiber,
        auditControls.x,auditControls.y,vec3(1.0),auditControls.z,auditControls.w,auditOptics.x,auditOptics.y>.5,auditOptics.z,auditOptics.w);
    outColor=vec4(result,1.0);
}
)GLSL";
    const auto pipeline=builder.FullscreenPipeline(FullscreenVertexShader(),fragment);
    const auto owner=builder.Finish();
    const auto setTable=[&](int kind) {
        for(unsigned y=0;y<16;++y)for(unsigned x=0;x<16;++x) {
            const float u=(float(x)+.5f)/16,v=(float(y)+.5f)/16;
            moments[y*16+x]=kind==1?glm::vec4(.95f-.4f*v-.1f*u,.1f+.2f*v,.04f+.1f*u,.5f-.2f*v):
                kind==2?glm::vec4(.999f,.15f,.03f,.6f):kind==3?glm::vec4(.7f,1e-6f,.03f,.6f):
                kind==4?glm::vec4(0):glm::vec4(.7f,.25f,.03f,.6f);
        }
    };
    const auto setCase=[&](const SourceDirectGoldenCase& c) {
        data.base=glm::vec4(Vector(c.base),0);data.tangent=glm::vec4(Vector(c.tangent),0);
        data.view=glm::vec4(Vector(c.view),0);data.light=glm::vec4(Vector(c.light),0);
        data.fiber={1.55f,c.roughness,c.azimuthal,c.absorption};data.controls={c.primary,c.tilt,c.multiple,c.density};
        data.optics={c.path,c.spatial?1.f:0.f,c.backscatter,c.visibility};setTable(c.tableKind);
    };
    const auto evaluate=[&] {
        auto encoder=device.BeginFrame({0,4,4});Check(encoder.KeepAlive(owner),"Pin source-direct oracle resources");
        Check(encoder.SetRenderTarget({target,4,4,RHICommand::ClearColor})&&
            encoder.UpdateUniformBufferBytes(constants,&data,sizeof(data),0)&&encoder.BindUniformBuffer(constants,8),"Upload source-direct oracle inputs");
        const auto* bytes=reinterpret_cast<const std::byte*>(moments.data());
        for(std::uint32_t offset=0;offset<sizeof(moments);offset+=256)
            Check(encoder.UpdateUniformBufferBytes(momentBuffer,bytes+offset,256,offset),"Upload source-direct oracle LUT");
        Check(encoder.BindUniformBuffer(momentBuffer,9)&&encoder.BindTexture(texture,0)&&encoder.BindPipeline(pipeline)&&
            encoder.BindMesh(mesh)&&encoder.DrawIndexed({3}),"Draw source-direct oracle");
        Submit(device,encoder);const auto pixels=OnDevice(device,[&]{return device.BackendDiagnostics()->ReadTexture(output);});
        Check(pixels.size()==64,"Read source-direct oracle RGBA32F");
        for(float value:pixels)Check(std::isfinite(value)&&value>=0,"Nonfinite source-direct scattering response");
        return glm::vec3(pixels[0],pixels[1],pixels[2]);
    };
    float maximumAbsolute=0,maximumRelative=0;
    for(const auto& c:SourceDirectGolden) {
        setCase(c);const auto actual=evaluate(),expected=Vector(c.expected);
        for(int k=0;k<3;++k) {
            const float error=std::abs(actual[k]-expected[k]);maximumAbsolute=std::max(maximumAbsolute,error);
            maximumRelative=std::max(maximumRelative,error/std::max(std::abs(expected[k]),2e-5f));
            if(error>std::max(2e-5f,std::abs(expected[k])*.001f))
                std::cerr<<"Source direct "<<c.name<<" channel "<<k<<" GPU="<<actual[k]<<" sourceSSA="<<expected[k]<<'\n';
            Check(error<=std::max(2e-5f,std::abs(expected[k])*.001f),"Emissive direct GPU/source-SSA golden mismatch");
        }
    }
    setCase(SourceDirectGolden[1]);const auto original=evaluate();data.fiber.x=1.5f;
    Check(glm::length(evaluate()-original)<1e-6f,"Fixed-IOR direct single unexpectedly consumes material IOR with fixed injected DSLUT");
    data.base.w=1;const auto generic=evaluate();
    Check(glm::length(generic-original)>1e-3f,"Generic/source direct A/B did not expose the recovered closure");
    setCase(SourceDirectGolden[1]);setTable(4);evaluate(); // zero af/ab limiting guard
    setCase(SourceDirectGolden[1]);data.tangent=data.view=data.light=glm::vec4(0);evaluate(); // degenerate geometric guard
    std::cout<<"Source Emissive direct: "<<SourceDirectGolden.size()<<" native-Ntt/DXIL SSA goldens, raw/capped af, tiny ab, visibility/back gain, N0/20, grazing, IOR separation and generic A/B passed; maxAbs="
        <<maximumAbsolute<<", maxRel="<<maximumRelative<<"; zero-LUT/vector guards finite\n";
}
} // namespace Render::TestData
