#pragma once
#include <string>

namespace Render::SurfaceCacheDetail {
// UE 5.7.1 provenance (the local source is authoritative):
// Shaders/Private/Lumen/LumenSceneLighting.ush:26-35 moves card shadow
// origins along L by B*(1+2*saturate(1-dot(L,N))).
// Source/Runtime/Renderer/Private/Lumen/LumenSceneDirectLighting.cpp:85-89
// defaults B to 1cm; the project supplies its existing scParameters.y in metres.
// LumenSceneDirectLightingHardwareRayTracing.usf:282-305 computes the finite
// light endpoint and then applies the origin offset. Project IO adaptation:
// keep the old 2*B endpoint margin AND deduct the actual start displacement,
// so shifting the origin cannot extend the ray into/beyond the light. The
// existing SCNativeIntersect still excludes the open endpoint with its ULP
// decrement; its TMin/any-hit/fallback/instrumentation are not duplicated here.
// Append after GeometryGLSL(hw), including its existing diagnostic wrapper.
// Invalid inputs are separately unreadable, never certified valid-black data.
inline std::string UeLumenCardShadowVisibilityGLSL(bool hardwareVisibility=true) {
    std::string s=R"GLSL(
bool ueCardDirectInputsReadable=true;
bool UECardDirectFinite3(vec3 v){return !any(isnan(v))&&!any(isinf(v));}
bool UECardDirectVisible(vec3 p,vec3 geometricNormal,vec3 lightDirection,float lightDistance){
    float baseBias=scParameters.y;
    float normalLengthSquared=dot(geometricNormal,geometricNormal);
    float directionLengthSquared=dot(lightDirection,lightDirection);
    if(!UECardDirectFinite3(p)||!UECardDirectFinite3(geometricNormal)||
       !UECardDirectFinite3(lightDirection)||isnan(lightDistance)||isinf(lightDistance)||
       isnan(baseBias)||isinf(baseBias)||baseBias<0.0||
       isnan(normalLengthSquared)||isinf(normalLengthSquared)||normalLengthSquared<=1e-20||
       isnan(directionLengthSquared)||isinf(directionLengthSquared)||directionLengthSquared<=1e-20){
        ueCardDirectInputsReadable=false;return false;
    }
    // No arbitrary normal-axis fallback: degenerate normals above are invalid.
    // Finite non-unit geometric inputs have a stable normalized direction.
    vec3 n=geometricNormal*inversesqrt(normalLengthSquared);
    vec3 d=lightDirection*inversesqrt(directionLengthSquared);
    float startOffset=baseBias*(1.0+2.0*clamp(1.0-dot(d,n),0.0,1.0));
    float maximum=lightDistance-2.0*baseBias-startOffset;
    if(isnan(startOffset)||isinf(startOffset)||isnan(maximum)||isinf(maximum)){
        ueCardDirectInputsReadable=false;return false;
    }
    // The retained interval is empty at/below the existing SC ray TMin.
    // This is the existing SCIntersect/SCNativeIntersect no-hit convention;
    // it is not a new visibility sample and must initialize no query.
    if(maximum<=1e-5)return true;
    vec3 origin=p+d*startOffset;
    if(!UECardDirectFinite3(origin)){
        ueCardDirectInputsReadable=false;return false;
    }
    vec3 unused;int triangle;bool exhausted;bool found;
)GLSL";
    if(hardwareVisibility)s+=
        "    found=SCNativeIntersect(origin,d,maximum,unused,triangle,exhausted,true);\n";
    else s+=R"GLSL(
    if((int(scParameters.w+.5)&4)!=0)
        found=SCAnyHitReusingBounds(origin,d,maximum,exhausted);
    else found=SCIntersect(origin,d,maximum,unused,triangle,exhausted,true);
)GLSL";
    return s+"    return !found&&!exhausted;\n}\n";
}
} // namespace Render::SurfaceCacheDetail
