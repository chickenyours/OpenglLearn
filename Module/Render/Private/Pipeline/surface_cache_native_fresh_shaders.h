#pragma once
#include "surface_cache_transport_deferred_shaders.h"
#include "surface_cache_exposure_shaders.h"
#include "surface_cache_native_contact_mask_shaders.h"

namespace Render::SurfaceCacheDetail {
inline void NativeFreshReplaceAll(std::string& source,const std::string& from,const std::string& to) {
    std::size_t at=0;while((at=source.find(from,at))!=std::string::npos){source.replace(at,from.size(),to);at+=to.size();}
}
// Two original fragment bodies run for every invocation, including all their
// function-local early returns. Exposure derivatives therefore retain complete
// quads. Only declarations/entry points change; no exposure predicate changes.
inline std::string NativeFreshContactMaskShader(bool expand,bool objectMotion) {
    auto mask=expand?ContactMaskExpandShader():NativeContactMaskShader();
    TransportDeferredReplace(mask,"in vec2 vUV;layout(location=0) out vec4 outColor;","vec4 nfMaskValue;");
    NativeFreshReplaceAll(mask,"outColor","nfMaskValue");
    TransportDeferredReplace(mask,"void main() {","void NFMaskMain() {");
    auto fresh=ExposureSamplingShader(objectMotion);
    TransportDeferredReplace(fresh,"#version 450 core\n","");
    TransportDeferredReplace(fresh,"in vec2 vUV;layout(location=0) out vec4 outColor;","vec4 nfFreshValue;");
    NativeFreshReplaceAll(fresh,"outColor","nfFreshValue");
    TransportDeferredReplace(fresh,"void main() {","void NFFreshMain() {");
    TransportDeferredReplace(fresh,"layout(std140,binding=14) uniform ExposureSamplingData","layout(std140,binding=11) uniform ExposureSamplingData");
    TransportDeferredReplace(fresh,"layout(binding=0) uniform sampler2D seViewPositions;","layout(binding=2) uniform sampler2D seViewPositions;");
    TransportDeferredReplace(fresh,"layout(binding=1) uniform sampler2D seWorldNormals;","layout(binding=3) uniform sampler2D seWorldNormals;");
    TransportDeferredReplace(fresh,"layout(binding=2) uniform sampler2D seCurrentAlbedo;","layout(binding=4) uniform sampler2D seCurrentAlbedo;");
    TransportDeferredReplace(fresh,"layout(binding=3) uniform sampler2D seHistoryPositions;","layout(binding=12) uniform sampler2D seHistoryPositions;");
    TransportDeferredReplace(fresh,"layout(binding=4) uniform sampler2D seHistoryNormals;","layout(binding=13) uniform sampler2D seHistoryNormals;");
    TransportDeferredReplace(fresh,"layout(binding=6) uniform sampler2D seObjectPreviousPositions;","layout(binding=14) uniform sampler2D seObjectPreviousPositions;");
    return mask+fresh+R"GLSL(
layout(location=0) out vec4 outColor;
layout(location=1) out vec4 nfFreshOutput;
void main(){NFMaskMain();NFFreshMain();outColor=nfMaskValue;nfFreshOutput=nfFreshValue;}
)GLSL";
}

inline std::string NativeReceiverFreshRiskGLSL(unsigned binding=11){
    return "layout(binding="+std::to_string(binding)+") uniform sampler2D nbFreshSampling;\n"+R"GLSL(
int NBMovingBudgetTag(vec4 fresh){
    if(isnan(fresh.z)||isinf(fresh.z)||fresh.z<0.0||fresh.z>31.0)return 0;
    int tag=int(floor(fresh.z+.5)),multiplier=(tag&15)>>1;
    return fresh.z==float(tag)&&(multiplier==2||multiplier==4)?tag:0;
}
float NBEffectiveMask(float mask,ivec2 pixel){
    if(wgReceiverOptions.w<2.5)return mask;
    vec4 fresh=texelFetch(nbFreshSampling,pixel,0);
    if(isnan(fresh.x)||isinf(fresh.x))return mask;
    float effective=max(mask,clamp(fresh.x,0.0,1.0));
    return (NBMovingBudgetTag(fresh)&16)!=0?max(effective,1.0):effective;
}
)GLSL";}
inline std::string NativeReceiverFreshGLSL(bool forceRasterCore=false){auto source=std::string(R"GLSL(
int NBReceiverRayCount(ivec2 pixel) {
    int base=int(wgParameters.z+.5);
    if(wgReceiverOptions.w<2.5)return base;
    float originalMask=texelFetch(nbSoftMask,pixel,0).x;
    vec4 fresh=texelFetch(nbFreshSampling,pixel,0);
    int batches=1;
    if(fresh.x>.000001&&!any(isnan(fresh.xy))&&!any(isinf(fresh.xy)))
        batches=max(batches,int(clamp(floor(fresh.y/float(base)+.5),1.0,4.0)));
    int tag=NBMovingBudgetTag(fresh),moving=(tag&15)>>1;
    if(moving>1&&(originalMask>=.25||(tag&16)!=0))batches=max(batches,moving);
    return base*clamp(batches,1,4);
}
vec3 NBReceiverDirection(vec3 n,int logicalRay,int base,uint seed,uint phase) {
    int batch=logicalRay/base,ray=logicalRay%base;
    uint batchSeed=batch==0?seed:WGHash(seed^WGHash(uint(batch)+0x3c6ef372u));
    return WGDirection(n,ray,base,batchSeed,phase);
}
)GLSL");
    return source;
}
} // namespace Render::SurfaceCacheDetail
