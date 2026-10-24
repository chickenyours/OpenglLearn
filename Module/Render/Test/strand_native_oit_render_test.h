#pragma once
#include "Render/Private/Pipeline/strand_native_oit_pass.h"
#include "Render/Private/Pipeline/strand_native_oit_contract.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <future>
#include <iostream>
#include <limits>
#include <numeric>
#include <string>
#include <type_traits>
#include <vector>

namespace Render::TestData {
namespace NativeOitRenderAudit {
namespace Cpu=PipelineDetail::NativeOit;
using namespace PipelineDetail;
inline void Check(bool q,const std::string& text){if(!q)throw std::runtime_error(text);}
template<class F> auto OnDevice(RHIDevice& device,F operation){
    using T=std::invoke_result_t<F>;auto promise=std::make_shared<std::promise<T>>();auto result=promise->get_future();
    device.async_ExecuteCode([promise,operation=std::move(operation)]()mutable{try{
        if constexpr(std::is_void_v<T>){operation();promise->set_value();}else promise->set_value(operation());
    }catch(...){promise->set_exception(std::current_exception());}});return result.get();
}
inline void Submit(RHIDevice& device,RHIFrameEncoder& encoder){
    Check(encoder.End(false),"End native OIT production fixture");auto done=std::make_shared<bool>(false);
    device.async_SubmitFrameCommands(encoder.GetCommandBuffer(),[done]{*done=true;});
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(30);
    while(!*done){device.returnSystem.DrainCallbacks();Check(std::chrono::steady_clock::now()<end,"Native OIT render fixture timeout");
        if(!*done)device.returnSystem.WaitForCallbacks(std::chrono::milliseconds(2));}
}
struct alignas(16) Line {glm::vec4 a,b,color0,color1,motion0,motion1;glm::uvec4 tiles;};
static_assert(sizeof(Line)==112);
struct Target {StrandPassTarget render;RenderResourceHandle<RHITextureSpec> opaque;RenderResourceHandle<RenderTargetSpec> opaqueTarget;std::shared_ptr<const void> owner;};
inline Target MakeTarget(RHIDevice& device,unsigned size){
    ResourceBuilder b(device);Target t;t.render.width=t.render.height=size;
    t.render.color=b.Texture(size,size,RHITextureFormat::RGBA32F);t.render.depth=b.Texture(size,size,RHITextureFormat::Depth32F);
    t.opaque=b.Texture(size,size,RHITextureFormat::Depth32F);t.opaqueTarget=b.Target({},t.opaque);
    t.render.framebuffer=b.Target(t.render.color,t.render.depth);t.owner=b.Finish();return t;
}
struct Image {unsigned size=0,samples=0,textureWidth=0,textureHeight=0,debugWidth=0,debugHeight=0,pairCapacity=0;
    std::string label;
    std::array<bool,6> valid{}; // resolved,nearest,coverage,composite,motion,destination
    std::vector<float> color,destination,nearest,coverage,depth,motion;
    std::vector<Line> lines;std::vector<glm::uvec4> tiles;std::vector<std::uint32_t> meta;std::vector<Cpu::Node> global;};
template<class T> std::vector<T> Buffer(const StrandNativeOitDebugBuffers& debug,Compute::BufferHandle handle,std::size_t count){
    const auto bytes=debug.owner->Readback(handle,count*sizeof(T));Check(bytes.size()==count*sizeof(T),"Native OIT debug buffer size");
    std::vector<T> out(count);if(!bytes.empty())std::memcpy(out.data(),bytes.data(),bytes.size());return out;
}
inline Image Read(RHIDevice& device,const Target& target,const StrandTemporalTextures& textures,const StrandNativeOitDebugBuffers& debug,
                  std::string label="regular capture"){
    Image out;out.size=target.render.width;out.samples=target.render.samples;
    out.label=std::move(label);
    out.textureWidth=textures.width;out.textureHeight=textures.height;out.debugWidth=debug.width;out.debugHeight=debug.height;
    out.pairCapacity=debug.pairCapacity;
    out.valid={textures.resolvedColor.IsValid(),textures.nearestDepth.IsValid(),textures.coverage.IsValid(),
               textures.compositeDepth.IsValid(),textures.motion.IsValid(),target.render.color.IsValid()};
    const auto texture=[&](auto h){return OnDevice(device,[&device,h]{return device.BackendDiagnostics()->ReadTexture(h);});};
    out.destination=texture(target.render.color);
    if(textures.resolvedColor.IsValid())out.color=texture(textures.resolvedColor);else out.color=out.destination;
    if(textures.nearestDepth.IsValid())out.nearest=texture(textures.nearestDepth);
    if(textures.coverage.IsValid())out.coverage=texture(textures.coverage);
    if(textures.compositeDepth.IsValid())out.depth=texture(textures.compositeDepth);
    if(textures.motion.IsValid())out.motion=texture(textures.motion);
    if(debug.owner){out.lines=Buffer<Line>(debug,debug.lines,debug.pieces);
        out.tiles=Buffer<glm::uvec4>(debug,debug.tiles,((out.size+15u)/16u)*((out.size+15u)/16u));
        out.meta=Buffer<std::uint32_t>(debug,debug.meta,8);out.global=Buffer<Cpu::Node>(debug,debug.global,std::size_t(out.size)*out.size*4);}
    for(float q:out.color)Check(std::isfinite(q),"Native OIT production color is nonfinite");return out;
}
inline void Record(RHIDevice&,RHIFrameEncoder& encoder,StrandRenderPass& pass,const Target& target,
                   std::span<const StrandRenderItem> items,StrandPassView view,float opaque){
    Check(encoder.KeepAlive(target.owner),"Pin native OIT fixture targets");
    RHICommand::SetRenderTarget clear;clear.target=target.opaqueTarget;clear.width=target.render.width;clear.height=target.render.height;
    clear.clearFlags=RHICommand::ClearDepth;clear.clearDepth=opaque;Check(encoder.SetRenderTarget(clear),"Clear native independent opaque depth");
    clear.target=target.render.framebuffer;clear.clearFlags=RHICommand::ClearDepth|RHICommand::ClearColor;clear.clearDepth=1;clear.clearColor=glm::vec4(0);
    Check(encoder.SetRenderTarget(clear),"Clear native production destination");view.opaqueDepth=target.opaque;
    pass.Record(encoder,items,target.render,view);
}
// This evaluator is independent of the production GLSL and atlas. It follows
// the frozen source four-control-point position equation; no CPU production path.
inline glm::vec3 Curve(glm::vec3 p0,glm::vec3 p1,glm::vec3 p2,glm::vec3 p3,float t){
    const float a=std::sqrt(glm::length(p1-p0)),b=std::sqrt(glm::length(p2-p1)),c=std::sqrt(glm::length(p3-p2));
    Check(std::min({a,b,c})>1e-6f,"Native OIT fixture has a collapsed curve span");
    const float u=b*t,v=a+u,w=b-u,z=b+c-u;
    const auto a1=(-u*p0+v*p1)/a,a2=(w*p1+u*p2)/b,a3=(z*p2-w*p3)/c;
    return (w*((w*a1+v*a2)/(a+b))+u*((z*a2+u*a3)/(b+c)))/b;
}
inline void ProjectionOracle(const Image& image,const StrandRenderItem& item,const StrandPassView& view){
    const auto source=item.geometry->SegmentSource();Check(source&&source->directedChains,"Native OIT fixture lost directed source data");
    std::vector<std::uint32_t> next(source->points.size(),~0u);for(const auto r:source->segments)next[r.x]=r.y;
    const auto inverse=glm::inverse(view.viewProjection);const float scale=std::max({glm::length(item.model[0]),glm::length(item.model[1]),glm::length(item.model[2])});
    const auto forward=glm::normalize(glm::length(view.cameraForward)>1e-7f?view.cameraForward:glm::vec3(0,0,-1));
    const auto cameraDepth=[&](glm::vec3 p){return std::abs(glm::dot(glm::vec3(item.model*glm::vec4(p,1))-view.cameraPosition,forward));};
    float minimumDepth=std::numeric_limits<float>::max(),maximumDepth=0;
    for(unsigned c=0;c<8;++c){const auto corner=glm::mix(item.geometry->BoundsMin(),item.geometry->BoundsMax(),
        glm::vec3(float(c&1u),float((c>>1u)&1u),float((c>>2u)&1u)));const float d=cameraDepth(corner);
        minimumDepth=std::min(minimumDepth,d);maximumDepth=std::max(maximumDepth,d);}
    const float bucketScale=1.f/std::max(maximumDepth-minimumDepth,1e-5f),bucketOffset=minimumDepth*bucketScale;
    const auto bucket=[&](glm::vec3 p,bool fused){const float d=cameraDepth(p);
        const float v=fused?std::fma(d,bucketScale,-bucketOffset):Cpu::Add(Cpu::Mul(d,bucketScale),-bucketOffset);
        return unsigned(std::clamp(v,0.f,1.f)*23.f);};
    std::size_t index=0;
    for(const auto r:source->segments){
        const glm::vec3 p1(source->points[r.x].positionArc),p2(source->points[r.y].positionArc);
        const glm::vec3 p0=r.w?2.f*p1-p2:glm::vec3(source->points[r.z].positionArc);
        const glm::vec3 p3=next[r.y]==~0u?2.f*p2-p1:glm::vec3(source->points[next[r.y]].positionArc);
        const auto project=[&](glm::vec3 p){const auto q=view.viewProjection*item.model*glm::vec4(p,1);return glm::vec3(q)/q.w;};
        const auto a=project(p1),b=project(p2);
        const float l1=(std::abs(a.x-b.x)+std::abs(a.y-b.y))*.5f*float(image.size);
        const unsigned count=item.material.sourceCurveGeometry?unsigned(std::floor(std::clamp(l1*0.1111111119389534f,1.f,15.f))):1u;
        // Native coarseRasterMixSubD SSA147 precedes the child loop: original
        // raw endpoints select ONE bucket inherited by every child. Permit
        // only source-project FMA contraction at an integer floor boundary.
        const unsigned rawBucket=std::max(bucket(p1,false),bucket(p2,false)),fusedBucket=std::max(bucket(p1,true),bucket(p2,true));
        unsigned inheritedBucket=~0u;
        for(unsigned child=0;child<count;++child){Check(index<image.lines.size(),"Native projection omitted a visible child");const auto& line=image.lines[index++];
            const unsigned actualBucket=unsigned(line.color0.a);
            Check(actualBucket==rawBucket||actualBucket==fusedBucket,"Native depth bucket used curve child endpoints instead of original raw segment endpoints");
            if(inheritedBucket==~0u)inheritedBucket=actualBucket;
            Check(actualBucket==inheritedBucket,"Children of one original segment did not inherit the same native depth bucket");
            for(unsigned end=0;end<2;++end){const float t=float(child+end)/float(count);
                const auto local=item.material.sourceCurveGeometry?Curve(p0,p1,p2,p3,t):(end?p2:p1);
                const auto n=project(local);const auto actual=end?line.b:line.a;
                const glm::vec3 expected((n.x*.5f+.5f)*image.size,(.5f-.5f*n.y)*image.size,.5f-.5f*n.z);
                Check(glm::all(glm::lessThan(glm::abs(glm::vec3(actual)-expected),glm::vec3(.0007f,.0007f,.000003f))),
                    "Native project world->top-left/reverseZ differs from independent CPU projection");
                const auto unproject=[&](glm::vec2 xy){const auto q=inverse*glm::vec4(xy,n.z,1);return glm::vec3(q)/q.w-view.cameraPosition;};
                const auto delta=unproject({1.f/image.size,-1.f/image.size})-unproject({0,0});
                const float radius=glm::mix(source->radii[r.x],source->radii[r.y],item.material.sourceCurveGeometry?t:float(end));
                float width=std::max(2.f*std::abs(radius)*item.material.widthScale,item.material.minWidthMetres);
                if(item.material.maxWidthMetres>0)width=std::min(width,item.material.maxWidthMetres);
                const float expectedWidth=std::max(width*scale/glm::length(delta),0x1p-23f);
                Check(std::abs(actual.w-expectedWidth)<.00015f,"Native diagonal inverse-projection width has wrong scale or depth domain");
                Check(glm::length(glm::vec3(end?line.color1:line.color0)-item.material.rootColor)<.00001f,
                    "Native projected endpoint did not consume packed BaseColor segment output");
                const auto motion=end?line.motion1:line.motion0;
                const bool prior=view.motionHistoryValid&&item.previousTransformValid&&item.geometry->PreviousInputMatched();
                if(prior){
                    // All fixture streams deliberately retain identical old
                    // local positions; rigid/model/view changes are independent.
                    const auto oldClip=view.previousViewProjection*item.previousModel*glm::vec4(local,1);
                    const auto old=glm::vec3(oldClip)/oldClip.w;
                    const auto quant=[](float q){return Cpu::HalfToFloat(Cpu::FloatToHalf(q));};
                    const glm::vec4 expectedMotion(quant((old.x-n.x)*.5f),quant((old.y-n.y)*.5f),old.z*.5f+.5f,1.f);
                    Check(glm::all(glm::lessThan(glm::abs(motion-expectedMotion),glm::vec4(.000003f))),
                        "Native endpoint static/rigid previous delta/depth/valid differs from CPU projection");
                }else Check(motion==glm::vec4(0),"Invalid native history emitted a valid motion endpoint");
            }
        }
    }
    Check(index==image.lines.size(),"Native projection has unexpected child records");
    const unsigned tiles=(image.size+15)/16;unsigned total=0,maxCount=0,cursor=0,fallbacks=0;
    for(unsigned y=0;y<tiles;++y)for(unsigned x=0;x<tiles;++x){unsigned n=0;
        for(const auto& line:image.lines)n+=x>=line.tiles.x&&y>=line.tiles.y&&x<=line.tiles.z&&y<=line.tiles.w;
        const auto q=image.tiles[y*tiles+x];Check(q.x==n&&q.w==n,"Native GPU tile count/scatter omitted a projected child");
        if(n){unsigned reserved=1;while(reserved<n)reserved<<=1;
            if(reserved>image.pairCapacity-cursor){
                ++fallbacks;Check(q.y==~0u&&q.z==0,"Native exhausted tile did not select complete ordered fallback");
            }else{Check(q.y==cursor&&q.z==reserved,"Native GPU prefix reservation differs from independent tile counts");cursor+=reserved;}
        }else Check(q.y==0&&q.z==0,"Empty native tile reserved candidate storage");
        total+=n;maxCount=std::max(maxCount,n);
    }
    Check(image.meta.size()==8&&image.meta[0]==total&&image.meta[1]==fallbacks&&image.meta[2]==maxCount&&image.meta[3]==cursor,
        "Native GPU meta/prefix counters disagree with projected tile bounds");
    Check(total>0,"Native scheduling fixture has no candidate references");
    if(view.nativeOitPairBudget==0)Check(fallbacks==0,"Automatic tiny-fixture budget unexpectedly overflowed");
    else if(view.nativeOitPairBudget==1)Check(image.pairCapacity==1&&fallbacks>0,"One-reference budget failed to exercise ordered fallback");
}
struct Expected {std::vector<std::array<Cpu::Node,4>> nodes;std::vector<glm::vec4> motion;};
inline Expected WholeImage(unsigned size,std::span<const Image> objectInputs,std::span<const StrandRenderItem> items,unsigned layers,float opaque){
    Expected out;out.nodes.resize(std::size_t(size)*size);out.motion.resize(out.nodes.size(),glm::vec4(0));
    for(auto& a:out.nodes)a.fill(Cpu::Empty);
    const unsigned tileCount=(size+15)/16;
    for(std::size_t object=0;object<items.size();++object)for(unsigned ty=0;ty<tileCount;++ty)for(unsigned tx=0;tx<tileCount;++tx){
        const auto& lines=objectInputs[object].lines;std::vector<unsigned> order;
        for(unsigned i=0;i<lines.size();++i){const auto q=lines[i].tiles;if(tx>=q.x&&ty>=q.y&&tx<=q.z&&ty<=q.w)order.push_back(i);}
        std::sort(order.begin(),order.end(),[&](unsigned a,unsigned b){return lines[a].color0.a<lines[b].color0.a||(lines[a].color0.a==lines[b].color0.a&&a<b);});
        std::array<std::array<Cpu::Node,8>,256> local;for(auto& a:local)a.fill(Cpu::Empty);
        std::array<bool,256> closed{},motionSet{};std::array<glm::vec4,256> firstMotion{};
        std::array<Cpu::Resolved,256> resolved{};
        const auto close=[&]{for(unsigned p=0;p<256;++p)if(!closed[p]){resolved[p]=Cpu::Resolve(std::span(local[p].data(),layers));closed[p]=Cpu::CloseBucket(resolved[p]);}};
        unsigned last=~0u;
        for(unsigned i:order){const auto& line=lines[i];const unsigned bucket=unsigned(line.color0.a);
            if(last!=~0u&&last!=bucket)close();last=bucket;
            Cpu::WuSegment s{{line.a.x-float(tx*16),line.a.y-float(ty*16)},{line.b.x-float(tx*16),line.b.y-float(ty*16)},
                {line.a.w,line.b.w},{line.a.z,line.b.z},{line.color0.r,line.color0.g,line.color0.b},{line.color1.r,line.color1.g,line.color1.b}};
            if(std::min(s.p0[0],s.p1[0])>=16||std::min(s.p0[1],s.p1[1])>=16||std::max(s.p0[0],s.p1[0])< -1||std::max(s.p0[1],s.p1[1])< -1)continue;
            for(const auto& d:Cpu::Wu(s,[&](int,int){return 1.f-opaque;})){
                const unsigned p=unsigned(d.pixel[1]*16+d.pixel[0]);if(closed[p])continue;Cpu::InnerInsert(std::span(local[p].data(),layers),d.node);
                if(!motionSet[p]){const bool steep=std::abs(s.p1[1]-s.p0[1])>std::abs(s.p1[0]-s.p0[0]);
                    const bool reverse=steep?s.p0[1]>s.p1[1]:s.p0[0]>s.p1[0];const float u=reverse?1.f-d.progress:d.progress;
                    const auto m=glm::mix(line.motion0,line.motion1,u);const unsigned x=tx*16+d.pixel[0],y=size-1u-(ty*16+d.pixel[1]);
                    firstMotion[p]={(float(x)+.5f)/size+m.x,(float(y)+.5f)/size+m.y,m.z,m.w>.999f?1.f:0.f};motionSet[p]=true;}
            }
        }
        close();
        for(unsigned y=0;y<16&&ty*16+y<size;++y)for(unsigned x=0;x<16&&tx*16+x<size;++x){const unsigned p=y*16+x;
            auto q=resolved[p];Cpu::Opacity(q,items[object].material.opacity);if(q.reverseZ==0||!(q.transmission<1.f))continue;
            const auto node=Cpu::Pack(q.reverseZ,q.color,q.transmission);const unsigned pixel=(size-1u-(ty*16+y))*size+tx*16+x;
            const bool nearest=node.hi>out.nodes[pixel][0].hi;Cpu::GlobalInsert(out.nodes[pixel],node);
            if(nearest){const auto m=firstMotion[p];out.motion[pixel]=m.w>.5f&&glm::all(glm::greaterThanEqual(glm::vec3(m),glm::vec3(0)))&&
                glm::all(glm::lessThanEqual(glm::vec3(m),glm::vec3(1)))?m:glm::vec4(0);}
        }
    }return out;
}
inline unsigned Compare(const Image& image,const Expected& expected,float opaque,float threshold){
    const auto pixels=std::size_t(image.size)*image.size;
    const bool dimensions=image.color.size()==pixels*4&&image.coverage.size()==pixels*4&&image.nearest.size()==pixels&&
        image.depth.size()==pixels&&image.motion.size()==pixels*4&&image.global.size()==pixels*4;
    if(!dimensions){
        std::cerr<<"Native render dimensions: case="<<image.label<<" target="<<image.size<<'x'<<image.size<<" samples="<<image.samples
            <<" temporal="<<image.textureWidth<<'x'<<image.textureHeight<<" debug="<<image.debugWidth<<'x'<<image.debugHeight
            <<" expectedPixels="<<pixels<<" color="<<image.color.size()<<" destination="<<image.destination.size()
            <<" nearest="<<image.nearest.size()<<" coverage="<<image.coverage.size()<<" compositeDepth="<<image.depth.size()
            <<" motion="<<image.motion.size()<<" globalNodes="<<image.global.size()<<" lines="<<image.lines.size()
            <<" tiles="<<image.tiles.size()<<" meta="<<image.meta.size()<<" valid(resolved/nearest/coverage/composite/motion/destination)=";
        for(bool valid:image.valid)std::cerr<<(valid?1:0);std::cerr<<'\n';
    }
    Check(dimensions,"Native render output dimensions");
    unsigned covered=0;
    for(std::size_t p=0;p<pixels;++p){
        for(unsigned k=0;k<4;++k)if(image.global[p*4+k]!=expected.nodes[p][k]){
            std::cerr<<"native whole-image node mismatch pixel="<<p<<" layer="<<k<<" actual="<<image.global[p*4+k].lo<<','<<image.global[p*4+k].hi
                <<" expected="<<expected.nodes[p][k].lo<<','<<expected.nodes[p][k].hi<<'\n';Check(false,"Native production packed whole-image differs from CPU Wu/local/global contract");}
        const auto q=Cpu::Resolve(expected.nodes[p]);const float alpha=1.f-q.transmission,depth=q.reverseZ>0?1.f-q.reverseZ:1.f;
        const bool selected=q.reverseZ>0&&alpha>=threshold;covered+=alpha>0;
        Check(std::abs(image.color[p*4+3]-alpha)<.000002f,"Native whole-image coverage mismatch");
        for(unsigned c=0;c<3;++c){const float rgb=alpha>0?q.color[c]/alpha:0;
            Check(std::abs(image.color[p*4+c]-rgb)<.00003f,"Native whole-image straight/premultiplied RGB mismatch");}
        Check(std::abs(image.nearest[p]-depth)<.000002f&&std::abs(image.coverage[p*4]-alpha)<.000002f&&
            std::abs(image.coverage[p*4+1]-(selected?1.f:0.f))<.000002f&&
            std::abs(image.depth[p]-(selected?std::min(depth,opaque):opaque))<.000002f,"Native nearest/coverage/threshold composite depth mismatch");
        for(unsigned c=0;c<4;++c)Check(std::abs(image.motion[p*4+c]-expected.motion[p][c])<.000003f,"Native previousUV/depth/valid consumer mismatch");
    }return covered;
}
inline float Difference(const std::vector<float>& a,const std::vector<float>& b){Check(a.size()==b.size(),"Native/legacy restore dimensions");float d=0;
    for(std::size_t i=0;i<a.size();++i)d=std::max(d,std::abs(a[i]-b[i]));return d;}
} // namespace NativeOitRenderAudit

inline void RunNativeOitRenderTests(RHIDevice& device){
    using namespace NativeOitRenderAudit;
    const auto target=MakeTarget(device,64),small=MakeTarget(device,32);StrandRenderPass pass(device);
    StrandPassView view;view.nativeOit=true;view.nativeOitLayers=4;view.cameraPosition=view.previousCameraPosition={0,0,2};
    view.motionHistoryValid=true;view.outputTemporalData=view.outputMotionData=true;view.outputStraightColor=true;
    view.debugMode=StrandDebugMode::BaseColor;view.frontLayerTransparency=true;view.frontDepthGroups=2;view.pairedLayerTransparency=true;
    view.lighting.lightColors.fill(glm::vec4(0));view.lighting.ambientAndExposure=glm::vec4(0);view.sunIntensity=0;
    StrandMesh mesh(device),second(device),equalMesh(device);std::vector<StrandVertex> points;std::vector<std::uint32_t> indices;
    for(unsigned i=0;i<12;++i){const float x=(float(int(i%3)-1))*.017f,z=-.5f+float(i)*.0625f;
        const unsigned start=unsigned(points.size());points.push_back({{x-.19f,-.73f,z,.006f+float(i%2)*.001f},{.2f,1,0,0}});
        points.push_back({{x+.17f,.71f,z,.006f+float(i%2)*.001f},{.2f,1,0,1}});indices.insert(indices.end(),{start,start+1});}
    Check(mesh.Update(points,indices)&&mesh.Update(points,indices,{},points),mesh.LastError());
    Check(second.Update(points,indices)&&second.Update(points,indices,{},points),second.LastError());
    StrandRenderItem item;item.geometry=mesh.Snapshot();item.previousTransformValid=true;item.material.sourceSegmentShading=true;
    item.material.physicalScattering=true;item.material.rootColor=item.material.tipColor={.5f,.125f,.03125f};item.material.opacity=1.f;
    auto other=item;other.geometry=second.Snapshot();other.material.rootColor=other.material.tipColor={.03125f,.25f,.5f};other.material.opacity=.375f;
    other.model=glm::translate(glm::mat4(1),glm::vec3(.015625f,0,.0625f));other.previousModel=other.model;
    unsigned captures=0,covered=0;
    const auto capture=[&](const Target& t,std::span<const StrandRenderItem> items,const StrandPassView& camera,float opaque=1.f){
        auto encoder=device.BeginFrame({0,t.render.width,t.render.height});Record(device,encoder,pass,t,items,camera,opaque);
        const auto debug=pass.NativeOitDebug();const auto textures=pass.TemporalTextures();Submit(device,encoder);++captures;
        if(camera.nativeOit)Check(pass.NativeOitActive()&&pass.NativeOitComputePasses()>0&&pass.NativeOitLayers()==camera.nativeOitLayers&&pass.NativeOitBytes()>0,"Native OIT production routing/statistics inactive");
        return Read(device,t,textures,debug,"regular capture "+std::to_string(captures));
    };
    const auto oracle=[&](const Target& t,std::span<const StrandRenderItem> items,const StrandPassView& camera,float opaque=1.f){
        std::vector<Image> inputs;for(const auto& draw:items){inputs.push_back(capture(t,std::span(&draw,1),camera,opaque));ProjectionOracle(inputs.back(),draw,camera);}
        auto image=items.size()==1?std::move(inputs.front()):capture(t,items,camera,opaque);
        if(items.size()==1)inputs[0]=image;
        covered+=Compare(image,WholeImage(t.render.width,inputs,items,camera.nativeOitLayers,opaque),opaque,camera.depthWriteThreshold);return image;
    };
    const auto one=std::span(&item,1);const auto linear4=oracle(target,one,view);
    Check(linear4.meta[0]>0&&linear4.meta[1]==0,"Automatic native budget baseline is empty or already overflowed");
    view.nativeOitPairBudget=1;const auto overflow=oracle(target,one,view);
    Check(overflow.meta[1]>0&&overflow.global==linear4.global&&Difference(overflow.color,linear4.color)==0,
        "Ordered fallback dropped/reordered a fragment or changed packed whole-image output");
    view.nativeOitPairBudget=0;const auto autoRestored=oracle(target,one,view);
    Check(autoRestored.meta[0]>0&&autoRestored.meta[1]==0&&autoRestored.global==linear4.global&&Difference(autoRestored.color,linear4.color)==0,
        "Restoring automatic pair budget retained overflow state or changed output");
    bool highDepth=false,staticMotion=false;for(std::size_t p=0;p<linear4.color.size()/4;++p){highDepth|=linear4.coverage[p*4+1]>.5f;staticMotion|=linear4.motion[p*4+3]>.5f;}
    Check(highDepth&&staticMotion,"Native high-coverage/static-history fixture is vacuous");
    view.nativeOitLayers=8;oracle(target,one,view);
    item.material.sourceCurveGeometry=true;view.nativeOitLayers=4;oracle(target,one,view);
    view.nativeOitLayers=8;oracle(target,one,view);
    // A real bend exercises predecessor/successor controls in addition to the
    // straight-chain overflow groom, whose >8 coincident candidates test tails.
    std::array<StrandVertex,3> bend{{{{-.61f,-.69f,-.5f,.009f},{0,1,0,0}},{{.09f,-.03f,.125f,.01f},{1,1,0,.5f}},{{-.19f,.67f,.5f,.007f},{0,1,0,1}}}};
    const std::array<std::uint32_t,4> bendIndices{0,1,1,2};StrandMesh bendMesh(device);
    Check(bendMesh.Update(bend,bendIndices)&&bendMesh.Update(bend,bendIndices,{},bend),bendMesh.LastError());auto bent=item;bent.geometry=bendMesh.Snapshot();
    const auto depthBend=oracle(target,std::span(&bent,1),view);
    // ProjectionOracle already checks every child against the independent
    // original-endpoint formula using ACTUAL Geometry bounds. Those bounds
    // include radius padding: do not substitute the unpadded raw-z min/max.
    std::vector<unsigned> bendBuckets;for(const auto& line:depthBend.lines)bendBuckets.push_back(unsigned(line.color0.a));
    std::sort(bendBuckets.begin(),bendBuckets.end());bendBuckets.erase(std::unique(bendBuckets.begin(),bendBuckets.end()),bendBuckets.end());
    const bool separated=bendBuckets.size()==2&&bendBuckets.back()-bendBuckets.front()>=8u;
    if(!separated){std::cerr<<"Changing-depth curve buckets:";for(unsigned b:bendBuckets)std::cerr<<' '<<b;
        std::cerr<<" boundsZ="<<bent.geometry->BoundsMin().z<<','<<bent.geometry->BoundsMax().z<<'\n';}
    Check(separated,"Changing-depth curve did not exercise two separated inherited raw-segment buckets");
    item.material.sourceCurveGeometry=false;view.nativeOitLayers=4;std::array<StrandRenderItem,2> objects{item,other};oracle(target,objects,view);
    auto perspective=view;perspective.viewProjection=glm::perspective(glm::radians(50.f),1.f,.1f,8.f)*glm::lookAt(glm::vec3(0,0,2),glm::vec3(0),glm::vec3(0,1,0));
    perspective.previousViewProjection=perspective.currentUnjitteredViewProjection=perspective.previousUnjitteredViewProjection=perspective.viewProjection;oracle(target,one,perspective);
    // Static/rigid history: world transform delta is binary-exact, and the
    // source half2 delta must become the absolute old UV expected by hair TAA.
    auto moving=item;moving.model=glm::translate(glm::mat4(1),glm::vec3(.125f,-.0625f,0));
    const auto rigid=oracle(target,std::span(&moving,1),view);
    for(const auto& line:rigid.lines){Check(std::abs(line.motion0.x+.0625f)<1e-7f&&std::abs(line.motion0.y-.03125f)<1e-7f&&line.motion0.w==1,"Native rigid previousUV delta sign or validity");}
    auto invalid=view;invalid.motionHistoryValid=false;oracle(target,one,invalid);
    // Strict equal-depth rejection and zero-opacity object must leave global
    // nodes empty, not fabricate nearest depth/valid motion at alpha zero.
    const std::array<StrandVertex,2> equalPoints{{{{-.3f,-.7f,.25f,.012f},{0,1,0,0}},{{.3f,.7f,.25f,.012f},{0,1,0,1}}}};
    const std::array<std::uint32_t,2> equalIndices{0,1};Check(equalMesh.Update(equalPoints,equalIndices)&&equalMesh.Update(equalPoints,equalIndices,{},equalPoints),equalMesh.LastError());
    auto equal=item;equal.geometry=equalMesh.Snapshot();const auto rejected=oracle(target,std::span(&equal,1),view,.625f);
    for(std::size_t a=3;a<rejected.color.size();a+=4)Check(rejected.color[a]==0,"Equal opaque depth was accepted by native Wu");
    auto invisible=item;invisible.material.opacity=0;oracle(target,std::span(&invisible,1),view);
    auto low=item;low.material.opacity=.25f;const auto lowImage=oracle(target,std::span(&low,1),view,.9f);
    for(std::size_t p=0;p<lowImage.color.size()/4;++p)Check(lowImage.coverage[p*4+1]==0,"Low object opacity unexpectedly wrote high-coverage depth");
    auto legacy=view;legacy.nativeOit=false;const auto before=capture(target,one,legacy);oracle(target,one,view);const auto after=capture(target,one,legacy);
    Check(!pass.NativeOitActive()&&Difference(before.destination,after.destination)<1e-6f,"Native off/on/off changed the restored legacy image");
    {auto cancelled=device.BeginFrame({0,64,64});Record(device,cancelled,pass,target,one,view,1);Check(cancelled.End(false),"End cancelled native frame");pass.InvalidateVolume();}
    const auto rerecorded=oracle(target,one,view);Check(Difference(linear4.color,rerecorded.color)<1e-6f,"Cancelled native work contaminated the next submitted frame");
    // Empty work must reset both old and native activity/cost snapshots, even
    // after the preceding nonempty native generation has allocated resources.
    for(bool requested:{false,true}){
        auto emptyView=view;emptyView.nativeOit=requested;auto encoder=device.BeginFrame({0,64,64});
        Record(device,encoder,pass,target,std::span<const StrandRenderItem>{},emptyView,1);
        Check(!pass.NativeOitActive()&&pass.NativeOitComputePasses()==0&&pass.NativeOitLayers()==0&&pass.NativeOitBytes()==0&&
            pass.PassCount()==0&&pass.TransparencyRasterPassCount()==0&&!pass.NativeOitDebug().owner,
            "Empty old/native item list retained previous active statistics or debug generation");
        Submit(device,encoder);++captures;
        const auto clear=OnDevice(device,[&]{return device.BackendDiagnostics()->ReadTexture(target.render.color);});
        Check(clear.size()==64u*64u*4u&&std::all_of(clear.begin(),clear.end(),[](float q){return q==0;}),"Empty native/old work changed the cleared destination");
    }
    {
        ResourceBuilder b(device);Target msaa;msaa.render.width=msaa.render.height=64;msaa.render.samples=4;
        msaa.render.color=b.Texture(64,64,RHITextureFormat::RGBA16F,4);msaa.render.depth=b.Texture(64,64,RHITextureFormat::Depth32F,4);
        msaa.render.framebuffer=b.Target(msaa.render.color,msaa.render.depth);msaa.opaque=b.Texture(64,64,RHITextureFormat::Depth32F);
        msaa.opaqueTarget=b.Target({},msaa.opaque);const auto resolved=b.Texture(64,64,RHITextureFormat::RGBA16F);
        const auto resolvedTarget=b.Target(resolved,{});msaa.owner=b.Finish();
        const auto multisample=[&](bool native){auto camera=view;camera.nativeOit=native;camera.outputStraightColor=false;
            auto encoder=device.BeginFrame({0,64,64});
            Record(device,encoder,pass,msaa,one,camera,1);
            Check(!pass.NativeOitActive()&&pass.NativeOitComputePasses()==0&&pass.NativeOitBytes()==0&&
                pass.MultisampleTransparencyFallback()&&pass.TemporalDataMultisampleFallback(),"MSAA native request did not report legacy/temporal fallback");
            Check(encoder.ResolveRenderTarget({msaa.render.framebuffer,resolvedTarget,true,false}),"Resolve native-request MSAA fallback");
            Submit(device,encoder);++captures;return OnDevice(device,[&]{return device.BackendDiagnostics()->ReadTexture(resolved);});};
        const auto old=multisample(false),fallback=multisample(true);
        Check(Difference(old,fallback)<1e-6f&&std::any_of(fallback.begin(),fallback.end(),[](float q){return q>0;}),
            "Native request changed/vacuously skipped the actual multisample legacy fallback");
        for(float q:fallback)Check(std::isfinite(q),"Native MSAA fallback contains nonfinite values");
    }
    // Record two different resource generations, resize and release the pass
    // before either submission. Encoders must retain all old compute/RT owners.
    auto first=device.BeginFrame({0,64,64});Record(device,first,pass,target,one,view,1);
    const auto firstDebug=pass.NativeOitDebug();const auto firstTextures=pass.TemporalTextures();
    auto resized=device.BeginFrame({0,32,32});Record(device,resized,pass,small,one,view,1);
    const auto secondDebug=pass.NativeOitDebug();const auto secondTextures=pass.TemporalTextures();pass.Shutdown();
    Submit(device,first);auto retained=Read(device,target,firstTextures,firstDebug,"shutdown-before-submit retained64");ProjectionOracle(retained,item,view);
    const std::array<Image,1> retainedInput{retained};covered+=Compare(retained,WholeImage(64,retainedInput,one,4,1),1,view.depthWriteThreshold);
    Submit(device,resized);auto resizedImage=Read(device,small,secondTextures,secondDebug,"shutdown-before-submit retained32");ProjectionOracle(resizedImage,item,view);
    const std::array<Image,1> resizedInput{resizedImage};covered+=Compare(resizedImage,WholeImage(32,resizedInput,one,4,1),1,view.depthWriteThreshold);captures+=2;
    Check(covered>100,"Native OIT production fixture is vacuous");
    Check(OnDevice(device,[&]{return device.BackendDiagnostics()->ValidationErrorCount();})==0,"Native OIT render fixture validation error");
    std::cout<<"Native OIT production: "<<captures<<" captures, whole-image packed Wu/local4/8/global4, forced ordered overflow/auto restore, projection/width, curve, objects, strict depth, motion, empty/MSAA fallback, legacy restore, cancel and pre-submit resize/shutdown passed\n";
}
} // namespace Render::TestData
