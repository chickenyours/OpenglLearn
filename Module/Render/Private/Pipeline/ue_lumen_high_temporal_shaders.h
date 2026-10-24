#pragma once
#include <string>
#include "ue_lumen_high_short_range_ao_shaders.h"
namespace Render::SurfaceCacheDetail {
// Reconstruct the expectation of UE's four geometry-weighted samples.
// This display chain has no final TSR/TAA; filter irradiance before albedo/AO
// instead of presenting a single stochastic corner. No extra traced samples.
inline std::string UeLumenHighCurrentReconstructionGLSL(){return R"GLSL(
bool UHTReadCurrent(sampler2D rawTexture,sampler2D movingTexture,ivec2 pixel,
    out vec4 raw,out float moving){
    raw=vec4(0);moving=0.0;ivec2 base;vec4 weights;
    if(!UHReconstructionWeights(pixel,base,weights))return false;
    const ivec2 offsets[4]=ivec2[4](ivec2(0),ivec2(1,0),ivec2(0,1),ivec2(1));
    float mass=0.0;
    for(int i=0;i<4;++i){
        if(weights[i]<=0.0)continue;
        ivec2 q=clamp(base+offsets[i],ivec2(0),uhExtent.zw-1);
        vec4 value=texelFetch(rawTexture,q,0);
        if(!(value.a>0.0)||any(isnan(value))||any(isinf(value)))continue;
        float speed=texelFetch(movingTexture,q,0).r;
        if(!UHFinite(speed))speed=0.0;
        raw+=value*weights[i];moving+=max(speed,0.0)*weights[i];mass+=weights[i];
    }
    if(mass<.0001)return false;
    raw/=mass;moving/=mass;return true;
}
)GLSL";}

// UE 5.7.1: StochasticLightingTileClassification.usf 823-867 and
// LumenScreenProbeGatherTemporal.usf 583-619. GI remains unoccluded here.
// Normal rejection is UE's existing documented character-foot quality option.
inline std::string UeLumenHighTemporalShader() {
    return std::string("#version 450 core\n")+UeLumenHighSamplingGLSL()+UeLumenHighCurrentReconstructionGLSL()+UeLumenHighTemporalMetadataGLSL()+R"GLSL(
layout(binding=0) uniform sampler2D uhtRawGi;
layout(binding=1) uniform sampler2D uhtHistoryGi;
layout(binding=4) uniform sampler2D uhtHistoryPositions;
layout(binding=5) uniform sampler2D uhtHistoryNormals;
layout(binding=8) uniform sampler2D uhtHistoryAux;
layout(binding=9) uniform sampler2D uhtLightMoving;
layout(binding=10) uniform sampler2D uhtObjectPreviousPositions;
layout(std140,binding=1) uniform UeObjectMotion {mat4 uhtPreviousNormals[64];};
layout(std140,binding=8) uniform UeHighTemporalData {
    mat4 uhtPreviousViewProjection,uhtPreviousView;
    ivec4 uhtFlags; // history valid, object mapping valid, normal reject, unused
    vec4 uhtParameters; // .01 distance, 10 frames, 10 inverse moving fraction, .9 max fast
};
layout(location=0) out vec4 outGi;
layout(location=1) out vec4 outReprojection;
layout(location=2) out vec4 outPosition;
layout(location=3) out vec4 outNormal;
bool UHTFinite(vec3 p){return !any(isnan(p))&&!any(isinf(p));}
void main() {
    ivec2 pixel=ivec2(gl_FragCoord.xy);
    outGi=vec4(0);outReprojection=vec4(0,0,1,0);
    outPosition=vec4(0);outNormal=vec4(0);
    vec4 view=texelFetch(uhPositions,pixel,0),normal=texelFetch(uhNormals,pixel,0);
    if(view.w<.5||normal.w<.5||view.z>=0||!UHTFinite(view.xyz)||
       !UHTFinite(normal.xyz)||dot(normal.xyz,normal.xyz)<.5)return;
    vec4 raw;float lightMoving;
    if(!UHTReadCurrent(uhtRawGi,uhtLightMoving,pixel,raw,lightMoving))return;
    if(!(raw.a>0)||!UHTFinite(raw.rgb)||isnan(raw.a)||isinf(raw.a))return;
    vec3 world=(uhInverseView*vec4(view.xyz,1)).xyz,n=normalize(normal.xyz);
    if(!UHTFinite(world))return;
    outPosition=vec4(world,view.w);outNormal=vec4(n,-view.z);
    outGi=vec4(max(raw.rgb,vec3(0)),0);
    // Same current-only auxiliary guard as the removed pass, before any
    // history clip/depth/mass early return. Cold history encodes fast0.
    float fast=0.0;
    if(uhtFlags.x!=0&&!isnan(lightMoving)&&!isinf(lightMoving)){
        fast=clamp(lightMoving*uhtParameters.z,0.0,1.0);
        fast=clamp(min((fast-.2)/.8,uhtParameters.w),0.0,1.0);
    }
    outReprojection.w=UHPackMetadata(0u,true,fast,raw.a);
    if(uhtFlags.x==0)return;
    vec3 expected=world,expectedNormal=n;
    if(uhtFlags.y!=0) {
        vec4 previous=texelFetch(uhtObjectPreviousPositions,pixel,0);
        if(previous.w>=1&&previous.w<=64&&UHTFinite(previous.xyz)) {
            int owner=int(previous.w+.5)-1;
            if(uhtPreviousNormals[owner][3].w>1.5) {
                vec3 pn=mat3(uhtPreviousNormals[owner])*n;
                if(UHTFinite(pn)&&dot(pn,pn)>1e-12){expected=previous.xyz;expectedNormal=normalize(pn);}
            }
        }
    }
    vec4 clip=uhtPreviousViewProjection*vec4(expected,1);
    float depth=-(uhtPreviousView*vec4(expected,1)).z;
    if(!UHTFinite(clip.xyz)||!UHFinite(clip.w)||!UHFinite(depth)||clip.w<=0||depth<=0)return;
    vec3 ndc=clip.xyz/clip.w;
    if(any(lessThan(ndc,vec3(-1)))||any(greaterThan(ndc,vec3(1))))return;
    vec2 historyPixel=(ndc.xy*.5+.5)*vec2(uhExtent.xy)-.5;
    ivec2 first=ivec2(floor(historyPixel));vec2 f=fract(historyPixel);
    float tolerance=depth*uhtParameters.x*mix(.5,1.5,UHBlueNoiseScalar(pixel));
    vec3 sum=vec3(0);float mass=0,count=0,oldFast=0;int validMask=0;
    for(int y=0;y<2;++y)for(int x=0;x<2;++x) {
        ivec2 at=first+ivec2(x,y);
        if(any(lessThan(at,ivec2(0)))||any(greaterThanEqual(at,uhExtent.xy)))continue;
        float weight=(x==0?1-f.x:f.x)*(y==0?1-f.y:f.y);
        if(weight<=0)continue;
        vec4 p=texelFetch(uhtHistoryPositions,at,0),pn=texelFetch(uhtHistoryNormals,at,0);
        vec4 h=texelFetch(uhtHistoryGi,at,0),a=texelFetch(uhtHistoryAux,at,0);
        if(p.w<.5||p.w!=view.w||!UHMetadataReadable(a.w)||!UHTFinite(p.xyz)||!UHTFinite(pn.xyz)||!UHFinite(pn.w)||
           !UHTFinite(h.rgb)||
           isnan(h.a)||isinf(h.a)||h.a<0||abs(pn.w-depth)>=tolerance)continue;
        if(uhtFlags.z!=0&&dot(expectedNormal,pn.xyz)<.70710678118)continue;
        validMask|=1<<(x+2*y);sum+=h.rgb*weight;mass+=weight;
        count+=(h.a+1)*weight;oldFast+=UHMetadataFast(a.w)*weight;
    }
    if(mass<.0001)return;
    if(isnan(lightMoving)||isinf(lightMoving))lightMoving=0;
    float previousFast=min(oldFast/mass,uhtParameters.w);
    float activeFast=max(fast,previousFast);
    float frames=min(min(count/mass,uhtParameters.y),(1-activeFast)*uhtParameters.y);
    float alpha=1.0/(1.0+frames);
    // UE GatherTemporal.usf:622-624: low-weight fallback RGB is not a
    // fresh valid lighting sample. LightIsMoving=0 carries that validity bit.
    if(lightMoving<=0.0&&frames>=1.0)alpha=1.0/(1.0+4.0*frames);
    outGi=vec4(mix(max(sum/mass,vec3(0)),max(raw.rgb,vec3(0)),alpha),frames);
    outReprojection=vec4(historyPixel,alpha,UHPackMetadata(uint(validMask),true,fast,raw.a));
}
)GLSL";
}

inline std::string UeLumenHighCountShader() {
    return std::string("#version 450 core\n")+UeLumenHighTemporalMetadataGLSL()+R"GLSL(
layout(binding=0) uniform sampler2D uhtCountHistory;
layout(binding=1) uniform sampler2D uhtCountAux;
layout(location=0) out vec4 outColor;
void main(){ivec2 p=ivec2(gl_FragCoord.xy);
    if(!UHMetadataReadable(texelFetch(uhtCountAux,p,0).w)){outColor=vec4(0,0,0,1);return;}
    float c=clamp(texelFetch(uhtCountHistory,p,0).a/10.0,0.0,1.0);
    outColor=vec4(c,c,c,1);
}
)GLSL";
}

} // namespace Render::SurfaceCacheDetail
