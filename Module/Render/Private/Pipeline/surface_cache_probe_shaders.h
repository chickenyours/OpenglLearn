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
        if(status==1||status==9||status==10) {
            if(!any(isnan(value.rgb))&&!any(isinf(value.rgb))) {
                sum+=max(value.rgb,vec3(0));++readable;
            }
        } else if(status==5||status==13)++readable;
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
inline std::string ProbeReconstructShader() {
    return std::string("#version 450 core\n")+ScreenProbeTraceDetail::ConstantsGLSL()+
        ScreenProbePlacementDetail::SamplingGLSL()+LookupGLSL()+R"GLSL(
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
vec4 PGLighting(int probe) {return texelFetch(pgIrradiance,ivec2(probe%spGrid.x,probe/spGrid.x),0);}
float PGGeometryWeight(vec3 p,vec3 n,float depth,int probe) {
    return SPGiPlaneWeight(p,n,depth,PGProbePart(probe,0),PGProbePart(probe,1));
}
float PGAdaptiveSupport(ivec2 pixel,int probe) {
    vec4 clip=spViewProjection*vec4(PGProbePart(probe,0).xyz,1);
    if(clip.w<=0)return 0;
    vec2 screen=(clip.xy/clip.w*.5+.5)*vec2(spScreen.xy)-.5;
    vec2 d=abs(screen-vec2(pixel))/float(spGrid.w);
    // Compact support prevents a depth-compatible adaptive probe from
    // influencing an entire row/column of an unrelated screen tile.
    return max(0.0,1.0-max(d.x,d.y));
}
void main() {
    ivec2 pixel=ivec2(gl_FragCoord.xy);outColor=vec4(0);
    vec4 view=texelFetch(pgViewPositions,pixel,0),normal=texelFetch(pgWorldNormals,pixel,0);
    if(view.w<.5||normal.w<.5||dot(normal.xyz,normal.xyz)<.5||
        any(isnan(view.xyz))||any(isinf(view.xyz))||any(isnan(normal.xyz))||any(isinf(normal.xyz)))return;
    vec3 p=(spInverseView*vec4(view.xyz,1)).xyz,n=normalize(normal.xyz);
    float depth=max(abs(view.z),.0001);
    ivec2 base=SPCornerBase(pixel);vec2 f=SPBilinearFractions(pixel,base);
    vec4 sum=vec4(0);float weightSum=0;
    for(int corner=0;corner<4;++corner) {
        ivec2 tile=SPCorner(base,corner);
        int chosen=tile.x+tile.y*spGrid.x;
        float bilinear=(corner%2==0?1.0-f.x:f.x)*(corner/2==0?1.0-f.y:f.y);
        float weight=bilinear*PGGeometryWeight(p,n,depth,chosen);
        if(spOptions.z>.5&&weight<.01) {
            for(int candidate=0;candidate<16;++candidate) {
                if(candidate>=spGrid.z)break;
                int probe=spTraceAtlas.z+tile.y*spGrid.x*spGrid.z+tile.x*spGrid.z+candidate;
                float adaptive=PGGeometryWeight(p,n,depth,probe)*PGAdaptiveSupport(pixel,probe);
                if(adaptive>weight){weight=adaptive;chosen=probe;}
            }
        }
        sum+=PGLighting(chosen)*weight;weightSum+=weight;
    }
    if(weightSum<.01)return;
    sum/=weightSum;
    vec3 rho=max(texelFetch(pgReceiverAlbedo,pixel,0).rgb,vec3(0));
    outColor=vec4(rho*sum.rgb*scParameters.z,clamp(sum.a,0.0,1.0));
}
)GLSL";
}
} // namespace Render::SurfaceCacheDetail
