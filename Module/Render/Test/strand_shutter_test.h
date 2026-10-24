#pragma once
#include "Render/Private/Pipeline/strand_shutter_pass.h"
#include "Render/Test/hair_temporal_test.h"
#include <numeric>

namespace Render::TestData {
namespace StrandShutterAuditDetail {
using HairTemporalAuditDetail::Check;
using HairTemporalAuditDetail::OnDevice;
using HairTemporalAuditDetail::Submit;
inline constexpr unsigned Size=64;
struct Image {
    std::vector<float> mean;
    std::array<std::vector<float>,4> samples;
    PipelineDetail::StrandShutterStatistics stats;
};
inline double Area(const std::vector<float>& image){double value=0;for(std::size_t i=3;i<image.size();i+=4)value+=image[i];return value;}
inline double Centroid(const std::vector<float>& image,int color=-1){
    double weight=0,sum=0;
    for(unsigned y=0;y<Size;++y)for(unsigned x=0;x<Size;++x){const auto p=std::size_t(y*Size+x)*4;
        if(color==0&&image[p]<=image[p+2])continue;if(color==2&&image[p+2]<=image[p])continue;
        weight+=image[p+3];sum+=(double(x)+.5)*image[p+3];}
    Check(weight>1e-3,"Shutter centroid has no covered sample");return sum/weight;
}
inline float Difference(const std::vector<float>& a,const std::vector<float>& b){
    Check(a.size()==b.size(),"Shutter image dimensions differ");float value=0;
    for(std::size_t i=0;i<a.size();++i)value=std::max(value,std::abs(a[i]-b[i]));return value;
}
inline std::array<StrandVertex,2> Line(float x,float z=-.4f,float radius=.04f){return {{
    {{x,-.6f,z,radius},{0,1,0,0}},{{x,.6f,z,radius},{0,1,0,1}}}};}
inline void MeanOracle(const Image& image){
    Check(image.mean.size()==Size*Size*4,"Shutter mean dimensions");
    for(std::size_t p=0;p<image.mean.size();p+=4){
        double alpha=0;glm::dvec3 rgb(0);
        for(const auto& sample:image.samples){Check(sample.size()==image.mean.size(),"Independent shutter sample dimensions");
            const double a=sample[p+3];alpha+=a*.25;rgb+=glm::dvec3(sample[p],sample[p+1],sample[p+2])*(a*.25);}
        Check(std::abs(image.mean[p+3]-alpha)<2e-5,"Time samples were alpha-over combined instead of averaged");
        for(unsigned c=0;c<3;++c)Check(std::abs(image.mean[p+c]-rgb[c])<2e-5,"Straight shutter color was premultiplied incorrectly");
        for(unsigned c=0;c<4;++c)Check(std::isfinite(image.mean[p+c])&&image.mean[p+c]>=0,"Shutter produced nonfinite/negative values");
        Check(image.mean[p+3]<=1.f,"Shutter average coverage exceeds one");
    }
}
} // namespace StrandShutterAuditDetail

inline void RunStrandShutterGpuOracle(RHIDevice& device){
    using namespace StrandShutterAuditDetail;
    PipelineDetail::ResourceBuilder resources(device);
    const auto bodyColor=resources.Texture(Size,Size,RHITextureFormat::RGBA32F);
    const auto bodyDepth=resources.Texture(Size,Size,RHITextureFormat::Depth32F);
    const auto opaqueDepth=resources.Texture(Size,Size,RHITextureFormat::Depth32F);
    const auto bodyTarget=resources.Target(bodyColor,bodyDepth),opaqueTarget=resources.Target({},opaqueDepth);
    const auto owner=resources.Finish();
    {
        auto encoder=device.BeginFrame({0,Size,Size});Check(encoder.KeepAlive(owner),"Pin shutter audit body/opaque resources");
        RHICommand::SetRenderTarget body;body.target=bodyTarget;body.width=body.height=Size;
        body.clearFlags=RHICommand::ClearColor|RHICommand::ClearDepth;body.clearColor={.2f,.4f,.6f,1};body.clearDepth=.1f;
        Check(encoder.SetRenderTarget(body),"Initialize body image poisoned with closer depth");
        RHICommand::SetRenderTarget opaque;opaque.target=opaqueTarget;opaque.width=opaque.height=Size;opaque.clearFlags=RHICommand::ClearDepth;opaque.clearDepth=.9f;
        Check(encoder.SetRenderTarget(opaque),"Initialize independent opaque depth for shutter");Submit(device,encoder);
    }
    const auto read=[&](RenderResourceHandle<RHITextureSpec> texture){return OnDevice(device,[&device,texture]{return device.BackendDiagnostics()->ReadTexture(texture);});};
    const auto bodyBefore=read(bodyColor),depthBefore=read(bodyDepth),opaqueBefore=read(opaqueDepth);
    PipelineDetail::StrandPassTarget target{bodyTarget,bodyColor,bodyDepth,Size,Size,1};
    PipelineDetail::StrandPassView view;view.cameraPosition=view.previousCameraPosition={0,0,1000000};view.motionHistoryValid=true;
    view.frontLayerTransparency=true;view.pairedLayerTransparency=true;view.frontDepthGroups=3;view.opaqueDepth=opaqueDepth;
    view.lighting.lightColors.fill(glm::vec4(0));view.lighting.ambientAndExposure={1,1,1,1};
    PipelineDetail::StrandShutterPass shutter(device);
    unsigned captures=0;double maximumAreaError=0,maximumCentroidError=0;
    const auto capture=[&](std::span<const StrandRenderItem> items,const PipelineDetail::StrandPassView& camera,float fraction,bool stable=false){
        auto encoder=device.BeginFrame({0,Size,Size});Check(encoder.KeepAlive(owner),"Pin geometry shutter fixture inputs");
        shutter.Record(encoder,items,target,camera,fraction,stable);Submit(device,encoder);++captures;
        Image image;image.mean=read(shutter.PremultipliedColor());image.stats=shutter.Statistics();
        const auto textures=shutter.SampleTextures();for(unsigned i=0;i<4;++i)image.samples[i]=read(textures[i]);
        return image;
    };
    const std::array<std::uint32_t,2> topology{0,1};
    StrandMesh mesh(device);const auto current=Line(.1f);Check(mesh.Update(current,topology),"Upload current shutter strand");
    Check(mesh.Update(current,topology,{},current),"Upload static shutter strand history");
    StrandRenderItem item;item.geometry=mesh.Snapshot();item.previousTransformValid=true;item.guides=true;
    item.material.opacity=.6f;item.material.rootColor=item.material.tipColor={.7f,.2f,.1f};
    const auto currentOnly=capture(std::span(&item,1),view,0.f),still=capture(std::span(&item,1),view,1.f);
    MeanOracle(currentOnly);MeanOracle(still);
    Check(Difference(currentOnly.mean,still.mean)<.001f,"Static geometry shutter changed color/coverage");
    Check(Area(still.mean)>20.,"Shutter used contaminated scene/hair depth instead of independent opaque depth");
    for(std::size_t p=0;p<still.mean.size();p+=4)Check(still.mean[p+3]<.601f,"Four static time samples thickened a .6-opacity fiber");
    Check(still.stats.samples==4&&still.stats.passes==4*(3*3+2)+2&&still.stats.rasterPasses==4*(2*3+1),"Paired shutter pass/raster accounting");
    const auto old=Line(-.4f);Check(mesh.Update(current,topology,{},old),"Upload translating shutter strand history");item.geometry=mesh.Snapshot();
    const auto translated=capture(std::span(&item,1),view,1.f);MeanOracle(translated);
    const double areaError=std::abs(Area(translated.mean)/Area(currentOnly.mean)-1.);maximumAreaError=std::max(maximumAreaError,areaError);
    Check(areaError<.02,"Geometry shutter increased/lost projected fiber area during translation");
    const double expectedCentroid=(.5*(double(old[0].positionRadius.x)+current[0].positionRadius.x)*.5+.5)*Size;
    maximumCentroidError=std::abs(Centroid(translated.mean)-expectedCentroid);
    Check(maximumCentroidError<.12,"Trailing midpoint shutter did not follow the real fiber translation");
    for(unsigned i=0;i<4;++i){
        const double t=(double(i)+.5)*.25,x=double(old[0].positionRadius.x)*(1-t)+current[0].positionRadius.x*t;
        const double error=std::abs(Centroid(translated.samples[i])-(x*.5+.5)*Size);maximumCentroidError=std::max(maximumCentroidError,error);
        Check(error<.12,"Independent shutter sample used the wrong geometry time");
    }
    {
        StrandMesh redMesh(device),blueMesh(device);const auto redOld=Line(-.4f,-.4f),redNow=Line(.4f,-.4f);
        const auto blueOld=Line(.4f,-.2f),blueNow=Line(-.4f,-.2f);
        Check(redMesh.Update(redOld,topology)&&redMesh.Update(redNow,topology,{},redOld),"Upload red opposing strand history");
        Check(blueMesh.Update(blueOld,topology)&&blueMesh.Update(blueNow,topology,{},blueOld),"Upload blue opposing strand history");
        std::array<StrandRenderItem,2> opposing{item,item};opposing[0].geometry=redMesh.Snapshot();opposing[1].geometry=blueMesh.Snapshot();
        opposing[0].material.rootColor=opposing[0].material.tipColor={.8f,.1f,.05f};
        opposing[1].material.rootColor=opposing[1].material.tipColor={.05f,.1f,.8f};
        const auto image=capture(opposing,view,1.f);MeanOracle(image);
        for(unsigned i=0;i<4;++i){const double t=(double(i)+.5)*.25;
            const double red=(-.4+.8*t)*.5+.5,blue=(.4-.8*t)*.5+.5;
            Check(std::abs(Centroid(image.samples[i],0)-red*Size)<.12&&std::abs(Centroid(image.samples[i],2)-blue*Size)<.12,
                "Opposite fiber velocities were averaged before geometry shutter sampling");}
    }
    {
        auto invalid=view;invalid.motionHistoryValid=false;
        const auto image=capture(std::span(&item,1),invalid,1.f);
        Check(!image.stats.historyUsed&&Difference(image.mean,currentOnly.mean)<.001f,"Invalid global history generated a shutter trail");
        auto widthChanged=item;widthChanged.previousTransformValid=false;widthChanged.material.widthScale=1.8f;
        const auto reference=capture(std::span(&widthChanged,1),view,0.f),guarded=capture(std::span(&widthChanged,1),view,1.f);
        Check(!guarded.stats.historyUsed&&Difference(reference.mean,guarded.mean)<.001f,"Width/control provenance change reused old fiber shape");
        const std::array<std::uint32_t,2> reversed{1,0};Check(mesh.Update(current,reversed,{},old),"Upload topology-changing shutter strand");
        Check(!mesh.Snapshot()->PreviousInputMatched(),"Shutter topology fixture failed to invalidate previous geometry");item.geometry=mesh.Snapshot();
        const auto topologyReference=capture(std::span(&item,1),view,0.f),topologyFallback=capture(std::span(&item,1),view,1.f);
        Check(!topologyFallback.stats.historyUsed&&Difference(topologyReference.mean,topologyFallback.mean)<.001f,"Changed topology accepted old shutter positions");
        Check(mesh.Update(current,topology,{},old)&&mesh.Update(current,topology,{},old),"Restore matched shutter topology");item.geometry=mesh.Snapshot();
    }
    {
        auto jittered=view;const glm::vec2 jitter(.25f/Size,0);
        jittered.viewProjection=JitterProjection(glm::mat4(1),jitter);
        jittered.previousViewProjection=JitterProjection(glm::mat4(1),-jitter);
        Check(mesh.Update(current,topology,{},current),"Restore static shutter strand history");item.geometry=mesh.Snapshot();
        const auto reference=capture(std::span(&item,1),jittered,0.f,true),guarded=capture(std::span(&item,1),jittered,1.f,true);
        Check(reference.stats.stableOutput&&Difference(reference.mean,guarded.mean)<.001f,"Previous projection jitter created physical shutter blur");
        // Legacy transparency remains available and must follow the same time
        // integration contract; paired draw fusion is an independent option.
        auto legacy=view;legacy.pairedLayerTransparency=false;
        const auto legacyImage=capture(std::span(&item,1),legacy,1.f);MeanOracle(legacyImage);
        Check(Difference(legacyImage.mean,currentOnly.mean)<.003f,"Geometry shutter broke legacy transparency identity");
    }
    {
        auto volumeItem=item;volumeItem.guides=false;volumeItem.material.physicalScattering=false;
        volumeItem.material.spatialVolume=true;volumeItem.material.volumeVoxelSize=.1f;volumeItem.material.volumeResolution=8;
        volumeItem.material.volumeOpaqueBlockers=true;volumeItem.material.volumeDepthThreshold=.05f;
        const auto first=capture(std::span(&volumeItem,1),view,1.f),cached=capture(std::span(&volumeItem,1),view,.5f);
        Check(first.stats.volumePasses==7&&first.stats.blockerPasses==3,"Shutter did not share one current density/G update across four samples");
        Check(cached.stats.volumePasses==0&&cached.stats.blockerPasses==0,"Shutter fraction rebuilt the frozen current volume");
        auto dropped=device.BeginFrame({0,Size,Size});Check(dropped.KeepAlive(owner),"Pin dropped shutter inputs");
        shutter.Record(dropped,std::span(&volumeItem,1),target,view,1.f,false);Check(dropped.End(false),"End discarded shutter frame");
        shutter.InvalidateVolume();const auto retried=capture(std::span(&volumeItem,1),view,1.f);
        Check(retried.stats.volumePasses==7&&retried.stats.blockerPasses==3,"Discarded shutter cache did not retrace after invalidation");
    }
    Check(bodyBefore==read(bodyColor)&&depthBefore==read(bodyDepth)&&opaqueBefore==read(opaqueDepth),"Hair shutter modified body color/depth or source opaque depth");
    shutter.Shutdown();
    std::cout<<"Geometry hair shutter: "<<captures<<" four-midpoint captures; area relative error="<<maximumAreaError
        <<", centroid max pixels="<<maximumCentroidError
        <<"; static identity/premul average/opposing layers/history-width-topology/jitter/paired-legacy/frozen-volume/discard/body isolation passed\n";
}
} // namespace Render::TestData
