#pragma once
#include <string>
#include "screen_probe_placement_shaders.h"
#include "screen_probe_trace_shaders.h"
#include "surface_cache_probe_origin_shaders.h"
#include "surface_cache_shaders.h"

namespace Render::SurfaceCacheDetail {
inline std::string ContactConstantsGLSL() {return R"GLSL(
layout(std140,binding=14) uniform ContactResolveData {
    vec4 ctParameters; // near-hit distance, tan(start angle), tan(end angle), risky geometric weight fraction
    ivec4 ctOptions; // base receiver ray count, local-filter radius, parallel-layer validation, feature bits
    vec4 ctFilter; // local-filter strength, albedo tolerance, normal cosine, numeric clearance
    uvec4 ctSampling; // base seed, committed sequence, independent contact directions, continuous confidence
    vec4 ctGuide; // tan(hit-parallax angle), maximum guided radius, receiver screen detection, coherent probe risk
};
bool CTGuided(){return (ctOptions.w&1)!=0;}
int CTBaseRayCount(float mask){return ctOptions.x*((ctOptions.w&32)!=0&&mask>=.75?2:1);}
float CTMeasurementDistance(float encoded){
    float fraction=clamp(encoded-2.0*floor(encoded*.5),0.0,.99999);
    return fraction/max(1.0-fraction,.00001);
}
)GLSL";}

// This sampler is declared only in the receiver Gather/Resolve/Filter shaders.
// The mask shader uses slot 15 for probe risk; Gather instead needs cache
// attributes at 12 and must not alias those with its fresh-receiver texture.
inline std::string ContactFreshSamplingGLSL() {return R"GLSL(
// Optional, independently classified newly visible receivers. The baseline
// ray budget and contact risk are unchanged: X supplies only current receiver
// confidence, Y is the geometry-exposure (base + extra) ray budget. Z bit zero
// retains geometry freshness; bits 1..3 may request independent moving batches.
// Bit 4 additionally requests full current measurements on certified moving
// surfaces; it never modifies geometry exposure or the geometric risk texture.
#if CT_FRESH_SAMPLING
layout(binding=15) uniform sampler2D ctFreshSampling;
int CTMovingBudgetTag(vec4 fresh){
    if(isnan(fresh.z)||isinf(fresh.z)||fresh.z<0.0||fresh.z>31.0)return 0;
    int tag=int(floor(fresh.z+.5)),multiplier=(tag&15)>>1;
    return fresh.z==float(tag)&&(multiplier==2||multiplier==4)?tag:0;
}
float CTEffectiveMask(float mask,ivec2 pixel){
    if((ctOptions.w&16)==0)return mask;
    vec4 fresh=texelFetch(ctFreshSampling,pixel,0);
    if(isnan(fresh.x)||isinf(fresh.x))return mask;
    float effective=max(mask,clamp(fresh.x,0.0,1.0));
    return (CTMovingBudgetTag(fresh)&16)!=0?max(effective,1.0):effective;
}
int CTActualRayCount(ivec2 pixel,float originalMask){
    if((ctOptions.w&16)==0)return CTBaseRayCount(originalMask);
    vec4 fresh=texelFetch(ctFreshSampling,pixel,0);
    // Additional work is whole independently scrambled base-stratum batches.
    // Reject malformed non-positive counts and retain the existing 8..32 base.
    int batches=CTBaseRayCount(originalMask)/ctOptions.x;
    if(fresh.x>.000001&&!any(isnan(fresh.xy))&&!any(isinf(fresh.xy)))
        batches=max(batches,int(clamp(floor(fresh.y/float(ctOptions.x)+.5),1.0,4.0)));
    // Risk-only mode retains the original near-occlusion gate exactly. Full
    // current mode separately requests genuine measurements on the certified
    // moving surface; both modes retain the original geometric exposure age.
    int tag=CTMovingBudgetTag(fresh),movingMultiplier=(tag&15)>>1;
    if(movingMultiplier>1&&(originalMask>=.25||(tag&16)!=0))
        batches=max(batches,movingMultiplier);
    return ctOptions.x*batches;
}
#else
float CTEffectiveMask(float mask,ivec2 pixel){return mask;}
#endif
float CTMeasurementCoverage(float encoded,ivec2 pixel,float originalMask){
#if CT_FRESH_SAMPLING
    return encoded<0?0.0:floor(encoded*.5)/float(CTActualRayCount(pixel,originalMask));
#else
    return encoded<0?0.0:floor(encoded*.5)/float(CTBaseRayCount(originalMask));
#endif
}
vec4 CTResolveMeasurement(vec4 base,vec4 measurement,float mask,ivec2 pixel,float originalMask){
    if(mask<=.000001)return base;
    if(measurement.a<0)return vec4(0);
    return mix(base,vec4(measurement.rgb,CTMeasurementCoverage(measurement.a,pixel,originalMask)),clamp(mask,0.0,1.0));
}
)GLSL";}

// Geometry only: no RGB, luminance or readability chooses a risky probe. A
// harmonic reciprocal distance retains the fraction of the full ray budget
// occupied by close geometry instead of letting one tiny hit dominate alone.
inline std::string ContactProbeRiskShader() {
    return std::string("#version 450 core\n")+ScreenProbeTraceDetail::ConstantsGLSL()+ContactConstantsGLSL()+R"GLSL(
in vec2 vUV;layout(location=0) out vec4 outColor;
layout(binding=7) uniform sampler2D ctRays;
void main() {
    int probe=int(gl_FragCoord.x)+int(gl_FragCoord.y)*spGrid.x;
    outColor=vec4(0);if(probe>=spTraceAtlas.w)return;
    int count=spTracing.x*spTracing.x;float reciprocal=0,nearCount=0,minimum=spTraceParams.x;
    for(int ray=0;ray<256;++ray) {
        if(ray>=count)break;int at=(probe*count+ray)*4;
        int status=int(texelFetch(ctRays,ivec2(at%spTraceAtlas.x,at/spTraceAtlas.x),0).w+.5);
        // 1/17 are finite screen geometry hits; 9..12 are exact refined mesh
        // or global hits, including known geometry whose cache is unreadable.
        if(status!=1&&status!=9&&status!=10&&status!=11&&status!=12&&status!=17)continue;
        ++at;float distance=texelFetch(ctRays,ivec2(at%spTraceAtlas.x,at/spTraceAtlas.x),0).w;
        if(!(distance>0)||isnan(distance)||isinf(distance))continue;
        minimum=min(minimum,distance);
        if(distance<ctParameters.x) {
            // Fade the cutoff continuously. Trace distance itself is not
            // changed; the later actual-receiver gather traces the full scene.
            float nearWeight=1.0-smoothstep(.75*ctParameters.x,ctParameters.x,distance);
            reciprocal+=nearWeight/max(distance,.00001);nearCount+=nearWeight;
        }
    }
    outColor=vec4(reciprocal/float(count),nearCount/float(count),minimum,1);
}
)GLSL";
}

