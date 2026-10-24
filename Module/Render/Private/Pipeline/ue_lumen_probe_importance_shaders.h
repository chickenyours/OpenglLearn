#pragma once
#include <string>
#include <stdexcept>
#include "screen_probe_trace_shaders.h"
#include "ue_lumen_radiance_cache_shaders.h"

namespace Render::SurfaceCacheDetail {
// UE5.7.1 LumenScreenProbeImportanceSampling.cpp:17-68: enabled=1,
// levels=1, incoming lighting=1, radiance history=1, BRDF resolution=8,
// MinPDFToTrace=.1. TracingCommon.ush:67-97 supplies the public RayInfo ABI.
inline std::string UeLumenProbeImportanceConstantsGLSL(){return R"GLSL(
layout(std140,binding=15) uniform UeProbeImportanceData {
    ivec4 uiTraceInfo;
    mat4 uiHistoryViewProjection;
    ivec4 uiHistoryExtent;
    ivec4 uiHistoryAtlas;
    vec4 uiHistoryJitter;
    ivec4 uiHistoryFlags;
};
)GLSL";}
inline std::string UeLumenProbeImportanceCommonGLSL(){return
    ScreenProbeTraceDetail::ConstantsGLSL()+UeLumenProbeImportanceConstantsGLSL()+R"GLSL(
layout(binding=4) uniform sampler2D uiUniform;
layout(binding=5) uniform sampler2D uiSpawned;
bool UIFinite3(vec3 p){return !any(isnan(p))&&!any(isinf(p));}
vec4 UIProbe(uint probe,int part){bool uniformProbe=probe<uint(spTraceAtlas.z);
    uint slot=uniformProbe?probe:probe-uint(spTraceAtlas.z),columns=uint(spGrid.x)*(uniformProbe?1u:uint(spGrid.z));
    ivec2 at=ivec2(2u*(slot%columns)+uint(part),slot/columns);
    return uniformProbe?texelFetch(uiUniform,at,0):texelFetch(uiSpawned,at,0);}
bool UIActive(uint probe,out vec4 p,out vec4 n){p=vec4(0);n=vec4(0);
    if(probe>=uint(spTraceAtlas.w))return false;p=UIProbe(probe,0);n=UIProbe(probe,1);
    return p.w>=.5&&n.w>0&&UIFinite3(p.xyz)&&UIFinite3(n.xyz)&&dot(n.xyz,n.xyz)>.5;}
ivec2 UIProbePixel(vec3 world){vec4 clip=spViewProjection*vec4(world,1);
    return clamp(ivec2(floor((clip.xy/clip.w*.5+.5)*vec2(spScreen.xy))),ivec2(0),spScreen.xy-1);}
ivec2 UITileJitter(){uint phase=uint(spScreen.z);
    return ivec2(vec2(float(phase)/8.0,float(bitfieldReverse(phase))/4294967296.0)*float(spGrid.w));}
ivec2 UIScreenTile(vec3 world){return ivec2((uvec2(UIProbePixel(world))-uvec2(UITileJitter()))/uint(spGrid.w));}
// SHCommon.ush:232-248,291-315; the supported opaque diffuse material branch.
float UIBasis(int k,vec3 v){if(k==0)return .282095;if(k==1)return -.488603*v.y;
    if(k==2)return .488603*v.z;if(k==3)return -.488603*v.x;if(k==4)return 1.092548*v.x*v.y;
    if(k==5)return -1.092548*v.y*v.z;if(k==6)return .315392*(3*v.z*v.z-1);
    if(k==7)return -1.092548*v.x*v.z;return .546274*(v.x*v.x-v.y*v.y);}
float UIDiffuseTransfer(int k,vec3 normal){return UIBasis(k,normal)*(k==0?3.14159265358979323846:
    k<4?2.0943951023931954923:.7853981633974483096);}
vec3 UIEquiArea(vec2 uv){uv=2.0*uv-1.0;float d=1.0-abs(uv.x)-abs(uv.y),r=1.0-abs(d);
    float phi=r==0.0?0.0:.7853981633974483*((abs(uv.y)-abs(uv.x))/r+1.0),f=r*sqrt(max(0.0,2.0-r*r));
    return vec3(f*sign(uv.x)*abs(cos(phi)),f*sign(uv.y)*abs(sin(phi)),sign(d)*(1.0-r*r));}
uint UIPackRay(ivec2 at,uint level){return (uint(at.x)&63u)|((uint(at.y)&63u)<<6u)|((level&15u)<<12u);}
float UIHalf(float x){return unpackHalf2x16(packHalf2x16(vec2(x,0))).x;}
)GLSL";}

// Each original 8x8 probe workgroup samples the surrounding material footprint,
// accumulates diffuse SH3 with the original plane gate, then reduces by fours.
inline std::string UeLumenProbeBrdfPdfShader(){return std::string("#version 450 core\n")+
    UeLumenProbeImportanceCommonGLSL()+R"GLSL(
layout(local_size_x=8,local_size_y=8) in;
layout(binding=2) uniform sampler2D uiPositions;
layout(binding=3) uniform sampler2D uiNormals;
layout(std430,set=2,binding=0) buffer UIBrdfSH {float uiBrdf[];};
shared float uiSharedSH[1152];
shared uint uiSHCount;
void main(){uint lane=gl_LocalInvocationIndex,probe=gl_WorkGroupID.y*uint(spGrid.x)+gl_WorkGroupID.x;
    vec4 position,normalDepth;bool probeValid=UIActive(probe,position,normalDepth);
    if(!probeValid){if(lane<9u)uiBrdf[probe*9u+lane]=0;return;}
    if(lane==0u)uiSHCount=0u;barrier();
    ivec2 threadAt=ivec2(gl_LocalInvocationID.xy),centerPixel=UIProbePixel(position.xyz);
    vec2 offset=((vec2(threadAt)+.5)/8.0*2.0-1.0)*float(spGrid.w);
    bool center=all(equal(threadAt,ivec2(4)));if(center)offset=vec2(0);
    // UE forms a center UV, offsets in pixels, clamps, then converts to uint2.
    ivec2 pixel=ivec2(clamp(vec2(centerPixel)+.5+offset,vec2(0),vec2(spScreen.xy-1)));
    vec4 source=texelFetch(uiPositions,pixel,0),sourceNormal=texelFetch(uiNormals,pixel,0);
    bool valid=source.z<0&&sourceNormal.w>=.5&&UIFinite3(source.xyz)&&UIFinite3(sourceNormal.xyz)&&dot(sourceNormal.xyz,sourceNormal.xyz)>.5;
    vec3 world=(spInverseView*vec4(source.xyz,1)).xyz,n=normalize(sourceNormal.xyz);
    float plane=abs(dot(position.xyz-world,n)),relative=plane/normalDepth.w;
    if(valid&&(exp2(-10000.0*relative*relative)>.1||center)){
        uint index=atomicAdd(uiSHCount,1u);for(int k=0;k<9;++k)uiSharedSH[index*9u+uint(k)]=UIDiffuseTransfer(k,n);}
    barrier();
    uint count=uiSHCount,offsetSH=0u;
    while(count>1u){uint first=lane*4u;if(first<count){for(uint k=0u;k<9u;++k){float sum=uiSharedSH[(first+offsetSH)*9u+k];
        if(first+1u<count)sum+=uiSharedSH[(first+1u+offsetSH)*9u+k];
        if(first+2u<count)sum+=uiSharedSH[(first+2u+offsetSH)*9u+k];
        if(first+3u<count)sum+=uiSharedSH[(first+3u+offsetSH)*9u+k];
        uiSharedSH[(lane+offsetSH+count)*9u+k]=sum;}}
        offsetSH+=count;count=(count+3u)/4u;barrier();}
    if(lane<9u)uiBrdf[probe*9u+lane]=uiSHCount>0u?UIHalf(uiSharedSH[offsetSH*9u+lane]/float(uiSHCount)):0;
}
)GLSL";}

inline std::string UeLumenProbeLightingPdfShader(){return std::string("#version 450 core\n")+
    UeLumenProbeImportanceCommonGLSL()+UeRadianceCacheReadGLSL()+R"GLSL(
layout(local_size_x=8,local_size_y=8) in;
layout(binding=7) uniform sampler2D uiBlueNoise;
layout(binding=10) uniform sampler2D uiHistoryRadiance;
layout(binding=11) uniform sampler2D uiHistoryUniform;
layout(binding=12) uniform sampler2D uiPreviousWorld;
layout(rgba16f,set=3,binding=0) uniform writeonly image2D uiLightingOutput;
shared float uiSharedPdf[256];
vec2 UIBlueNoise(ivec2 tile){uvec3 p=uvec3(uvec2(tile),uint(uiTraceInfo.z))&uvec3(127,127,63);
    return texelFetch(uiBlueNoise,ivec2(p.x,p.z*128u+p.y),0).xy;}
vec3 UIHistoryRay(ivec2 tile,uint ray){uint probe=uint(tile.y*uiHistoryExtent.z+tile.x),flatIndex=probe*64u+ray;
    return texelFetch(uiHistoryRadiance,ivec2(flatIndex%uint(uiHistoryAtlas.y),flatIndex/uint(uiHistoryAtlas.y)),0).rgb;}
void main(){uint lane=gl_LocalInvocationIndex,probe=gl_WorkGroupID.y*uint(spGrid.x)+gl_WorkGroupID.x;
    ivec2 rayAt=ivec2(gl_LocalInvocationID.xy),outputAt=ivec2(gl_WorkGroupID.xy)*8+rayAt;
    vec4 position,normalDepth;if(!UIActive(probe,position,normalDepth)){imageStore(uiLightingOutput,outputAt,vec4(0));return;}
    vec3 lighting=vec3(0);float transparency=1;
    if(uiHistoryFlags.x!=0){vec3 previousPosition=position.xyz;
        if(uiHistoryFlags.y!=0){vec4 previous=texelFetch(uiPreviousWorld,UIProbePixel(position.xyz),0);
            if(previous.w>0&&UIFinite3(previous.xyz))previousPosition=previous.xyz;}
        vec4 historyClip=uiHistoryViewProjection*vec4(previousPosition,1);
        vec2 historyUV=historyClip.xy/historyClip.w*.5+.5;
        if(historyClip.w>0&&all(greaterThanEqual(historyUV,vec2(0)))&&all(lessThanEqual(historyUV,vec2(1)))){
            vec2 historyCoord=(historyUV*vec2(uiHistoryExtent.xy)-(uiHistoryJitter.xy+.5))/float(uiHistoryAtlas.x);
            float historyWeight=0;vec3 historyRadiance=vec3(0),n=normalize(normalDepth.xyz);
            for(int y=0;y<2;++y)for(int x=0;x<2;++x){ivec2 tile=ivec2(clamp(historyCoord+vec2(x,y),vec2(0),vec2(uiHistoryExtent.zw-1)));
                ivec2 at=ivec2(tile.x*2,tile.y);vec4 oldPosition=texelFetch(uiHistoryUniform,at,0),oldNormal=texelFetch(uiHistoryUniform,at+ivec2(1,0),0);
                if(oldPosition.w>=.5&&oldNormal.w>=0){float relative=abs(dot(oldPosition.xyz-position.xyz,n))/normalDepth.w;
                    float weight=exp2(-10000.0*relative*relative)>.1?1.0:0.0;
                    historyRadiance+=UIHistoryRay(tile,lane)*weight;historyWeight+=weight;}}
            if(historyWeight>0)lighting=historyRadiance/historyWeight;
            transparency=1.0-clamp(historyWeight/4.0,0.0,1.0);
        }
    }
    if(transparency>0){vec2 uv=(vec2(rayAt)+UIBlueNoise(UIScreenTile(position.xyz)))/8.0;
        vec3 direction=UREquiArea(uv);int level;ivec3 first;float cutoff;
        if(URCoverage(position.xyz,level,first,cutoff))lighting+=URSample(position.xyz,direction,level,first).rgb*transparency;
        else lighting=vec3(1); // UE's formal no-history / no-RC-coverage fallback.
    }
    float pdf=UIFinite3(lighting)?dot(lighting,vec3(.2126390059,.7151686788,.0721923154)):0;
    uiSharedPdf[lane]=pdf;barrier();uint count=64u,offset=0u;
    while(count>1u){uint first=lane*4u;if(first<count){float sum=uiSharedPdf[first+offset];
        if(first+1u<count)sum+=uiSharedPdf[first+1u+offset];if(first+2u<count)sum+=uiSharedPdf[first+2u+offset];
        if(first+3u<count)sum+=uiSharedPdf[first+3u+offset];uiSharedPdf[lane+offset+count]=sum;}
        offset+=count;count=(count+3u)/4u;barrier();}
    imageStore(uiLightingOutput,outputAt,vec4(pdf/max(uiSharedPdf[offset],.0001),0,0,0));
}
)GLSL";}

inline std::string UeLumenProbeGenerateRaysShader(){return std::string("#version 450 core\n")+
    UeLumenProbeImportanceCommonGLSL()+R"GLSL(
layout(local_size_x=8,local_size_y=8) in;
layout(binding=0) uniform sampler2D uiLightingPdf;
layout(std430,set=2,binding=0) readonly buffer UIBrdfSH {float uiBrdf[];};
layout(rgba32f,set=3,binding=0) uniform writeonly image2D uiRayOutput;
shared uvec2 uiRays[128];
shared uint uiSubdivideCount;
uvec2 UISortRay(ivec2 at,uint level,float pdf){return uvec2((uint(at.x)&255u)|((uint(at.y)&255u)<<8u)|((level&255u)<<16u),floatBitsToUint(pdf));}
void UIUnpackSort(uvec2 info,out ivec2 at,out uint level,out float pdf){at=ivec2(info.x&255u,(info.x>>8u)&255u);level=(info.x>>16u)&255u;pdf=uintBitsToFloat(info.y);}
void main(){uint lane=gl_LocalInvocationIndex,probe=gl_WorkGroupID.y*uint(spGrid.x)+gl_WorkGroupID.x;
    ivec2 rayAt=ivec2(gl_LocalInvocationID.xy),outputAt=ivec2(gl_WorkGroupID.xy)*8+rayAt;
    imageStore(uiRayOutput,outputAt,vec4(uintBitsToFloat(UIPackRay(rayAt,1u)),0,0,0));
    vec4 position,normalDepth;if(!UIActive(probe,position,normalDepth))return;
    vec3 direction=UIEquiArea((vec2(rayAt)+.5)/8.0);float brdf=0;
    for(int k=0;k<9;++k)brdf+=uiBrdf[probe*9u+uint(k)]*UIBasis(k,direction);
    brdf=max(brdf,0);float pdf=brdf*texelFetch(uiLightingPdf,outputAt,0).x*64.0;
    if(brdf>=.1)pdf=max(pdf,.1);
    uiRays[lane]=UISortRay(rayAt,1u,pdf);barrier();
    uint smaller=0;for(uint other=0u;other<64u;++other){float key=uintBitsToFloat(uiRays[other].y);
        if(key<pdf||(key==pdf&&other>lane))++smaller;}
    uiRays[64u+smaller]=uiRays[lane];if(lane==0u)uiSubdivideCount=0u;barrier();
    uint mergeLane=lane%3u,mergeIndex=lane/3u,toRefine=63u-mergeIndex,third=mergeIndex*3u+2u;
    if(third<toRefine&&uintBitsToFloat(uiRays[64u+third].y)<.1){ivec2 source;uint level;float original;
        UIUnpackSort(uiRays[64u+toRefine],source,level,original);
        uiRays[64u+lane]=UISortRay(source*2+ivec2((mergeLane+1u)%2u,(mergeLane+1u)/2u),level-1u,0);
        if(mergeLane==0u)atomicAdd(uiSubdivideCount,1u);}
    barrier();
    if(lane<uiSubdivideCount){uint index=63u-lane;ivec2 source;uint level;float original;
        UIUnpackSort(uiRays[64u+index],source,level,original);uiRays[64u+index]=UISortRay(source*2,level-1u,0);}
    barrier();ivec2 source;uint level;float pdfUnused;UIUnpackSort(uiRays[64u+lane],source,level,pdfUnused);
    imageStore(uiRayOutput,outputAt,vec4(uintBitsToFloat(UIPackRay(source,level)),0,0,0));
}
)GLSL";}
} // namespace Render::SurfaceCacheDetail
