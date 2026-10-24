#pragma once
#include <string>
#include "surface_cache_spatial_shaders.h"

namespace Render::SurfaceCacheDetail {
// Optional low-frequency alternative to SpatialFilterShader. This is not the
// same estimator as integrating the old per-ray hit/parallax-guided filter.
// Keep that filter available for the strict reference and risky support.
//
// Input/output use ProbeDirectionalIntegrateShader's four horizontal RGBA
// texels per logical probe: (E/pi, readable/full N), Bx, By, Bz. The RGB L1
// coefficients are in world coordinates. All alphas are immutable center
// metadata; in particular Bx.a remains the center's raw geometry-hit support.
// Use the existing 16-entry SpatialNeighborsShader table, including its
// geometry/visibility decisions. Every accepted neighbor has the
// same geometric weight for E and all three coefficients. Readability never
// weights or normalizes the lighting, so partial unknown budgets keep their
// original full-ray denominator and known black keeps its spatial mass.
inline std::string ProbeCoefficientSpatialFilterShader() {
    return std::string("#version 450 core\n")+ScreenProbeTraceDetail::ConstantsGLSL()+
        SpatialConstantsGLSL()+SpatialProbeGLSL()+R"GLSL(
in vec2 vUV;layout(location=0) out vec4 outColor;
layout(binding=0) uniform sampler2D pcInput;
layout(binding=5) uniform sampler2D sfNeighbors;
layout(binding=10) uniform sampler2D sfSafeOrigins;
bool PCFinite(vec4 value) {
    return SFFinite(value.rgb)&&!isnan(value.a)&&!isinf(value.a);
}
vec4 PCCoefficient(int probe,int part) {
    return texelFetch(pcInput,ivec2((probe%spGrid.x)*4+part,probe/spGrid.x),0);
}
bool PCSourceValid(int probe,vec4 position,vec4 normal) {
    if(probe<0||probe>=spTraceAtlas.w||!SFProbeValid(position,normal))return false;
    if(sfOptions.w==0)return true;
    vec4 safe=texelFetch(sfSafeOrigins,ivec2(probe%spGrid.x,probe/spGrid.x),0);
    return safe.w>0&&SFFinite(safe.xyz);
}
void main() {
    ivec2 pixel=ivec2(gl_FragCoord.xy);int part=pixel.x%4;
    int probe=pixel.x/4+pixel.y*spGrid.x;outColor=vec4(0);
    if(probe>=spTraceAtlas.w)return;
    vec4 ownE=PCCoefficient(probe,0),ownX=PCCoefficient(probe,1);
    vec4 ownY=PCCoefficient(probe,2),ownZ=PCCoefficient(probe,3);
    if(!PCFinite(ownE)||!PCFinite(ownX)||!PCFinite(ownY)||!PCFinite(ownZ))return;
    outColor=part==0?ownE:(part==1?ownX:(part==2?ownY:ownZ));
    vec4 position=SFProbe(probe,0),nd=SFProbe(probe,1);
    if(sfFilter.w<=0||!PCSourceValid(probe,position,nd))return;
    // No readable angular measurement at the center is still unknown black.
    // Do not turn it into a positive estimate using neighboring probes.
    if(!(ownE.a>0))return;
    vec3 normal=normalize(nd.xyz),sum=outColor.rgb;float mass=1;
    int tableWidth=textureSize(sfNeighbors,0).x;
    int localAddress=probe*SF_NEIGHBOR_COUNT+8;
    int localCount=clamp(int(texelFetch(sfNeighbors,ivec2(localAddress%tableWidth,localAddress/tableWidth),0).z+.5),0,8);
    for(int pair=0;pair<SF_NEIGHBOR_COUNT;++pair) {
        if(pair<8&&pair>=4*sfOptions.y)continue;
        if(pair>=8&&pair>=8+localCount)break;
        int address=probe*SF_NEIGHBOR_COUNT+pair;
        vec4 neighbor=texelFetch(sfNeighbors,ivec2(address%tableWidth,address/tableWidth),0);
        if(neighbor.x<.5||neighbor.y<.0001||!PCFinite(neighbor))continue;
        int index=int(neighbor.x+.5)-1;
        if(index<0||index>=spTraceAtlas.w)continue;
        vec4 other=SFProbe(index,0),otherNd=SFProbe(index,1);
        if(!PCSourceValid(index,other,otherNd))continue;
        vec4 e=PCCoefficient(index,0),bx=PCCoefficient(index,1);
        vec4 by=PCCoefficient(index,2),bz=PCCoefficient(index,3);
        if(!PCFinite(e)||!PCFinite(bx)||!PCFinite(by)||!PCFinite(bz))continue;
        vec3 value;
        if(part==0) {
            // E is the exact integral at that probe's launch normal. Rebase
            // the neighbor's same world-space L1 field to this normal before
            // averaging. Equal-normal planar support uses E verbatim.
            vec3 deltaNormal=normal-normalize(otherNd.xyz);
            value=max(e.rgb+(2.0/3.0)*(bx.rgb*deltaNormal.x+by.rgb*deltaNormal.y+bz.rgb*deltaNormal.z),vec3(0));
        }else value=part==1?bx.rgb:(part==2?by.rgb:bz.rgb);
        // e.a==0 is a complete unknown-black budget, not a missing neighbor.
        // It retains this geometry weight instead of renormalizing onto the
        // readable subset. The producer already divided RGB by full N.
        sum+=value*neighbor.y;mass+=neighbor.y;
    }
    outColor.rgb=mix(outColor.rgb,sum/mass,clamp(sfFilter.w,0.0,1.0));
    // E stores the own-normal integral directly; no B term is folded back
    // into it here. Alpha remains the center's original sample coverage.
}
)GLSL";
}

// Reuse surfaceCacheSpatialIrradiance and ProbeReconstructShader unchanged:
// copy part zero out of the final coefficient atlas after the three passes.
// This compact probe-domain pass does not reconstruct or filter screen RGB.
inline std::string ProbeCoefficientExtractIrradianceShader() {
    return std::string("#version 450 core\n")+ScreenProbeTraceDetail::ConstantsGLSL()+R"GLSL(
in vec2 vUV;layout(location=0) out vec4 outColor;
layout(binding=0) uniform sampler2D pcInput;
void main() {
    ivec2 pixel=ivec2(gl_FragCoord.xy);int probe=pixel.x+pixel.y*spGrid.x;
    outColor=probe<spTraceAtlas.w?texelFetch(pcInput,ivec2(pixel.x*4,pixel.y),0):vec4(0);
}
)GLSL";
}
} // namespace Render::SurfaceCacheDetail