inline std::string ContactMaskShader() {
    return std::string("#version 450 core\n")+ScreenProbeTraceDetail::ConstantsGLSL()+ContactConstantsGLSL()+R"GLSL(
layout(binding=2) uniform sampler2D ctPositions;
layout(binding=3) uniform sampler2D ctNormals;
layout(binding=10) uniform sampler2D ctActive;
layout(binding=11) uniform sampler2D ctOrigins;
#define SPGI_SURFACE_POSITIONS ctPositions
#define SPGI_ACTIVE_PROBES(tile) int(texelFetch(ctActive,tile,0).x+.5)
bool CTProbeUsable(int probe);
vec4 CTProbeRisk(sampler2D atlas,int probe,vec3 p,vec3 n,float depth);
float CTProbeCurvedSupport(int probe,vec3 p,vec3 n,float depth);
#define SPGI_PROBE_USABLE(probe) CTProbeUsable(probe)
#define SPGI_SAMPLE_LIGHTING(atlas,probe,p,n,depth) CTProbeRisk(atlas,probe,p,n,depth)
#define SPGI_CURVED_SUPPORT(probe,p,n,depth) CTProbeCurvedSupport(probe,p,n,depth)
)GLSL"+ScreenProbePlacementDetail::SamplingGLSL()+R"GLSL(
in vec2 vUV;layout(location=0) out vec4 outColor;
layout(binding=0) uniform sampler2D ctInput;
layout(binding=5) uniform sampler2D ctUniform;
layout(binding=6) uniform sampler2D ctSpawned;
layout(binding=8) uniform sampler2D ctDirectional;
layout(binding=15) uniform sampler2D ctRisk;
bool CTFinite(vec3 value){return !any(isnan(value))&&!any(isinf(value));}
bool CTProbeUsable(int probe) {
    if(spCurvature.z<.5)return true;
    vec4 origin=texelFetch(ctOrigins,ivec2(probe%spGrid.x,probe/spGrid.x),0);
    return origin.w>0&&CTFinite(origin.xyz);
}
float CTProbeCurvedSupport(int probe,vec3 p,vec3 n,float depth) {
    if((ctOptions.w&8)==0)return 0.0;
    vec3 delta=SPGiAtlasPart(ctUniform,ctSpawned,probe,0).xyz-p;
    float inverseDistance=texelFetch(ctDirectional,ivec2((probe%spGrid.x)*4+1,probe/spGrid.x),0).a;
    return SPGiCurvedHitSupport(delta,n,inverseDistance);
}
vec4 CTProbeRisk(sampler2D atlas,int probe,vec3 p,vec3 n,float depth) {
    vec3 point=SPGiAtlasPart(ctUniform,ctSpawned,probe,0).xyz;
    vec3 delta=point-p;
    float tangentDistance=length(delta-n*dot(delta,n));
    vec4 geometricRisk=texelFetch(atlas,ivec2(probe%spGrid.x,probe/spGrid.x),0);
    float inverseDistance=geometricRisk.x;
    float risk=smoothstep(ctParameters.y,ctParameters.z,tangentDistance*inverseDistance);
    if(ctGuide.w>.5) {
        // Parallax alone is zero exactly at a probe source, even when that
        // probe contains genuine close occlusion. Its near-hit fraction is a
        // geometry-only risk floor; it includes dark/back-facing/cache-unknown
        // geometry and retains the complete ray budget as its denominator.
        // Taper this floor continuously over the local world-space band. The
        // existing parallax term still handles extrapolation farther away.
        float support=1.0-smoothstep(.15*ctParameters.x,ctParameters.x,tangentDistance);
        risk=max(risk,clamp(geometricRisk.y,0.0,1.0)*support);
    }
    if(ctOptions.z!=0) {
        // A view-depth tolerance can merge the ceiling and a parallel trim
        // underside only 3 cm below it. Curvature explains tangent-plane error
        // on a smooth surface; unexplained error above pixel precision asks
        // for new receiver rays instead of discarding a dark measurement.
        vec4 nd=SPGiAtlasPart(ctUniform,ctSpawned,probe,1);
        if(nd.w>0&&dot(nd.xyz,nd.xyz)>.5) {
            vec3 pn=normalize(nd.xyz);float alignment=clamp(dot(n,pn),-1.0,1.0);
            float plane=max(abs(dot(delta,n)),abs(dot(delta,pn)));
            float curvatureAllowance=1.5*length(delta)*sqrt(max(0.0,.5*(1.0-alignment)));
            float clipDepth=min(abs((spViewProjection*vec4(p,1)).w),abs((spViewProjection*vec4(point,1)).w));
            float pixelWorld=2.0*clipDepth*max(abs(spInverseProjection[0][0])/float(spScreen.x),
                abs(spInverseProjection[1][1])/float(spScreen.y));
            float tolerance=max(8.0*ctFilter.w,.5*pixelWorld);
            risk=max(risk,smoothstep(tolerance,2.0*tolerance,max(0.0,plane-curvatureAllowance)));
        }
    }
    return vec4(risk,0,0,1);
}
float CTReceiverScreenRisk(ivec2 pixel,vec3 p,vec3 n) {
    if(ctGuide.z<.5)return 0;
    float clipDepth=abs((spViewProjection*vec4(p,1)).w);
    float pixelWorld=2.0*clipDepth*max(abs(spInverseProjection[0][0])/float(spScreen.x),
        abs(spInverseProjection[1][1])/float(spScreen.y));
    float tolerance=max(8.0*ctFilter.w,.5*pixelWorld),risk=0;
    const ivec2 directions[8]=ivec2[8](ivec2(-1,0),ivec2(1,0),ivec2(0,-1),ivec2(0,1),
        ivec2(-1,-1),ivec2(1,-1),ivec2(-1,1),ivec2(1,1));
    for(int ring=0;ring<6;++ring)for(int direction=0;direction<8;++direction){
        ivec2 at=pixel+directions[direction]*(1<<ring);
        if(any(lessThan(at,ivec2(0)))||any(greaterThanEqual(at,spScreen.xy)))continue;
        vec4 view=texelFetch(ctPositions,at,0),nd=texelFetch(ctNormals,at,0);
        if(view.w<.5||nd.w<.5||!CTFinite(view.xyz)||!CTFinite(nd.xyz)||dot(nd.xyz,nd.xyz)<.5)continue;
        vec3 q=(spInverseView*vec4(view.xyz,1)).xyz,nn=normalize(nd.xyz),delta=q-p;
        float distance=length(delta);if(!CTFinite(q)||distance>=ctParameters.x)continue;
        // Faces around an ordinary convex edge lie behind both outward
        // tangent planes; they cannot obstruct either receiver hemisphere.
        if(max(dot(delta,n),dot(-delta,nn)) < -tolerance)continue;
        float alignment=clamp(dot(n,nn),-1.0,1.0),midpointPlane;
        if(alignment>-.9)midpointPlane=abs(dot(delta,normalize(n+nn)));
        else midpointPlane=max(abs(dot(delta,n)),abs(dot(delta,nn)));
        // A smooth curve's chord lies in its midpoint tangent plane. A nearby
        // unrelated visible face, corner or parallel layer cannot explain both
        // its normal discontinuity and this residual as surface curvature.
        float discontinuity=max(1.0-smoothstep(.8660254,.9659258,alignment),
            smoothstep(tolerance,2.0*tolerance,midpointPlane));
        // Blend the correction over the outer band: a narrow cutoff creates
        // a seam between probe reconstruction and the actual receiver signal.
        // The innermost 15 percent retains full contact correction.
        float proximity=1.0-smoothstep(.15*ctParameters.x,ctParameters.x,distance);
        risk=max(risk,proximity*discontinuity);
        if(risk>=1.0)return 1.0;
    }
    return risk;
}
void main() {
    ivec2 pixel=ivec2(gl_FragCoord.xy);outColor=vec4(0);
    vec4 view=texelFetch(ctPositions,pixel,0),normal=texelFetch(ctNormals,pixel,0);
    if(view.w<.5||normal.w<.5||!CTFinite(view.xyz)||!CTFinite(normal.xyz)||dot(normal.xyz,normal.xyz)<.5)return;
    vec3 p=(spInverseView*vec4(view.xyz,1)).xyz,n=normalize(normal.xyz);
    if(!CTFinite(p))return;
    if((ctOptions.w&2)!=0){outColor=vec4(1,1,0,1);return;}
    // A genuine receiver with no reconstructible probe measurement may be
    // recovered only by actual receiver rays, not by brightness-picked reuse.
    if(texelFetch(ctInput,pixel,0).a<=0) {outColor=vec4(1,1,0,1);return;}
    float depth=max(abs(view.z),.0001);vec4 adaptiveLighting[4],uniformWeights,adaptiveWeights;float uniformWeight;
    SPGiReconstructionStencil(pixel,p,n,depth,ctUniform,ctSpawned,ctRisk,true,
        adaptiveLighting,uniformWeights,adaptiveWeights,uniformWeight);
    float mass=dot(uniformWeights+adaptiveWeights,vec4(1)),riskSum=0;
    ivec2 base=SPCornerBase(pixel);
    for(int corner=0;corner<4;++corner) {
        ivec2 tile=SPCorner(base,corner);int probe=tile.x+tile.y*spGrid.x;
        riskSum+=CTProbeRisk(ctRisk,probe,p,n,depth).x*uniformWeights[corner];
        riskSum+=adaptiveLighting[corner].x*adaptiveWeights[corner];
    }
    float risk=mass>.0001?riskSum/mass:1.0;
    // Retain full correction in the geometric core, but avoid compressing
    // a weak risk field into a narrow full-RGB estimator switch at its fringe.
    float mask=ctGuide.w>.5?smoothstep(0.0,4.0*ctParameters.w,risk):
        smoothstep(.5*ctParameters.w,1.5*ctParameters.w,risk);
    if(mask<.999)mask=max(mask,CTReceiverScreenRisk(pixel,p,n));
    outColor=vec4(mask,risk,mass,1);
}
)GLSL";
}

