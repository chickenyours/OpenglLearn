#pragma once
#include <string>
#include "screen_probe_placement_shaders.h"
#include "screen_probe_trace_shaders.h"
#include "surface_cache_shaders.h"

namespace Render::SurfaceCacheDetail {
// A probe stores E / pi, rather than receiver radiance: its albedo must be
// applied only by the final receiver. With a cosine-hemisphere PDF the Monte
// Carlo weight is exactly one, including known-black rays in the denominator.
// The compact integration atlas has spGrid.x columns and
// spGrid.y * (1 + spGrid.z) rows, in the existing logical-probe index order.
inline std::string ProbeIntegrateShader() {
    return std::string("#version 450 core\n")+ScreenProbeTraceDetail::ConstantsGLSL()+R"GLSL(
in vec2 vUV;layout(location=0) out vec4 outColor;
layout(binding=0) uniform sampler2D pgUniform;
layout(binding=1) uniform sampler2D pgSpawned;
layout(binding=7) uniform sampler2D pgRays;
vec4 PGProbePosition(int probe) {
    bool uniformProbe=probe<spTraceAtlas.z;int slot=uniformProbe?probe:probe-spTraceAtlas.z;
    int columns=spGrid.x*(uniformProbe?1:spGrid.z);
    ivec2 at=ivec2(2*(slot%columns),slot/columns);
    return uniformProbe?texelFetch(pgUniform,at,0):texelFetch(pgSpawned,at,0);
}
void main() {
    int probe=int(gl_FragCoord.x)+int(gl_FragCoord.y)*spGrid.x;
    outColor=vec4(0);if(probe>=spTraceAtlas.w||PGProbePosition(probe).w<.5)return;
    int count=spTracing.x*spTracing.x,readable=0;vec3 sum=vec3(0);
    for(int ray=0;ray<256;++ray) {
        if(ray>=count)break;int address=(probe*count+ray)*4;
        vec4 value=texelFetch(pgRays,ivec2(address%spTraceAtlas.x,address/spTraceAtlas.x),0);
        int status=int(value.w+.5);
        if(status==1||status==5||status==9||status==10||status==13) {
            if(!any(isnan(value.rgb))&&!any(isinf(value.rgb))) {
                sum+=max(value.rgb,vec3(0));++readable;
            }
        }
    }
    // Unknown coverage stays black but remains in the full sample count.
    // Dividing by readable count would systematically brighten cache holes.
    outColor=vec4(sum/float(count),float(readable)/float(count));
}
)GLSL";
}

