#pragma once
#include <stdexcept>
#include <string>
#include "screen_probe_trace_shaders.h"
#include "surface_cache_shaders.h"
#include "surface_cache_candidate_intersection.h"

namespace Render::SurfaceCacheDetail {
// Keep the accepted screen-probe sampling implementation as the only source of
// the octahedral directions, hash and two-dimensional cosine strata. In
// particular, spTracing.w carries the original seed/sequence unchanged.
inline std::string NativeScreenProbeSamplingGLSL() {
    const auto source=ScreenProbeTraceDetail::MarkerTraceGLSL();
    const auto begin=source.find("vec3 STRayDirection(int ray) {"),
        end=source.find("vec4 STReadRay(int probe,int ray,int part) {");
    if(begin==std::string::npos||end==std::string::npos||end<=begin)
        throw std::logic_error("Native screen-probe sampling source changed");
    return source.substr(begin,end-begin);
}

// Minimal native geometry access: no software BVH, private traversal stack or
// light integration is reachable from this program. The candidate helper is
// exactly the hybrid used by coherent receiver tracing. Diagnostic counts are
// actual rayQueryInitializeEXT calls, including the rare second traversal.
inline std::string NativeScreenProbeIntersectionGLSL(bool queryDiagnostics=false) {
    return std::string(queryDiagnostics?"uvec4 scTraceDiag[2];\n":"")+R"GLSL(
#define SC_NATIVE_GEOMETRY_SLOT 9
#ifdef RENDER_NATIVE_RAY_QUERY
layout(set=2,binding=0) uniform accelerationStructureEXT scAcceleration;
#endif
layout(binding=9) uniform sampler2D scTriangles;
layout(binding=15) uniform sampler2D wgGeometryResidual;
float nsGeometryFetches=0;
vec4 SCGeometry(int index) {
    ++nsGeometryFetches;
    ivec2 at=scGeometry.y==1024&&index>=0?ivec2(index&1023,index>>10):
        ivec2(index%scGeometry.y,index/scGeometry.y);
    return texelFetch(scTriangles,at,0);
}
#ifdef RENDER_NATIVE_FLOAT64
dvec3 WGPreciseVertex(int index,vec3 high) {
    ++nsGeometryFetches;
    return dvec3(high)+dvec3(texelFetch(wgGeometryResidual,ivec2(index&1023,index>>10),0).xyz);
}
#endif
)GLSL"+TransportCandidateIntersectionGLSL(queryDiagnostics);
}