inline std::string ContactBudgetScreenRiskShader(bool classifier) {
    auto source=ContactMaskShader();const auto main=source.rfind("void main() {");
    if(main==std::string::npos)throw std::logic_error("Contact risk main anchor changed");
    if(classifier) {
        source.resize(main);
        source+=R"GLSL(
void main() {
    ivec2 pixel=ivec2(gl_FragCoord.xy)*2;outColor=vec4(0);
    vec4 view=texelFetch(ctPositions,pixel,0),normal=texelFetch(ctNormals,pixel,0);
    if(view.w<.5||normal.w<.5||!CTFinite(view.xyz)||!CTFinite(normal.xyz)||dot(normal.xyz,normal.xyz)<.5)return;
    vec3 p=(spInverseView*vec4(view.xyz,1)).xyz,n=normalize(normal.xyz);
    outColor=vec4(CTReceiverScreenRisk(pixel,p,n),0,0,1);
}
)GLSL";
        return source;
    }
    source.insert(main,R"GLSL(
layout(binding=1) uniform sampler2D ctBudgetRisk;
float CTSharedScreenRisk(ivec2 pixel,vec3 p,vec3 n) {
    ivec2 cell=pixel/2,representative=cell*2;
    vec4 view=texelFetch(ctPositions,representative,0),normal=texelFetch(ctNormals,representative,0);
    if(view.w<.5||normal.w<.5||!CTFinite(view.xyz)||!CTFinite(normal.xyz)||dot(normal.xyz,normal.xyz)<.5)
        return CTReceiverScreenRisk(pixel,p,n);
    vec3 q=(spInverseView*vec4(view.xyz,1)).xyz,nn=normalize(normal.xyz);
    float tolerance=max(8.0*ctFilter.w,.0005*abs(view.z));
    // Never share across a silhouette, another layer or a sharp normal change.
    if(dot(n,nn)<.9659258||abs(dot(q-p,normalize(n+nn)))>tolerance)
        return CTReceiverScreenRisk(pixel,p,n);
    ivec2 size=textureSize(ctBudgetRisk,0),side=ivec2((pixel.x&1)==0?-1:1,(pixel.y&1)==0?-1:1);
    float risk=0;
    for(int y=0;y<2;++y)for(int x=0;x<2;++x)
        risk=max(risk,texelFetch(ctBudgetRisk,clamp(cell+ivec2(x,y)*side,ivec2(0),size-1),0).x);
    return risk;
}
)GLSL");
    const std::string from="mask=max(mask,CTReceiverScreenRisk(pixel,p,n));";
    const auto at=source.find(from);if(at==std::string::npos)throw std::logic_error("Contact risk evaluation anchor changed");
    source.replace(at,from.size(),"mask=max(mask,CTSharedScreenRisk(pixel,p,n));");return source;
}

// Extend only geometric risk, never radiance. A one-pixel soft fringe fills
// sparse screen-stencil holes without lowering the contact core or carrying
// lighting across a face. Every added receiver still traces its own rays.
inline std::string ContactMaskExpandShader() {
    return std::string("#version 450 core\n")+ScreenProbeTraceDetail::ConstantsGLSL()+ContactConstantsGLSL()+R"GLSL(
in vec2 vUV;layout(location=0) out vec4 outColor;
layout(binding=0) uniform sampler2D cmRawMask;
layout(binding=2) uniform sampler2D cmPositions;
layout(binding=3) uniform sampler2D cmNormals;
layout(binding=4) uniform sampler2D cmAlbedo;
bool CMFinite(vec3 value){return !any(isnan(value))&&!any(isinf(value));}
bool CMGeometry(vec4 p,vec4 n){return p.w>.5&&p.z<0&&n.w>.5&&CMFinite(p.xyz)&&
    CMFinite(n.xyz)&&dot(n.xyz,n.xyz)>.5;}
void main() {
    ivec2 pixel=ivec2(gl_FragCoord.xy);vec4 current=texelFetch(cmRawMask,pixel,0);
    outColor=current;
    vec4 view=texelFetch(cmPositions,pixel,0),nd=texelFetch(cmNormals,pixel,0);
    vec3 world=(spInverseView*vec4(view.xyz,1)).xyz;
    // Keep derivatives defined on complete quads, including preserved cores.
    float footprint=max(length(dFdx(world)),length(dFdy(world)));
    if(current.x>=.5||!CMGeometry(view,nd)||!CMFinite(world)||isnan(footprint)||isinf(footprint))return;
    vec3 n=normalize(nd.xyz),albedo=texelFetch(cmAlbedo,pixel,0).rgb;
    if(!CMFinite(albedo))return;
    float depth=max(-view.z,.0001),epsilon=max(ctFilter.w,.000001);
    float pixelWorld=clamp(footprint,.000001,max(8.0*epsilon,.025*depth));
    float planeTolerance=max(8.0*epsilon,min(.0005*depth,.2*pixelWorld));
    for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x) {
        if(x==0&&y==0)continue;ivec2 offset=ivec2(x,y),at=pixel+offset;
        if(any(lessThan(at,ivec2(0)))||any(greaterThanEqual(at,spScreen.xy)))continue;
        float risk=texelFetch(cmRawMask,at,0).x*(x==0||y==0?.5:.25);
        if(!(risk>outColor.x)||isnan(risk)||isinf(risk))continue;
        vec4 position=texelFetch(cmPositions,at,0),normalDepth=texelFetch(cmNormals,at,0);
        if(!CMGeometry(position,normalDepth))continue;
        vec3 neighborWorld=(spInverseView*vec4(position.xyz,1)).xyz,neighborNormal=normalize(normalDepth.xyz);
        if(!CMFinite(neighborWorld)||dot(n,neighborNormal)<ctFilter.z)continue;
        vec3 delta=neighborWorld-world,neighborAlbedo=texelFetch(cmAlbedo,at,0).rgb;
        float plane=abs(dot(delta,normalize(n+neighborNormal)));
        float worldLimit=1.5*pixelWorld*(length(vec2(offset))+.75);
        vec3 albedoDelta=abs(neighborAlbedo-albedo);
        if(plane>planeTolerance||length(delta)>max(worldLimit,2.0*planeTolerance)||!CMFinite(neighborAlbedo)||
            max(albedoDelta.x,max(albedoDelta.y,albedoDelta.z))>ctFilter.y)continue;
        outColor.x=risk;
    }
}
)GLSL";
}