// A bounded first-order directional fit to the existing cosine-hemisphere
// samples, not a full-sphere SH projection. Each probe occupies four adjacent
// RGBA texels: (E/pi, readability), then the RGB world-space x/y/z coefficients.
// Its irradiance evaluated along the launch normal remains exactly E/pi.
// Only the x-coefficient texel's otherwise unused alpha stores the nearest
// geometry-hit inverse distance for conservative curved-support reuse.
inline std::string ProbeDirectionalIntegrateShader() {
    return std::string("#version 450 core\n")+ScreenProbeTraceDetail::ConstantsGLSL()+R"GLSL(
in vec2 vUV;layout(location=0) out vec4 outColor;
layout(binding=0) uniform sampler2D pgUniform;
layout(binding=1) uniform sampler2D pgSpawned;
layout(binding=7) uniform sampler2D pgRays;
vec4 PGDirectionalProbePart(int probe,int part) {
    bool uniformProbe=probe<spTraceAtlas.z;int slot=uniformProbe?probe:probe-spTraceAtlas.z;
    int columns=spGrid.x*(uniformProbe?1:spGrid.z);
    ivec2 at=ivec2(2*(slot%columns)+part,slot/columns);
    return uniformProbe?texelFetch(pgUniform,at,0):texelFetch(pgSpawned,at,0);
}
bool PGDirectionalFinite(vec3 value) {return !any(isnan(value))&&!any(isinf(value));}
void main() {
    int column=int(gl_FragCoord.x),part=column%4;
    int probe=column/4+int(gl_FragCoord.y)*spGrid.x;
    outColor=vec4(0);if(probe>=spTraceAtlas.w||PGDirectionalProbePart(probe,0).w<.5)return;
    vec3 normal=PGDirectionalProbePart(probe,1).xyz;
    if(!PGDirectionalFinite(normal)||dot(normal,normal)<.5)return;
    normal=normalize(normal);
    int count=spTracing.x*spTracing.x,readable=0;
    bool collectHitSupport=part==1&&(spTracing.w&2)!=0&&(int(spRefinement.x+.5)&8)!=0;
    float nearestHitInverse=0;
    vec3 sum=vec3(0),directionSum=vec3(0),momentX=vec3(0),momentY=vec3(0),momentZ=vec3(0);
    for(int ray=0;ray<256;++ray) {
        if(ray>=count)break;int address=(probe*count+ray)*4;
        vec4 value=texelFetch(pgRays,ivec2(address%spTraceAtlas.x,address/spTraceAtlas.x),0);
        vec3 direction=texelFetch(pgRays,ivec2((address+3)%spTraceAtlas.x,(address+3)/spTraceAtlas.x),0).xyz;
        // An invalid direction cannot contribute a directional measurement.
        // Its scalar sample and denominator still obey the original contract.
        direction=PGDirectionalFinite(direction)&&dot(direction,direction)>.5?normalize(direction):normal;
        vec3 radiance=vec3(0);int status=int(value.w+.5);
        if(collectHitSupport&&(status==1||status==17||status==9||status==10||status==11||status==12)) {
            // A single certified close surface can invalidate displaced
            // reuse. Include black and cache-unknown geometry conservatively.
            float hitDistance=texelFetch(pgRays,ivec2((address+1)%spTraceAtlas.x,(address+1)/spTraceAtlas.x),0).w;
            if(hitDistance>0&&!isnan(hitDistance)&&!isinf(hitDistance))nearestHitInverse=max(nearestHitInverse,1.0/hitDistance);
        }
        if((status==1||status==5||status==9||status==10||status==13)&&PGDirectionalFinite(value.rgb)) {
            radiance=max(value.rgb,vec3(0));++readable;
        }
        sum+=radiance;directionSum+=direction;
        momentX+=radiance*direction.x;momentY+=radiance*direction.y;momentZ+=radiance*direction.z;
    }
    float inverseCount=1.0/float(count);vec3 mean=sum*inverseCount;
    if(part==0){outColor=vec4(mean,float(readable)*inverseCount);return;}
    vec3 meanDirection=directionSum*inverseCount;
    // Center with the actual finite sample mean: constant input gives zero
    // directional terms even when the jittered sample moments are imperfect.
    vec3 covarianceX=momentX*inverseCount-mean*meanDirection.x;
    vec3 covarianceY=momentY*inverseCount-mean*meanDirection.y;
    vec3 covarianceZ=momentZ*inverseCount-mean*meanDirection.z;
    vec3 covarianceNormal=covarianceX*normal.x+covarianceY*normal.y+covarianceZ*normal.z;
    // The cosine-hemisphere covariance is 1/4 in the tangent directions and
    // 1/18 along the normal. Write its inverse in world coordinates.
    vec3 bx=4.0*covarianceX+14.0*normal.x*covarianceNormal;
    vec3 by=4.0*covarianceY+14.0*normal.y*covarianceNormal;
    vec3 bz=4.0*covarianceZ+14.0*normal.z*covarianceNormal;
    vec3 normalProjection=bx*normal.x+by*normal.y+bz*normal.z;
    vec3 coefficientLength=sqrt(max(bx*bx+by*by+bz*bz,vec3(0)));
    // For g(w)=a+b.w with a=mean-(2/3)b.normal, this limit gives a>=|b|
    // per color channel. It prevents negative lobes without changing mean.
    vec3 limit=coefficientLength+(2.0/3.0)*normalProjection;
    vec3 scale=min(vec3(1),mean/max(limit,vec3(.000001)));
    bx*=scale;by*=scale;bz*=scale;
    outColor=vec4(part==1?bx:(part==2?by:bz),collectHitSupport?nearestHitInverse:0.0);
}
)GLSL";
}

// One exact occupancy mask per base tile. Bits only skip inactive reserved
// slots; every active representative retains its original position and weight.
inline std::string ProbeActiveMaskShader() {
    return std::string("#version 450 core\n")+ScreenProbeTraceDetail::ConstantsGLSL()+R"GLSL(
in vec2 vUV;layout(location=0) out vec4 outColor;
layout(binding=1) uniform sampler2D pgSpawned;
void main() {
    ivec2 tile=ivec2(gl_FragCoord.xy);int mask=0;
    for(int candidate=0;candidate<16;++candidate) {
        if(candidate>=spGrid.z)break;
        ivec2 at=ivec2((tile.x*spGrid.z+candidate)*2,tile.y);
        if(texelFetch(pgSpawned,at,0).w>.5)mask|=1<<candidate;
    }
    outColor=vec4(float(mask),0,0,1);
}
)GLSL";
}