// Inputs retain the fallback-stage binding convention: placement uniform 4,
// spawned 5, original certified sourceProbeOrigins 10, geometry 9/residual15,
// cache attributes12/metadata13/lighting14; PlacementData UBO9 and SC UBO12.
// Outputs are the original TraceOutputsShader four MRTs at atlasWidth/4 by
// atlasHeight, one complete original logical ray per fragment:
//   0 radiance/status, 1 hit.xyz/distance, 2 metadata, 3 direction/fetches.
// The unchanged PackTraceOutputsShader reconstructs the public 4-texel/ray
// atlas. No trace is repeated to produce another tuple component.
inline std::string NativeScreenProbeTraceCommonGLSL(bool queryDiagnostics=false) {
    return ScreenProbeTraceDetail::ConstantsGLSL()+NativeScreenProbeSamplingGLSL()+
        LookupGLSL()+NativeScreenProbeIntersectionGLSL(queryDiagnostics)+R"GLSL(
layout(binding=4) uniform sampler2D nsUniform;
layout(binding=5) uniform sampler2D nsSpawned;
layout(binding=10) uniform sampler2D nsSafeOrigins;
bool NSFinite(vec3 value){return !any(isnan(value))&&!any(isinf(value));}
vec4 NSProbe(int probe,int part) {
    bool uniformProbe=probe<spTraceAtlas.z;
    int slot=uniformProbe?probe:probe-spTraceAtlas.z;
    int columns=spGrid.x*(uniformProbe?1:spGrid.z);
    ivec2 at=ivec2(2*(slot%columns)+part,slot/columns);
    return uniformProbe?texelFetch(nsUniform,at,0):texelFetch(nsSpawned,at,0);
}
void NSNativeTrace(int probe,int ray,out vec4 color,out vec4 hit,out vec4 metadata,out vec4 directionData) {
    color=vec4(0);hit=vec4(0);metadata=vec4(-1,-1,0,0);directionData=vec4(0);
    if(probe>=spTraceAtlas.w)return;
    vec4 position=NSProbe(probe,0),normalDepth=NSProbe(probe,1);
    if(position.w<.5)return;
    vec3 normal=normalDepth.xyz;
    // The old screen stage first generates a direction, then the native
    // fallback normalizes that stored direction once more. Retain both
    // normalization operations for direct ray/direction ABI comparisons.
    vec3 direction=normalize(STRayDirection(ray));directionData.xyz=direction;hit.xyz=position.xyz;
    if(!NSFinite(position.xyz)||!NSFinite(normal)||dot(normal,normal)<.5) {
        color.w=16;return;
    }
    normal=normalize(normal);bool cosineHemisphere=(spTracing.w&2)!=0;
    if(cosineHemisphere)direction=normalize(STCosineRayDirection(probe,ray,normal));
    direction=normalize(direction);directionData.xyz=direction;
    if(!NSFinite(direction)||dot(direction,direction)<.5){color.w=16;return;}
    if(dot(direction,normal)<=(cosineHemisphere?0.0:.02)){color.w=5;return;}
    vec4 safeOrigin=texelFetch(nsSafeOrigins,ivec2(probe%spGrid.x,probe/spGrid.x),0);
    if(!(safeOrigin.w>0)||!NSFinite(safeOrigin.xyz)||scGeometry.z<=0||
        isnan(spTraceParams.x)||isinf(spTraceParams.x)||!(spTraceParams.x>1e-5)) {
        color.w=16;return;
    }
    // Origin certification remains the original once-per-probe pass. This
    // complete interval is queried once, with a second native initialization
    // only if the expanded nearest candidate fails its original predicate.
    vec3 origin=safeOrigin.xyz,hitNormal;float distance=spTraceParams.x;
    int triangle=-1;bool exhausted;
    bool found=SCNativeIntersect(origin,direction,distance,hitNormal,triangle,exhausted,false);
    hit=vec4(origin+direction*distance,distance);
    metadata=vec4(-1,-1,1,float(triangle));directionData.w=nsGeometryFetches;
    if(exhausted){color.w=15;return;}
    // Known misses and one-sided back faces are readable black. Unknown
    // cache lighting remains black with an unreadable status, never a reason
    // to remove this direction from the full N-ray integration denominator.
    if(!found){color.w=13;return;}
    if(dot(hitNormal,-direction)<=0){color.w=9;return;}
    int owner=SCTriangleOwner(triangle);vec3 radiance=vec3(0);
    if(owner<0||!SCSampleLightingObject(hit.xyz,hitNormal,owner,radiance)) {
        color.w=11;return;
    }
    color=NSFinite(radiance)?vec4(radiance,9):vec4(0,0,0,7);
}
)GLSL";
}

inline std::string NativeScreenProbeTraceShader(bool queryDiagnostics=false) {
    auto source=std::string("#version 450 core\n")+NativeScreenProbeTraceCommonGLSL(queryDiagnostics)+R"GLSL(
layout(location=0) out vec4 outColor;
layout(location=1) out vec4 outHit;
layout(location=2) out vec4 outMetadata;
layout(location=3) out vec4 outDirection;
void main() {
    nsGeometryFetches=0;
)GLSL";
    if(queryDiagnostics)source+="    scTraceDiag[0]=uvec4(0);scTraceDiag[1]=uvec4(0);\n";
    source+=R"GLSL(
    outColor=vec4(0);outHit=vec4(0);outMetadata=vec4(-1,-1,0,0);outDirection=vec4(0);
    if(spTracing.x<=0||spTraceAtlas.x<4)return;
    ivec2 pixel=ivec2(gl_FragCoord.xy);int raysPerProbe=spTracing.x*spTracing.x;
    int rayGlobal=pixel.y*(spTraceAtlas.x/4)+pixel.x;
    NSNativeTrace(rayGlobal/raysPerProbe,rayGlobal%raysPerProbe,outColor,outHit,outMetadata,outDirection);
)GLSL";
    // This deliberately matches PSTrace's diagnostic metadata convention:
    // xy=(software query count, actual native initialization count). The
    // production shader leaves xy=-1, exactly as off-screen cache hits do.
    if(queryDiagnostics)source+="    outMetadata.xy=vec2(0,float(scTraceDiag[0].x));\n";
    return source+"}\n";
}
} // namespace Render::SurfaceCacheDetail