// Same source anchoring and outward clearance contract as ProbeOriginShader.
// This is evaluated only for receivers that actually need local retracing.
// Never jump through a close face or skip a first backface to find light.
inline std::string ContactReceiverOriginGLSL() {return R"GLSL(
layout(std140,binding=15) uniform OriginSafetyData {vec4 poSafety;};
bool CTFinite(vec3 value){return !any(isnan(value))&&!any(isinf(value));}
bool CTReceiverOrigin(vec3 position,vec3 normal,out vec3 origin) {
    origin=position;if(!CTFinite(position)||!CTFinite(normal)||scGeometry.z<=0)return false;
    vec4 clip=spViewProjection*vec4(position,1);if(!(clip.w>0)||!CTFinite(clip.xyz))return false;
    vec3 ndc=clip.xyz/clip.w;
    vec4 corner=spInverseProjection*vec4(ndc.xy+1.0/vec2(spScreen.xy),ndc.z,1);
    if(abs(corner.w)<1e-10)return false;corner/=corner.w;
    vec3 cornerWorld=(spInverseView*vec4(corner.xyz,1)).xyz;if(!CTFinite(cornerWorld))return false;
    float footprint=length(cornerWorld-position);
    float requestedBias=max(spTraceParams.z,2.0*abs(dot(cornerWorld-position,normal)));
    if(isnan(requestedBias)||isinf(requestedBias)||requestedBias<0)return false;
    float coordinateScale=max(1.0,max(abs(position.x),max(abs(position.y),abs(position.z))));
    float epsilon=max(poSafety.y,8.0*1.1920928955078125e-7*coordinateScale);
    vec4 nearView=spInverseProjection*vec4(ndc.xy,-1,1),farView=spInverseProjection*vec4(ndc.xy,1,1);
    if(abs(nearView.w)<1e-10||abs(farView.w)<1e-10)return false;
    vec3 nearWorld=(spInverseView*vec4(nearView.xyz/nearView.w,1)).xyz;
    vec3 farWorld=(spInverseView*vec4(farView.xyz/farView.w,1)).xyz;
    vec3 primaryDirection=farWorld-nearWorld;
    if(!CTFinite(primaryDirection)||dot(primaryDirection,primaryDirection)<1e-12)return false;
    primaryDirection=normalize(primaryDirection);
    vec3 depthLowerDelta=vec3(0),depthUpperDelta=vec3(0);float deviceDepth=ndc.z*.5+.5;
    if(poSafety.z<.5&&deviceDepth>=0&&deviceDepth<=1) {
        uint depthBits=floatBitsToUint(deviceDepth)&0x7fffffffu;
        float lower=uintBitsToFloat(depthBits>0u?depthBits-1u:0u),upper=uintBitsToFloat(min(depthBits+1u,0x3f800000u));
        vec4 lowerView=spInverseProjection*vec4(ndc.xy,lower*2.0-1.0,1),upperView=spInverseProjection*vec4(ndc.xy,upper*2.0-1.0,1);
        if(abs(lowerView.w)<1e-10||abs(upperView.w)<1e-10)return false;
        vec3 lowerWorld=(spInverseView*vec4(lowerView.xyz/lowerView.w,1)).xyz;
        vec3 upperWorld=(spInverseView*vec4(upperView.xyz/upperView.w,1)).xyz;
        if(!CTFinite(lowerWorld)||!CTFinite(upperWorld))return false;
        depthLowerDelta=lowerWorld-position;depthUpperDelta=upperWorld-position;
    }
    float baseNormalTolerance=max(8.0*epsilon,max(.01*footprint,32.0*1.1920928955078125e-7*coordinateScale));
    float storedNormalTolerance=baseNormalTolerance+max(abs(dot(depthLowerDelta,normal)),abs(dot(depthUpperDelta,normal)));
    float viewFacing=abs(dot(normal,-primaryDirection));
    float alongAcceptance=min(storedNormalTolerance/max(viewFacing,.02),max(baseNormalTolerance,.25*footprint));
    float anchorWindow=2.0*alongAcceptance,anchorDistance=2.0*anchorWindow;
    vec3 anchorStart=position-primaryDirection*anchorWindow,sourceNormal;int sourceTriangle;bool exhausted;
    if(!CT_ORIGIN_INTERSECT(anchorStart,primaryDirection,anchorDistance,sourceNormal,sourceTriangle,exhausted,false)||exhausted)return false;
    vec3 anchor=anchorStart+primaryDirection*anchorDistance,anchorDelta=anchor-position;
    float alignment=dot(sourceNormal,normal);
    float sourceTolerance=baseNormalTolerance+max(abs(dot(depthLowerDelta,sourceNormal)),abs(dot(depthUpperDelta,sourceNormal)));
    if(!CTFinite(anchor)||length(anchorDelta)>alongAcceptance||abs(dot(anchorDelta,sourceNormal))>sourceTolerance||
        abs(dot(anchorDelta,normal))>storedNormalTolerance||alignment<=.5||dot(sourceNormal,-primaryDirection)<=1e-5)return false;
    float distance=requestedBias+epsilon;vec3 hitNormal;int triangle;
    bool found=CT_ORIGIN_INTERSECT(anchor,normal,distance,hitNormal,triangle,exhausted,false);
    if(exhausted)return false;float bias=requestedBias;
    if(found)bias=min(requestedBias,max(0.0,distance-epsilon)*clamp(poSafety.x,.05,.95));
    if(bias*alignment<epsilon)return false;
    origin=anchor+normal*bias;return CTFinite(origin);
}
)GLSL";}

