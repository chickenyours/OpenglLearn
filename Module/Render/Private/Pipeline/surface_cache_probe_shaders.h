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

// Four geometry-aware probe corners reconstruct the lighting field. Adaptive
// probes replace corners lacking surface coverage, as in UE's
// CalculateUpsampleInterpolationWeights (LumenScreenProbeGather.usf). This is
// the definition of the probe signal, with no image-space denoising/history.
inline std::string ProbeReconstructShader(bool coverageDiagnostic=false) {
    return std::string("#version 450 core\n")+ScreenProbeTraceDetail::ConstantsGLSL()+
        ScreenProbePlacementDetail::SamplingGLSL()+LookupGLSL()+
        (coverageDiagnostic?"\n#define PG_COVERAGE_DIAGNOSTIC 1\n":"\n#define PG_COVERAGE_DIAGNOSTIC 0\n")+R"GLSL(
in vec2 vUV;layout(location=0) out vec4 outColor;
layout(binding=2) uniform sampler2D pgViewPositions;
layout(binding=3) uniform sampler2D pgWorldNormals;
layout(binding=4) uniform sampler2D pgReceiverAlbedo;
layout(binding=5) uniform sampler2D pgUniform;
layout(binding=6) uniform sampler2D pgSpawned;
layout(binding=7) uniform sampler2D pgIrradiance;
vec4 PGProbePart(int probe,int part) {
    bool uniformProbe=probe<spTraceAtlas.z;int slot=uniformProbe?probe:probe-spTraceAtlas.z;
    int columns=spGrid.x*(uniformProbe?1:spGrid.z);
    ivec2 at=ivec2(2*(slot%columns)+part,slot/columns);
    return uniformProbe?texelFetch(pgUniform,at,0):texelFetch(pgSpawned,at,0);
}
int PGDebugMode(){return (int(scParameters.w+.5)>>3)&3;}
vec4 PGDebugLighting(vec4 lighting,bool adaptive) {
    int mode=PGDebugMode();
    return mode==1?vec4(1):(mode==2?vec4(vec3(adaptive?.75:.25),1):lighting);
}
vec4 PGLighting(int probe) {
    return PGDebugLighting(texelFetch(pgIrradiance,ivec2(probe%spGrid.x,probe/spGrid.x),0),probe>=spTraceAtlas.z);
}
float PGGeometryWeight(vec3 p,vec3 n,float depth,int probe) {
    return SPGiPlaneWeight(p,n,depth,PGProbePart(probe,0),PGProbePart(probe,1));
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
            sum+=PGLighting(uniformProbe)*uniformWeights[corner];
            sum+=PGDebugLighting(adaptiveLighting[corner],true)*adaptiveWeights[corner];
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
        float weight=bilinear*PGGeometryWeight(p,n,depth,chosen);
        if(PG_COVERAGE_DIAGNOSTIC==0)sum+=PGLighting(chosen)*weight;
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
            if(bestProbes[rank]>=0)sum+=PGLighting(bestProbes[rank])*bestWeights[rank];
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