// Four geometry-aware probe corners reconstruct the lighting field. Adaptive
// probes replace corners lacking surface coverage, as in UE's
// CalculateUpsampleInterpolationWeights (LumenScreenProbeGather.usf). This is
// the definition of the probe signal, with no image-space denoising/history.
inline std::string ProbeReconstructShader(bool coverageDiagnostic=false) {
    return std::string("#version 450 core\n")+ScreenProbeTraceDetail::ConstantsGLSL()+
        R"GLSL(
layout(binding=2) uniform sampler2D pgViewPositions;
layout(binding=3) uniform sampler2D pgWorldNormals;
layout(binding=4) uniform sampler2D pgReceiverAlbedo;
#define SPGI_SURFACE_POSITIONS pgViewPositions
layout(binding=10) uniform sampler2D pgActiveMask;
#define SPGI_ACTIVE_PROBES(tile) int(texelFetch(pgActiveMask,tile,0).x+.5)
layout(binding=11) uniform sampler2D pgSafeOrigins;
bool PGTransportProbeAvailable(int probe);
#define SPGI_PROBE_USABLE(probe) PGTransportProbeAvailable(probe)
vec4 PGSampleLighting(sampler2D atlas,int probe,vec3 p,vec3 n,float depth);
#define SPGI_SAMPLE_LIGHTING(atlas,probe,p,n,depth) PGSampleLighting(atlas,probe,p,n,depth)
float PGCurvedSupport(int probe,vec3 p,vec3 n,float depth);
#define SPGI_CURVED_SUPPORT(probe,p,n,depth) PGCurvedSupport(probe,p,n,depth)
)GLSL"+
        ScreenProbePlacementDetail::SamplingGLSL()+LookupGLSL()+
        (coverageDiagnostic?"\n#define PG_COVERAGE_DIAGNOSTIC 1\n":"\n#define PG_COVERAGE_DIAGNOSTIC 0\n")+R"GLSL(