// Optional full-resolution source-origin prepass. It uses the same receiver
// selection and exact source/clearance queries as the fused Gather. The target
// must be RGBA32F: xyz stores the uncompressed origin; w is one only after the
// complete safety contract succeeds. A failed launch remains unknown.
inline std::string ContactOriginShader(bool freshSampling=false,bool hardwareOrigins=false,
    bool reuseOriginSoftwareCertification=false,bool reuseClosestBounds=false,bool fastGeometryAddressing=false) {
    return std::string("#version 450 core\n#define CT_FRESH_SAMPLING ")+(freshSampling?"1\n":"0\n")+
        (hardwareOrigins?(reuseOriginSoftwareCertification?"#define CT_ORIGIN_INTERSECT SCNativeOriginIntersectReusingCertification\n":
            "#define CT_ORIGIN_INTERSECT SCNativeOriginIntersect\n"):"#define CT_ORIGIN_INTERSECT SCIntersect\n")+
        ScreenProbeTraceDetail::ConstantsGLSL()+ContactConstantsGLSL()+ContactFreshSamplingGLSL()+
        SurfaceCacheDataGLSL()+SoftwareBvhVisibilityGLSL(9,reuseClosestBounds,fastGeometryAddressing)+
        (hardwareOrigins?NativeSurfaceCacheIntersectionGLSL():std::string{})+ContactReceiverOriginGLSL()+R"GLSL(
in vec2 vUV;layout(location=0) out vec4 outColor;
layout(binding=1) uniform sampler2D ctMask;
layout(binding=2) uniform sampler2D ctPositions;
layout(binding=3) uniform sampler2D ctNormals;
void main() {
    ivec2 pixel=ivec2(gl_FragCoord.xy);outColor=vec4(0);
    float originalMask=texelFetch(ctMask,pixel,0).x,mask=CTEffectiveMask(originalMask,pixel);
    if(mask<=.000001)return;
    vec4 view=texelFetch(ctPositions,pixel,0),normal=texelFetch(ctNormals,pixel,0);
    if(view.w<.5||normal.w<.5||!CTFinite(view.xyz)||!CTFinite(normal.xyz)||dot(normal.xyz,normal.xyz)<.5)return;
    vec3 p=(spInverseView*vec4(view.xyz,1)).xyz,n=normalize(normal.xyz),origin;
    if(!CTReceiverOrigin(p,n,origin))return;
    outColor=vec4(origin,1);
}
)GLSL";
}

