#pragma once
#include "Render/Private/Pipeline/pipeline_resource_builder.h"
#include "Render/Private/Pipeline/post_process_shaders.h"
#include "Render/Private/Pipeline/fiber_scattering_shaders.h"
#include "Render/Private/Pipeline/fiber_native_moment_lut.h"
#include "Render/Private/Pipeline/fiber_native_moment_producer_shaders.h"
#include "Render/Test/native_moment_golden.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <stdexcept>

namespace Render::TestData {
namespace NativeMomentAuditDetail {
inline void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F> auto OnDevice(RHIDevice& device,F operation) {
    using T=std::invoke_result_t<F>;auto promise=std::make_shared<std::promise<T>>();auto result=promise->get_future();
    device.async_ExecuteCode([promise,operation=std::move(operation)]()mutable {
        try{if constexpr(std::is_void_v<T>){operation();promise->set_value();}else promise->set_value(operation());}
        catch(...){promise->set_exception(std::current_exception());}
    });return result.get();
}
inline void Submit(RHIDevice& device,RHIFrameEncoder& encoder) {
    Check(encoder.End(false),"End native DSLUT audit frame");auto complete=std::make_shared<bool>(false);
    device.async_SubmitFrameCommands(encoder.GetCommandBuffer(),[complete]{*complete=true;});
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(30);
    while(!*complete){device.returnSystem.DrainCallbacks();Check(std::chrono::steady_clock::now()<deadline,"Native DSLUT GPU timeout");
        if(!*complete)device.returnSystem.WaitForCallbacks(std::chrono::milliseconds(2));}
}
inline StrandTextureTable LoadTable() {
    auto path=std::filesystem::path("Asset/Diana/hair-dslut.lutbin");
    if(!std::filesystem::exists(path))path=std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path()/path;
    std::ifstream input(path,std::ios::binary);Check(bool(input),"Open actual native64x64x16 DSLUT asset");
    std::array<char,8> magic{};std::array<std::uint32_t,4> header{};
    input.read(magic.data(),magic.size());input.read(reinterpret_cast<char*>(header.data()),sizeof(header));
    Check(magic==std::array<char,8>{'D','H','S','L','U','T','0','1'}&&header==std::array<std::uint32_t,4>{1,64,1024,4},"Native DSLUT asset descriptor");
    StrandTextureTable table;table.width=64;table.height=1024;table.texels.resize(65536);
    input.read(reinterpret_cast<char*>(table.texels.data()),std::streamsize(table.texels.size()*sizeof(glm::vec4)));
    Check(bool(input)&&input.peek()==std::char_traits<char>::eof(),"Native DSLUT asset exact payload size");
    std::uint64_t fnv=14695981039346656037ull;
    const auto accumulate=[&](const void* data,std::size_t size){const auto* p=static_cast<const unsigned char*>(data);
        for(std::size_t i=0;i<size;++i){fnv^=p[i];fnv*=1099511628211ull;}};
    accumulate(magic.data(),magic.size());accumulate(header.data(),sizeof(header));accumulate(table.texels.data(),table.texels.size()*sizeof(glm::vec4));
    Check(fnv==NativeMomentAssetFNV,"Actual native DSLUT asset hash differs from audited source asset");
    Check(PipelineDetail::ValidFiberNativeMomentTable(table),"Native DSLUT finite/range/dimensions contract");return table;
}
} // namespace NativeMomentAuditDetail

// Independent real RHI test: no Model headers, native graphics calls, original
// game execution or startup CPU generation. Call on a started GL/Vulkan device.
inline void RunNativeMomentGpuOracle(RHIDevice& device) {
    using namespace NativeMomentAuditDetail;
    const auto table=LoadTable();
    StrandMaterial material;material.ior=1.55f;material.azimuthalRoughness=.8f;material.cuticleShift=.02f;
    const auto key=PipelineDetail::MakeFiberNativeMomentMaterialKey(material);
    material.longitudinalRoughness=.2f;Check(PipelineDetail::FiberNativeMomentKeyMatches(material,key),"Full DSLUT key incorrectly includes current longitudinal roughness");
    material.ior=1.5f;Check(!PipelineDetail::FiberNativeMomentKeyMatches(material,key),"Stale DSLUT key accepted changed IOR");
    material.ior=1.55f;material.cuticleShift=.03f;Check(!PipelineDetail::FiberNativeMomentKeyMatches(material,key),"Stale DSLUT key accepted changed cuticle tilt");
    material.cuticleShift=.02f;material.azimuthalRoughness=.4f;Check(!PipelineDetail::FiberNativeMomentKeyMatches(material,key),"Stale DSLUT key accepted changed azimuthal width");
    material.azimuthalRoughness=.8f;
    struct alignas(16) Constants {glm::vec4 query{0},parameters{0};} data;
    data.parameters={key[0],key[1],key[2],key[3]};
    std::array<glm::vec4,256> legacy{};
    for(unsigned y=0;y<16;++y)for(unsigned x=0;x<16;++x){const float u=(x+.5f)/16,v=(y+.5f)/16;legacy[y*16+x]={.1f+.2f*u+.3f*v,.05f+.1f*v,.01f+.02f*u,.2f+.1f*v};}
    std::vector<glm::vec4> axis(65536);
    for(unsigned z=0;z<16;++z)for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x)
        axis[(z*64+y)*64+x]={float(x)/64,float(y)/64,float(z)/16,float(x+y+z)/256};
    PipelineDetail::ResourceBuilder builder(device);
    const auto output=builder.Texture(1,1,RHITextureFormat::RGBA32F);const auto target=builder.Target(output,{});
    const auto mesh=builder.FullscreenMesh();const auto buffer=builder.Uniform(sizeof(data)),oldBuffer=builder.Uniform(sizeof(legacy));
    const auto makeTexture=[&](const void* pixels){CreateRHITextureSpec desc;desc.width=64;desc.height=1024;desc.data=pixels;
        desc.textureDataStoreType=RHITextureFormat::RGBA16F;desc.textureUseType=RHITextureFormat::RGBA32F;
        desc.filterMode=RHIFilterMode::Nearest;desc.addressMode=RHIAddressMode::ClampToEdge;desc.mipmaps=false;
        return builder.Create<RHITextureSpec>([&](auto cb){device.async_CreateTexture(desc,cb);});};
    const auto texture=makeTexture(table.texels.data()),axisTexture=makeTexture(axis.data());
    const auto makeFragment=[&](bool native) {
        std::string source=R"GLSL(#version 450 core
layout(std140,binding=8) uniform NativeMomentAudit {vec4 auditQuery,auditParameters;};
layout(std140,binding=9) uniform LegacyMoments {vec4 strandMomentLut[256];};
layout(location=0) out vec4 outColor;
#define FIBER_MOMENT_LUT 1
)GLSL";
        if(native)source+=R"GLSL(
#define FIBER_NATIVE_MOMENT_LUT 1
#define FIBER_NATIVE_MOMENT_ACTIVE (auditQuery.w>.5)
#define FIBER_NATIVE_MOMENT_ROUGHNESS auditQuery.y
)GLSL";
        source+=PipelineDetail::FiberScatteringGLSL();source+=PipelineDetail::FiberNativeMomentProducerGLSL();
        source+=R"GLSL(
