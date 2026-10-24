#pragma once
#include "Render/Private/Pipeline/pipeline_resource_builder.h"
#include "Render/Private/Pipeline/strand_render_pass.h"
#include "Render/Private/Pipeline/strand_shaders.h"
#include <array>
#include <chrono>
#include <cmath>
#include <future>
#include <iostream>
#include <glm/gtc/matrix_transform.hpp>

namespace Render::TestData {
namespace StrandSpatialAuditDetail {
inline void Check(bool ok,const char* message){if(!ok)throw std::runtime_error(message);}
template<class F> auto OnDevice(RHIDevice& device,F operation) {
    using T=std::invoke_result_t<F>;auto promise=std::make_shared<std::promise<T>>();auto result=promise->get_future();
    device.async_ExecuteCode([promise,operation=std::move(operation)]()mutable{
        try{if constexpr(std::is_void_v<T>){operation();promise->set_value();}else promise->set_value(operation());}
        catch(...){promise->set_exception(std::current_exception());}
    });return result.get();
}
inline void Submit(RHIDevice& device,RHIFrameEncoder& encoder) {
    Check(encoder.End(false),"End spatial IBL oracle");auto done=std::make_shared<bool>(false);
    Check(device.async_SubmitFrameCommands(encoder.GetCommandBuffer(),[done]{*done=true;}),"Submit spatial IBL oracle");
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
    while(!*done){device.returnSystem.DrainCallbacks();Check(std::chrono::steady_clock::now()<deadline,"Spatial IBL oracle timeout");
        if(!*done)device.returnSystem.WaitForCallbacks(std::chrono::milliseconds(2));}
}
inline float Difference(const std::vector<float>& a,const std::vector<float>& b) {
    Check(a.size()==b.size(),"Spatial IBL image dimensions");float result=0;
    for(std::size_t i=0;i<a.size();++i){Check(std::isfinite(a[i])&&std::isfinite(b[i]),"Nonfinite spatial IBL output");result=std::max(result,std::abs(a[i]-b[i]));}
    return result;
}
} // namespace StrandSpatialAuditDetail

// Independent illumination invariants exercise the real material packing and
// ribbon pass. A white R-only closure is achromatic, so its actual GPU output
// is a response/coverage reference, without copying the scattering formulas.
inline void RunStrandSpatialIblGpuOracle(RHIDevice& device) {
    using namespace StrandSpatialAuditDetail;
    constexpr unsigned size=64;
    PipelineDetail::ResourceBuilder builder(device);
    const auto color=builder.Texture(size,size,RHITextureFormat::RGBA16F);
    const auto depth=builder.Texture(size,size,RHITextureFormat::Depth32F);
    const auto target=builder.Target(color,depth);
    const auto single=builder.Texture(1,1,RHITextureFormat::RGBA32F);
    const auto singleTarget=builder.Target(single,{});
    auto incidentShader=PipelineDetail::StrandFragmentShader(false);
    const auto main=incidentShader.rfind("void main()");Check(main!=std::string::npos,"Locate production spatial IBL incident helper");
    incidentShader.erase(main);
    incidentShader+="void main(){outColor=vec4(StrandIblIncident(vec3(0,.2,.3),vec3(0,1,0)),1);}\n";
    const auto incident=builder.FullscreenPipeline(FullscreenVertexShader(),incidentShader);
    const auto quad=builder.FullscreenMesh();
    const auto materialBuffer=builder.Uniform((14+256)*sizeof(glm::vec4));
    const auto cubeBuffer=builder.Uniform(7*sizeof(glm::vec4));
    const auto lightBuffer=builder.Uniform(sizeof(Material::PbrPassConstants));
    const auto skyBuffer=builder.Uniform(sizeof(SkyLightConstants));
    const auto owner=builder.Finish();
    const glm::vec3 positiveY(2,.3f,.1f),negativeY(.1f,.4f,1.4f),side(.4f,.5f,.6f);
    std::array<glm::vec4,14+256> constants{};constants[4]={1,12,0,.8f};constants[6].w=.51f;
    std::array<glm::vec4,7> cube{};for(auto& v:cube)v=glm::vec4(side,0);cube[2]=glm::vec4(positiveY,0);cube[3]=glm::vec4(negativeY,0);cube[6]={1,1,1,0};
    Material::PbrPassConstants lights;lights.lightColors.fill(glm::vec4(0));lights.ambientAndExposure=glm::vec4(0);
    const SkyLightConstants sky{};
    const auto directIncident=[&](bool spatial) {
        constants[13].w=spatial?1.f:0.f;auto encoder=device.BeginFrame({0,1,1});Check(encoder.KeepAlive(owner),"Pin incident oracle");
        Check(encoder.SetRenderTarget({singleTarget,1,1,RHICommand::ClearColor})&&encoder.BindPipeline(incident),"Set incident oracle target");
        const auto* bytes=reinterpret_cast<const std::byte*>(constants.data());
        for(std::uint32_t offset=0;offset<sizeof(constants);offset+=256)
            Check(encoder.UpdateUniformBufferBytes(materialBuffer,bytes+offset,std::min(256u,std::uint32_t(sizeof(constants))-offset),offset),"Upload incident material prefix/table");
        Check(encoder.UpdateUniformBufferBytes(cubeBuffer,cube.data(),sizeof(cube),0)&&
            encoder.UpdateUniformBufferBytes(lightBuffer,&lights,sizeof(lights),0)&&encoder.UpdateUniformBufferBytes(skyBuffer,&sky,sizeof(sky),0)&&
            encoder.BindUniformBuffer(materialBuffer,2)&&encoder.BindUniformBuffer(lightBuffer,3)&&encoder.BindUniformBuffer(cubeBuffer,7)&&
            encoder.BindUniformBuffer(skyBuffer,SkyLightBinding)&&encoder.BindMesh(quad)&&encoder.DrawIndexed({3}),"Draw production incident helper");
        Submit(device,encoder);return OnDevice(device,[&]{return device.BackendDiagnostics()->ReadTexture(single);});
    };
    float incidentError=0;
    for(bool spatial:{false,true}) {
        const auto result=directIncident(spatial);Check(result.size()==4,"Read incident helper");
        const glm::dvec3 illumination=glm::dvec3(positiveY)*double(.8f);
        const glm::dvec3 weights=spatial?glm::dvec3(.2126729041337967,.7151522040367126,.07217500358819962):glm::dvec3(.2126,.7152,.0722);
        const glm::dvec3 base=spatial?glm::dvec3(.001,.2,.3):glm::dvec3(0,.2,.3);
        const glm::dvec3 expected=(1.-double(.51f))*illumination+double(.51f)*glm::dot(illumination,weights)*base;
        for(int c=0;c<3;++c){incidentError=std::max(incidentError,float(std::abs(double(result[c])-expected[c])));
            Check(std::abs(double(result[c])-expected[c])<4e-7,"Source exact luminance/albedo floor or disabled incident identity mismatch");}
    }
    StrandMesh geometry(device),shifted(device);
    std::array<StrandVertex,2> vertices{{{{.23f,-.75f,0,.04f},{0,1,0,0}},{{.23f,.75f,0,.04f},{0,1,0,1}}}};
    const std::array<std::uint32_t,2> line{0,1};Check(geometry.Update(vertices,line),"Create spatial IBL groom");
    StrandRenderItem item;item.geometry=geometry.Snapshot();auto& m=item.material;
    m.rootColor=m.tipColor=glm::vec3(1);m.physicalScattering=true;m.volumeSkyTransmission=true;m.dualScatteringIbl=true;
    m.multipleScattering=0;m.albedoBlendRate=0;m.indirectLightingMultiplier=1;
    m.specularStrength=1;m.transmission=0;m.secondarySpecular=0;m.opacity=.9f;
    PipelineDetail::StrandPassView view;view.cameraPosition={0,0,3};view.lighting=lights;view.indirect.enabled=true;
    const PipelineDetail::StrandPassTarget destination{target,color,depth,size,size,1};
    PipelineDetail::StrandRenderPass pass(device);
    const auto draw=[&](const StrandRenderItem& drawItem,bool white) {
        view.indirect.coefficients.fill(white?glm::vec3(1):side);
        if(!white){view.indirect.coefficients[2]=positiveY;view.indirect.coefficients[3]=negativeY;}
        auto encoder=device.BeginFrame({0,size,size});Check(encoder.KeepAlive(owner),"Pin spatial groom target");
        Check(encoder.SetRenderTarget({target,size,size,RHICommand::ClearColor|RHICommand::ClearDepth,glm::vec4(0,0,0,1)}),"Clear spatial groom target");
        const std::array<StrandRenderItem,1> items{drawItem};pass.Record(encoder,items,destination,view);Submit(device,encoder);
        return OnDevice(device,[&]{return device.BackendDiagnostics()->ReadTexture(color);});
    };
    const auto whiteAngular=draw(item,true),angular=draw(item,false);
    m.sourceSpatialIbl=true;const auto whiteSpatial=draw(item,true),spatial=draw(item,false);
    Check(Difference(whiteAngular,whiteSpatial)<.0001f,"Constant incident field changed between angular/spatial adapters");
    const unsigned x=39;float response=0,error=0;bool sawPlus=false,sawMinus=false;
    for(unsigned y:{16u,20u,44u,48u}) {
        const auto i=(y*size+x)*4;response=std::max(response,whiteSpatial[i]);
        const bool plus=spatial[i]>spatial[i+2];sawPlus|=plus;sawMinus|=!plus;
        const auto expected=plus?positiveY:negativeY;
        for(unsigned c=0;c<3;++c){error=std::max(error,std::abs(spatial[i+c]-whiteSpatial[i+c]*expected[c]));
            Check(std::abs(spatial[i+c]-whiteSpatial[i+c]*expected[c])<.0015f,"One position-derived incident value was not reused by both IBL closures");
            Check(std::abs(angular[i+c]-whiteAngular[i+c]*side[c])<.0015f,"Disabled adapter no longer uses original two angular queries");}
    }
    Check(response>.002f&&sawPlus&&sawMinus,"Spatial IBL fixture did not observe opposite groom hemispheres");
    const glm::vec3 worldShift(.31f,-.17f,.2f);item.model=glm::translate(glm::mat4(1),worldShift);
    view.viewProjection=glm::translate(glm::mat4(1),-worldShift);view.cameraPosition=glm::vec3(0,0,3)+worldShift;
    const auto translated=draw(item,false);
    Check(Difference(translated,spatial)<.001f,"Spatial center did not follow the world model translation");
    const glm::vec3 localShift(-.37f,.11f,.3f);for(auto& v:vertices)v.positionRadius+=glm::vec4(localShift,0);
    Check(shifted.Update(vertices,line),"Create translated-local groom");item.geometry=shifted.Snapshot();item.model=glm::translate(glm::mat4(1),-localShift);
    view.viewProjection=glm::mat4(1);view.cameraPosition={0,0,3};
    Check(Difference(draw(item,false),spatial)<.001f,"Spatial center used local coordinates in a world-space query");
    item.geometry=geometry.Snapshot();item.model=glm::mat4(1);m.sourceSpatialIbl=false;
    Check(Difference(draw(item,false),angular)<.0001f,"Switching spatial IBL off failed to restore original adapter");
    Check(Difference(spatial,angular)>.005f,"Spatial and angular queries did not distinguish anisotropic illumination");
    pass.Shutdown();
    std::cout<<"Spatial IBL adapter: source luminance error="<<incidentError<<", two-branch shared illumination error="<<error
        <<"; anisotropic hemispheres, constant-field identity, world/local centers and disabled restoration passed (host center estimate)\n";
}
} // namespace Render::TestData