inline std::string ContactGatherShader(bool diagnostics=false,bool freshSampling=false,bool hardwareIntersection=false,bool hardwareOrigins=false,
    bool reuseOriginSoftwareCertification=false,bool reuseClosestBounds=false,bool fastGeometryAddressing=false,
    bool splitReceiverOrigins=false,bool reuseLightingLookup=false,unsigned traceDiagnosticPage=0,bool fastLightingAddressing=false) {
    auto geometry=GeometryGLSL(false,hardwareIntersection||hardwareOrigins,reuseClosestBounds,fastGeometryAddressing);
    if(diagnostics&&traceDiagnosticPage>0)geometry=InstrumentSurfaceCacheTraceDiagnosticsGLSL(geometry);
    auto source=std::string("#version 450 core\n#define CT_SAMPLING_DIAGNOSTICS ")+(diagnostics?"1\n":"0\n")+
        "#define CT_FRESH_SAMPLING "+(freshSampling?"1\n":"0\n")+
        (hardwareIntersection?"#define CT_INTERSECT SCNativeIntersect\n":"#define CT_INTERSECT SCIntersect\n")+
        (hardwareOrigins?(reuseOriginSoftwareCertification?"#define CT_ORIGIN_INTERSECT SCNativeOriginIntersectReusingCertification\n":
            "#define CT_ORIGIN_INTERSECT SCNativeOriginIntersect\n"):"#define CT_ORIGIN_INTERSECT SCIntersect\n")+
        ScreenProbeTraceDetail::ConstantsGLSL()+ContactConstantsGLSL()+ContactFreshSamplingGLSL()+
        LookupGLSL(reuseLightingLookup,fastLightingAddressing)+geometry+ContactReceiverOriginGLSL()+R"GLSL(
in vec2 vUV;layout(location=0) out vec4 outColor;
layout(binding=0) uniform sampler2D ctInput;
layout(binding=1) uniform sampler2D ctMask;
layout(binding=2) uniform sampler2D ctPositions;
layout(binding=3) uniform sampler2D ctNormals;
layout(binding=4) uniform sampler2D ctAlbedo;
uint CTHash(uint h){h=(h^(h>>16))*0x7feb352du;h=(h^(h>>15))*0x846ca68bu;return h^(h>>16);}
void main() {
    ivec2 pixel=ivec2(gl_FragCoord.xy);vec4 inputValue=texelFetch(ctInput,pixel,0);
#if CT_SAMPLING_DIAGNOSTICS
    float selected=0.0;
#if CT_FRESH_SAMPLING
    if((ctOptions.w&16)!=0) {
        float fresh=texelFetch(ctFreshSampling,pixel,0).x;
        if(!isnan(fresh)&&!isinf(fresh)&&fresh>.000001)selected=1.0;
    }
#endif
    // The diagnostic pass independently executes the identical receiver
    // launch and real ray loop. Planned classifier budgets are not ray counts.
    outColor=vec4(selected,0,float(ctOptions.x),0);
#else
    outColor=inputValue;
#endif
    float originalMask=texelFetch(ctMask,pixel,0).x,mask=CTEffectiveMask(originalMask,pixel);
    if(mask<=.000001)return;
    vec4 view=texelFetch(ctPositions,pixel,0),normal=texelFetch(ctNormals,pixel,0);
    if(view.w<.5||normal.w<.5||!CTFinite(view.xyz)||!CTFinite(normal.xyz)||dot(normal.xyz,normal.xyz)<.5){
#if !CT_SAMPLING_DIAGNOSTICS
        outColor=vec4(0);
#endif
        return;
    }
    vec3 p=(spInverseView*vec4(view.xyz,1)).xyz,n=normalize(normal.xyz),origin;
    // A failed receiver launch stays unknown. Neither the old probe signal nor
    // temporal history can certify a light sample from an invalid receiver.
    if(!CTReceiverOrigin(p,n,origin)){
#if !CT_SAMPLING_DIAGNOSTICS
        outColor=vec4(0,0,0,CTGuided()?-1.0:0.0);
#endif
        return;
    }
    vec3 axis=abs(n.z)<.99?vec3(0,0,1):vec3(0,1,0),t=normalize(cross(axis,n)),b=cross(n,t);
    uint seed=CTHash(uint(pixel.x)+uint(pixel.y)*uint(spScreen.x));
    uint phaseSeed=uint(spTracing.w)>>2;
    if(ctSampling.z!=0u)phaseSeed=ctSampling.x;
    seed=CTHash(seed^CTHash(phaseSeed+0x9e3779b9u));
#if CT_SAMPLING_DIAGNOSTICS
    int actualTraced=0;
#endif
#if CT_FRESH_SAMPLING
    vec3 sum=vec3(0);int readable=0,count=CTActualRayCount(pixel,originalMask);float inverseHitDistance=0;
    for(int batch=0;batch<4;++batch) {
      if(batch*ctOptions.x>=count)break;
      // The first batch retains every original direction bit for bit. Extra
      // batches stratify the same cosine hemisphere with independent pixel
      // scrambles rather than changing the base strata or tracing duplicates.
      uint batchSeed=batch==0?seed:CTHash(seed^CTHash(uint(batch)+0x3c6ef372u));
      float azimuthShift=float(CTHash(batchSeed^0x68bc21ebu)&0x00ffffffu)/16777216.0;
#define CT_SAMPLE_SEED batchSeed
#else
    float azimuthShift=float(CTHash(seed^0x68bc21ebu)&0x00ffffffu)/16777216.0;
    vec3 sum=vec3(0);int readable=0,count=CTBaseRayCount(originalMask);float inverseHitDistance=0;
#define CT_SAMPLE_SEED seed
#endif
      for(int ray=0;ray<32;++ray) {
#if CT_FRESH_SAMPLING
        if(ray>=ctOptions.x)break;
#else
        if(ray>=count)break;
#endif
        float radialJitter=(float(CTHash(CT_SAMPLE_SEED^CTHash(uint(ray)))&0x0007ffffu)+.5)/524288.0;
        float azimuth=fract(float(bitfieldReverse(uint(ray))>>8)/16777216.0+azimuthShift);
        if(ctSampling.z!=0u) {
            // Fixed-point R2 shifts advance both coordinates independently of
            // the probe's short direction cycle. Pixel scrambling stays fixed;
            // only successfully committed GI frames advance the sequence.
            uint radialPhase=ctSampling.y*0xc13fa9a9u,azimuthPhase=ctSampling.y*0x91e10da5u;
            radialJitter=(float((CTHash(CT_SAMPLE_SEED^CTHash(uint(ray)))+radialPhase)>>14)+.5)/262144.0;
            azimuth=float((bitfieldReverse(uint(ray))+CTHash(CT_SAMPLE_SEED^0x68bc21ebu)+azimuthPhase)>>8)/16777216.0;
        }
#if CT_FRESH_SAMPLING
        float radial=(float(ray)+radialJitter)/float(ctOptions.x);
#else
        float radial=(float(ray)+radialJitter)/float(count);
#endif
        float phi=6.283185307179586*azimuth;
        vec3 direction=t*(sqrt(radial)*cos(phi))+b*(sqrt(radial)*sin(phi))+n*sqrt(max(0.0,1.0-radial));
        float distance=scParameters.x;vec3 hitNormal;int triangle;bool exhausted;
#if CT_SAMPLING_DIAGNOSTICS
        ++actualTraced;
#endif
        bool found=CT_INTERSECT(origin,direction,distance,hitNormal,triangle,exhausted,false);
        if(exhausted)continue;
        if(!found){++readable;continue;} // completed empty ray is known black
        // Geometry, including black/back-facing/cache-unknown hits, guides the
        // filter independently of radiance and stays on the full ray budget.
        if(CTGuided()&&distance>0&&!isnan(distance)&&!isinf(distance))inverseHitDistance+=1.0/max(distance,.00001);
        if(dot(hitNormal,-direction)<=0){++readable;continue;} // one-sided backface is known black
        int owner=SCTriangleOwner(triangle);vec3 radiance;
        if(owner>=0&&SCSampleLightingObject(origin+direction*distance,hitNormal,owner,radiance)&&CTFinite(radiance)) {
            sum+=max(radiance,vec3(0));++readable;
        }
      }
#if CT_FRESH_SAMPLING
    }
#endif
#if CT_SAMPLING_DIAGNOSTICS
    outColor=vec4(selected,float(actualTraced),float(ctOptions.x),float(readable));
    return;
#endif
    // Unknown samples remain in the entire real-ray budget. Do not normalize
    // by readable, ignore black hits, or choose whichever estimate is brighter.
    vec3 rho=max(texelFetch(ctAlbedo,pixel,0).rgb,vec3(0));
    vec4 measured=vec4(rho*sum*(scParameters.z/float(count)),float(readable)/float(count));
    if(CTGuided()) {
        float harmonic=inverseHitDistance>0?min(scParameters.x,float(count)/inverseHitDistance):scParameters.x;
        // Internal only: an exact integer readable count plus bounded distance
        // fraction. No bit-cast NaNs, and public textures decode true coverage.
        outColor=vec4(measured.rgb,2.0*float(readable)+harmonic/(1.0+harmonic));
    } else outColor=mix(inputValue,measured,clamp(mask,0.0,1.0));
}
)GLSL";
    if(splitReceiverOrigins) {
        const std::string sampler="layout(binding=4) uniform sampler2D ctAlbedo;";
        source.replace(source.find(sampler),sampler.size(),sampler+"\nlayout(binding=5) uniform sampler2D ctReceiverOrigins;");
        const std::string fused="if(!CTReceiverOrigin(p,n,origin)){";
        source.replace(source.find(fused),fused.size(),
            "vec4 storedOrigin=texelFetch(ctReceiverOrigins,pixel,0);origin=storedOrigin.xyz;\n    if(storedOrigin.w<.5){");
    }
    // Replay-only local counters. The original page-zero diagnostic and every
    // timed shader keep their original ray loop without counter writes. Pages
    // 2/3 report source/clearance helper work; 4/5 report real-ray helper work.
    // A split-origin diagnostic repeats the same safety query only in this
    // untimed replay, then traces from the actual prepass's stored origin.
    if(diagnostics&&traceDiagnosticPage>0) {
        const auto replace=[&](const std::string& before,const std::string& after) {
            source.replace(source.find(before),before.size(),after);
        };
        replace("void main() {",R"GLSL(
vec4 ctTraceTerminal=vec4(0),ctTracePixelState=vec4(0);
uvec4 ctTraceOriginLow=uvec4(0),ctTraceOriginHigh=uvec4(0);
void CTTraceReplayMain() {
)GLSL");
        replace("if(mask<=.000001)return;", "if(mask<=.000001)return;ctTracePixelState.x=1.0;");
        replace("vec3 p=(spInverseView*vec4(view.xyz,1)).xyz,n=normalize(normal.xyz),origin;",
            "ctTracePixelState.y=1.0;\n    vec3 p=(spInverseView*vec4(view.xyz,1)).xyz,n=normalize(normal.xyz),origin;");
        const std::string capture="ctTraceOriginLow=scTraceDiag[0];ctTraceOriginHigh=scTraceDiag[1];SCTraceDiagReset();\n    ";
        if(splitReceiverOrigins) {
            replace("vec4 storedOrigin=texelFetch(ctReceiverOrigins,pixel,0);origin=storedOrigin.xyz;",
                "vec3 replayOrigin;bool replayLaunched=CTReceiverOrigin(p,n,replayOrigin);\n    "+capture+
                "vec4 storedOrigin=texelFetch(ctReceiverOrigins,pixel,0);origin=storedOrigin.xyz;");
        } else {
            replace("if(!CTReceiverOrigin(p,n,origin)){",
                "bool launched=CTReceiverOrigin(p,n,origin);\n    "+capture+"if(!launched){");
        }
        replace("vec3 axis=abs(n.z)<.99?", "ctTracePixelState.z=1.0;\n    vec3 axis=abs(n.z)<.99?");
        replace("if(exhausted)continue;", "if(exhausted){ctTraceTerminal.x+=1.0;continue;}");
        replace("if(!found){++readable;continue;}", "if(!found){ctTraceTerminal.y+=1.0;++readable;continue;}");
        replace("if(dot(hitNormal,-direction)<=0){++readable;continue;}",
            "if(dot(hitNormal,-direction)<=0){ctTraceTerminal.z+=1.0;++readable;continue;}");
        replace("sum+=max(radiance,vec3(0));++readable;", "sum+=max(radiance,vec3(0));++readable;ctTraceTerminal.w+=1.0;");
        source+="\nvoid main(){SCTraceDiagReset();CTTraceReplayMain();outColor=";
        switch(traceDiagnosticPage) {
        case 1:source+="ctTraceTerminal";break;
        case 2:source+="vec4(ctTraceOriginLow)";break;
        case 3:source+="vec4(ctTraceOriginHigh)";break;
        case 4:source+="vec4(scTraceDiag[0])";break;
        case 5:source+="vec4(scTraceDiag[1])";break;
        default:source+="ctTracePixelState";break;
        }
        source+=";}\n";
    }
    return source;
}

