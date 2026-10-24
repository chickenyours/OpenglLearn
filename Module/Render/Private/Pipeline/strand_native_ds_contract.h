#pragma once
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace Render::PipelineDetail::NativeDs {
// Pure arithmetic/reference scheduling. Production volume construction and
// sampling execute on the GPU; no CPU whole-frame density renderer lives here.
using Vec2=std::array<float,2>;using Vec3=std::array<float,3>;using UVec3=std::array<std::uint32_t,3>;
inline constexpr std::uint32_t LowMask=0xffffffu;
inline float Add(float a,float b){volatile float q=a+b;return q;}
inline float Mul(float a,float b){volatile float q=a*b;return q;}
inline float Div(float a,float b){volatile float q=a/b;return q;}
inline float RoundEven(float a){const float b=std::floor(a),f=Add(a,-b);return f<.5f?b:f>.5f?Add(b,1):std::fmod(std::abs(b),2.f)==0.f?b:Add(b,1);}
inline float MinVoxel(Vec3 v){return std::min(std::min(v[0],v[1]),v[2]);}
inline float NormalizedWidth(std::uint32_t packed,float average){return std::max(Div(Mul(float(packed&65535),0.00001525902189314365f),average),0.f);}
inline float DoseTotal(float rasterWidthScale,float w0,float w1,float progress,float hCm){
    const float scale=Mul(rasterWidthScale,0.004999999888241291f),start=Mul(scale,w0),delta=Mul(scale,Add(w1,-w0));
    return Mul(Div(Add(Mul(progress,delta),start),hCm),1000.f);
}
inline std::uint32_t Dose(float total){return std::uint32_t(RoundEven(total))&LowMask;}
inline std::array<std::uint32_t,8> Intpij(float total,Vec3 fraction){
    const float ix=Add(1,-fraction[0]),iy=Add(1,-fraction[1]),iz=Add(1,-fraction[2]);
    const float a=Mul(total,ix),b=Mul(total,fraction[0]),c=Mul(a,iy),d=Mul(b,iy),e=Mul(a,fraction[1]),f=Mul(b,fraction[1]);
    return {Dose(Mul(c,iz)),Dose(Mul(d,iz)),Dose(Mul(e,iz)),Dose(Mul(f,iz)),
        Dose(Mul(c,fraction[2])),Dose(Mul(d,fraction[2])),Dose(Mul(e,fraction[2])),Dose(Mul(f,fraction[2]))};
}
inline Vec2 Copy(std::uint32_t raw){return {Mul(float(raw&LowMask),.0010000000474974513f),std::clamp(Mul(float(raw>>24),.003921568859368563f),0.f,1.f)};}
inline Vec2 Mip(const std::array<Vec2,8>& children){Vec2 q=children[0];for(unsigned i=1;i<8;++i)for(unsigned c=0;c<2;++c)q[c]=Add(q[c],children[i][c]);
    for(auto& c:q)c=Mul(c,.125f);return q;}
struct Info {Vec3 minimum{},maximum{},voxel{},inverseExtent{};UVec3 actual{};std::uint32_t resourceR=0;};
inline Info MakeInfo(Vec3 minimum,Vec3 maximum,float requestedCm,std::uint32_t resourceR){
    Info out;out.minimum=minimum;out.maximum=maximum;out.resourceR=resourceR;
    for(unsigned i=0;i<3;++i){const float e=Add(maximum[i],-minimum[i]);const auto n=std::uint32_t(RoundEven(Div(std::abs(e),requestedCm)));
        out.actual[i]=std::min(n,resourceR);out.voxel[i]=n<resourceR?requestedCm:Div(std::abs(e),float(resourceR));out.inverseExtent[i]=Div(1,e);}
    return out;
}
inline Vec3 Coordinate(Vec3 position,Vec3 minimum,Vec3 inverseExtent,UVec3 actual){Vec3 q;
    for(unsigned i=0;i<3;++i)q[i]=Mul(std::clamp(Mul(Add(position[i],-minimum[i]),inverseExtent[i]),0.f,1.f),float(actual[i]-1));return q;}
inline Vec3 TextureUV(Vec3 normalized,UVec3 actual,std::uint32_t resourceR){Vec3 out;
    for(unsigned i=0;i<3;++i)out[i]=Mul(Div(normalized[i],float(resourceR-1)),float(actual[i]-1));return out;}
struct Lod {float weight=0,rounded=0;std::uint32_t level=0;bool valid=false;};
inline Lod TryLod(float factor,float baseDs,float voxelX){Lod q;q.weight=Div(Mul(factor,baseDs),voxelX);
    q.rounded=std::min(RoundEven(std::log2(q.weight)),5.f);q.valid=q.weight>0&&std::isfinite(q.rounded)&&q.rounded>=0;
    if(q.valid)q.level=std::uint32_t(q.rounded);return q;}
inline float NextFactor(float factor,float delta,std::uint32_t traceLimit){return std::min(Mul(delta,factor),float(traceLimit));}
inline void Accumulate4(const std::array<Vec2,4>& reads,const std::array<float,4>& weights,Vec2& ng){
    for(unsigned i=0;i<4;++i){ng[0]=Add(Mul(reads[i][0],weights[i]),ng[0]);ng[1]=std::max(ng[1],reads[i][1]);}}
inline std::uint32_t MipEdge(std::uint32_t resourceR,unsigned level){return resourceR>>level;}
inline UVec3 Neighbor(UVec3 base,UVec3 actual,unsigned corner){for(unsigned i=0;i<3;++i)base[i]=std::min(base[i]+((corner>>i)&1u),actual[i]-1);return base;}
// Atlas layout is a host adapter. Native Texture physical mip is R>>LOD;
// logical odd actualRes must never be used to clip integer loads. For R128,
// actual81 has occupied mip1 coordinate40 (base80 + zero padding81).
inline bool LoadInBounds(UVec3 coordinate,std::uint32_t resourceR,unsigned level){const auto r=MipEdge(resourceR,level);
    return coordinate[0]<r&&coordinate[1]<r&&coordinate[2]<r;}
struct FilterAxis {int low=0,high=0;float fraction=0;};
inline FilterAxis ClampLinearAxis(float normalized,std::uint32_t resourceEdge){const float p=Add(Mul(normalized,float(resourceEdge)),-.5f),b=std::floor(p);
    return {std::clamp(int(b),0,int(resourceEdge)-1),std::clamp(int(b)+1,0,int(resourceEdge)-1),Add(p,-b)};}
// Read is called with integer0..R-1 coordinates. This is the documented host
// clamp-trilinear sampler policy; the native sampler state is not captured.
template<class Read> Vec2 ClampTrilinear(Vec3 uv,std::uint32_t edge,Read read){
    std::array<FilterAxis,3> a;for(unsigned i=0;i<3;++i)a[i]=ClampLinearAxis(uv[i],edge);
    std::array<Vec2,8> p;for(unsigned i=0;i<8;++i)p[i]=read(UVec3{std::uint32_t((i&1)?a[0].high:a[0].low),
        std::uint32_t((i&2)?a[1].high:a[1].low),std::uint32_t((i&4)?a[2].high:a[2].low)});
    const auto lerp=[](Vec2 x,Vec2 y,float t){for(unsigned c=0;c<2;++c)x[c]=Add(x[c],Mul(Add(y[c],-x[c]),t));return x;};
    return lerp(lerp(lerp(p[0],p[1],a[0].fraction),lerp(p[2],p[3],a[0].fraction),a[1].fraction),
        lerp(lerp(p[4],p[5],a[0].fraction),lerp(p[6],p[7],a[0].fraction),a[1].fraction),a[2].fraction);
}
struct TracePlan {Vec3 entry{},step{},minimum{},inverseExtent{};UVec3 actual{};std::uint32_t resourceR=32;
    float baseCount=0,baseDs=0,voxelX=1,delta=1.1f;std::uint32_t traceLimit=8,interpolationSamples=4;};
struct TraceRead {float cursor=0,factor=1,weight=0;UVec3 base{},coordinate{};Vec3 uv{};std::uint32_t level=0;bool filtered=false;};
struct TraceResult {Vec2 ng{};std::vector<TraceRead> reads;float finalCursor=0;bool invalidLod=false;};
// Given an already-clipped source ray, preserve new-cell-only growth, NEW
// factor cursor advance and the FOUR-attempt opacity check. The callback owns
// actual image values; this is not a CPU volume production/rendering path.
template<class Read> TraceResult TraceSchedule(const TracePlan& p,Read read,bool clampNegativeLod=false){
    TraceResult out;if(p.baseCount<=0)return out;
    if(p.delta<1||p.traceLimit<1)throw std::invalid_argument("Reference trace requires monotone source step factors");
    float cursor=0,factor=1;UVec3 last{~0u,~0u,~0u};Vec3 relative;
    for(unsigned i=0;i<3;++i)relative[i]=Add(p.entry[i],-p.minimum[i]);
    while(cursor<p.baseCount){std::array<Vec2,4> values{};std::array<float,4> weights{1,1,1,1};
        for(unsigned lane=0;lane<4;++lane){Vec3 normalized,q;UVec3 cell;
            for(unsigned i=0;i<3;++i){normalized[i]=std::clamp(Mul(Add(relative[i],Mul(cursor,p.step[i])),p.inverseExtent[i]),0.f,1.f);
                q[i]=Mul(normalized[i],float(p.actual[i]-1));cell[i]=std::uint32_t(q[i]);}
            if(cursor<p.baseCount&&cell!=last){const auto lod=TryLod(factor,p.baseDs,p.voxelX);
                if(!lod.valid&&!clampNegativeLod){out.invalidLod=true;out.finalCursor=cursor;return out;}
                TraceRead request{cursor,factor,lod.weight,cell,{},TextureUV(normalized,p.actual,p.resourceR),lod.level,cursor<float(p.interpolationSamples)};
                for(unsigned i=0;i<3;++i)request.coordinate[i]=cell[i]>>lod.level;
                out.reads.push_back(request);values[lane]=read(request);weights[lane]=lod.weight;last=cell;factor=NextFactor(factor,p.delta,p.traceLimit);}
            const float next=Add(cursor,factor);if(next<=cursor)throw std::runtime_error("Reference trace cursor did not advance");cursor=next;
        }
        Accumulate4(values,weights,out.ng);if(out.ng[1]>=1)break;
    }
    out.finalCursor=cursor;return out;
}
} // namespace Render::PipelineDetail::NativeDs
