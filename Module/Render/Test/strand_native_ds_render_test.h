#pragma once
#include "Render/Test/fiber_near_shadow_test.h"
#include "Render/Private/Pipeline/strand_render_pass.h"
#include <cstring>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/matrix_transform.hpp>

namespace Render::TestData {
// Exercise the actual ribbon/composite and endpoint-shading consumers, rather
// than replacing the fragment shader with a DS query-only diagnostic.
inline void RunNativeDsRenderGpuOracle(RHIDevice& device) {
    using namespace FiberNearAuditDetail;
    using namespace PipelineDetail;
    constexpr unsigned size=64;
    ResourceBuilder resources(device);
    const auto color=resources.Texture(size,size,RHITextureFormat::RGBA32F);
    const auto depth=resources.Texture(size,size,RHITextureFormat::Depth32F);
    const auto opaque=resources.Texture(size,size,RHITextureFormat::Depth32F);
    const auto target=resources.Target(color,depth),opaqueTarget=resources.Target({},opaque);
    const auto owner=resources.Finish();
    std::array<StrandVertex,8> points{};
    std::array<std::uint32_t,8> lines{};
    // Raw bounds have nonzero XYZ extent; half the chains lie behind the
    // opaque plane. Ribbons, camera depth and volume use the same metre space.
    for(unsigned chain=0;chain<4;++chain)for(unsigned end=0;end<2;++end) {
        const unsigned i=chain*2+end;lines[i]=i;
        points[i].positionRadius={chain&1?.035f:-.035f,end?.18f:-.18f,chain&2?.04f:-.04f,.0015f};
        points[i].tangentArc={0,1,0,float(end)};
    }
    StrandMesh mesh(device);Check(mesh.Update(points,lines),"Create native DS 3D render groom");
    std::array<StrandRenderItem,2> items{};
    for(unsigned i=0;i<items.size();++i) {
        auto& item=items[i];item.geometry=mesh.Snapshot();
        item.model=glm::translate(glm::mat4(1),glm::vec3(i?.13f:-.13f,0,0));
        auto& m=item.material;m.rootColor={.32f,.19f,.09f};m.tipColor={.65f,.43f,.23f};
        m.physicalScattering=true;m.useNativeMomentLut=false;m.useNttLookup=false;
        m.spatialVolume=true;m.volumeSkyTransmission=true;m.volumePointIbl=true;
        m.dualScatteringIbl=true;m.volumeResolution=32;m.volumeVoxelSize=.01f;
        m.volumeAverageWidth=.003f;m.volumeRasterWidthScale=1.5f;
        m.volumeOpaqueBlockers=false;m.minPixelWidth=1.5f;m.opacity=.8f;
    }
    StrandPassView view;view.cameraPosition={0,0,1.3f};view.cameraForward={0,0,-1};
    view.viewProjection=glm::perspectiveRH_NO(glm::radians(45.f),1.f,.1f,4.f)*
        glm::lookAtRH(view.cameraPosition,glm::vec3(0),glm::vec3(0,1,0));
    view.opaqueDepth=opaque;view.sunColor={1,.9f,.75f};
    view.lighting.lightColors.fill(glm::vec4(0));view.lighting.ambientAndExposure=glm::vec4(0);
    view.indirect.coefficients.fill(glm::vec3(.3f,.4f,.5f));
    view.indirect.coefficients[2]={.8f,.5f,.2f};view.indirect.coefficients[3]={.2f,.3f,.7f};
    const glm::vec4 plane=view.viewProjection*glm::vec4(0,0,0,1);
    const float planeDepth=.5f*(plane.z/plane.w)+.5f;
    Check(planeDepth>0&&planeDepth<1,"Native DS render camera depth range");
    StrandRenderPass pass(device);unsigned captures=0;
    const auto draw=[&] {
        auto encoder=device.BeginFrame({0,size,size});Check(encoder.KeepAlive(owner),"Pin native DS render targets");
        // An infinite opaque plane's exact camera depth, stored independently
        // from the active depth attachment; no sampled-attachment feedback.
        RHICommand::SetRenderTarget clear;clear.target=opaqueTarget;clear.width=clear.height=size;
        clear.clearFlags=RHICommand::ClearDepth;clear.clearDepth=planeDepth;
        Check(encoder.SetRenderTarget(clear),"Clear native DS independent camera depth");
        clear.target=target;clear.clearFlags=RHICommand::ClearColor|RHICommand::ClearDepth;
        clear.clearColor={0,0,0,1};Check(encoder.SetRenderTarget(clear),"Clear native DS composite");
        pass.Record(encoder,items,{target,color,depth,size,size,1},view);Submit(device,encoder);++captures;
        auto image=Read(device,color);Check(image.size()==size*size*4,"Native DS render readback size");
        float peak=0,nativeSidePeak=0;for(std::size_t i=0;i<image.size();++i) {
            Check(std::isfinite(image[i]),"Native DS rendering produced nonfinite radiance/coverage");
            if(i%4!=3){peak=std::max(peak,image[i]);if((i/4)%size<size/2)nativeSidePeak=std::max(nativeSidePeak,image[i]);}
        }
        Check(peak>1e-6f&&nativeSidePeak>1e-6f,"Native DS render lighting/coverage consumer produced zero RGB");return image;
    };
    const auto identical=[](const std::vector<float>& a,const std::vector<float>& b) {
        return a.size()==b.size()&&std::memcmp(a.data(),b.data(),a.size()*sizeof(float))==0;
    };
    for(bool segment:{false,true})for(unsigned illumination=0;illumination<3;++illumination) {
        for(auto& item:items)item.material.sourceSegmentShading=segment;
        view.indirect.enabled=illumination==0;view.sunIntensity=illumination==1?2.f:0.f;
        view.sunDirection={0,0,1};view.lighting.lightColors.fill(glm::vec4(0));
        if(illumination==2){view.lighting.lightPositions[0]={.1f,.12f,.5f,1};view.lighting.lightColors[0]={2,1.5f,1,.01f};}
        items[0].material.sourceDSVolume=false;const auto legacyBefore=draw();
        items[0].material.sourceDSVolume=true;draw(); // native + legacy in the same frame
        Check(pass.VolumeStatistics().updated>0,"Native/legacy mixed frame did not produce its volume");
        if(segment)Check(pass.SegmentShadingStatistics().passes>0,"Native DS endpoint producer was not executed");
        items[0].material.sourceDSVolume=false;
        Check(identical(legacyBefore,draw()),"Native UBO14/texture4 contaminated restored legacy output");
    }
    // With no spatial volume, the native selector must be a strict identity,
    // including direct sun/point visibility, for both shading implementations.
    for(bool segment:{false,true}) {
        for(auto& item:items){item.material.spatialVolume=false;item.material.sourceSegmentShading=segment;}
        view.sunIntensity=1;view.indirect.enabled=true;items[0].material.sourceDSVolume=false;
        const auto disabled=draw();items[0].material.sourceDSVolume=true;
        Check(identical(disabled,draw()),"Disabled native volume changed the existing shadow/shading path");
    }
    for(auto& item:items)item.material.spatialVolume=true;
    // The source native dual closure and the explicit host six-axis forward
    // adapter must both consume the hierarchy without silently blacking out.
    view.sunIntensity=0;view.lighting.lightColors.fill(glm::vec4(0));view.indirect.enabled=true;
    view.lighting.ambientAndExposure={.15f,.2f,.25f,1}; // explicit legacy forward illumination adapter
    for(bool segment:{false,true})for(bool dual:{false,true}) {
        for(auto& item:items){item.material.sourceSegmentShading=segment;item.material.dualScatteringIbl=dual;}
        draw();
    }
    // The producer oracle checks packed G and mip averages exactly. Here the
    // real camera shell is exercised through full rendering, with no assumed
    // source beauty-image value or claim that its host adapter is source code.
    view.indirect.enabled=false;view.sunIntensity=2;view.sunDirection={0,0,1};
    for(bool shell:{false,true}){items[0].material.volumeOpaqueBlockers=shell;draw();}
    pass.Shutdown();
    std::cout<<"Native DS complete render: "<<captures<<" captures; mixed native/legacy, fragment/segment, "
        <<"IBL/sun/point, disabled identity, bitwise legacy restore, forward adapter and camera shell passed\n";
}
} // namespace Render::TestData