in vec2 vUV;layout(location=0) out vec4 outColor;
layout(binding=5) uniform sampler2D pgUniform;
layout(binding=6) uniform sampler2D pgSpawned;
layout(binding=7) uniform sampler2D pgIrradiance;
layout(binding=8) uniform sampler2D pgDirectional;
vec4 PGProbePart(int probe,int part) {
    bool uniformProbe=probe<spTraceAtlas.z;int slot=uniformProbe?probe:probe-spTraceAtlas.z;
    int columns=spGrid.x*(uniformProbe?1:spGrid.z);
    ivec2 at=ivec2(2*(slot%columns)+part,slot/columns);
    return uniformProbe?texelFetch(pgUniform,at,0):texelFetch(pgSpawned,at,0);
}
int PGDebugMode(){return (int(scParameters.w+.5)>>3)&7;}
float PGCurvedSupport(int probe,vec3 p,vec3 n,float depth) {
    if(PG_COVERAGE_DIAGNOSTIC!=0||PGDebugMode()!=0)return 1.0;
    // Bit 128 certifies internal hit metadata, independently of bit 64's RGB
    // direction-fit toggle. Missing metadata falls back to strict geometry.
    if(!SPGiCurvatureAwareWeightsEnabled()||(int(scParameters.w+.5)&128)==0)return 0.0;
    ivec2 at=ivec2((probe%spGrid.x)*4+1,probe/spGrid.x);
    float inverseHitDistance=texelFetch(pgDirectional,at,0).a;
    return SPGiCurvedHitSupport(PGProbePart(probe,0).xyz-p,n,inverseHitDistance);
}
bool PGTransportProbeAvailable(int probe) {
    // Only a failed launch certification removes an entire representative.
    // Readability alpha never selects samples: valid black and partially
    // unknown angular integrals keep their original full-ray denominator.
    if(PG_COVERAGE_DIAGNOSTIC!=0||PGDebugMode()!=0||spCurvature.z<.5)return true;
    vec4 origin=texelFetch(pgSafeOrigins,ivec2(probe%spGrid.x,probe/spGrid.x),0);
    return origin.w>0&&!any(isnan(origin.xyz))&&!any(isinf(origin.xyz));
}
vec4 PGSampleLighting(sampler2D atlas,int probe,vec3 p,vec3 n,float depth) {
    int mode=PGDebugMode();
    if(mode==1)return vec4(1);
    if(mode==2)return vec4(vec3(probe>=spTraceAtlas.z?.75:.25),1);
    if(mode==3) {
        vec3 position=PGProbePart(probe,0).xyz;
        float affine=clamp(.45+.04*dot(position,vec3(1,2,.5)),.05,.95);
        return vec4(vec3(affine),1);
    }
    if(mode==4)return vec4(vec3(probe==spTraceDisplay.y?1.0:0.0),1);
    vec4 lighting=texelFetch(atlas,ivec2(probe%spGrid.x,probe/spGrid.x),0);
    if((int(scParameters.w+.5)&64)==0)return lighting;
    vec3 probeNormal=PGProbePart(probe,1).xyz;
    if(any(isnan(probeNormal))||any(isinf(probeNormal))||dot(probeNormal,probeNormal)<.5)return lighting;
    probeNormal=normalize(probeNormal);
    ivec2 at=ivec2((probe%spGrid.x)*4,probe/spGrid.x);
    vec3 bx=texelFetch(pgDirectional,at+ivec2(1,0),0).rgb;
    vec3 by=texelFetch(pgDirectional,at+ivec2(2,0),0).rgb;
    vec3 bz=texelFetch(pgDirectional,at+ivec2(3,0),0).rgb;
    if(any(isnan(bx))||any(isinf(bx))||any(isnan(by))||any(isinf(by))||any(isnan(bz))||any(isinf(bz)))return lighting;
    vec3 deltaNormal=n-probeNormal;
    // Keep the public scalar atlas as the exact own-normal baseline, including
    // valid black and unreadable coverage. Directional fitting never changes
    // that alpha, nor extrapolates an unknown sample into readable lighting.
    lighting.rgb=max(lighting.rgb+(2.0/3.0)*(bx*deltaNormal.x+by*deltaNormal.y+bz*deltaNormal.z),vec3(0));
    return lighting;
}
vec4 PGLighting(int probe,vec3 p,vec3 n,float depth) {
    return PGSampleLighting(pgIrradiance,probe,p,n,depth);
}
float PGGeometryWeight(ivec2 pixel,vec3 p,vec3 n,float depth,int probe) {
    if(!PGTransportProbeAvailable(probe))return 0;
    return SPGiUniformWeight(pixel,p,n,depth,PGProbePart(probe,0),PGProbePart(probe,1));
}
void main() {
    ivec2 pixel=ivec2(gl_FragCoord.xy);outColor=vec4(0);
    vec4 view=texelFetch(pgViewPositions,pixel,0),normal=texelFetch(pgWorldNormals,pixel,0);
    if(view.w<.5||normal.w<.5||dot(normal.xyz,normal.xyz)<.5||
        any(isnan(view.xyz))||any(isinf(view.xyz))||any(isnan(normal.xyz))||any(isinf(normal.xyz)))return;
    vec3 p=(spInverseView*vec4(view.xyz,1)).xyz,n=normalize(normal.xyz);
    float depth=max(abs(view.z),.0001);
    if(SPGiRefinementEnabled()) {
        vec4 adaptiveLighting[4];vec4 uniformWeights,adaptiveWeights;float uniformWeight;
        SPGiReconstructionStencil(pixel,p,n,depth,pgUniform,pgSpawned,pgIrradiance,PG_COVERAGE_DIAGNOSTIC==0,
            adaptiveLighting,uniformWeights,adaptiveWeights,uniformWeight);
        float weightSum=dot(uniformWeights+adaptiveWeights,vec4(1));
        if(PG_COVERAGE_DIAGNOSTIC!=0) {
            // Orange includes local intersection refinement, even where a
            // uniform probe previously counted as geometric coverage.
            bool usable=weightSum>=.01,usesAdaptive=dot(adaptiveWeights,vec4(1))>.0001;
            outColor=!usable?vec4(1,0,1,weightSum>=.0001?1:0):
                (usesAdaptive?vec4(1,.35,.03,1):vec4(.04,.85,.20,1));return;
        }
        // UE distinguishes sufficient coverage from a still usable estimate.
        // Preserve the latter through a smooth transition; valid black light
        // and unreadable ray coverage retain their separate RGB/alpha values.
        if(weightSum<.0001)return;
        ivec2 base=SPCornerBase(pixel);vec4 sum=vec4(0);
        for(int corner=0;corner<4;++corner) {
            ivec2 tile=SPCorner(base,corner);int uniformProbe=tile.x+tile.y*spGrid.x;
            sum+=PGLighting(uniformProbe,p,n,depth)*uniformWeights[corner];
            sum+=adaptiveLighting[corner]*adaptiveWeights[corner];
        }
        sum/=weightSum;
        vec3 rho=max(texelFetch(pgReceiverAlbedo,pixel,0).rgb,vec3(0));
        outColor=vec4(PGDebugMode()!=0?sum.rgb:rho*sum.rgb*scParameters.z,clamp(sum.a,0.0,1.0));return;
    }
    ivec2 base=SPCornerBase(pixel);vec2 f=SPBilinearFractions(pixel,base);
    vec4 sum=vec4(0);float weightSum=0;
    for(int corner=0;corner<4;++corner) {
        ivec2 tile=SPCorner(base,corner);
        int chosen=tile.x+tile.y*spGrid.x;
        float bilinear=(corner%2==0?1.0-f.x:f.x)*(corner/2==0?1.0-f.y:f.y);
        float weight=bilinear*PGGeometryWeight(pixel,p,n,depth,chosen);
        if(PG_COVERAGE_DIAGNOSTIC==0)sum+=PGLighting(chosen,p,n,depth)*weight;
        weightSum+=weight;
    }
    bool uniformCovered=weightSum>=.01;
    if(!uniformCovered&&spOptions.z>.5) {
        // Adaptive candidates occupy physical screen subcells, not only the
        // four forward jittered uniform corners. Search the receiver tile and
        // its neighbors; every accepted candidate has compact support and
        // exactly the same normal / symmetric-plane rejection as allocation.
        ivec2 receiverTile=pixel/spGrid.w;vec4 bestWeights=vec4(0);ivec4 bestProbes=ivec4(-1);
        for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x) {
            ivec2 tile=receiverTile+ivec2(x,y);
            if(any(lessThan(tile,ivec2(0)))||any(greaterThanEqual(tile,spGrid.xy)))continue;
            for(int candidate=0;candidate<16;++candidate) {
                if(candidate>=spGrid.z)break;
                int probe=spTraceAtlas.z+tile.y*spGrid.x*spGrid.z+tile.x*spGrid.z+candidate;
                if(!PGTransportProbeAvailable(probe))continue;
                float weight=SPGiAdaptiveWeight(pixel,p,n,depth,PGProbePart(probe,0),PGProbePart(probe,1));
                // Geometric validity is already enforced by SPGiPlaneWeight.
                // As with uniform corners, compact-support confidences are
                // added before the final coverage threshold is applied.
                if(weight<=0)continue;
                for(int rank=0;rank<4;++rank)if(weight>bestWeights[rank]) {
                    for(int move=3;move>rank;--move){bestWeights[move]=bestWeights[move-1];bestProbes[move]=bestProbes[move-1];}
                    bestWeights[rank]=weight;bestProbes[rank]=probe;break;
                }
            }
        }
        sum=vec4(0);weightSum=dot(bestWeights,vec4(1));
        if(PG_COVERAGE_DIAGNOSTIC==0)for(int rank=0;rank<4;++rank)
            if(bestProbes[rank]>=0)sum+=PGLighting(bestProbes[rank],p,n,depth)*bestWeights[rank];
    }
    if(PG_COVERAGE_DIAGNOSTIC!=0) {
        // This is geometric probe coverage, independent of readable ray or
        // Surface Cache coverage. A black but valid ray must not look missing.
        outColor=uniformCovered?vec4(.04,.85,.20,1):
            (weightSum>=.01?vec4(1,.35,.03,1):vec4(1,0,1,0));return;
    }
    if(weightSum<.01)return;
    sum/=weightSum;
    vec3 rho=max(texelFetch(pgReceiverAlbedo,pixel,0).rgb,vec3(0));
    outColor=vec4(PGDebugMode()!=0?sum.rgb:rho*sum.rgb*scParameters.z,clamp(sum.a,0.0,1.0));
}
)GLSL";
}
} // namespace Render::SurfaceCacheDetail
