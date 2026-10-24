#pragma once
#include "Render/Private/Pipeline/strand_native_ds_volume.h"
#include "Render/Private/Pipeline/strand_native_ds_contract.h"
#include "Render/Test/fiber_near_shadow_test.h"
#include <array>
#include <bit>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <numeric>

namespace Render::TestData {
namespace NativeDsProducerDetail {
using namespace PipelineDetail;
namespace Ref=PipelineDetail::NativeDs;
using FiberNearAuditDetail::Check;
inline std::size_t Index(unsigned x,unsigned y,unsigned z,unsigned edge){return x+std::size_t(edge)*(y+std::size_t(edge)*z);}
inline std::vector<std::uint32_t> ReadRaw(const NativeDSVolumeSnapshot& s){
    const auto bytes=s.owner->Readback(s.raw);std::vector<std::uint32_t> raw(bytes.size()/4);
    std::memcpy(raw.data(),bytes.data(),bytes.size());return raw;
}
// Independent scalar walk over ORIGINAL raw lines. Only the source arithmetic
// library is shared with the already independently checked SSA goldens; the
// control flow and addressing below do not call production producer code.
inline std::vector<std::uint32_t> ExpectedRaw(const NativeDSVolumeSnapshot& snapshot,const StrandSegmentSourceData& input,const NativeDSVolumeSettings& settings){
    const auto& c=snapshot.constants;const unsigned edge=settings.resolution;
    std::vector<std::uint32_t> raw(std::size_t(edge)*edge*edge);
    Ref::Vec3 minimum{c.minimum.x,c.minimum.y,c.minimum.z},inverse{c.inverseExtent.x,c.inverseExtent.y,c.inverseExtent.z};
    Ref::UVec3 actual{c.resolution.x,c.resolution.y,c.resolution.z};
    const float h=c.voxel.w,step=Ref::Mul(h,settings.rasterTraceStepScale);
    for(const auto& segment:input.segments){
        Ref::Vec3 start{},delta{};float squared=0;
        for(unsigned axis=0;axis<3;++axis){start[axis]=input.points[segment.x].positionArc[axis];delta[axis]=Ref::Add(input.points[segment.y].positionArc[axis],-start[axis]);
            squared=Ref::Add(squared,Ref::Mul(delta[axis],delta[axis]));}
        const float length=std::sqrt(squared);if(!(length>0))continue;
        const unsigned count=unsigned(std::min(std::ceil(Ref::Div(length,step)),float(10*edge)));
        Ref::UVec3 previous{~0u,~0u,~0u};const float denominator=std::max(float(count)-1.f,1.f);
        const float w0=Ref::Div(Ref::Mul(2,input.radii[segment.x]),settings.averageWidth),w1=Ref::Div(Ref::Mul(2,input.radii[segment.y]),settings.averageWidth);
        for(unsigned sample=0;sample<count;++sample){
            Ref::Vec3 p{};for(unsigned axis=0;axis<3;++axis)p[axis]=Ref::Add(start[axis],Ref::Mul(Ref::Mul(float(sample),step),Ref::Div(delta[axis],length)));
            const auto q=Ref::Coordinate(p,minimum,inverse,actual);Ref::UVec3 cell{unsigned(q[0]),unsigned(q[1]),unsigned(q[2])};
            if(cell==previous)continue;previous=cell;
            const float total=Ref::DoseTotal(settings.widthScale,w0,w1,Ref::Div(float(sample),denominator),Ref::Mul(h,100));
            if(settings.interpolationInjection){
                const auto doses=Ref::Intpij(total,{Ref::Add(q[0],-float(cell[0])),Ref::Add(q[1],-float(cell[1])),Ref::Add(q[2],-float(cell[2]))});
                for(unsigned corner=0;corner<8;++corner){const auto n=Ref::Neighbor(cell,actual,corner);raw[Index(n[0],n[1],n[2],edge)]+=doses[corner];}
            }else raw[Index(cell[0],cell[1],cell[2],edge)]+=Ref::Dose(total);
        }
    }
    return raw;
}
inline void CheckHierarchy(RHIDevice& device,const NativeDSVolumeSnapshot& snapshot,const std::vector<std::uint32_t>& raw,unsigned& checks,const char* stage="hierarchy"){
    const auto image=FiberNearAuditDetail::Read(device,snapshot.atlas);const auto& c=snapshot.constants;
    Check(image.size()==std::size_t(snapshot.width)*snapshot.height*4,"Native DS producer atlas readback size");
    std::vector<bool> used(std::size_t(snapshot.width)*snapshot.height,false);
    std::vector<Ref::Vec2> prior;unsigned priorEdge=unsigned(c.atlas.z);
    prior.reserve(raw.size());for(auto word:raw)prior.push_back(Ref::Copy(word));
    for(unsigned level=0;level<6;++level){
        const auto layout=c.levels[level];const unsigned edge=layout.y;
        std::vector<Ref::Vec2> next;
        if(level){next.resize(std::size_t(edge)*edge*edge);
            for(unsigned z=0;z<edge;++z)for(unsigned y=0;y<edge;++y)for(unsigned x=0;x<edge;++x){
                std::array<Ref::Vec2,8> children;
                for(unsigned corner=0;corner<8;++corner)children[corner]=prior[Index(x*2+(corner&1u),y*2+((corner>>1u)&1u),z*2+((corner>>2u)&1u),priorEdge)];
                next[Index(x,y,z,edge)]=Ref::Mip(children);
            }
            prior=std::move(next);priorEdge=edge;
        }
        for(unsigned z=0;z<edge;++z)for(unsigned y=0;y<edge;++y)for(unsigned x=0;x<edge;++x){
            const auto pixel=std::size_t(layout.x+(z/layout.z)*edge+y)*snapshot.width+(z%layout.z)*edge+x;
            used[pixel]=true;const auto expected=prior[Index(x,y,z,edge)];
            for(unsigned channel=0;channel<2;++channel){const float got=image[pixel*4+channel];
                const float tolerance=1e-6f*(1+std::abs(expected[channel]));
                if(!std::isfinite(got)||std::abs(got-expected[channel])>tolerance){
                    std::cerr<<std::setprecision(9)<<"Native DS hierarchy mismatch stage="<<stage<<" R="<<unsigned(c.atlas.z)
                        <<" logical="<<c.resolution.x<<','<<c.resolution.y<<','<<c.resolution.z<<" level="<<level
                        <<" cell="<<x<<','<<y<<','<<z<<" atlas="<<pixel%snapshot.width<<','<<pixel/snapshot.width
                        <<" channel="<<(channel?'G':'R')<<" expectedRG="<<expected[0]<<','<<expected[1]
                        <<" actualRGBA="<<image[pixel*4]<<','<<image[pixel*4+1]<<','<<image[pixel*4+2]<<','<<image[pixel*4+3]
                        <<" tolerance="<<tolerance<<" expectedBits=0x"<<std::hex<<std::bit_cast<std::uint32_t>(expected[channel])
                        <<" actualBits=0x"<<std::bit_cast<std::uint32_t>(got)<<std::dec;
                    if(!level)std::cerr<<" packed=0x"<<std::hex<<raw[Index(x,y,z,edge)]<<std::dec;
                    else {const auto lower=c.levels[level-1];std::cerr<<" actualChildRG=";
                        for(unsigned corner=0;corner<8;++corner){const unsigned cx=x*2+(corner&1u),cy=y*2+((corner>>1u)&1u),cz=z*2+((corner>>2u)&1u);
                            const auto child=std::size_t(lower.x+(cz/lower.z)*lower.y+cy)*snapshot.width+(cz%lower.z)*lower.y+cx;
                            std::cerr<<'['<<image[child*4]<<','<<image[child*4+1]<<']';}}
                    std::cerr<<'\n';Check(false,"Native DS copy or eight-child RG mip differs from scalar oracle");
                }++checks;
            }
            if(image[pixel*4+2]!=0||image[pixel*4+3]!=0){
                std::cerr<<"Native DS stale channels stage="<<stage<<" level="<<level<<" cell="<<x<<','<<y<<','<<z
                    <<" atlas="<<pixel%snapshot.width<<','<<pixel/snapshot.width<<" BA="<<image[pixel*4+2]<<','<<image[pixel*4+3]<<'\n';
                Check(false,"Native DS unused atlas channels contain stale data");
            }
        }
    }
    for(std::size_t pixel=0;pixel<used.size();++pixel)if(!used[pixel])for(unsigned channel=0;channel<4;++channel)
        if(image[pixel*4+channel]!=0){std::cerr<<"Native DS stale padding stage="<<stage<<" atlas="<<pixel%snapshot.width<<','<<pixel/snapshot.width
            <<" channel="<<channel<<" actual="<<image[pixel*4+channel]<<'\n';Check(false,"Native DS unused slice/region padding was not cleared");}
}
} // namespace NativeDsProducerDetail

inline void RunNativeDsProducerGpuOracle(RHIDevice& device){
    using namespace PipelineDetail;using namespace FiberNearAuditDetail;using namespace NativeDsProducerDetail;
    auto diagnostics=Compute::Device::Borrow(device);const auto initial=diagnostics->Diagnostics();
    constexpr float average=.0001f;
    std::array<StrandVertex,8> vertices{};
    const std::array<glm::vec3,8> positions{{{0,0,0},{.405f,0,0},{0,.485f,.39f},{.405f,.485f,.39f},
        {0,.2f,.18f},{.405f,.2f,.18f},{.15f,0,.2f},{.15f,.485f,.2f}}};
    for(unsigned i=0;i<vertices.size();++i){vertices[i].positionRadius=glm::vec4(positions[i],average*.5f*(i%2?1.1f:.7f));vertices[i].tangentArc={1,0,0,float(i%2)};}
    constexpr std::array<std::uint32_t,8> lines{0,1,2,3,4,5,6,7};
    StrandMesh mesh(device);Check(mesh.Update(vertices,lines),"Create native DS raw segment fixture");
    StrandRenderItem item;item.geometry=mesh.Snapshot();StrandPassView view;view.viewProjection=glm::mat4(1);view.cameraForward={0,0,-1};
    NativeDSVolumePass volume(device);NativeDSVolumeSettings settings;settings.enabled=true;settings.averageWidth=average;settings.opaque=false;
    unsigned checks=0,frames=0;std::uint64_t maximumRawDifference=0;
    const auto render=[&](const NativeDSVolumeSettings& options){
        volume.Begin(1);auto encoder=device.BeginFrame({0,16,16});volume.Prepare(encoder,0,item,view,options);Submit(device,encoder);++frames;
        const auto snapshot=volume.Snapshot(0);
        Check(volume.Statistics().passes==(options.opaque?9u:8u)-(snapshot.injectionSegmentCount?0u:1u)&&
            volume.Statistics().segments==snapshot.injectionSegmentCount,"Native DS producer pass/submitted prefix statistics");
        return volume.Snapshot(0);
    };
    NativeDSVolumeSnapshot retained;
    for(unsigned resolution:{32u,64u,128u})for(bool interpolation:{false,true}){
        settings.resolution=resolution;settings.interpolationInjection=interpolation;auto snapshot=render(settings);
        const auto raw=ReadRaw(snapshot),expected=ExpectedRaw(snapshot,*item.geometry->SegmentSource(),settings);
        Check(raw.size()==expected.size(),"Native DS raw cube size");std::uint64_t actualTotal=0,expectedTotal=0;
        for(std::size_t i=0;i<raw.size();++i){actualTotal+=raw[i];expectedTotal+=expected[i];
            const auto difference=raw[i]>expected[i]?raw[i]-expected[i]:expected[i]-raw[i];maximumRawDifference=std::max(maximumRawDifference,std::uint64_t(difference));
            // Positions/direction normalization may differ by a GPU ulp. The
            // source quantizer itself is separately bit-checked against SSA.
            Check(difference<=2,"Native DS raw straight-line walk/splat differs from independent raster oracle");}
        Check(actualTotal&&std::abs(double(actualTotal)-double(expectedTotal))<=.01*double(expectedTotal),"Native DS raw occupancy total lost contributions");
        CheckHierarchy(device,snapshot,raw,checks,interpolation?"raw Intpij":"raw nearest");
        if(resolution==128)Check(snapshot.constants.resolution==glm::uvec4(81,97,78,6),"Native DS odd logical resolution was rounded/padded to cube dimensions");
        Check(glm::vec3(snapshot.constants.minimum)==glm::vec3(0)&&glm::vec3(snapshot.constants.maximum)==glm::vec3(.405f,.485f,.39f),"Native DS bounds include rendered radius padding");
        if(resolution==32&&!interpolation)retained=snapshot;
    }
    // Native CPU submission selects a prefix of the bound ordered list and
    // compensates its width. Explicit fixture expectations are independent of
    // the resolver; a full-buffer read or density-only rate would both fail.
    settings.resolution=32;settings.interpolationInjection=true;
    for(unsigned scenario=0;scenario<6;++scenario){
        auto options=settings;options.injectionRate=.5f;options.injectionQuality=1;
        unsigned expectedCount=2;float expectedScale=3.f;
        if(scenario==1){options.injectionQuality=2;expectedCount=4;expectedScale=1.5f;}
        if(scenario==2){options.injectionQuality=2;options.injectionMinLodFactor=true;}
        if(scenario==3){options.injectionQuality=2;options.injectionLodFraction=.75f;expectedCount=3;expectedScale=2.f;}
        if(scenario==4){options.injectionLodFraction=0;expectedCount=0;expectedScale=150000.f;}
        if(scenario==5){options.injectionRate=0;expectedCount=0;expectedScale=150000.f;}
        const auto snapshot=render(options);Check(snapshot.injectionSegmentCount==expectedCount&&
            snapshot.injectionWidthScale==expectedScale,"Native DS CPU writer prefix/width compensation differs");
        auto input=*item.geometry->SegmentSource();input.segments.resize(expectedCount);
        auto expectedOptions=options;expectedOptions.widthScale=expectedScale;
        const auto expected=ExpectedRaw(snapshot,input,expectedOptions),raw=ReadRaw(snapshot);
        for(std::size_t i=0;i<raw.size();++i){const auto difference=raw[i]>expected[i]?raw[i]-expected[i]:expected[i]-raw[i];
            Check(difference<=2,"Native DS selected-prefix raster differs from independent fixture");}
        CheckHierarchy(device,snapshot,raw,checks,"CPU selected prefix / compensated width");
    }
    // Real depth texture, identity canonical VP: opaque plane at z=0, with a
    // thin back slab strictly behind it. No proxy geometry participates.
    ResourceBuilder depthResources(device);std::array<float,16*16> depthData;depthData.fill(.5f);
    CreateRHITextureSpec depthSpec;depthSpec.width=depthSpec.height=16;depthSpec.mipmaps=false;
    depthSpec.textureDataStoreType=depthSpec.textureUseType=RHITextureFormat::Depth32F;depthSpec.data=depthData.data();
    const auto depth=depthResources.Create<RHITextureSpec>([&](auto callback){device.async_CreateTexture(depthSpec,callback);});auto depthOwner=depthResources.Finish();
    view.opaqueDepth=depth;settings.resolution=32;settings.opaque=true;settings.widthScale=0;
    auto shell=render(settings);auto rawShell=ReadRaw(shell);unsigned marked=0;
    for(unsigned z=0;z<32;++z)for(unsigned y=0;y<32;++y)for(unsigned x=0;x<32;++x){
        const float worldZ=(float(z)/31.f)*.39f;const bool inside=worldZ>0&&worldZ<settings.threshold;
        Check(rawShell[Index(x,y,z,32)]==(inside?0xff000000u:0u),"Native DS depth shell is not strict front-depth plus metre-threshold OR");marked+=inside;
    }
    Check(marked>0,"Native DS opaque fixture is vacuous");CheckHierarchy(device,shell,rawShell,checks,"opaque shell");
    const auto shellImage=Read(device,shell.atlas);const auto level=shell.constants.levels[1];
    const auto firstPixel=std::size_t(level.x)*shell.width;
    Check(shellImage[firstPixel*4+1]==.5f,"Native DS G mip used max instead of average");
    // Full-width source inputs can deliberately overflow low24 via whole-u32
    // atomicAdd. Two coincident but topologically independent chains agree.
    std::array<StrandVertex,8> carryVertices=vertices;for(auto& v:carryVertices)v.positionRadius.w=.1f;
    carryVertices[4].positionRadius=carryVertices[0].positionRadius;carryVertices[5].positionRadius=carryVertices[1].positionRadius;
    StrandMesh carryMesh(device);Check(carryMesh.Update(carryVertices,lines),"Create native DS carry fixture");auto original=item;
    item.geometry=carryMesh.Snapshot();settings.opaque=false;settings.averageWidth=.0000001f;settings.widthScale=1.5f;settings.interpolationInjection=false;
    auto carry=render(settings);const auto rawCarry=ReadRaw(carry);bool hasCarry=false;for(auto word:rawCarry)hasCarry|=(word>>24u)!=0u;
    Check(hasCarry,"Native DS low24 overflow fixture never reached high8");CheckHierarchy(device,carry,rawCarry,checks,"whole-u32 carry");item=original;
    // A held snapshot must keep BOTH SSBO and texture generations after a
    // resize and Shutdown. A cancelled producer is never treated as cached work.
    const auto retainedRaw=ReadRaw(retained);
    const auto retainedImage=Read(device,retained.atlas);
    settings.averageWidth=average;settings.widthScale=1.5f;settings.resolution=64;
    volume.Begin(1);auto cancelled=device.BeginFrame({0,16,16});volume.Prepare(cancelled,0,item,view,settings);
    Check(cancelled.Cancel(),"Cancel native DS producer recording");volume.Invalidate();
    Check(ReadRaw(retained)==retainedRaw&&Read(device,retained.atlas)==retainedImage,"Native DS resize/cancel retired a leased old generation");
    auto resumed=render(settings);const auto resumedRaw=ReadRaw(resumed);CheckHierarchy(device,resumed,resumedRaw,checks,"resumed after cancel");
    volume.Begin(2);auto disabled=device.BeginFrame({0,16,16});NativeDSVolumeSettings off;
    volume.Prepare(disabled,1,item,view,off);volume.Bind(disabled,1);Check(disabled.Cancel(),"Cancel disabled native DS binding");
    Check(volume.Statistics().passes==0,"Disabled native DS producer recorded compute work");
    volume.Shutdown();Check(ReadRaw(retained)==retainedRaw&&Read(device,retained.atlas)==retainedImage,"Native DS shutdown invalidated leased diagnostics");
    retained={};shell={};carry={};resumed={};
    const auto final=diagnostics->Diagnostics();
    Check(final.liveBuffers==initial.liveBuffers&&final.livePipelines==initial.livePipelines&&final.allocatedBytes==initial.allocatedBytes,
        "Native DS producer/cancel/snapshot lifecycle leaked compute resources");
    Check(final.submittedBatches==initial.submittedBatches&&!final.validationErrors,"Native DS producer used standalone submits or generated validation errors");
    std::cout<<"Native DS GPU producer: "<<frames<<" frames, "<<checks<<" RG checks, max raw rounding difference="<<maximumRawDifference
        <<"; original lines, odd logical R32/64/128, full-u32 carry, zero padding, RG mip averages, depth shell, leases/cancel passed\n";
}
} // namespace Render::TestData
