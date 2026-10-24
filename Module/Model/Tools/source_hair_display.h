#pragma once
// Offline research/reference helpers, independent of Render/RHI.
// Source: PRAGMATA tonemap.PostTonemap3Section pixel DXIL %139..238,
// advancedsystem.ColorCorrectWithUIMask %45..131, and ScreenOutputSRGB.
// The coefficient construction also follows the native writer at
// 0x140391448..0x140391517. Original runtime OCIO/RGC/whitepoint/variant
// selection are NOT defaults here; MakeSectionStudy requires explicit inputs.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <string>

namespace Model::EditorPreview::SourceHairDisplay {
using RGB = std::array<float, 3>;

struct SectionParameters {
    // Exact source CB bytes 0..47, three vec4 groups, in declaration order.
    std::array<float, 4> curve0; // contrast, linearBegin, linearLength, toe
    std::array<float, 4> curve1; // maxNit, linearStart, maxNit-S1, contrastFactor
    std::array<float, 4> curve2; // -linearStart*contrastFactor, 1/linearBegin,
                               // linearBegin*(1-contrast), isHDRMode
};

inline SectionParameters MakeSectionStudy(float contrast, float linearBegin,
        float linearLength, float toe, float maxNit) {
    if (!std::isfinite(contrast) || !std::isfinite(linearBegin) ||
        !std::isfinite(linearLength) || !std::isfinite(toe) || !std::isfinite(maxNit) ||
        contrast <= 0.f || linearBegin <= 0.f || maxNit <= linearBegin ||
        linearLength < 0.f || linearLength >= 1.f || toe <= 0.f)
        throw std::invalid_argument("Invalid source three-section study parameters");
    const float l0 = (maxNit-linearBegin)*linearLength/contrast;
    const float linearStart = linearBegin+l0;
    const float s1 = linearBegin+contrast*l0;
    // DXIL Exp is exp2; the coefficient must include log2(e).
    const float c2 = contrast*maxNit/(maxNit-s1);
    const float factor = (1.f/maxNit)*c2*(-1.4426950408889634f);
    return {{contrast,linearBegin,linearLength,toe},
        {maxNit,linearStart,maxNit-s1,factor},
        {-linearStart*factor,1.f/linearBegin,linearBegin*(1.f-contrast),0.f}};
}

inline float ThreeSection(float x, const SectionParameters& p) {
    if (p.curve2[3] != 0.f) return x; // This inspected SDR 3Section variant bypasses in HDR.
    const float m=p.curve0[1], u=x*p.curve2[1];
    const float toeBlend=x<m ? u*u*(3.f-2.f*u) : 1.f;
    const float shoulder=x<p.curve1[1] ? 0.f : 1.f;
    const float t=m*std::pow(u,p.curve0[3]);
    const float l=p.curve0[0]*x+p.curve2[2];
    const float s=p.curve1[0]-p.curve1[2]*std::exp2(p.curve1[3]*x+p.curve2[0]);
    return t*(1.f-toeBlend)+l*(toeBlend-shoulder)+s*shoulder;
}

inline float EncodeCct(float x) {
    return x>.0078125f ? (std::log2(x)+9.720000267028809f)*.05707762390375137f
        : x*10.540237426757812f+.072905533015728f;
}
inline float DecodeCct(float x) {
    if(x<.155251145362854f)return (x-.072905533015728f)*.09487452358007431f;
    if(x<1.4679962396621704f)return std::exp2(x*17.520000457763672f-9.720000267028809f);
    return 65504.f;
}
inline float SrgbOutput(float x,float displayGamma,float convertToLimit,float lowerLimit) {
    const float encoded=x>.0031308000907301903f
        ? 1.0549999475479126f*std::pow(x,.4166666567325592f)-.054999999701976776f
        : x*12.920000076293945f;
    return std::pow(encoded,displayGamma)*convertToLimit+lowerLimit;
}

// Interleaved RGBA, exact original order x-fastest, then y, then z; no gamma.
inline RGB SampleCube(const float* rgba,std::size_t count,unsigned size,RGB encoded) {
    if(!rgba || size<2 || count!=std::size_t(size)*size*size*4)
        throw std::invalid_argument("Invalid source 3D color table");
    unsigned lo[3],hi[3];float f[3];
    for(unsigned i=0;i<3;++i){const float q=std::clamp(encoded[i],0.f,1.f)*float(size-1);
        lo[i]=unsigned(std::floor(q));hi[i]=std::min(lo[i]+1,size-1);f[i]=q-float(lo[i]);}
    RGB out{};
    for(unsigned z=0;z<2;++z)for(unsigned y=0;y<2;++y)for(unsigned x=0;x<2;++x){
        const float w=(x?f[0]:1.f-f[0])*(y?f[1]:1.f-f[1])*(z?f[2]:1.f-f[2]);
        const std::size_t at=4*((std::size_t(z?hi[2]:lo[2])*size+(y?hi[1]:lo[1]))*size+(x?hi[0]:lo[0]));
        for(unsigned c=0;c<3;++c)out[c]+=rgba[at+c]*w;
    }
    return out;
}
inline RGB ApplyCube(RGB linear,const float* rgba,std::size_t count,unsigned size) {
    RGB encoded;for(unsigned c=0;c<3;++c)encoded[c]=EncodeCct(linear[c]);
    RGB result=SampleCube(rgba,count,size,encoded);
    for(auto& c:result)c=std::max(0.f,DecodeCct(c));
    return result;
}

inline std::string GLSL() { return R"GLSL(
float SourceDisplayThreeSection1(float x,vec4 p0,vec4 p1,vec4 p2) {
    if(p2.w!=0.0)return x;
    float u=x*p2.y;
    float toeBlend=x<p0.y?u*u*(3.0-2.0*u):1.0;
    float shoulder=x<p1.y?0.0:1.0;
    float t=p0.y*pow(u,p0.w);
    float l=p0.x*x+p2.z;
    float s=p1.x-p1.z*exp2(p1.w*x+p2.x);
    return t*(1.0-toeBlend)+l*(toeBlend-shoulder)+s*shoulder;
}
vec3 SourceDisplayThreeSection(vec3 x,vec4 p0,vec4 p1,vec4 p2) {
    return vec3(SourceDisplayThreeSection1(x.r,p0,p1,p2),
        SourceDisplayThreeSection1(x.g,p0,p1,p2),SourceDisplayThreeSection1(x.b,p0,p1,p2));
}
float SourceDisplayEncodeCct1(float x) {
    return x>0.0078125?(log2(x)+9.720000267028809)*0.05707762390375137
        :x*10.540237426757812+0.072905533015728;
}
float SourceDisplayDecodeCct1(float x) {
    if(x<0.155251145362854)return (x-0.072905533015728)*0.09487452358007431;
    if(x<1.4679962396621704)return exp2(x*17.520000457763672-9.720000267028809);
    return 65504.0;
}
vec3 SourceDisplayEncodeCct(vec3 x) {
    return vec3(SourceDisplayEncodeCct1(x.r),SourceDisplayEncodeCct1(x.g),SourceDisplayEncodeCct1(x.b));
}
vec3 SourceDisplayDecodeCct(vec3 x) {
    return max(vec3(SourceDisplayDecodeCct1(x.r),SourceDisplayDecodeCct1(x.g),SourceDisplayDecodeCct1(x.b)),vec3(0.0));
}
// DHSLUT01 2D transport: width=N, height=N*N, original z slices stacked in y.
// Explicit eight-texel interpolation prevents filtering across slice seams.
vec3 SourceDisplaySampleCube(sampler2D flatTable,vec3 encoded,int size) {
    vec3 q=clamp(encoded,vec3(0.0),vec3(1.0))*float(size-1);
    ivec3 lo=ivec3(floor(q)),hi=min(lo+ivec3(1),ivec3(size-1));vec3 f=fract(q);
    vec3 result=vec3(0.0);
    for(int z=0;z<2;++z)for(int y=0;y<2;++y)for(int x=0;x<2;++x){
        ivec3 i=ivec3(x!=0?hi.x:lo.x,y!=0?hi.y:lo.y,z!=0?hi.z:lo.z);
        float weight=(x!=0?f.x:1.0-f.x)*(y!=0?f.y:1.0-f.y)*(z!=0?f.z:1.0-f.z);
        result+=texelFetch(flatTable,ivec2(i.x,i.y+i.z*size),0).rgb*weight;
    }
    return result;
}
vec3 SourceDisplayApplyCube(sampler2D flatTable,vec3 linear,int size) {
    return SourceDisplayDecodeCct(SourceDisplaySampleCube(flatTable,SourceDisplayEncodeCct(linear),size));
}
float SourceDisplaySrgb1(float x) {
    return x>0.0031308000907301903?1.0549999475479126*pow(x,0.4166666567325592)-0.054999999701976776
        :x*12.920000076293945;
}
vec3 SourceDisplaySrgb(vec3 linear,float displayGamma,float convertToLimit,float lowerLimit) {
    vec3 encoded=vec3(SourceDisplaySrgb1(linear.r),SourceDisplaySrgb1(linear.g),SourceDisplaySrgb1(linear.b));
    return pow(encoded,vec3(displayGamma))*convertToLimit+lowerLimit;
}
)GLSL"; }
} // namespace Model::EditorPreview::SourceHairDisplay
