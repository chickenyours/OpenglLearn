#pragma once
#include <string>
namespace Render::PipelineDetail {
// Emissive LightParameter punctual branch, SSA2640..2731. No derivatives or
// graphics API calls: shared verbatim by fragment and segment-compute shading.
inline std::string StrandSourceDirectGLSL(){return R"GLSL(
struct StrandSourceLight {
    vec4 positionRange,directionFalloff,attenuation,colorTolerance;
    ivec4 shadowIes;vec4 extended,unusedIntensityScale;
};
layout(std140,binding=12) uniform StrandSourceDirect {
    vec4 strandSourceDirectOptions;
    StrandSourceLight strandSourceLights[64];
};
layout(binding=7) uniform sampler2D strandSourceIes;
bool StrandSourceDirectSample(int index,vec3 position,out vec3 L,out vec3 incident){
    StrandSourceLight light=strandSourceLights[index];
    vec3 delta=light.positionRange.xyz-position;
    float worldR2=dot(delta,delta),worldR=sqrt(max(worldR2,0.0));
    float r=worldR*strandSourceDirectOptions.w,r2=r*r,R=light.positionRange.w;
    L=vec3(0.0,1.0,0.0);incident=vec3(0.0);
    if(r2>R*R||R<=0.0)return false;
    if(worldR>1e-8)L=delta/worldR;
    vec2 c12=unpackHalf2x16(floatBitsToUint(light.colorTolerance.w));
    vec2 ec0=unpackHalf2x16(floatBitsToUint(light.extended.x));
    float start=light.extended.y,x=max(r,start)-start;
    float fade=clamp(1.0-pow(x/max(R-start,1e-8),ec0.x),0.0,1.0);
    float denominator=ec0.y+c12.x*x+c12.y*x*x;
    // The native coefficients must define a positive denominator at P. The
    // tiny guard only keeps coincident ideal-point diagnostics finite.
    float inverseDistance=1.0/max(denominator,1e-8);
    float cone=clamp(clamp(dot(-light.directionFalloff.xyz,L)*light.attenuation.w+
        light.attenuation.z,0.0,1.0)/light.directionFalloff.w,0.0,1.0);
    float value=fade*fade*cone*cone*(3.0-2.0*cone)*inverseDistance;
    if(light.shadowIes.y>=0&&strandSourceDirectOptions.z>.5){
        ivec2 size=textureSize(strandSourceIes,0);
        if(light.shadowIes.y<size.y){
            vec2 uv=vec2(clamp(dot(L,light.directionFalloff.xyz)*.5+.5,0.0,1.0),
                (float(light.shadowIes.y)+.5)/float(size.y));
            value*=max(textureLod(strandSourceIes,uv,0.0).r,0.0);
        }
    }
    incident=max(light.colorTolerance.rgb,vec3(0.0))*value;
    return value>0.0&&any(greaterThan(incident,vec3(0.0)));
}
)GLSL";}
}
