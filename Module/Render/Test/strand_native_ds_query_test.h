#pragma once
#include "Render/Private/Pipeline/strand_native_ds_volume.h"
#include "Render/Private/Pipeline/strand_native_ds_query.h"
#include "Render/Private/Pipeline/strand_native_ds_contract.h"
#include "Render/Test/fiber_near_shadow_test.h"
#include <glm/gtc/type_ptr.hpp>

namespace Render::TestData {
inline void RunNativeDsQueryGpuOracle(RHIDevice& device){
    using namespace PipelineDetail;
    using namespace FiberNearAuditDetail;
    namespace Ref=PipelineDetail::NativeDs;
    ResourceBuilder programs(device);
    const auto shader=std::string("#version 450 core\n")+StrandNativeDsQueryGLSL()+R"GLSL(
layout(binding=6) uniform sampler2D auditPosition;
layout(binding=7) uniform sampler2D auditDirection;
layout(location=0) out vec4 result;
void main(){int x=int(gl_FragCoord.x);vec4 p=texelFetch(auditPosition,ivec2(x,0),0);
vec4 d=texelFetch(auditDirection,ivec2(x,0),0);
result=vec4(StrandNativeDsPath(p.xyz,d.xyz,p.w),0,1);})GLSL";
    const auto pipeline=programs.FullscreenPipeline(FullscreenVertexShader(),shader);
    const auto triangle=programs.FullscreenMesh();const auto uniform=programs.Uniform(sizeof(NativeDSVolumeConstants));
    const auto programOwner=programs.Finish();
    unsigned checks=0;double maxError=0;
    for(unsigned field=0;field<5;++field){
        NativeDSVolumeConstants c;
        c.minimum={0,0,0,0};c.maximum={1,1,1,0};c.resolution={31,27,25,6};
        c.inverseExtent={1,1,1,0};c.voxel={1.f/32,field==4?1.f/64:1.f/32,1.f/32,1.f/32};
        c.trace={1.1f,0,8,4};c.opaqueInfo={1,.05f,1,0};
        unsigned height=0,width=0;
        for(unsigned lod=0;lod<6;++lod){unsigned edge=32>>lod,columns=unsigned(std::ceil(std::sqrt(float(edge))));
            unsigned rows=(edge+columns-1)/columns;c.levels[lod]={height,edge,columns,rows};
            height+=edge*rows;width=std::max(width,edge*columns);}
        c.atlas={float(width),float(height),32,1};
        const auto value=[&](unsigned lod,Ref::UVec3 p)->Ref::Vec2{
            if(field==0)return {4.f,0.f}; // N exceeds old clamp80
            if(field==1)return {1.f,1.f}; // four attempts before opaque exit
            if(field==2)return {.3f*float(lod+1),.125f*float(lod)};
            return {.01f*float(p[0]+3*p[1]+7*p[2]+lod),field==3&&p[0]<4?.7f:0.f};
        };
        std::vector<glm::vec4> data(std::size_t(width)*height,glm::vec4(0));
        for(unsigned lod=0;lod<6;++lod){const auto level=c.levels[lod];
            for(unsigned z=0;z<level.y;++z)for(unsigned y=0;y<level.y;++y)for(unsigned x=0;x<level.y;++x){
                const auto v=value(lod,{x,y,z});const unsigned px=(z%level.z)*level.y+x,py=level.x+(z/level.z)*level.y+y;
                data[std::size_t(py)*width+px]={v[0],v[1],0,0};}}
        const std::vector<glm::vec4> positions{{.01,.35,.4,.95},{.99,.35,.4,.95},{-.1,.5,.5,2},
            {-.1,.5,.5,2},{.3,.2,.4,.6},{.3,.2,.4,0},{.3,.2,.4,.6},{1,.5,.5,.1}};
        const std::vector<glm::vec4> directions{{1,0,0,0},{-1,0,0,0},{1,0,0,0},
            {-1,0,0,0},{1,2,1,0},{1,0,0,0},{0,0,0,0},{1,0,0,0}};
        ResourceBuilder resources(device);
        const auto image=[&](unsigned w,unsigned h,const void* ptr){CreateRHITextureSpec desc;
            desc.width=w;desc.height=h;desc.mipmaps=false;desc.textureDataStoreType=desc.textureUseType=RHITextureFormat::RGBA32F;
            desc.data=ptr;desc.addressMode=RHIAddressMode::ClampToEdge;desc.filterMode=RHIFilterMode::Nearest;
            return resources.Create<RHITextureSpec>([&](auto cb){device.async_CreateTexture(desc,cb);});};
        const auto volume=image(width,height,data.data()),pimage=image(unsigned(positions.size()),1,positions.data()),dimage=image(unsigned(directions.size()),1,directions.data());
        const auto output=resources.Texture(unsigned(positions.size()),1,RHITextureFormat::RGBA32F);
        const auto target=resources.Target(output,{});
        const auto owner=resources.Finish();
        for(bool enabled:{false,true})for(float offset:{0.f,1.f}){
            c.atlas.w=enabled?1.f:0.f;c.trace.y=offset;
            auto encoder=device.BeginFrame({0,unsigned(positions.size()),1});
            Check(encoder.KeepAlive(owner)&&encoder.KeepAlive(programOwner),"Pin native DS query fixture");
            Check(encoder.SetRenderTarget({target,unsigned(positions.size()),1,RHICommand::ClearColor}),"Native DS query target");
            const auto* bytes=reinterpret_cast<const std::byte*>(&c);
            for(unsigned at=0;at<sizeof(c);at+=256)Check(encoder.UpdateUniformBufferBytes(uniform,bytes+at,std::min(256u,unsigned(sizeof(c))-at),at),"Native DS query constants");
            Check(encoder.BindUniformBuffer(uniform,14)&&encoder.BindTexture(volume,4)&&encoder.BindTexture(pimage,6)&&encoder.BindTexture(dimage,7)&&
                encoder.BindPipeline(pipeline)&&encoder.BindMesh(triangle)&&encoder.DrawIndexed({3}),"Native DS query draw");
            Submit(device,encoder);const auto actual=Read(device,output);
            Check(actual.size()==positions.size()*4,"Native DS query readback size");
            for(unsigned i=0;i<positions.size();++i){
                Ref::Vec2 expected{0,1};const auto p=positions[i];const auto d=directions[i];
                if(enabled&&p.w>0&&glm::length(glm::vec3(d))>1e-12f){
                    const glm::vec3 initial=glm::normalize(glm::vec3(d)),origin=glm::vec3(p)+offset*glm::vec3(c.voxel)*initial;
                    const glm::vec3 delta=glm::vec3(p)+initial*p.w-origin,ray=glm::normalize(delta);
                    float start=0,finish=glm::length(delta);bool miss=false;
                    for(unsigned axis=0;axis<3;++axis){if(std::abs(ray[axis])<1e-12f){miss|=origin[axis]<0||origin[axis]>1;}
                        else{float a=-origin[axis]/ray[axis],b=(1-origin[axis])/ray[axis];start=std::max(start,std::min(a,b));finish=std::min(finish,std::max(a,b));}}
                    miss|=finish<=start||finish<0;
                    if(miss)expected={.5,1};else{
                        const float length=std::min(std::max(finish-start,0.f),1000.f),h=std::min({c.voxel.x,c.voxel.y,c.voxel.z}),count=std::ceil(length/h);
                        if(count>0){Ref::TracePlan plan;const auto entry=origin+ray*start;
                            for(unsigned axis=0;axis<3;++axis){plan.entry[axis]=entry[axis];plan.step[axis]=h*ray[axis];plan.inverseExtent[axis]=1;plan.actual[axis]=c.resolution[axis];}
                            plan.resourceR=32;plan.baseCount=count;plan.baseDs=length/count;plan.voxelX=c.voxel.x;plan.delta=c.trace.x;
                            auto result=Ref::TraceSchedule(plan,[&](const Ref::TraceRead& q){
                                if(q.filtered)return Ref::ClampTrilinear(q.uv,32>>q.level,[&](Ref::UVec3 cell){return value(q.level,cell);});
                                return Ref::LoadInBounds(q.coordinate,32,q.level)?value(q.level,q.coordinate):Ref::Vec2{};
                            },true);
                            expected={result.ng[0],1-result.ng[1]};
                        }
                    }
                }
                for(unsigned channel=0;channel<2;++channel){const float got=actual[i*4+channel];
                    const double error=std::abs(double(got)-expected[channel]);maxError=std::max(maxError,error);
                    Check(std::isfinite(got)&&error<.002*(1+std::abs(expected[channel])),"Native DS query independent ray/schedule oracle mismatch");++checks;}
            }
        }
    }
    std::cout<<"Native DS actual-point query: "<<checks<<" checks (six levels, clamp filtering, integer loads, G stop after four, N>80, anisotropic voxels, miss/zero/disabled), max error="<<maxError<<'\n';
}
} // namespace Render::TestData
