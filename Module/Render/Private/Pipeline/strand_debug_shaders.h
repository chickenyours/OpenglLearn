#pragma once
#include <string>

namespace Render::PipelineDetail {
// Diagnostics call the production closures with zero inter-fiber strength;
// there is no second implementation of R/TT/TRT or transport. In the source
// closures these are the visible ballistic terms, including exp(-N) and G.
// The generic non-spatial closure couples strength to extinction, so its
// full-minus-zero-strength view is explicitly an ablation residual, not a
// positive-energy decomposition. Legacy tangent lobes have no such split.
inline std::string StrandDebugGLSL() { return R"GLSL(
vec3 StrandDebugBallisticLight(vec3 base,vec3 T,vec3 V,vec3 L,vec3 radiance,
    float pathCount,float visibility,vec3 mask) {
    if(strandScattering.x<.5)return vec3(0.0);
    vec3 strengths=vec3(strandSurface.z,strandVolume.x,strandSurface.w)*mask;
    if(strandColorVariation.w>.5)
        return FiberSourceDirect(base,T,V,L,strandFiber,strandSurface.x,strandVolume.w,
            strengths,0.0,strandScattering.z,pathCount,fiberAtlas.w>.5,strandIbl.x,visibility)*radiance;
    return FiberScatter(base,T,V,L,strandFiber,strandSurface.x,strandVolume.w,
        strengths,0.0,strandScattering.z,pathCount,fiberAtlas.w>.5)*radiance*visibility;
}
vec3 StrandDebugExternallyShadowedBallisticLight(vec3 base,vec3 T,vec3 V,vec3 L,vec3 radiance,
    float pathCount,float dsVisibility,float externalVisibility,vec3 mask) {
    float visibility=externalVisibility*dsVisibility;
    if(strandScattering.x>.5&&strandColorVariation.w>.5)
        return FiberSourceDirect(base,T,V,L,strandFiber,strandSurface.x,strandVolume.w,
            vec3(strandSurface.z,strandVolume.x,strandSurface.w)*mask,0.0,strandScattering.z,pathCount,
            fiberAtlas.w>.5,strandIbl.x,visibility,dsVisibility)*radiance;
    return StrandDebugBallisticLight(base,T,V,L,radiance,pathCount,visibility,mask);
}
vec3 StrandDebugBallisticIbl(vec3 base,vec3 T,vec3 V,vec2 sourceCounts,
    vec2 sourceVisibility,vec3 mask) {
    vec3 radial=StrandSafe(V-T*dot(T,V),vec3(0.0,0.0,1.0));
    vec3 B=StrandSafe(cross(T,radial),vec3(1.0,0.0,0.0));
    vec3 N=StrandSafe(cross(B,T),radial);
    vec3 directions[2]=vec3[2](StrandSafe(N+B,N),StrandSafe(N-B,N));
    vec3 incident=strandSpatialIncident.w>.5?StrandIblIncident(base,
        StrandSafe(strandWorldPosition-strandSpatialIncident.xyz,vec3(0.0,1.0,0.0))):vec3(0.0);
    vec3 result=vec3(0.0);
    for(int i=0;i<2;++i) {
        float count=fiberAtlas.w>.5&&fiberTrace.w>1.5?max(sourceCounts[i],0.0):0.0;
        vec3 response=FiberIblDirection(base,T,V,directions[i],strandFiber,strandSurface.x,strandVolume.w,
            vec3(strandSurface.z,strandVolume.x,strandSurface.w)*mask,0.0,
            count,strandScattering.z,strandIbl.x,sourceVisibility[i]);
        result+=(strandSpatialIncident.w>.5?incident:StrandIblIncident(base,directions[i]))*response*(.5*FiberPi);
    }
    return result;
}
vec3 StrandDebugIndirectIncident(vec3 base,vec3 T,vec3 V) {
    // The actual incident adapter, after authored albedo colorization and
    // producer gain, before any fiber response or N/G transport. This is not
    // a fabricated white-light control, and remains scene-linear radiance.
    if(strandScattering.x>.5&&strandIbl.y>.5&&strandIbl.z>.5) {
        if(strandSpatialIncident.w>.5)return StrandIblIncident(base,
            StrandSafe(strandWorldPosition-strandSpatialIncident.xyz,vec3(0.0,1.0,0.0)));
        vec3 radial=StrandSafe(V-T*dot(T,V),vec3(0.0,0.0,1.0));
        vec3 B=StrandSafe(cross(T,radial),vec3(1.0,0.0,0.0));
        vec3 N=StrandSafe(cross(B,T),radial);
        return .5*(StrandIblIncident(base,StrandSafe(N+B,N))+
                   StrandIblIncident(base,StrandSafe(N-B,N)));
    }
    vec3 ambient=max(strandAmbient.rgb,vec3(0.0));
    if(strandScattering.x<.5)return ambient;
    ambient=(ambient+StrandSkyDiffuse(T))*strandRaster.w;
    return mix(ambient,vec3(dot(ambient,vec3(.2126,.7152,.0722)))*base,strandScattering.w);
}
float StrandDebugCount(float pathCount) {
    // A monotone, bounded visualization of the effective count used by the
    // closure. It changes neither the physical atlas nor production transport.
    return 1.0-exp(-max(pathCount,0.0)*max(strandScattering.z,0.0));
}
)GLSL"; }
} // namespace Render::PipelineDetail
