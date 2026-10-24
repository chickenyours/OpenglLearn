#pragma once
#include <string>
namespace Render::PipelineDetail {
// Original pixel arithmetic; see Asset/Diana/hair-display-evidence.json.
inline std::string SourceDisplayGLSL(){return R"GLSL(
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
)GLSL";}
}
