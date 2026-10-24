#pragma once
#include "Render/Private/Pipeline/fiber_point_paths_shaders.h"
#include "Render/Test/fiber_near_shadow_test.h"
#include <limits>

namespace Render::TestData {
namespace FiberPointAuditDetail {
using FiberNearAuditDetail::Check;
using FiberNearAuditDetail::Read;
using FiberNearAuditDetail::Submit;
inline constexpr unsigned Grid=32,Columns=6,Width=Grid*Columns,Height=Grid*Columns;
inline constexpr float H=.01f;
struct Query {glm::vec4 pointDistance,directionEnabled;bool compareProducer=true;};
inline std::size_t Pixel(unsigned x,unsigned y,unsigned z){return std::size_t(z/Columns*Grid+y)*Width+z%Columns*Grid+x;}
inline std::vector<glm::vec4> Field(float value=0){return std::vector<glm::vec4>(std::size_t(Width)*Height,glm::vec4(value,0,0,0));}
inline Query Ray(glm::vec3 point,glm::vec3 direction,float distance=.15f,bool enabled=true){return {{point,distance},{direction,enabled?1.f:0.f}};}
inline glm::vec3 Positive(glm::vec3 tangent){
    const auto b=glm::normalize(glm::cross(tangent,glm::vec3(0,0,1)));
    return glm::normalize(glm::cross(b,tangent)+b);
}
} // namespace FiberPointAuditDetail

// Actual production point-path helper vs separately invoked existing N/G
// producers, plus independent analytic fields and crossing-ray counterexamples.
// The caller owns adapter selection, GL/Vulkan validation and build scheduling.
inline void RunFiberPointPathsGpuOracle(RHIDevice& device){
    using namespace FiberPointAuditDetail;
    PipelineDetail::ResourceBuilder programs(device);
    auto shader=PipelineDetail::FiberVolumePathsShader();shader.erase(shader.rfind("void main()"));
    const auto opaque=PipelineDetail::FiberVolumeOpaquePathsShader();
    const auto begin=opaque.find("float FiberTraceOpaqueVisibility("),end=opaque.rfind("void main()");
    Check(begin!=std::string::npos&&end>begin,"Locate independent existing blocker marcher");
    shader+="\nlayout(binding=13) uniform sampler2D fiberOpaqueMask;\n"+opaque.substr(begin,end-begin);
    // The original producer prefix already declares the shared exact-cell
    // primitives. Append the new consumer once without redeclaring them.
    auto point=PipelineDetail::FiberPointPathsGLSL();const auto exact=PipelineDetail::FiberVolumeExactGLSL();
    Check(point.starts_with(exact),"Point-path exact helper dependency contract");point.erase(0,exact.size());shader+=point;
    shader+=R"GLSL(
layout(binding=6) uniform sampler2D auditPoint;
layout(binding=7) uniform sampler2D auditDirection;
void main(){
    int x=int(gl_FragCoord.x);vec4 p=texelFetch(auditPoint,ivec2(x,0),0),d=texelFetch(auditDirection,ivec2(x,0),0);
    vec2 actual=FiberPointIblPath(p.xyz,d.xyz,p.w,d.w>.5);
    if(d.w<.5){result=vec4(actual,0.0,1.0);return;}
    bool blockers=fiberOpaque.x>.5&&(fiberOpaque.y>0.0||fiberQuality.z>.5);
    result=vec4(actual,FiberTracePath(p.xyz,d.xyz,p.w),blockers?FiberTraceOpaqueVisibility(p.xyz,d.xyz,p.w):1.0);
}
)GLSL";
    const auto pipeline=programs.FullscreenPipeline(FullscreenVertexShader(),shader);
    const auto mesh=programs.FullscreenMesh();
    const auto uniform=programs.Uniform(sizeof(PipelineDetail::FiberVolumeConstants));
    const auto programOwner=programs.Finish();
    unsigned draws=0,checks=0;double maximumProducerDifference=0,maximumAnalyticError=0;
    const auto run=[&](const char* name,const std::vector<glm::vec4>& density,const std::vector<glm::vec4>& mask,
        const std::vector<Query>& queries,bool blockers,auto oracle){
        PipelineDetail::ResourceBuilder resources(device);
        const auto image=[&](unsigned w,unsigned h,const void* data){
            CreateRHITextureSpec desc;desc.width=w;desc.height=h;desc.mipmaps=false;
            desc.textureDataStoreType=desc.textureUseType=RHITextureFormat::RGBA32F;desc.filterMode=RHIFilterMode::Linear;
            desc.addressMode=RHIAddressMode::ClampToEdge;desc.data=data;
            return resources.Create<RHITextureSpec>([&](auto callback){device.async_CreateTexture(desc,callback);});
        };
        std::vector<glm::vec4> positions,directions;for(const auto& q:queries){positions.push_back(q.pointDistance);directions.push_back(q.directionEnabled);}
        const auto densityImage=image(Width,Height,density.data()),maskImage=image(Width,Height,mask.data());
        const auto queryPoint=image(unsigned(queries.size()),1,positions.data()),queryDirection=image(unsigned(queries.size()),1,directions.data());
        const auto output=resources.Texture(unsigned(queries.size()),1,RHITextureFormat::RGBA32F);
        const auto target=resources.Target(output,{});
        const auto owner=resources.Finish();
        for(bool filtering:{false,true})for(bool exactIntegration:{false,true}){
            PipelineDetail::FiberVolumeConstants constants;constants.minimumStep={0,0,0,H};
            constants.size={Grid,Grid,Grid,Columns};constants.atlas={Width,Height,1,1};
            constants.trace={1.1f,.5f,0,2};constants.cameraLocal.w=1.f;
            constants.opaque={blockers?1.f:0.f,.05f,0,filtering?1.f:0.f};constants.quality.x=exactIntegration?1.f:0.f;
            auto encoder=device.BeginFrame({0,unsigned(queries.size()),1});Check(encoder.KeepAlive(programOwner)&&encoder.KeepAlive(owner),"Pin actual-point path oracle");
            Check(encoder.SetRenderTarget({target,unsigned(queries.size()),1,RHICommand::ClearColor}),"Bind actual-point oracle target");
            PipelineDetail::FiberVolumeUpload(encoder,uniform,constants);
            Check(encoder.BindTexture(densityImage,4)&&encoder.BindTexture(maskImage,5)&&encoder.BindTexture(densityImage,12)&&encoder.BindTexture(maskImage,13)&&
                encoder.BindTexture(queryPoint,6)&&encoder.BindTexture(queryDirection,7)&&encoder.BindPipeline(pipeline)&&encoder.BindMesh(mesh)&&encoder.DrawIndexed({3}),"Draw actual-point path oracle");
            Submit(device,encoder);++draws;const auto values=Read(device,output);Check(values.size()==queries.size()*4,"Actual-point oracle dimensions");
            for(std::size_t i=0;i<queries.size();++i){const auto p=i*4;
                for(unsigned c=0;c<4;++c)Check(std::isfinite(values[p+c]),"Nonfinite actual-point N/G output");
                Check(values[p]>=0.f&&values[p]<=80.f&&values[p+1]>=0.f&&values[p+1]<=1.f,"Actual-point N/G bounds");
                if(queries[i].directionEnabled.w>.5f&&queries[i].compareProducer){
                    const double difference=std::max(std::abs(double(values[p])-values[p+2]),std::abs(double(values[p+1])-values[p+3]));
                    maximumProducerDifference=std::max(maximumProducerDifference,difference);Check(difference<.0003,"Actual-point combined march differs from separate production N/G march");
                }else if(queries[i].directionEnabled.w<.5f)Check(values[p]==0.f&&values[p+1]==1.f,"Disabled point path did not retain exact identity");
                ++checks;
            }
            oracle(values,exactIntegration,filtering);
        }
        std::cout<<"Actual-point path field "<<name<<": manual/linear x march/exact passed\n";
    };
    {
        auto density=Field(10),empty=Field();
        const std::vector<Query> queries{Ray({.055f,.055f,.055f},{1,0,0},.1f),Ray({.055f,.055f,.055f},{7,0,0},.1f),
            Ray({.055f,.055f,.055f},{0,0,0}),Ray({.055f,.055f,.055f},{1,0,0},0),Ray({-.1f,.055f,.055f},{-1,0,0})};
        run("uniform/direction/zero",density,empty,queries,true,[&](const auto& v,bool,bool){
            for(unsigned i=0;i<2;++i){const double error=std::abs(double(v[i*4])-.95);maximumAnalyticError=std::max(maximumAnalyticError,error);Check(error<.0001&&v[i*4+1]==1.f,"Analytic uniform optical depth or direction normalization");}
            for(unsigned i=2;i<queries.size();++i)Check(v[i*4]==0.f&&v[i*4+1]==1.f,"Zero/outward point ray was not empty");
        });
        run("empty fields",empty,empty,{queries.front()},true,[](const auto& v,bool,bool){Check(v[0]==0.f&&v[1]==1.f,"Empty fields changed optical depth or visibility");});
        auto poison=Field(std::numeric_limits<float>::quiet_NaN());
        run("disabled poisoned optional fields",poison,poison,{Ray({.055f,.055f,.055f},{1,0,0},.1f,false)},true,[](const auto&,bool,bool){});
        run("blocker disabled poisoned mask",density,poison,{queries.front()},false,[](const auto& v,bool,bool){Check(std::abs(v[0]-.95f)<.0001f&&v[1]==1.f,"Disabled blocker sampled poison mask");});
    }
    {
        auto inward=Ray({-10.f,.055f,.055f},{1,0,0},11.f),reverse=Ray({10.32f,.055f,.055f},{-1,0,0},11.f);
        auto nearEntry=Ray({-.002f,.055f,.055f},{1,0,0},1.f);
        // The previous producer only accepts interior voxel centers. Comparing
        // its outside march would preserve the exact bug under investigation.
        inward.compareProducer=reverse.compareProducer=nearEntry.compareProducer=false;
        run("outside inward entry and origin-relative bias",Field(10),Field(1),{inward,reverse,nearEntry},true,[&](const auto& v,bool exactIntegration,bool){
            // Integral of the constant interior plus its two half-cell linear
            // border ramps: rho*(32*h-h/4). The third ray begins 3mm INSIDE
            // after its origin-relative 5mm bias; remove integral[0,3mm].
            const std::array<double,3> expected{3.175,3.175,3.1555};
            for(unsigned i=0;i<3;++i){const double error=std::abs(double(v[i*4])-expected[i]);
                if(exactIntegration)maximumAnalyticError=std::max(maximumAnalyticError,error);
                Check(error<(exactIntegration?.001:.04)&&v[i*4+1]<.0001f,"Outside entry wasted trace budget, reset physical bias or lost a blocker");}
        });
    }
    {
        auto density=Field(),mask=Field();
        for(unsigned z=0;z<Grid;++z)for(unsigned y=0;y<Grid;++y){density[Pixel(11,y,z)].r=50;mask[Pixel(11,y,z)].r=1;}
        run("subvoxel actual point vs center",density,mask,{Ray({.109f,.055f,.055f},{0,1,0}),Ray({.105f,.055f,.055f},{0,1,0})},true,[&](const auto& v,bool exactIntegration,bool filtering){
            const double error=std::abs(double(v[0])-2.9);maximumAnalyticError=std::max(maximumAnalyticError,error);
            // Hardware XY interpolation quantizes a weight (desktop NVIDIA
            // uses 8 fractional bits). Bound one full 1/256 weight step:
            // N<=rho*length/256=.02832; visibility<=1/256=.00391.
            const bool hardwareMarch=filtering&&!exactIntegration;
            std::cout<<"Actual-point sheet exact="<<exactIntegration<<", linear="<<filtering<<", N="<<v[0]<<", visibility="<<v[1]<<'\n';
            Check(error<(hardwareMarch?.03:.001)&&std::abs(v[1]-.6f)<(hardwareMarch?.0041:.001),"Off-center sheet analytic N/G");
            Check(v[4]<.0001f&&v[5]>.9999f&&v[0]>v[4]+2.f&&v[1]<v[5]-.3f,"Voxel-center cache concealed the actual-point sheet");
        });
    }
    {
        auto density=Field(),mask=Field();
        for(unsigned z=14;z<=16;++z)for(unsigned y=13;y<=15;++y)for(unsigned x=10;x<=12;++x){density[Pixel(x,y,z)].r=40;mask[Pixel(x,y,z)].r=1;}
        const auto actual=Positive({1,0,0}),mean=Positive(glm::normalize(glm::vec3(1,1,0)));
        run("crossing tangent thin peak/blocker",density,mask,{Ray({.115f,.245f,.055f},actual,.22f),Ray({.115f,.245f,.055f},mean,.22f)},true,[](const auto& v,bool,bool){
            Check(v[0]>.25f&&v[1]<.6f,"Actual tangent ray missed independent thin density peak/blocker");
            Check(v[4]<.0001f&&v[5]>.9999f,"Mean tangent negative control unexpectedly hit the thin peak");
        });
    }
    for(bool blockerFirst:{false,true}){
        auto density=Field(),mask=Field();
        for(unsigned z=0;z<Grid;++z)for(unsigned y=0;y<Grid;++y)for(unsigned x=0;x<Grid;++x){
            if(x>=(blockerFirst?20u:6u)&&x<=(blockerFirst?23u:9u))density[Pixel(x,y,z)].r=10000;
            if(x>=(blockerFirst?6u:20u)&&x<=(blockerFirst?9u:23u))mask[Pixel(x,y,z)].r=1;
        }
        run(blockerFirst?"G saturated before N":"N saturated before G",density,mask,{Ray({.055f,.055f,.055f},{1,0,0},.24f)},true,[](const auto& v,bool,bool){
            Check(v[0]==80.f&&v[1]<.0001f,"N/G early stop discarded the other independent path quantity");
        });
    }
    std::cout<<"Actual-point fiber IBL: "<<draws<<" GPU draws / "<<checks<<" queries; producer max difference="<<maximumProducerDifference
        <<", analytic max error="<<maximumAnalyticError<<"; actual position/direction, disabled identity, blocker routing and independent saturation passed\n";
}
} // namespace Render::TestData
