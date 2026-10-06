#pragma once
#include <string>
namespace Render {
// The production PBR shader aliases this sampler to the existing GI slot,
// keeping its fragment sampler count and material texture slots unchanged.
inline std::string RealtimeGiQueryGLSL() {
    return R"GLSL(
#ifndef REALTIME_GI_CACHE
layout(binding=15) uniform sampler2D realtimeGiCache;
#define REALTIME_GI_CACHE realtimeGiCache
#endif
layout(std140,binding=15) uniform RealtimeGiData {
    vec4 rtOrigin,rtSpacing,rtCounts,rtTrace;
    ivec4 rtOptions,rtGeometry;
    vec4 rtSunDirectionIntensity,rtSunColor,rtRuntime;
    vec4 rtSpotPositionRadius,rtSpotDirectionOuter,rtSpotIntensityInner;
};
void RtBasis(vec3 n,out float b[9]) {
    b[0]=.282095; b[1]=.488603*n.y; b[2]=.488603*n.z; b[3]=.488603*n.x;
    b[4]=1.092548*n.x*n.y; b[5]=1.092548*n.y*n.z; b[6]=.315392*(3*n.z*n.z-1);
    b[7]=1.092548*n.x*n.z; b[8]=.546274*(n.x*n.x-n.y*n.y);
}
vec2 RtOct(vec3 d) {
    d/=max(abs(d.x)+abs(d.y)+abs(d.z),1e-8); vec2 p=d.xy;
    if(d.z<0) p=(1.0-abs(p.yx))*mix(vec2(-1),vec2(1),greaterThanEqual(p,vec2(0)));
    return p*.5+.5;
}
vec3 RtDecodeOct(vec2 uv) {
    vec2 p=uv*2-1; vec3 d=vec3(p,1-abs(p.x)-abs(p.y));
    if(d.z<0) d.xy=(1-abs(p.yx))*mix(vec2(-1),vec2(1),greaterThanEqual(p,vec2(0)));
    return normalize(d);
}
vec3 RtPosition(int index) {
    ivec3 count=ivec3(rtCounts.xyz);
    return rtOrigin.xyz+rtSpacing.xyz*vec3(index%count.x,(index/count.x)%count.y,index/(count.x*count.y));
}
vec4 RtCache(int index,int row) { return texelFetch(REALTIME_GI_CACHE,ivec2(index,row),0); }
float RtVisibility(int index,vec3 direction,float distance) {
    vec2 p=RtOct(direction)*4-.5,phase=fract(p); ivec2 lo=ivec2(floor(p)); vec2 moments=vec2(0);
    for(int y=0;y<2;++y) for(int x=0;x<2;++x) {
        ivec2 c=clamp(lo+ivec2(x,y),ivec2(0),ivec2(3));
        float weight=(x==0?1-phase.x:phase.x)*(y==0?1-phase.y:phase.y);
        moments+=RtCache(index,9+c.y*4+c.x).xy*weight;
    }
    if(distance<=moments.x+rtRuntime.z) return 1;
    float variance=max(moments.y-moments.x*moments.x,.00001);
    float delta=distance-moments.x-rtRuntime.z;
    float probability=variance/(variance+delta*delta);
    return probability*probability*probability;
}
float EvaluateRealtimeGi(vec3 position,vec3 N,vec3 R,float roughness,out vec3 irradiance,out float skyVisibility) {
    irradiance=vec3(0); skyVisibility=1;
    vec3 coord=(position-rtOrigin.xyz)/rtSpacing.xyz;
    vec3 outside=max(max(-.5-coord,coord-(rtCounts.xyz-.5)),vec3(0));
    float coverage=1-clamp(max(outside.x,max(outside.y,outside.z))*4,0,1);
    if(coverage<=0) return 0;
    vec3 grid=clamp((position+N*rtRuntime.y-rtOrigin.xyz)/rtSpacing.xyz,vec3(0),rtCounts.xyz-1);
    ivec3 base=min(ivec3(floor(grid)),ivec3(rtCounts.xyz)-2); vec3 phase=grid-vec3(base);
    float b[9],r[9]; RtBasis(N,b); RtBasis(R,r); float sum=0,sky=0;
    for(int z=0;z<2;++z) for(int y=0;y<2;++y) for(int x=0;x<2;++x) {
        vec3 f=mix(1-phase,phase,vec3(x,y,z));
        float trilinear=f.x*f.y*f.z;
        // Clamping to a volume face, edge or corner makes some neighbors
        // exactly irrelevant. Avoid their classification, visibility and SH
        // reads without discarding any small nonzero lighting contribution.
        if(trilinear==0.0) continue;
        ivec3 c=base+ivec3(x,y,z); int index=c.x+int(rtCounts.x)*(c.y+int(rtCounts.y)*c.z);
        if(RtCache(index,25).x<.5) continue;
        vec3 delta=position+N*rtRuntime.y-RtPosition(index); float distance=length(delta);
        vec3 direction=distance>1e-6?delta/distance:N;
        float w=trilinear*RtVisibility(index,direction,distance)*pow(max(.05,.5+.5*dot(N,-direction)),2);
        vec3 E=vec3(0); float v=0;
        for(int i=0;i<9;++i) {
            vec4 sh=RtCache(index,i); E+=sh.rgb*b[i];
            float convolution=i==0?1:(i<4?2.0/3.0:.25);
            v+=sh.a*r[i]*mix(1.0,convolution,roughness*roughness);
        }
        irradiance+=max(E,vec3(0))*w; sky+=clamp(v,0,1)*w; sum+=w;
    }
    if(sum>1e-8) { irradiance/=sum; skyVisibility=sky/sum; } else skyVisibility=0;
    return coverage;
}
)GLSL";
}
} // namespace Render