void main(){outColor=auditQuery.w>1.5?FiberNativeProduceMoment(ivec3(auditQuery.xyz),auditParameters):FiberMoments(auditQuery.x,auditQuery.z);}
)GLSL";return source;
    };
    const auto nativePipeline=builder.FullscreenPipeline(FullscreenVertexShader(),makeFragment(true));
    const auto legacyPipeline=builder.FullscreenPipeline(FullscreenVertexShader(),makeFragment(false));
    const auto owner=builder.Finish();
    const auto downloaded=OnDevice(device,[&]{return device.BackendDiagnostics()->ReadTexture(texture);});
    Check(downloaded.size()==table.texels.size()*4,"Read all native DSLUT texels from actual RGBA16F RHI storage");
    const auto* expectedData=reinterpret_cast<const float*>(table.texels.data());
    for(std::size_t i=0;i<downloaded.size();++i)Check(downloaded[i]==expectedData[i],"Native RGBA16F upload changed exact half-expanded table content");
    const auto evaluate=[&](glm::vec4 query,RenderResourceHandle<RHITextureSpec> lookup,RenderResourceHandle<PipelineSpec> pipeline) {
        data.query=query;auto encoder=device.BeginFrame({0,1,1});Check(encoder.KeepAlive(owner),"Pin native DSLUT test resources");
        Check(encoder.SetRenderTarget({target,1,1,RHICommand::ClearColor})&&encoder.UpdateUniformBufferBytes(buffer,&data,sizeof(data),0)&&
            encoder.BindUniformBuffer(buffer,8),"Set native DSLUT oracle constants");
        const auto* bytes=reinterpret_cast<const std::byte*>(legacy.data());
        for(std::uint32_t offset=0;offset<sizeof(legacy);offset+=256)Check(encoder.UpdateUniformBufferBytes(oldBuffer,bytes+offset,256,offset),"Upload retained legacy moment slice");
        Check(encoder.BindUniformBuffer(oldBuffer,9)&&encoder.BindTexture(lookup,9)&&encoder.BindPipeline(pipeline)&&encoder.BindMesh(mesh)&&encoder.DrawIndexed({3}),"Draw native DSLUT oracle");
        Submit(device,encoder);const auto pixels=OnDevice(device,[&]{return device.BackendDiagnostics()->ReadTexture(output);});
        Check(pixels.size()==4,"Read native DSLUT sample");for(float v:pixels)Check(std::isfinite(v)&&v>=0&&v<=1,"Nonfinite or unbounded native moment");
        return glm::vec4(pixels[0],pixels[1],pixels[2],pixels[3]);
    };
    float lookupError=0,producerError=0;
    for(const auto& sample:NativeMomentLookupGolden){const auto a=evaluate({sample.input[0],sample.input[1],sample.input[2],1},texture,nativePipeline);
        for(int c=0;c<4;++c){lookupError=std::max(lookupError,std::abs(a[c]-sample.expected[c]));Check(std::abs(a[c]-sample.expected[c])<3e-6f,"Native64x64x16 trilinear source-table golden mismatch");}}
    const auto mid=evaluate({.5f,.5f,.5f,1},texture,nativePipeline),below=evaluate({.5f,.4921875f,.5f,1},texture,nativePipeline),above=evaluate({.5f,.5078125f,.5f,1},texture,nativePipeline);
    Check(glm::length(mid-.5f*(below+above))<2e-6f&&glm::length(above-below)>1e-4f,"Dynamic roughness did not interpolate both native neighboring slices");
    const glm::vec3 query(.37f,.499f,.17f);const glm::vec3 p=glm::clamp(query*glm::vec3(64,64,16)-.5f,glm::vec3(0),glm::vec3(63,63,15));
    const auto axes=evaluate(glm::vec4(query,1),axisTexture,nativePipeline);const glm::vec4 axisExpected(p.x/64,p.y/64,p.z/16,(p.x+p.y+p.z)/256);
    Check(glm::length(axes-axisExpected)<2e-6f,"Native atlas axis order or eight-neighbor interpolation is incorrect");
    for(const auto& sample:NativeMomentProducerGolden){const auto a=evaluate({sample.index[0],sample.index[1],sample.index[2],2},texture,nativePipeline);
        for(int c=0;c<4;++c){const float error=std::abs(a[c]-sample.expected[c]);producerError=std::max(producerError,error);
            if(error>std::max(2e-5f,.001f*std::abs(sample.expected[c])))std::cerr<<"DSLUT source producer cell "<<sample.index[0]<<','<<sample.index[1]<<','<<sample.index[2]<<" channel "<<c<<" actual="<<a[c]<<" SSA="<<sample.expected[c]<<'\n';
            Check(error<=std::max(2e-5f,.001f*std::abs(sample.expected[c])),"Native GPU computeDSLUT quadrature differs from parsed source SSA reference");}}
    const auto off=evaluate({query,0},texture,nativePipeline),old=evaluate({query,0},texture,legacyPipeline),offAxes=evaluate({query,0},axisTexture,nativePipeline);
    Check(glm::length(off-old)<1e-7f&&glm::length(off-offAxes)<1e-7f,"Disabled native table changed retained legacy slice path");
    Check(glm::length(evaluate({query,1},texture,nativePipeline)-off)>1e-3f,"Native/legacy table A/B is not observable");
    std::cout<<"Native DSLUT: full65536 RGBA16F upload exact; "<<NativeMomentLookupGolden.size()<<" trilinear/clamp goldens; "<<NativeMomentProducerGolden.size()
        <<" parsed-source quadrature cells; roughness neighbors, axis order, material key and disabled legacy identity passed; lookupMaxAbs="<<lookupError<<", producerMaxAbs="<<producerError<<'\n';
}
} // namespace Render::TestData