// Diagnostic integers are bounded before packing. Two twelve-bit integers
// occupy at most 2^24-1, exactly representable by RGBA32F; this is arithmetic
// packing, with no NaN bit patterns and no truncated or clamped counter.
inline std::string ContactTraceCounterPackingGLSL() {return R"GLSL(
float CTTracePack(vec2 values) {
    if(any(isnan(values))||any(isinf(values))||any(lessThan(values,vec2(0)))||
        any(greaterThanEqual(values,vec2(4096)))||any(notEqual(values,floor(values))))return -1.0;
    return values.x+4096.0*values.y;
}
vec4 CTTracePackTwoPages(vec4 first,vec4 second) {
    return vec4(CTTracePack(first.xy),CTTracePack(first.zw),CTTracePack(second.xy),CTTracePack(second.zw));
}
)GLSL";}

// Single-fragment, four-attachment diagnostic. Attachment zero retains the
// original sampling RGBA. The other attachments losslessly contain pages
// (1,2), (3,4), (5,6), with each page represented by its packed XY/ZW pairs.
// All counters are observable in this one invocation, including early exits.
// This independent generator never changes a timed shader or the old pages.
inline std::string ContactGatherTraceMrtShader(bool freshSampling=false,bool hardwareIntersection=false,bool hardwareOrigins=false,
    bool reuseOriginSoftwareCertification=false,bool reuseClosestBounds=false,bool fastGeometryAddressing=false,
    bool splitReceiverOrigins=false,bool reuseLightingLookup=false,bool fastLightingAddressing=false) {
    auto source=ContactGatherShader(true,freshSampling,hardwareIntersection,hardwareOrigins,reuseOriginSoftwareCertification,
        reuseClosestBounds,fastGeometryAddressing,splitReceiverOrigins,reuseLightingLookup,6,fastLightingAddressing);
    const std::string original="void main(){SCTraceDiagReset();CTTraceReplayMain();outColor=ctTracePixelState;}";
    const auto at=source.find(original);
    if(at==std::string::npos)throw std::logic_error("Contact MRT diagnostic wrapper source anchor missing");
    source.replace(at,original.size(),ContactTraceCounterPackingGLSL()+R"GLSL(
layout(location=1) out vec4 ctTracePacked12;
layout(location=2) out vec4 ctTracePacked34;
layout(location=3) out vec4 ctTracePacked56;
void main() {
    outColor=vec4(0);ctTracePacked12=vec4(-1);ctTracePacked34=vec4(-1);ctTracePacked56=vec4(-1);
    ctTraceTerminal=vec4(0);ctTracePixelState=vec4(0);ctTraceOriginLow=uvec4(0);ctTraceOriginHigh=uvec4(0);
    SCTraceDiagReset();CTTraceReplayMain();
    ctTracePacked12=CTTracePackTwoPages(ctTraceTerminal,vec4(ctTraceOriginLow));
    ctTracePacked34=CTTracePackTwoPages(vec4(ctTraceOriginHigh),vec4(scTraceDiag[0]));
    ctTracePacked56=CTTracePackTwoPages(vec4(scTraceDiag[1]),ctTracePixelState);
}
)GLSL");
    return source;
}

inline std::string ContactMeasurementResolveShader(bool freshSampling=false) {
    return std::string("#version 450 core\n#define CT_FRESH_SAMPLING ")+(freshSampling?"1\n":"0\n")+
        ContactConstantsGLSL()+ContactFreshSamplingGLSL()+R"GLSL(
in vec2 vUV;layout(location=0) out vec4 outColor;
layout(binding=0) uniform sampler2D cmMeasurements;
layout(binding=1) uniform sampler2D cmMask;
layout(binding=5) uniform sampler2D cmBase;
void main(){ivec2 p=ivec2(gl_FragCoord.xy);
    float originalMask=texelFetch(cmMask,p,0).x,mask=CTEffectiveMask(originalMask,p);
    outColor=CTResolveMeasurement(texelFetch(cmBase,p,0),texelFetch(cmMeasurements,p,0),mask,p,originalMask);}
)GLSL";
}

