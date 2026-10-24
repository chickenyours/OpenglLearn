#pragma once
#include "Render/Test/fiber_near_shadow_test.h"
#include "Render/Private/Pipeline/strand_render_pass.h"
#include "Render/Private/Pipeline/strand_shaders.h"
#include "Render/Private/Pipeline/fiber_moment_lut.h"
#include "Render/Private/Pipeline/fiber_native_moment_lut.h"
#include "Render/Private/Pipeline/fiber_native_moment_shaders.h"
#include "Render/Public/Compute/gpu_compute.h"
#include <bit>
#include <cstring>

namespace Render::TestData {
namespace StrandRoughnessTest {
using FiberNearAuditDetail::Check;
struct alignas(16) Output {
    glm::vec4 fiber{},policy{},moment0{},moment137{},moment255{},nativeSample{};
};
static_assert(sizeof(Output)==96);
inline std::string Shader() {
    const auto production=PipelineDetail::StrandFragmentShader(false,0,true,true);
    const auto begin=production.find("layout(std140,binding=2) uniform StrandMaterial {");
    const auto end=production.find("\n};",begin);
    Check(begin!=std::string::npos&&end!=std::string::npos,"Locate actual strand roughness material layout");
    std::string source="#version 450 core\nlayout(local_size_x=1) in;\n";
    source+=production.substr(begin,end+3-begin)+"\n#define FIBER_NATIVE_MOMENT_LUT 1\n";
    source+=PipelineDetail::FiberNativeMomentGLSL();
    source+=R"GLSL(
struct Output {vec4 fiber,policy,moment0,moment137,moment255,nativeSample;};
layout(std430,set=2,binding=0) writeonly buffer Outputs {Output outputs[];};
layout(push_constant) uniform Count {uvec4 count;};
void main() {
    uint i=count.x;
    outputs[i].fiber=strandFiber;outputs[i].policy=strandNativeMoments;
    bool nativeActive=strandNativeMoments.x>.5;
    outputs[i].moment0=nativeActive?vec4(0):strandMomentLut[0];
    outputs[i].moment137=nativeActive?vec4(0):strandMomentLut[137];
    outputs[i].moment255=nativeActive?vec4(0):strandMomentLut[255];
    outputs[i].nativeSample=nativeActive?FiberNativeMoments(.35,strandFiber.y,.6):vec4(0);
}
)GLSL";
    return source;
}
inline bool Identical(const std::vector<float>& a,const std::vector<float>& b) {
    return a.size()==b.size()&&std::memcmp(a.data(),b.data(),a.size()*sizeof(float))==0;
}
inline double Difference(const std::vector<float>& a,const std::vector<float>& b) {
    Check(a.size()==b.size(),"Roughness render dimensions changed");double result=0;
    for(std::size_t i=0;i<a.size();++i)if(i%4!=3)result=std::max(result,std::abs(double(a[i])-b[i]));
    return result;
}
} // namespace StrandRoughnessTest

