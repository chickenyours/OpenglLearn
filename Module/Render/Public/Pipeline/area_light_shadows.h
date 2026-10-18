#pragma once
#include <cmath>
#include <cstdint>
#include <string>
#include <glm/glm.hpp>

namespace Render {
inline constexpr std::uint32_t AreaLightShadowBinding=12,AreaLightShadowTextureSlot=10;
struct AreaLightShadowSettings {
    // 4x4 emitter grid uses this tile size; 8x8 uses half, keeping 24 MiB at 256.
    std::uint32_t tileResolution=256;
    float nearPlane=.015f,depthBiasTexels=.3f,normalBiasTexels=.25f;
};
inline bool ValidateAreaLightShadowSettings(const AreaLightShadowSettings& s) {
    return s.tileResolution>=64&&s.tileResolution<=1024&&(s.tileResolution&(s.tileResolution-1))==0&&
        std::isfinite(s.nearPlane)&&s.nearPlane>=.001f&&s.nearPlane<=.1f&&
        std::isfinite(s.depthBiasTexels)&&s.depthBiasTexels>=0&&s.depthBiasTexels<=4&&
        std::isfinite(s.normalBiasTexels)&&s.normalBiasTexels>=0&&s.normalBiasTexels<=4;
}
struct AreaLightShadowConstants {
    glm::ivec4 atlas{1,1,1,1}; // width, height, tile pixels, tile columns
    glm::ivec4 options{0,4,0,0}; // enabled, emitter grid, light index, reserved
    glm::vec4 parameters{.015f,30,.3f,.25f}; // near, far, depth/normal bias in texels
};
static_assert(sizeof(AreaLightShadowConstants)==48);
// Opt-in opaque PBR only. The ordinary material keeps its existing sky/probe
// bindings; this variant uses one cached depth cube per emitter quadrature cell.
inline std::string AreaLightShadowGLSL() {return R"GLSL(
layout(std140,binding=12) uniform AreaLightShadowData {
    ivec4 areaShadowAtlas,areaShadowOptions;
    vec4 areaShadowParameters;
};
layout(binding=10) uniform sampler2D areaShadowDepth;
const vec3 areaShadowForward[6]=vec3[6](vec3(1,0,0),vec3(-1,0,0),vec3(0,1,0),vec3(0,-1,0),vec3(0,0,1),vec3(0,0,-1));
const vec3 areaShadowRight[6]=vec3[6](vec3(0,0,-1),vec3(0,0,1),vec3(1,0,0),vec3(1,0,0),vec3(1,0,0),vec3(-1,0,0));
const vec3 areaShadowUp[6]=vec3[6](vec3(0,-1,0),vec3(0,-1,0),vec3(0,0,1),vec3(0,0,-1),vec3(0,-1,0),vec3(0,-1,0));
float AreaLightVisibility(vec3 p,vec3 n,vec3 direction,float distance,int light,int emitterSample) {
    if(areaShadowOptions.x==0||light!=areaShadowOptions.z)return 1.0;
    int grid=areaShadowOptions.y,tileSize=areaShadowAtlas.z;
    if(emitterSample<0||emitterSample>=grid*grid||tileSize<1)return 1.0;
    vec3 delta=-direction*distance;
    float major=max(abs(delta.x),max(abs(delta.y),abs(delta.z)));
    float worldTexel=2.0*major/float(tileSize);
    float cosine=clamp(dot(n,direction),0.0,1.0);
    delta+=n*(areaShadowParameters.w*worldTexel*sqrt(max(0.0,1.0-cosine*cosine)));
    vec3 magnitude=abs(delta);int face;
    if(magnitude.x>=magnitude.y&&magnitude.x>=magnitude.z)face=delta.x>=0?0:1;
    else if(magnitude.y>=magnitude.z)face=delta.y>=0?2:3;
    else face=delta.z>=0?4:5;
    major=dot(delta,areaShadowForward[face]);
    float nearPlane=areaShadowParameters.x,farPlane=areaShadowParameters.y;
    if(major<=nearPlane||major>=farPlane)return 1.0;
    vec2 local=.5*(vec2(dot(delta,areaShadowRight[face]),dot(delta,areaShadowUp[face]))/major+1.0);
    int tile=emitterSample*6+face;
    ivec2 origin=ivec2(tile%areaShadowAtlas.w,tile/areaShadowAtlas.w)*tileSize;
    vec2 pixel=local*float(tileSize)-.5;ivec2 first=ivec2(floor(pixel));vec2 phase=fract(pixel);
    bool gatherInterior=all(greaterThanEqual(first,ivec2(0)))&&all(lessThan(first+ivec2(1),ivec2(tileSize)));
    vec4 gatheredDepth=vec4(0);
    if(gatherInterior) {
        // Gather at the footprint center, away from texel-boundary rounding.
        // GLSL returns (0,1),(1,1),(1,0),(0,0); reorder to x + 2*y below.
        vec2 gatherUV=(vec2(origin+first)+1.0)/vec2(areaShadowAtlas.xy);
        gatheredDepth=textureGather(areaShadowDepth,gatherUV,0).wzxy;
    }
    float projectionA=farPlane/(farPlane-nearPlane),projectionB=farPlane*nearPlane/(farPlane-nearPlane);
    float depthBias=areaShadowParameters.z*worldTexel*projectionB/(major*major);
    float planeNumerator=dot(n,delta),visibility=0.0;
    for(int y=0;y<2;++y)for(int x=0;x<2;++x) {
        ivec2 at=clamp(first+ivec2(x,y),ivec2(0),ivec2(tileSize-1));
        vec2 uv=(vec2(at)+.5)/float(tileSize)*2.0-1.0;
        vec3 ray=areaShadowForward[face]+areaShadowRight[face]*uv.x+areaShadowUp[face]*uv.y;
        float denominator=dot(n,ray),planeDepth=major;
        // Exact receiver-plane correction at the PCF texel center prevents
        // steep ceramic surfaces from self-shadowing under a wider footprint.
        if(abs(denominator)>1e-6)planeDepth=clamp(planeNumerator/denominator,
            max(nearPlane,major-4.0*worldTexel),major+4.0*worldTexel);
        float receiverDepth=projectionA-projectionB/planeDepth-depthBias;
        // At cube-tile boundaries retain the original clamped four fetches:
        // an atlas gather there could read an unrelated neighboring face.
        float storedDepth=gatherInterior?gatheredDepth[y*2+x]:texelFetch(areaShadowDepth,origin+at,0).r;
        float weight=(x==0?1.0-phase.x:phase.x)*(y==0?1.0-phase.y:phase.y);
        visibility+=(receiverDepth<=storedDepth?1.0:0.0)*weight;
    }
    return visibility;
}
)GLSL";}
} // namespace Render
