#pragma once
#include "Render/Private/Pipeline/strand_shaders.h"
#include <stdexcept>

namespace Render::PipelineDetail {
inline std::string StrandMotionObjectGLSL() {return R"GLSL(
layout(std140,binding=8) uniform StrandMotionObject {
    mat4 strandPreviousModel;
    vec4 strandMotionObject;
};
)GLSL";}
// Generate from the exact current raster shader. All current gl_Position and
// coverage operations remain identical, including invariant and future edits.
inline std::string StrandMotionVertexShader(bool curveGeometry=false) {
    auto source=StrandVertexShader(true,false,curveGeometry);
    const auto insertion=source.find("void main()");
    if(insertion==std::string::npos)throw std::runtime_error("Strand motion requires current raster main");
    // The regular raster shader now declares the same prior attributes/object
    // for optional geometry shutter sampling. Do not redeclare them here.
    source.insert(insertion,R"GLSL(
layout(location=6) out vec4 strandMotionPreviousClip;
vec4 StrandPreviousRibbonClip() {
    vec3 center=(strandPreviousModel*vec4(previousPositionRadius.xyz,1.0)).xyz;
    vec3 T=StrandSafe(mat3(strandPreviousModel)*previousTangentArc.xyz,vec3(0.0,-1.0,0.0));
    vec3 V=StrandSafe(strandPreviousCamera.xyz-center,vec3(0.0,0.0,1.0));
    vec3 axis=abs(T.y)<.95?vec3(0.0,1.0,0.0):vec3(1.0,0.0,0.0);
    vec3 side=StrandSafe(cross(T,V),StrandSafe(cross(T,axis),vec3(1.0,0.0,0.0)));
    float modelScale=max(length(strandPreviousModel[0].xyz),max(length(strandPreviousModel[1].xyz),length(strandPreviousModel[2].xyz)));
    // The producer accepts transform history only when width/LOD/filter state
    // matches the last submitted frame, so these current raster controls are
    // exactly the previous controls. Geometry stores the previous real radius.
    float fullWidth=max(2.0*abs(previousPositionRadius.w)*strandVolume.y,strandShadows.z);
    if(strandShadows.w>0.0)fullWidth=min(fullWidth,strandShadows.w);
    float radius=.5*fullWidth*modelScale;
    vec4 clip=strandPreviousViewProjection*vec4(center,1.0);
    vec4 edge=strandPreviousViewProjection*vec4(center+side*radius,1.0);
    vec2 delta=(edge.xy/max(abs(edge.w),1e-7)-clip.xy/max(abs(clip.w),1e-7))*strandViewport.xy*.5;
    float physicalHalfWidth=length(delta);
    vec2 pixelDirection=physicalHalfWidth>1e-7?delta/physicalHalfWidth:vec2(1.0,0.0);
    float filteredHalfWidth=min(physicalHalfWidth,.5*strandRaster.y);
    float filterSupport=.5*(abs(pixelDirection.x)+abs(pixelDirection.y));
    float halfWidth=strandShadows.y>.5?max(filteredHalfWidth+filterSupport,.5*strandRaster.x):
        clamp(physicalHalfWidth,.5*strandRaster.x,.5*strandRaster.y);
    float signedSide=previousPositionRadius.w<0.0?-1.0:1.0;
    clip.xy+=signedSide*pixelDirection*halfWidth*2.0/strandViewport.xy*clip.w;
    return clip;
}
)GLSL");
    if(curveGeometry)source.insert(source.find("void main()"),R"GLSL(
vec4 StrandPreviousCurveClip(vec4 currentClip,vec3 currentCenter,StrandCurveVertex curve){
    if(strandCurveAtlas.z==0u)return StrandPreviousRibbonClip();
    vec4 currentVertex=strandCurveInverseRasterProjection*currentClip;
    if(abs(currentVertex.w)<1e-8)return vec4(0.0);
    vec3 offset=currentVertex.xyz/currentVertex.w-currentCenter;
    vec3 oldCenter=(strandPreviousModel*vec4(curve.previousPositionRadius.xyz,1.0)).xyz;
    // Native fillTriangle uses the previous curve center plus CURRENT ribbon
    // offset. Recover the actual current filtered offset, so the host's AA
    // support also has exactly zero motion for an unchanged static curve.
    return strandPreviousViewProjection*vec4(oldCenter+offset,1.0);
}
)GLSL");
    const auto end=source.rfind('}');
    if(end==std::string::npos)throw std::runtime_error("Strand motion requires current raster closing brace");
    source.insert(end,curveGeometry?
        "    strandMotionPreviousClip=StrandPreviousCurveClip(gl_Position,center,curve);\n":
        "    strandMotionPreviousClip=StrandPreviousRibbonClip();\n");
    return source;
}
inline std::string StrandMotionFragmentShader() {
    auto source=StrandFragmentShader(true,3);
    source.insert(source.find('\n')+1,StrandMotionObjectGLSL()+R"GLSL(
layout(location=6) in vec4 strandMotionPreviousClip;
layout(location=1) out vec4 outMotionCounts;
)GLSL");
    const std::string original="outColor=vec4(-log(max(1.0-alpha,.0001)),0.0,0.0,0.0);";
    const auto output=source.find(original);
    if(output==std::string::npos)throw std::runtime_error("Strand motion requires current reveal output");
    source.replace(output,original.size(),R"GLSL(vec3 previous=vec3(0.0);
    bool valid=strandMotionState.x>.5&&strandMotionObject.x>.5&&strandMotionPreviousClip.w>1e-8&&
        !any(isnan(strandMotionPreviousClip))&&!any(isinf(strandMotionPreviousClip));
    if(valid) {
        previous=strandMotionPreviousClip.xyz/strandMotionPreviousClip.w*.5+.5;
        valid=all(greaterThanEqual(previous,vec3(0.0)))&&all(lessThanEqual(previous,vec3(1.0)));
    }
    outColor=valid?vec4(previous*alpha,alpha):vec4(0.0);
    // Every nearest-depth fragment contributes to total alpha. A missing old
    // position in any tied fragment rejects the group's history conservatively.
    outMotionCounts=vec4(alpha,valid?0.0:alpha,0.0,0.0);)GLSL");
    return source;
}
inline std::string StrandMotionResolveFragmentShader() {return R"GLSL(#version 450 core
layout(binding=0) uniform sampler2D strandMotionAccumulation;
layout(binding=1) uniform sampler2D strandMotionCounts;
layout(location=0) out vec4 outMotion;
void main() {
    ivec2 pixel=ivec2(gl_FragCoord.xy);
    vec4 accumulated=texelFetch(strandMotionAccumulation,pixel,0);
    vec2 counts=texelFetch(strandMotionCounts,pixel,0).rg;
    // Surviving raster alpha is >=.001, representable even in the half-float
    // scratch. Its strictly positive invalid sum must never be normalized away
    // by a much larger valid group.
    bool valid=counts.x>1e-7&&counts.y==0.0&&accumulated.a>1e-7;
    vec3 previous=valid?accumulated.rgb/accumulated.a:vec3(0.0);
    valid=valid&&!any(isnan(previous))&&!any(isinf(previous));
    outMotion=valid?vec4(previous,1.0):vec4(0.0);
}
)GLSL";}
} // namespace Render::PipelineDetail