// Root integration: include in strand_native_ds_gpu_test.cpp and call on each
// RHI backend. This reads the binding2 buffer published by a valid production
// Record, without constructing or uploading a separate test material snapshot.
inline void RunStrandRoughnessGpuOracle(RHIDevice& device) {
    using namespace StrandRoughnessTest;
    using namespace PipelineDetail;
    struct Golden {float authored;std::uint32_t variance;};
    // Independent f32 instruction-DAG vectors from native140353008..3079,
    // with constants read as bytes rather than reconstructed decimal literals.
    constexpr std::array<Golden,6> golden{{
        {.25f,0x3d5cf042u},{.3f,0x3dad48a1u},{.35f,0x3e000313u},
        {.5f,0x3ea40639u},{.6f,0x3f07b250u},{1.f,0x41db7e3fu}}};
    unsigned checks=0;
    Check(!StrandMaterial{}.sourceRoughnessMapping,"Generic callers unexpectedly enabled source roughness");++checks;
    Check(StrandSourceLongitudinalVariance(0)==0,"Native polynomial lost its zero endpoint");++checks;
    for(const auto row:golden) {
        const auto variance=StrandSourceLongitudinalVariance(row.authored);
        Check(std::bit_cast<std::uint32_t>(variance)==row.variance,"Source variance differs from native f32 DAG");++checks;
        StrandMaterial material;material.longitudinalRoughness=row.authored;
        Check(EffectiveStrandLongitudinalRoughness(material)==row.authored,"Default raw roughness was transformed");++checks;
        material.sourceRoughnessMapping=true;
        Check(EffectiveStrandLongitudinalRoughness(material)==variance&&material.longitudinalRoughness==row.authored,
            "Source mapping changed authored storage or applied twice");++checks;
    }
    StrandMaterial mapped;mapped.longitudinalRoughness=.5f;mapped.sourceRoughnessMapping=true;
    mapped.cuticleShift=.02f;
    StrandMaterial explicitVariance=mapped;explicitVariance.sourceRoughnessMapping=false;
    explicitVariance.longitudinalRoughness=std::bit_cast<float>(std::uint32_t{0x3ea40639});
    // No intervening insertion: equality here tests the cache key, not a
    // pointer retained across the bounded vector cache's reallocations.
    const auto* mappedSlice=&CachedFiberMomentTable(mapped);
    const auto* explicitSlice=&CachedFiberMomentTable(explicitVariance);
    Check(mappedSlice==explicitSlice,"Equivalent source/raw-v values did not share the fallback slice");++checks;
    const auto expectedSlice=BuildFiberMomentTable(explicitVariance);
    Check(std::memcmp(mappedSlice->data(),expectedSlice.data(),sizeof(expectedSlice))==0,
        "Source fallback slice was generated from authored r instead of effective hm_v");++checks;

    constexpr unsigned size=64,maxCaptures=64;
    ResourceBuilder resources(device);
    const auto color=resources.Texture(size,size,RHITextureFormat::RGBA32F);
    const auto depth=resources.Texture(size,size,RHITextureFormat::Depth32F);
    const auto target=resources.Target(color,depth);
    const auto owner=resources.Finish();
    auto compute=std::shared_ptr<Compute::Device>(Compute::Device::Borrow(device));
    Check(compute->Diagnostics().frameAvailable,"Source roughness requires frame compute");
    Compute::FramePipelineDesc desc;desc.source=Shader();desc.label="actual strand material hm_v and DSLUT roughness";
    desc.uniformMask=1u<<2;desc.sampledMask=1u<<9;desc.storageBuffers=1;desc.pushConstantBytes=16;
    const auto pipeline=compute->CreateFramePipeline(desc);
    const auto output=compute->CreateStorageBuffer(maxCaptures*sizeof(Output),nullptr,"actual strand roughness uploads");
    // Native atlas stores a half-exact Y ramp in R. Its sample must return
    // clamp(hm_v,.18,.6), independently of X/Z and texture filtering precision.
    auto table=std::make_shared<StrandTextureTable>();table->width=64;table->height=1024;
    table->texels.resize(FiberNativeMomentTexelCount);
    for(unsigned z=0;z<16;++z)for(unsigned y=0;y<64;++y)for(unsigned x=0;x<64;++x)
        table->texels[(z*64+y)*64+x]={(float(y)+.5f)/64.f,.25f,.03125f,.5f};
    Check(ValidFiberNativeMomentTable(*table),"Synthetic roughness atlas violated native input contract");
    const std::array<StrandVertex,4> points{{
        {{-.18f,-.7f,0,.018f},{0,1,0,0}},{{-.18f,.7f,0,.018f},{0,1,0,1}},
        {{.18f,-.7f,.03f,.018f},{.15f,.98f,.1f,0}},{{.39f,.7f,.17f,.018f},{.15f,.98f,.1f,1}}}};
    const std::array<std::uint32_t,4> indices{0,1,2,3};
    StrandMesh mesh(device);Check(mesh.Update(points,indices),"Create source roughness render fixture");
    StrandRenderItem item;item.geometry=mesh.Snapshot();item.material=mapped;
    auto& material=item.material;material.rootColor={.3f,.18f,.06f};material.tipColor=material.rootColor;
    material.physicalScattering=true;material.sourceDirectScattering=true;
    material.spatialVolume=false;material.receiveOpaqueShadows=false;
    material.multipleScattering=1;material.minPixelWidth=1;material.opacity=.85f;
    material.useNttLookup=false;material.nativeMomentTable=table;
    const auto key=MakeFiberNativeMomentMaterialKey(material);
    material.nativeMomentParameters={key[0],key[1],key[2],key[3]};
    StrandPassView view;view.cameraPosition={.1f,.2f,2};view.sunDirection={-.6f,.15f,-1};
    view.sunIntensity=2;view.sunColor={1,.9f,.75f};
    view.lighting.lightColors.fill(glm::vec4(0));view.lighting.ambientAndExposure=glm::vec4(0);
    view.indirect.enabled=false;
    StrandRenderPass pass(device);
    struct Expected {float variance;bool native;FiberMomentTable fallback{};};
    std::vector<Expected> expected;expected.reserve(maxCaptures);
    unsigned captures=0;double minimumRawDifference=1e30,minimumSqrtDifference=1e30;
    const auto draw=[&] {
        Check(captures<maxCaptures,"Roughness output capacity exhausted");
        Expected entry;entry.variance=EffectiveStrandLongitudinalRoughness(material);entry.native=material.useNativeMomentLut;
        if(!entry.native)entry.fallback=CachedFiberMomentTable(material);
        expected.push_back(entry);
        auto encoder=device.BeginFrame({0,size,size});
        Check(encoder.KeepAlive(owner)&&encoder.KeepAlive(compute),"Retain roughness oracle resources");
        RHICommand::SetRenderTarget clear;clear.target=target;clear.width=clear.height=size;
        clear.clearFlags=RHICommand::ClearColor|RHICommand::ClearDepth;clear.clearDepth=1;clear.clearColor={0,0,0,1};
        Check(encoder.SetRenderTarget(clear),"Clear roughness render fixture");
        pass.Record(encoder,std::span<const StrandRenderItem>(&item,1),{target,color,depth,size,size,1},view);
        if(material.sourceSegmentShading)Check(pass.SegmentShadingStatistics().passes>0,"Roughness segment shader did not execute");
        // Composite changes texture0/1 but leaves the real material buffer2
        // and native texture9 bound. Read those bindings, without rebinding a
        // test UBO. Detach graphics attachments before the compute dispatch.
        RHICommand::SetRenderTarget detach;detach.width=detach.height=1;
        Check(encoder.SetRenderTarget(detach),"Detach roughness graphics attachments");
        RHICommand::DispatchFrameCompute command;command.pipeline=pipeline;command.buffers[0]=output;
        const glm::uvec4 at(captures,0,0,0);command.pushBytes=sizeof(at);std::memcpy(command.pushConstants.data(),&at,sizeof(at));
        Check(encoder.DispatchCompute(command),"Read production Record material prefix and moment bindings");
        FiberNearAuditDetail::Submit(device,encoder);++captures;
        auto image=FiberNearAuditDetail::Read(device,color);
        Check(image.size()==size*size*4,"Read roughness render pixels");
        float peak=0;for(std::size_t i=0;i<image.size();++i){Check(std::isfinite(image[i]),"Roughness render produced nonfinite output");if(i%4!=3)peak=std::max(peak,image[i]);}
        Check(peak>1e-6f,"Roughness fixture produced no lit hair");
        return image;
    };
    for(bool native:{false,true})for(bool segment:{false,true}) {
        material.useNativeMomentLut=native;material.sourceSegmentShading=segment;
        material.sourceRoughnessMapping=false;material.longitudinalRoughness=.5f;const auto raw=draw();
        material.sourceRoughnessMapping=true;const auto source=draw();
        material.sourceRoughnessMapping=false;material.longitudinalRoughness=explicitVariance.longitudinalRoughness;
        Check(Identical(source,draw()),"Mapped and explicit native-v complete renders differ");++checks;
        material.longitudinalRoughness=.5f;
        Check(Identical(raw,draw()),"Restored default raw mode changed legacy pixels");++checks;
        minimumRawDifference=std::min(minimumRawDifference,Difference(raw,source));
        // CB40 sqrt_v is not read by this selected shader: using it must be
        // observably different from the recovered CB24 input.
        material.longitudinalRoughness=.5660035014152527f;
        minimumSqrtDifference=std::min(minimumSqrtDifference,Difference(source,draw()));
    }
    Check(minimumRawDifference>1e-6&&minimumSqrtDifference>1e-6,
        "Roughness fixture failed to distinguish authored r / sqrt_v negative controls");checks+=2;
    material.useNativeMomentLut=true;material.sourceSegmentShading=false;material.sourceRoughnessMapping=true;
    for(float r:{.02f,.25f,.35f,.6f,1.f}){material.longitudinalRoughness=r;draw();}
    const auto bytes=compute->Readback(output);Check(bytes.size()==maxCaptures*sizeof(Output),"Read actual roughness UBO outputs");
    std::vector<Output> got(maxCaptures);std::memcpy(got.data(),bytes.data(),bytes.size());
    double maximumLutError=0;
    for(unsigned i=0;i<captures;++i) {
        const auto& e=expected[i];const auto& g=got[i];
        Check(g.fiber.x==material.ior&&g.fiber.y==e.variance&&g.fiber.z==material.azimuthalRoughness&&g.fiber.w==material.absorptionScale,
            "Actual Record material prefix uploaded authored r, sqrt_v or a different fiber layout");++checks;
        Check(g.policy.x==(e.native?1.f:0.f),"Actual Record native moment selection disagrees with material");++checks;
        if(e.native) {
            const double error=std::abs(double(g.nativeSample.x)-std::clamp(double(e.variance),double(.18f),double(.6f)));
            maximumLutError=std::max(maximumLutError,error);
            Check(error<2e-6&&g.nativeSample.y==.25f&&g.nativeSample.z==.03125f&&g.nativeSample.w==.5f,
                "Native DSLUT Y used authored r, sqrt_v or omitted its recovered clamp");++checks;
        } else {
            Check(std::memcmp(&g.moment0,&e.fallback[0],sizeof(glm::vec4))==0&&
                std::memcmp(&g.moment137,&e.fallback[137],sizeof(glm::vec4))==0&&
                std::memcmp(&g.moment255,&e.fallback[255],sizeof(glm::vec4))==0,
                "Actual fallback table upload used a stale or authored-roughness slice");++checks;
        }
    }
    Check(compute->Diagnostics().validationErrors==0,"Roughness oracle generated frame compute validation errors");
    pass.Shutdown();compute->Destroy(output);compute->Destroy(pipeline);
    std::cout<<"Source longitudinal roughness: "<<golden.size()<<" exact f32 vectors, "<<captures<<" actual Record captures / "<<checks
        <<" checks; raw identity, native/fallback fragment/segment equality, real UBO/LUT readback, cache identity, native Y clamp; maxLutError="
        <<maximumLutError<<", minRawDifference="<<minimumRawDifference<<", minSqrtDifference="<<minimumSqrtDifference<<'\n';
}
} // namespace Render::TestData