// A strictly local current-frame filter for the actual receiver measurements.
// It retains center readability, accepts known black, and never changes pixels
// outside the retrace mask (including explicitly classified fresh receivers).
// Symmetric uniform support preserves affine
// gradients on a continuous plane; varying mask/hit support is not guaranteed
// to do so. Material/silhouette/plane discontinuities stop taps.
inline std::string ContactFilterShader(bool freshSampling=false) {
    return std::string("#version 450 core\n#define CT_FRESH_SAMPLING ")+(freshSampling?"1\n":"0\n")+
        ScreenProbeTraceDetail::ConstantsGLSL()+ContactConstantsGLSL()+ContactFreshSamplingGLSL()+R"GLSL(
in vec2 vUV;layout(location=0) out vec4 outColor;
layout(binding=0) uniform sampler2D ctUnfiltered;
layout(binding=1) uniform sampler2D ctMask;
layout(binding=2) uniform sampler2D ctPositions;
layout(binding=3) uniform sampler2D ctNormals;
layout(binding=4) uniform sampler2D ctAlbedo;
layout(binding=5) uniform sampler2D ctBase;
layout(binding=6) uniform sampler2D ctMeasurements;
bool CFFinite(vec3 value){return !any(isnan(value))&&!any(isinf(value));}
bool CFGeometry(vec4 p,vec4 n) {
    return p.w>.5&&p.z<0&&n.w>.5&&CFFinite(p.xyz)&&CFFinite(n.xyz)&&dot(n.xyz,n.xyz)>.5;
}
void main() {
    ivec2 pixel=ivec2(gl_FragCoord.xy);vec4 current=texelFetch(ctUnfiltered,pixel,0);
    bool guided=CTGuided(),finalPass=(ctOptions.w&4)!=0;
    float originalMask=texelFetch(ctMask,pixel,0).x,mask=CTEffectiveMask(originalMask,pixel);
    vec4 base=vec4(0),measurement=vec4(0);
    if(guided){base=texelFetch(ctBase,pixel,0);measurement=texelFetch(ctMeasurements,pixel,0);
        current.a=mask>.000001?CTResolveMeasurement(base,measurement,mask,pixel,originalMask).a:base.a;}
    outColor=guided&&finalPass?CTResolveMeasurement(base,vec4(current.rgb,measurement.a),mask,pixel,originalMask):current;
    vec4 view=texelFetch(ctPositions,pixel,0),nd=texelFetch(ctNormals,pixel,0);
    vec3 world=(spInverseView*vec4(view.xyz,1)).xyz;
    // Derivatives precede per-pixel early exits, so edge quads are defined.
    float footprint=max(length(dFdx(world)),length(dFdy(world)));
    if(mask<=.000001||ctFilter.x<=0||!(current.a>0)||!CFFinite(current.rgb)||
        !CFGeometry(view,nd)||!CFFinite(world)||isnan(footprint)||isinf(footprint))return;
    if(guided&&CTMeasurementCoverage(measurement.a,pixel,originalMask)<=0)return;
    vec3 n=normalize(nd.xyz),albedo=texelFetch(ctAlbedo,pixel,0).rgb;
    if(!CFFinite(albedo))return;
    int radius=clamp(ctOptions.y,1,guided?4:2);
    float depth=max(-view.z,.0001),epsilon=max(ctFilter.w,.000001);
    float pixelWorld=clamp(footprint,.000001,max(8.0*epsilon,.025*depth));
    // Tight symmetric-plane tolerances admit smooth tangent variation but do
    // not grow to the 3cm parallel trim/ceiling separation at these distances.
    float planeTolerance=max(8.0*epsilon,min(.0005*depth,.2*pixelWorld));
    const ivec2 directions[8]=ivec2[8](ivec2(-1,0),ivec2(1,0),ivec2(0,-1),ivec2(0,1),
        ivec2(-1,-1),ivec2(1,-1),ivec2(-1,1),ivec2(1,1));
    vec3 sum=current.rgb;float mass=1;
    for(int neighbor=0;neighbor<8;++neighbor) {
        bool compatible=true;int acceptedStep=0;vec3 value=vec3(0);float normalWeight=0,plane=0,neighborMask=0,confidence=1,hitWeight=1;
        // Radius two never jumps over an intermediate invalid/material/edge
        // pixel. The full screen path must belong to the same visible surface.
        for(int step=1;step<=4;++step) {
            if(step>radius)break;
            ivec2 offset=directions[neighbor]*step,at=pixel+offset;
            if(any(lessThan(at,ivec2(0)))||any(greaterThanEqual(at,spScreen.xy))){compatible=false;break;}
            vec4 neighborValue=texelFetch(ctUnfiltered,at,0),position=texelFetch(ctPositions,at,0),normalDepth=texelFetch(ctNormals,at,0);
            float originalCandidateMask=texelFetch(ctMask,at,0).x,candidateMask=CTEffectiveMask(originalCandidateMask,at);
            vec4 neighborMeasurement=vec4(0);
            if(guided){neighborMeasurement=texelFetch(ctMeasurements,at,0);
                if(CTMeasurementCoverage(neighborMeasurement.a,at,originalCandidateMask)<=0){compatible=false;break;}
                neighborValue.a=CTResolveMeasurement(texelFetch(ctBase,at,0),neighborMeasurement,candidateMask,at,originalCandidateMask).a;}
            if(candidateMask<=.000001||!(neighborValue.a>0)||(ctSampling.w==0u&&abs(neighborValue.a-current.a)>.001)||
                !CFFinite(neighborValue.rgb)||!CFGeometry(position,normalDepth)){compatible=false;break;}
            vec3 neighborWorld=(spInverseView*vec4(position.xyz,1)).xyz,neighborNormal=normalize(normalDepth.xyz);
            vec3 delta=neighborWorld-world,neighborAlbedo=texelFetch(ctAlbedo,at,0).rgb;
            float candidateNormal=dot(n,neighborNormal);
            float candidatePlane=max(abs(dot(delta,n)),abs(dot(delta,neighborNormal)));
            if(guided)candidatePlane=abs(dot(delta,normalize(n+neighborNormal)));
            float worldLimit=1.5*pixelWorld*(length(vec2(offset))+.75);
            vec3 albedoDelta=abs(neighborAlbedo-albedo);
            if(!CFFinite(neighborWorld)||candidateNormal<ctFilter.z||candidatePlane>planeTolerance||
                length(delta)>max(worldLimit,2.0*planeTolerance)||!CFFinite(neighborAlbedo)||
                max(albedoDelta.x,max(albedoDelta.y,albedoDelta.z))>ctFilter.y){compatible=false;break;}
            // Coverage is a confidence, not a surface identity. The contact
            // mask mixes probe and receiver readability continuously; testing
            // near-exact equality fragmented this same-surface noise band.
            // Keep unknown taps rejected and the center's real alpha intact.
            float candidateConfidence=min(confidence,min(neighborValue.a,current.a)/max(neighborValue.a,current.a));
            float candidateHit=hitWeight;
            if(guided&&step>1){
                // UE clamps the neighbor hit distance by the center's before
                // computing hit parallax, so distant lighting cannot erase a
                // close contact. Include a conservative normal-bias allowance.
                float hitDistance=min(CTMeasurementDistance(measurement.a),CTMeasurementDistance(neighborMeasurement.a));
                float tangent=length(delta-n*dot(delta,n))+max(spTraceParams.z,0.0)*length(n-neighborNormal);
                float angle=tangent/max(hitDistance,.00001);
                candidateHit=min(hitWeight,1.0-smoothstep(.5*ctGuide.x,ctGuide.x,angle));
                if(candidateHit<=0){compatible=false;break;}
            }
            value=neighborValue.rgb;normalWeight=candidateNormal;plane=candidatePlane;neighborMask=candidateMask;
            confidence=candidateConfidence;hitWeight=candidateHit;acceptedStep=step;
        }
        // Keep the previous pixel-scale support when a wider stencil meets a
        // close hit or discontinuity. Only the extra reach needs hit parallax.
        if(!compatible&&!(guided&&acceptedStep>0))continue;
        vec2 offset=vec2(directions[neighbor]*(guided?acceptedStep:radius));
        int kernelRadius=guided?max(acceptedStep,1):radius;
        float weight=exp2(-dot(offset,offset)/float(kernelRadius*kernelRadius))*pow(max(normalWeight,0.0),32.0)*
            exp2(-2.0*plane*plane/(planeTolerance*planeTolerance));
        // Fade filter support along with actual retracing at mask boundaries,
        // rather than pulling mostly uncorrected probe light into the band.
        weight*=min(mask,neighborMask)/max(mask,.000001);
        if(ctSampling.w!=0u)weight*=confidence;
        weight*=hitWeight;
        sum+=value*weight;mass+=weight;
    }
    outColor.rgb=mix(current.rgb,sum/mass,clamp(ctFilter.x*(guided?1.0:mask),0.0,1.0));
    // Readability belongs to the center's actual ray budget, never neighbor mass.
    outColor.a=current.a;
    if(guided&&finalPass)outColor=CTResolveMeasurement(base,vec4(outColor.rgb,measurement.a),mask,pixel,originalMask);
}
)GLSL";
}
} // namespace Render::SurfaceCacheDetail
