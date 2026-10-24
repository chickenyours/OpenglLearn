#pragma once
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace Render::PipelineDetail::NativeOit {
// Pure CPU arithmetic oracle, not a CPU renderer or a replacement for the GPU
// path. Source: normal fineRasterOIT4/8, SHA efb69f3d...5d00. The independent
// generated fixtures evaluate its literal DXIL SSA rather than this header.
struct Node {std::uint32_t lo=0,hi=0;friend bool operator==(Node,Node)=default;};
inline constexpr Node Empty{0,0x3c00};
using Color=std::array<float,3>;
struct Resolved {Color color{0,0,0};float transmission=1,reverseZ=0;};
inline bool IsEmpty(Node p){return p.hi==Empty.hi;}
inline bool FullLess(Node a,Node b){return a.hi<b.hi||(a.hi==b.hi&&a.lo<b.lo);}
// Explicit scalar round points prohibit host FMA contraction in the oracle.
inline float Add(float a,float b){volatile float q=a+b;return q;}
inline float Mul(float a,float b){volatile float q=a*b;return q;}
inline float Div(float a,float b){volatile float q=a/b;return q;}
inline Color Scale(float t,Color c){for(float& q:c)q=Mul(t,q);return c;}
inline Color Over(Color a,float t,Color b){for(unsigned i=0;i<3;++i)a[i]=Add(a[i],Mul(t,b[i]));return a;}
// IEEE binary16 nearest-even conversion. DXIL legacyF32ToF16 is followed by
// source truncation to unsigned11/11/10; no RGB gamma transfer is introduced.
inline std::uint16_t FloatToHalf(float x){
    const auto u=std::bit_cast<std::uint32_t>(x),sign=(u>>16)&0x8000u;
    const auto exponent=(u>>23)&255u;auto mantissa=u&0x7fffffu;
    if(exponent==255u)return std::uint16_t(sign|0x7c00u|(mantissa?0x200u:0u));
    int e=int(exponent)-127+15;
    if(e>=31)return std::uint16_t(sign|0x7c00u);
    if(e<=0){if(e< -10)return std::uint16_t(sign);mantissa|=0x800000u;
        const unsigned shift=unsigned(14-e),q=mantissa>>shift,remainder=mantissa&((1u<<shift)-1u),halfway=1u<<(shift-1u);
        return std::uint16_t(sign|q|0u)+std::uint16_t(remainder>halfway||(remainder==halfway&&(q&1u)));}
    auto q=mantissa>>13;const auto remainder=mantissa&0x1fffu;
    if(remainder>0x1000u||(remainder==0x1000u&&(q&1u)))if(++q==0x400u){q=0;if(++e>=31)return std::uint16_t(sign|0x7c00u);}
    return std::uint16_t(sign|(std::uint32_t(e)<<10)|q);
}
inline float HalfToFloat(std::uint16_t x){
    const auto sign=std::uint32_t(x&0x8000u)<<16;unsigned e=(x>>10)&31u,m=x&1023u;
    if(e==0){if(m==0)return std::bit_cast<float>(sign);int shift=0;while((m&1024u)==0){m<<=1;++shift;}
        return std::bit_cast<float>(sign|(std::uint32_t(113-shift)<<23)|((m&1023u)<<13));}
    return std::bit_cast<float>(sign|((e==31u?255u:e+112u)<<23)|(m<<13));
}
inline std::uint32_t RgbPack(Color c){const auto r=std::uint32_t(FloatToHalf(c[0])),g=std::uint32_t(FloatToHalf(c[1])),b=std::uint32_t(FloatToHalf(c[2]));
    return ((r>>4)&0x7ffu)|((g<<7)&0x3ff800u)|((b>>5)<<22);}
inline Color RgbUnpack(std::uint32_t p){return {HalfToFloat(std::uint16_t((p<<4)&0x7ff0u)),
    HalfToFloat(std::uint16_t((p>>7)&0x7ff0u)),HalfToFloat(std::uint16_t((p>>22)<<5))};}
inline std::uint32_t TransmissionByte(float t){return std::uint32_t(Add(Mul(std::sqrt(t),255),.5f));}
inline float Transmission(std::uint32_t p){const float q=Mul(float(p&255u),0.003921568859368563f);return Mul(q,q);}
inline Node Pack(float reverseZ,Color premultiplied,float t){return {RgbPack(premultiplied),
    ((std::bit_cast<std::uint32_t>(reverseZ)<<2)&0xffffff00u)|TransmissionByte(t)};}
inline Resolved Unpack(Node p){return IsEmpty(p)?Resolved{}:Resolved{RgbUnpack(p.lo),Transmission(p.hi),std::bit_cast<float>((p.hi>>2)&0x3fffffc0u)};}
inline Node Merge(Node front,Node back){const auto a=Unpack(front),b=Unpack(back);
    return {RgbPack(Over(a.color,a.transmission,b.color)),(front.hi&0xffffff00u)|TransmissionByte(Mul(a.transmission,b.transmission))};}
inline void InnerInsert(std::span<Node> layers,Node node){
    if(layers.size()!=4&&layers.size()!=8)throw std::invalid_argument("Native inner OIT supports4or8nodes");
    for(auto& slot:layers)if(FullLess(slot,node))std::swap(slot,node);
    if(!IsEmpty(node))layers.back()=Merge(layers.back(),node);
}
// The native global caller supplies a nonempty per-object aggregate. Global
// ties compare only high32, retaining old-before-new for identical high words.
inline void GlobalInsert(std::span<Node,4> layers,Node node){
    for(auto& slot:layers)if(node.hi>slot.hi||IsEmpty(node)){std::swap(slot,node);if(IsEmpty(slot))return;}
    if(!IsEmpty(node))layers.back()=Merge(layers.back(),node);
}
inline Resolved Resolve(std::span<const Node> layers){Resolved result;bool first=true;
    for(const auto node:layers){if(IsEmpty(node))break;const auto q=Unpack(node);if(first){result.reverseZ=q.reverseZ;first=false;}
        result.color=Over(result.color,result.transmission,q.color);result.transmission=Mul(result.transmission,q.transmission);}
    return result;
}
inline void Opacity(Resolved& q,float g){q.color=Scale(g,q.color);q.transmission=std::clamp(Add(Add(q.transmission,1),-g),0.f,1.f);}
inline bool CloseBucket(Resolved& q){if(q.transmission>.01f)return false;q.transmission=0;return true;}
inline Resolved ResolveScaled(std::span<const std::array<Node,4>> layerCorners,float fx,float fy){
    Resolved result;const auto lerp=[](float a,float b,float t){return Add(a,Mul(Add(b,-a),t));};
    for(std::size_t i=0;i<layerCorners.size();++i){std::array<Resolved,4> a;
        for(unsigned k=0;k<4;++k)a[k]=Unpack(layerCorners[i][k]);Resolved q;
        for(unsigned k=0;k<3;++k)q.color[k]=lerp(lerp(a[0].color[k],a[1].color[k],fx),lerp(a[2].color[k],a[3].color[k],fx),fy);
        q.transmission=lerp(lerp(a[0].transmission,a[1].transmission,fx),lerp(a[2].transmission,a[3].transmission,fx),fy);
        if(q.transmission==1.f)break;
        if(i==0)for(const auto& corner:a)result.reverseZ=std::max(result.reverseZ,corner.reverseZ);
        result.color=Over(result.color,result.transmission,q.color);result.transmission=Mul(result.transmission,q.transmission);
    }return result;
}
struct WuSegment {std::array<float,2> p0,p1,width,reverseZ;Color shade0,shade1;};
struct WuDeposit {std::array<int,2> pixel;Color color;float transmission=1,reverseZ=0,progress=0;Node node;};
inline float RoundEven(float x){const float base=std::floor(x),f=Add(x,-base);
    return f<.5f?base:f>.5f?Add(base,1):std::fmod(std::abs(base),2.f)==0.f?base:Add(base,1);}
// This bounded reference accepts the already-projected source inputs. It
// enumerates deposits only for CPU fixtures; production executes the GLSL.
template<class Opaque> std::vector<WuDeposit> Wu(WuSegment s,Opaque opaque){
    const bool steep=std::abs(Add(s.p1[1],-s.p0[1]))>std::abs(Add(s.p1[0],-s.p0[0]));
    float minor0=s.p0[steep?0:1],major0=s.p0[steep?1:0],minor1=s.p1[steep?0:1],major1=s.p1[steep?1:0];
    if(major0>major1){std::swap(minor0,minor1);std::swap(major0,major1);std::swap(s.width[0],s.width[1]);
        std::swap(s.reverseZ[0],s.reverseZ[1]);std::swap(s.shade0,s.shade1);}
    const float delta=Add(major1,-major0),slope=delta!=0?Div(Add(minor1,-minor0),delta):1;
    const float start=RoundEven(major0),end=RoundEven(major1);float y=Add(minor0,Mul(slope,Add(start,-major0)));
    std::vector<WuDeposit> out;
    const auto emit=[&](float m,float minor,float width,float z,Color shade,float t){
        const float low=std::floor(minor),fraction=Add(minor,-low),w=std::clamp(width,0.f,1.f);
        for(unsigned side=0;side<2;++side){const int q=int(low)+int(side),x=steep?q:int(m),yy=steep?int(m):q;
            if(x<0||x>=16||yy<0||yy>=16||!(z<1&&opaque(x,yy)<z))continue;
            const float alpha=Mul(side?fraction:Add(1,-fraction),w);const Color c=Scale(alpha,shade);const float tr=Add(1,-alpha);
            out.push_back({{x,yy},c,tr,z,t,Pack(z,c,tr)});}
    };
    if(start>=-1)emit(start,y,s.width[0],s.reverseZ[0],s.shade0,0);
    y=Add(y,slope);float m=Add(start,1),jump=0;
    if(!(y>=-1||m>=-1))jump=std::ceil(std::min(Div(Add(-1,-y),std::abs(slope)),Add(-2,-start)));
    const float inverse=Div(1,Add(end,-start));float t=jump?Mul(jump,inverse):0;
    y=Add(y,Mul(jump,slope));m=Add(m,jump);
    while(m<std::min(16.f,end)){t=Add(t,inverse);
        const float width=Add(s.width[0],Mul(t,Add(s.width[1],-s.width[0]))),z=Add(s.reverseZ[0],Mul(t,Add(s.reverseZ[1],-s.reverseZ[0])));
        Color c;for(unsigned i=0;i<3;++i)c[i]=Add(s.shade0[i],Mul(t,Add(s.shade1[i],-s.shade0[i])));
        emit(m,y,width,z,c,t);y=Add(y,slope);const float next=Add(m,1);if(next<=m)break;m=next;
    }return out;
}
inline std::vector<WuDeposit> Wu(WuSegment s){return Wu(s,[](int,int){return 0.f;});}
} // namespace Render::PipelineDetail::NativeOit
