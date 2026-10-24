#pragma once
#include <string>

namespace Render::ScreenProbePlacementDetail {
inline std::string Constants() { return R"GLSL(
layout(std140,binding=9) uniform PlacementData {
    mat4 spInverseView; mat4 spInverseProjection; mat4 spViewProjection; mat4 spView;
    ivec4 spGrid; ivec4 spScreen; vec4 spOptions;
    ivec4 spTracing; vec4 spTraceParams; ivec4 spTraceAtlas; ivec4 spTraceDisplay;
    vec4 spRefinement; // flags (bit0 junctions, bit1 visibility, bit2 curvature, bit3 curved weights), detection/support, normal cosine
    vec4 spCurvature; // normal cosine, plane pixel error, runtime transport validity enabled, continuous coarse interpolation
};
)GLSL"; }
// Shared by receiver reconstruction and probe-radiance spatial reuse. This is
// a lighting compatibility model, not a physical surface-cluster classifier.
inline std::string CurvatureWeightGLSL() {return R"GLSL(
bool SPGiCurvatureAwareWeightsEnabled() {
    return (spTracing.w&2)!=0&&(int(spRefinement.x+.5)&8)!=0;
}
float SPGiMidpointPlaneDistance(vec3 delta,vec3 normal,vec3 otherNormal) {
    // Compatible normals cannot cancel. A smooth curve's chord lies in its
    // midpoint tangent plane; separated parallel layers retain their gap.
    return abs(dot(delta,normalize(normal+otherNormal)));
}
float SPGiCurvedNormalWeight(float alignment,float hardMinimum) {
    // Keep 10-20 degree smooth normal changes at full weight. Fade to zero at
    // the caller's existing hard normal gate, including tighter spatial gates.
    return smoothstep(hardMinimum,max(.9,hardMinimum+.000001),alignment);
}
float SPGiCurvedPlaneBoundaryWeight(float relativePlane,float hardMaximum) {
    // The hard layer gate remains in force; its accepted-side weight must
    // reach zero continuously rather than drop a nonzero normalized sample.
    return 1.0-smoothstep(.75*hardMaximum,hardMaximum,relativePlane);
}
float SPGiCurvedNormalSupport(vec3 normal,vec3 otherNormal) {
    // The finite hemisphere direction fit is local. Large normal rotations
    // must keep the original support even on a geometrically smooth curve.
    return smoothstep(.9396926208,.9848077530,dot(normal,otherNormal));
}
float SPGiCurvedHitSupport(vec3 delta,vec3 receiverNormal,float inverseHitDistance) {
    if(inverseHitDistance<0.0||isnan(inverseHitDistance)||isinf(inverseHitDistance))return 0.0;
    vec3 tangentDelta=delta-receiverNormal*dot(delta,receiverNormal);
    float parallax=length(tangentDelta)*inverseHitDistance;
    return 1.0-smoothstep(.087488663525924,.176326980708465,parallax); // tan(5/10 degrees)
}
)GLSL";}
// Placement and final reconstruction must use exactly the same jittered tile
// stencil and surface-validity test. Otherwise the allocator can declare a
// curved surface covered by a probe the final lighting pass must reject.
inline std::string SamplingGLSL() {return CurvatureWeightGLSL()+R"GLSL(
vec2 SPHammersley(uint index,uint count,uvec2 seed) {
    return vec2(fract(float(index)/float(count)+float(seed.x)/65536.0),
        float((bitfieldReverse(index)>>16)^seed.y)/65536.0);
}
uvec2 SPSeed(ivec2 tile) {
    uvec3 v=uvec3(tile,spScreen.z)*1664525u+1013904223u;
    v.x+=v.y*v.z;v.y+=v.z*v.x;v.z+=v.x*v.y;
    v.x+=v.y*v.z;v.y+=v.z*v.x;v.z+=v.x*v.y;
    return (v>>16).xy;
}
ivec2 SPJitter(){return ivec2(SPHammersley(uint(spScreen.z),8u,uvec2(0))*float(spGrid.w));}
ivec2 SPUniformPixel(ivec2 tile){return min(tile*spGrid.w+SPJitter(),spScreen.xy-1);}
ivec2 SPCandidatePixel(ivec2 tile,int sampleIndex) {
    return tile*spGrid.w+SPJitter()+ivec2(clamp(
        SPHammersley(uint(sampleIndex),uint(spGrid.z),SPSeed(tile))*float(spGrid.w),
        vec2(0),vec2(spGrid.w-1)));
}
ivec2 SPSlot(ivec2 tile,int sampleIndex){return ivec2(tile.x*spGrid.z+sampleIndex,tile.y);}
ivec2 SPCornerBase(ivec2 pixel) {
    ivec2 full=clamp(pixel-SPJitter(),ivec2(0),spScreen.xy-1);
    return min(full/spGrid.w,max(spGrid.xy-2,ivec2(0)));
}
ivec2 SPCorner(ivec2 base,int corner){return min(base+ivec2(corner%2,corner/2),spGrid.xy-1);}
vec2 SPBilinearFractions(ivec2 pixel,ivec2 base) {
    if((spTracing.w&2)!=0&&spCurvature.w>.5) {
        // GI probes are actual pixel-center samples, not an expanded tile.
        // Interpolate between those locations so a change of the coarse
        // stencil does not drop one positive edge weight and introduce another.
        // The last row/column can have shorter spacing after viewport clamping.
        ivec2 first=SPUniformPixel(base);
        ivec2 last=SPUniformPixel(min(base+ivec2(1),spGrid.xy-1));
        return clamp(vec2(pixel-first)/vec2(max(last-first,ivec2(1))),vec2(0),vec2(1));
    }
    ivec2 full=clamp(pixel-SPJitter(),ivec2(0),spScreen.xy-1);
    vec2 f=vec2(full-base*spGrid.w+1)/float(spGrid.w+2);
    return (spTracing.w&2)!=0?clamp(f,vec2(0),vec2(1)):f;
}
float SPGiPlaneWeight(vec3 p,vec3 n,float depth,vec4 probe,vec4 normalDepth) {
    if(probe.w<.5||normalDepth.w<=0||dot(normalDepth.xyz,normalDepth.xyz)<.5)return 0;
    if(any(isnan(probe.xyz))||any(isinf(probe.xyz))||
        any(isnan(normalDepth.xyz))||any(isinf(normalDepth.xyz)))return 0;
    vec3 pn=normalize(normalDepth.xyz);float alignment=dot(n,pn);if(alignment<=.6)return 0;
    vec3 delta=probe.xyz-p;
    float plane=max(abs(dot(delta,n)),abs(dot(delta,pn)));
    float relative=plane/max(min(depth,normalDepth.w),.0001);
    // Values beneath the allocation threshold are missing coverage, not a
    // tiny confidence to normalize back into a full-strength light sample.
    if(relative>spOptions.x)return 0;
    float weight=exp2(-6.643856189774725*relative/spOptions.x)*pow(alignment,8.0);
    return weight>=.01?weight:0;
}
bool SPGiRefinementEnabled(){return (spTracing.w&2)!=0&&(int(spRefinement.x+.5)&1)!=0;}
bool SPGiVisibilityEnabled(){return (spTracing.w&2)!=0&&(int(spRefinement.x+.5)&2)!=0;}
bool SPGiCurvatureEnabled(){return (spTracing.w&2)!=0&&(int(spRefinement.x+.5)&4)!=0;}
// Surface membership remains the original strict endpoint-plane predicate.
// In particular, two angled faces must not merge into one representative just
// because their displacement cancels in the averaged-normal tangent plane.
float SPGiClusterPlaneWeight(vec3 p,vec3 n,float depth,vec4 probe,vec4 normalDepth) {
    if(probe.w<.5||normalDepth.w<=0||dot(normalDepth.xyz,normalDepth.xyz)<.5)return 0;
    if(any(isnan(probe.xyz))||any(isinf(probe.xyz))||
        any(isnan(normalDepth.xyz))||any(isinf(normalDepth.xyz)))return 0;
    vec3 pn=normalize(normalDepth.xyz);float alignment=dot(n,pn);if(alignment<=.6)return 0;
    vec3 delta=probe.xyz-p;
    float plane=max(abs(dot(delta,n)),abs(dot(delta,pn)));
    float relative=plane/max(min(depth,normalDepth.w),.0001);
    // Keep the hard geometric rejection. Confidence inside that support is
    // continuous: a small normal or plane change must not toggle lighting off.
    if(relative>spOptions.x)return 0;
    return exp2(-6.643856189774725*relative/spOptions.x)*pow(alignment,8.0);
}
float SPGiContinuousPlaneWeight(vec3 p,vec3 n,float depth,vec4 probe,vec4 normalDepth) {
    // Preserve the complete original arithmetic for controlled comparisons.
    if(!SPGiCurvatureAwareWeightsEnabled())return SPGiClusterPlaneWeight(p,n,depth,probe,normalDepth);
    if(probe.w<.5||normalDepth.w<=0||dot(normalDepth.xyz,normalDepth.xyz)<.5)return 0;
    if(any(isnan(probe.xyz))||any(isinf(probe.xyz))||
        any(isnan(normalDepth.xyz))||any(isinf(normalDepth.xyz)))return 0;
    vec3 pn=normalize(normalDepth.xyz);float alignment=dot(n,pn);if(alignment<=.6)return 0;
    float plane=SPGiMidpointPlaneDistance(probe.xyz-p,n,pn);
    float relative=plane/max(min(depth,normalDepth.w),.0001);
    if(relative>spOptions.x)return 0;
    return exp2(-6.643856189774725*relative/spOptions.x)*SPGiCurvedNormalWeight(alignment,.6)*
        SPGiCurvedPlaneBoundaryWeight(relative,spOptions.x);
}
vec2 SPGiProbePixel(vec4 probe) {
    vec4 clip=spViewProjection*vec4(probe.xyz,1);
    return clip.w>0?(clip.xy/clip.w*.5+.5)*vec2(spScreen.xy)-.5:vec2(-1e9);
}
float SPGiLocalSupport(ivec2 pixel,vec4 probe,float radius) {
    vec2 delta=abs(SPGiProbePixel(probe)-vec2(pixel))/max(float(spGrid.w)*radius,1.0);
    float support=max(0.0,1.0-max(delta.x,delta.y));
    return support*support*(3.0-2.0*support);
}
// A short receiver-to-probe segment checks local occluders that plane/normal
// similarity alone cannot identify. It shares the exact captured view-position
// texture with allocation and reconstruction; invalid/off-screen samples remain
// inconclusive rather than becoming fabricated black visibility.
float SPGiScreenVisibility(sampler2D positions,vec3 p,vec3 n,float depth,
    vec4 probe,vec4 normalDepth) {
    if(!SPGiVisibilityEnabled())return 1.0;
    float bias=max(spTraceParams.z,.0001);
    vec3 from=p+n*bias,to=probe.xyz+normalize(normalDepth.xyz)*bias;
    if(dot(to-from,to-from)<=4.0*bias*bias)return 1.0;
    float relativeTolerance=.25*max(min(spTraceParams.w,spOptions.x),.0001);
    float visibility=1.0;
    for(int tap=0;tap<3;++tap) {
        vec3 samplePosition=mix(from,to,float(tap+1)*.25);
        vec4 clip=spViewProjection*vec4(samplePosition,1);if(clip.w<=.000001)continue;
        vec3 ndc=clip.xyz/clip.w;
        if(any(lessThan(ndc.xy,vec2(-1)))||any(greaterThanEqual(ndc.xy,vec2(1)))||ndc.z< -1.0||ndc.z>1.0)continue;
        ivec2 pixel=ivec2(floor((ndc.xy*.5+.5)*vec2(spScreen.xy)));
        vec4 surface=texelFetch(positions,pixel,0);
        if(surface.w<.5||any(isnan(surface.xyz))||any(isinf(surface.xyz))||surface.z>=0)continue;
        float segmentDepth=-(spView*vec4(samplePosition,1)).z,observedDepth=-surface.z;
        if(segmentDepth<=0)continue;
        float localDepth=max(min(min(segmentDepth,observedDepth),min(depth,normalDepth.w)),.0001);
        float tolerance=bias+relativeTolerance*localDepth;
        // Definite foreground occlusion rejects the shared sample; depth near
        // the uncertainty band fades smoothly without relaxing geometry gates.
        float foreground=segmentDepth-observedDepth;
        visibility=min(visibility,1.0-smoothstep(tolerance,2.0*tolerance,foreground));
        if(visibility<=0)return 0.0;
    }
    return visibility;
}
float SPGiProbeVisibility(vec3 p,vec3 n,float depth,vec4 probe,vec4 normalDepth) {
    return SPGiScreenVisibility(SPGI_SURFACE_POSITIONS,p,n,depth,probe,normalDepth);
}
// The coarse estimate must remain a partition of its geometric corner weights.
// Restricting each corner with a query-local square makes the coarse signal
// itself alternate with the probe lattice before fine lighting is even added.
float SPGiUniformWeight(ivec2 pixel,vec3 p,vec3 n,float depth,vec4 probe,vec4 normalDepth) {
    float geometry=SPGiRefinementEnabled()?SPGiContinuousPlaneWeight(p,n,depth,probe,normalDepth):
        SPGiPlaneWeight(p,n,depth,probe,normalDepth);
    return geometry>0?geometry*SPGiProbeVisibility(p,n,depth,probe,normalDepth):0.0;
}
// Metadata stores junction detection confidence. The support setting limits
// allocation to that junction band; reconstruction never uses this field as a
// query-dependent coarse/fine mask or a minimum over neighboring probes.
float SPGiRefinementConfidence(float strength) {
    float fraction=clamp(spRefinement.z/max(spRefinement.y,.0001),.0001,1.0);
    float outer=1.0-fraction*fraction*(3.0-2.0*fraction);
    return smoothstep(outer,1.0,clamp(strength,0.0,1.0));
}
// Only curvature-only representatives fade in. Coverage obligations and
// junctions retain their full geometric weight, including known-black samples.
float SPGiProbeActivation(vec4 probe) {
    return probe.w>=4.0?clamp(probe.w-4.0,0.0,1.0):1.0;
}
float SPGiAdaptiveWeight(ivec2 pixel,vec3 p,vec3 n,float depth,vec4 probe,vec4 normalDepth) {
    float geometry=SPGiRefinementEnabled()?SPGiContinuousPlaneWeight(p,n,depth,probe,normalDepth):
        SPGiPlaneWeight(p,n,depth,probe,normalDepth);
    if(geometry==0)return 0;
    geometry*=SPGiProbeVisibility(p,n,depth,probe,normalDepth)*SPGiProbeActivation(probe);if(geometry<=0)return 0;
    if(SPGiRefinementEnabled()&&probe.w>1.5&&probe.w<4.0)
        return geometry*SPGiLocalSupport(pixel,probe,spRefinement.z)*clamp(probe.w-2.0,0.0,1.0);
    vec2 delta=abs(SPGiProbePixel(probe)-vec2(pixel))/float(spGrid.w);
    return geometry*max(0.0,1.0-max(delta.x,delta.y));
}
vec4 SPGiAtlasPart(sampler2D uniformAtlas,sampler2D spawnedAtlas,int probe,int part) {
    int uniformSlots=spGrid.x*spGrid.y;
    bool uniformProbe=probe<uniformSlots;int slot=uniformProbe?probe:probe-uniformSlots;
    int columns=spGrid.x*(uniformProbe?1:spGrid.z);
    ivec2 at=ivec2(2*(slot%columns)+part,slot/columns);
    return uniformProbe?texelFetch(uniformAtlas,at,0):texelFetch(spawnedAtlas,at,0);
}
// Two existing candidate slots belong to distinct visible surface clusters in
// each subcell. Capacity stays unchanged: 4 / 8 / 16 slots give 2 / 4 / 8 cells.
ivec2 SPGiSurfaceLayout() {
    int cells=spGrid.z/2,columns=cells>=8?4:2;
    return ivec2(columns,cells/columns);
}
void SPGiCellBounds(ivec2 tile,ivec2 cell,ivec2 cells,out ivec2 begin,out ivec2 end) {
    begin=tile*spGrid.w+cell*spGrid.w/cells;
    end=min(tile*spGrid.w+(cell+1)*spGrid.w/cells,spScreen.xy);
}
// A positive C1 quadratic B-spline has compact support and, on regular sample
// locations, constant total mass and linear precision. Query distances always
// use the actual projected representative, including coverage points that moved
// away from a subcell center. No nearest-winner selection changes the stencil.
float SPGiQuadraticBasis(float value) {
    float a=abs(value);
    if(a<.5)return .75-a*a;
    float edge=max(0.0,1.5-a);return .5*edge*edge;
}
float SPGiSurfaceKernel(ivec2 pixel,vec4 probe,ivec2 cells) {
    vec2 step=vec2(spGrid.w)/vec2(cells);
    vec2 delta=(SPGiProbePixel(probe)-vec2(pixel))/step;
    return SPGiQuadraticBasis(delta.x)*SPGiQuadraticBasis(delta.y);
}
// The radiance representation can override this before SamplingGLSL is emitted.
// Its result must remain (receiver-evaluated E/pi, readable coverage); all four
// channels use identical positive reconstruction weights.
#ifndef SPGI_SAMPLE_LIGHTING
#define SPGI_SAMPLE_LIGHTING(atlas,probe,p,n,depth) texelFetch(atlas,ivec2((probe)%spGrid.x,(probe)/spGrid.x),0)
#endif
// A completed-spawn summary may override this with the tile's exact live-slot
// bit mask. Placement/legacy users retain the full traversal by default.
#ifndef SPGI_ACTIVE_PROBES
#define SPGI_ACTIVE_PROBES(tile) 65535
#endif
// Transport can reject a whole launch point without treating its missing
// measurement as black. Allocation and geometry-only diagnostics retain all
// geometric representatives. Partial ray/cache coverage is not this predicate.
#ifndef SPGI_PROBE_USABLE
#define SPGI_PROBE_USABLE(probe) true
#endif
// Lighting users may restrict only the extra curved-surface reuse with measured
// hit parallax. Placement and geometry diagnostics never need that texture.
#ifndef SPGI_CURVED_SUPPORT
#define SPGI_CURVED_SUPPORT(probe,p,n,depth) 1.0
#endif
float SPGiReconstructionGeometryWeight(int probe,vec3 p,vec3 n,float depth,
    vec4 position,vec4 normalDepth,bool needLighting) {
    float curved=SPGiContinuousPlaneWeight(p,n,depth,position,normalDepth);
    if(!SPGiCurvatureAwareWeightsEnabled())return curved;
    float strict=SPGiClusterPlaneWeight(p,n,depth,position,normalDepth);
    if(curved<=0.0&&strict<=0.0)return 0.0;
    // Coverage diagnostics use the same angular support as analytic lighting;
    // only physical lighting needs the measured hit-distance texture.
    float support=needLighting?clamp(SPGI_CURVED_SUPPORT(probe,p,n,depth),0.0,1.0):1.0;
    support*=SPGiCurvedNormalSupport(n,normalize(normalDepth.xyz));
    return mix(strict,curved,support);
}
// Coarse corners and every compatible adaptive representative share one convex
// reconstruction. A fixed complete support window and actual positions avoid
// both nominal-center errors and per-pixel nearest/top-K switching. Metadata
// describes allocation reasons; it cannot create a coarse/fine blend mask.
void SPGiReconstructionStencil(ivec2 pixel,vec3 p,vec3 n,float depth,
    sampler2D uniformAtlas,sampler2D spawnedAtlas,sampler2D irradianceAtlas,bool needLighting,
    out vec4 adaptiveLighting[4],
    out vec4 uniformWeights,out vec4 adaptiveWeights,out float uniformWeight) {
    ivec2 base=SPCornerBase(pixel);vec2 f=SPBilinearFractions(pixel,base);
    uniformWeights=vec4(0);adaptiveWeights=vec4(0);
    for(int c=0;c<4;++c) {
        adaptiveLighting[c]=vec4(0);
        ivec2 tile=SPCorner(base,c);int probe=tile.x+tile.y*spGrid.x;
        float bilinear=(c%2==0?1.0-f.x:f.x)*(c/2==0?1.0-f.y:f.y);
        vec4 pp=SPGiAtlasPart(uniformAtlas,spawnedAtlas,probe,0),nd=SPGiAtlasPart(uniformAtlas,spawnedAtlas,probe,1);
        float geometry=SPGiRefinementEnabled()?
            SPGiReconstructionGeometryWeight(probe,p,n,depth,pp,nd,needLighting):SPGiPlaneWeight(p,n,depth,pp,nd);
        if(geometry>0)geometry*=SPGiProbeVisibility(p,n,depth,pp,nd);
        if(needLighting&&!SPGI_PROBE_USABLE(probe))geometry=0;
        uniformWeights[c]=bilinear*geometry;
    }
    uniformWeight=dot(uniformWeights,vec4(1));
    // A coarse tile's metadata cannot decide whether its interior contains
    // allocated surface representatives; inspect their complete compact support.
    if(spOptions.z<.5)return;

    ivec2 cells=SPGiSurfaceLayout();vec2 radius=1.5*vec2(spGrid.w)/vec2(cells);
    ivec2 reach=ivec2(ceil(radius/float(spGrid.w))),receiverTile=pixel/spGrid.w;
    vec4 weightedLighting[4];for(int c=0;c<4;++c)weightedLighting[c]=vec4(0);
    // Four-slot cells are twice as tall and need the second neighboring tile.
    // Eight/sixteen-slot layouts use only +/-1; AABB tests skip empty support
    // before either representative's position/normal is fetched.
    for(int y=-2;y<=2;++y)for(int x=-2;x<=2;++x) {
        if(abs(x)>reach.x||abs(y)>reach.y)continue;
        ivec2 tile=receiverTile+ivec2(x,y);
        if(any(lessThan(tile,ivec2(0)))||any(greaterThanEqual(tile,spGrid.xy)))continue;
        int activeMask=int(SPGI_ACTIVE_PROBES(tile));
        if(activeMask==0)continue;
        for(int cellIndex=0;cellIndex<8;++cellIndex) {
            if(cellIndex>=spGrid.z/2)break;
            if((activeMask&(3<<(cellIndex*2)))==0)continue;
            ivec2 cell=ivec2(cellIndex%cells.x,cellIndex/cells.x),begin,end;
            SPGiCellBounds(tile,cell,cells,begin,end);
            if(any(greaterThanEqual(begin,end)))continue;
            vec2 outside=max(max(vec2(begin-pixel),vec2(pixel-(end-1))),vec2(0));
            if(any(greaterThanEqual(outside,radius)))continue;
            for(int representative=0;representative<2;++representative) {
                int sampleIndex=cellIndex*2+representative;
                // This is exact liveness culling, never a confidence threshold:
                // every active representative keeps its original sample weight.
                if((activeMask&(1<<sampleIndex))==0)continue;
                int probe=spGrid.x*spGrid.y+(tile.y*spGrid.x+tile.x)*spGrid.z+sampleIndex;
                if(needLighting&&!SPGI_PROBE_USABLE(probe))continue;
                vec4 pp=SPGiAtlasPart(uniformAtlas,spawnedAtlas,probe,0);if(pp.w<.5)continue;
                float kernel=SPGiSurfaceKernel(pixel,pp,cells)*SPGiProbeActivation(pp);if(kernel<=0)continue;
                vec4 nd=SPGiAtlasPart(uniformAtlas,spawnedAtlas,probe,1);
                float geometry=SPGiReconstructionGeometryWeight(probe,p,n,depth,pp,nd,needLighting);if(geometry<=0)continue;
                geometry*=SPGiProbeVisibility(p,n,depth,pp,nd);if(geometry<=0)continue;
                float weight=kernel*geometry;int bucket=sampleIndex%4;
                adaptiveWeights[bucket]+=weight;
                if(needLighting)weightedLighting[bucket]+=
                    SPGI_SAMPLE_LIGHTING(irradianceAtlas,probe,p,n,depth)*weight;
            }
        }
    }
    for(int c=0;c<4;++c)if(adaptiveWeights[c]>0)
        adaptiveLighting[c]=weightedLighting[c]/adaptiveWeights[c];
}

)GLSL";}
inline std::string Common() { return std::string("#version 450 core\n")+Constants()+R"GLSL(
layout(binding=3) uniform sampler2D spGeometry;
#define SPGI_SURFACE_POSITIONS spGeometry
)GLSL"+SamplingGLSL()+R"GLSL(
in vec2 vUV;
layout(location=0) out vec4 outColor;
layout(binding=0) uniform sampler2D spInput;
layout(binding=1) uniform sampler2D spNormals;
layout(binding=2) uniform sampler2D spDepth;
layout(binding=4) uniform sampler2D spUniform;
layout(binding=5) uniform sampler2D spMarked;
layout(binding=6) uniform sampler2D spSpawned;
const float SP_MIN_WEIGHT=.01;
bool SPFinite(vec3 p){return !any(isnan(p))&&!any(isinf(p));}
vec4 SPPosition(sampler2D atlas,ivec2 slot){return texelFetch(atlas,ivec2(slot.x*2,slot.y),0);}
vec4 SPNormalDepth(sampler2D atlas,ivec2 slot){return texelFetch(atlas,ivec2(slot.x*2+1,slot.y),0);}
bool SPSurface(ivec2 pixel,out vec3 p,out vec3 n,out float depth) {
    p=vec3(0);n=vec3(0);depth=0;
    if(any(lessThan(pixel,ivec2(0)))||any(greaterThanEqual(pixel,spScreen.xy)))return false;
    vec4 geometry=texelFetch(spGeometry,pixel,0),normal=texelFetch(spNormals,pixel,0);
    if(geometry.w<.5||normal.w<.5||!SPFinite(geometry.xyz)||!SPFinite(normal.xyz)||dot(normal.xyz,normal.xyz)<.5)return false;
    p=(spInverseView*vec4(geometry.xyz,1)).xyz;n=normalize(normal.xyz);depth=max(abs(geometry.z),.0001);
    return SPFinite(p);
}
float SPPlaneWeight(vec3 p,vec3 n,float depth,vec4 probe,vec4 normalDepth) {
    if((spTracing.w&2)!=0)return SPGiPlaneWeight(p,n,depth,probe,normalDepth);
    if(probe.w<.5)return 0;
    return exp2(log2(SP_MIN_WEIGHT)*abs(dot(probe.xyz-p,n))/max(depth,.0001)/spOptions.x);
}
float SPGiAllocationWeight(ivec2 pixel,vec3 p,vec3 n,float depth,vec4 probe,vec4 normalDepth) {
    // Allocation stays conservative and independent of the lighting model.
    // Relaxing reuse cannot move representatives or alter the ray population.
    float geometry=SPGiRefinementEnabled()?SPGiClusterPlaneWeight(p,n,depth,probe,normalDepth):
        SPGiPlaneWeight(p,n,depth,probe,normalDepth);
    return geometry>0?geometry*SPGiProbeVisibility(p,n,depth,probe,normalDepth):0.0;
}
vec4 SPUniformWeights(ivec2 pixel,vec3 p,vec3 n,float depth) {
    ivec2 base=SPCornerBase(pixel);vec2 f=SPBilinearFractions(pixel,base);vec4 weights=vec4(0);
    for(int c=0;c<4;++c){float b=(c%2==0?1-f.x:f.x)*(c/2==0?1-f.y:f.y);
        ivec2 corner=SPCorner(base,c);
        vec4 nd=(spTracing.w&2)!=0?SPNormalDepth(spUniform,corner):vec4(0);
        weights[c]=b*((spTracing.w&2)!=0?SPGiAllocationWeight(pixel,p,n,depth,SPPosition(spUniform,corner),nd):
            SPPlaneWeight(p,n,depth,SPPosition(spUniform,corner),nd));}
    return weights;
}
float SPUniformCoverage(ivec2 pixel,vec3 p,vec3 n,float depth){return dot(SPUniformWeights(pixel,p,n,depth),vec4(1));}
float SPAdaptiveWeight(ivec2 pixel,vec3 p,vec3 n,float depth,ivec2 probePixel,vec4 probe,vec4 normalDepth) {
    vec2 delta=abs(vec2(probePixel-pixel));
    float support=(spTracing.w&2)!=0?max(delta.x,delta.y):min(delta.x,delta.y);
    return SPPlaneWeight(p,n,depth,probe,normalDepth)*(1-clamp(support/float(spGrid.w),0,1));
}
float SPGiWorldPixelFootprint(vec3 p,vec3 n) {
    vec4 clip=spViewProjection*vec4(p,1);
    if(clip.w<=0)return 0;
    vec4 x=spInverseView*(spInverseProjection*(clip+vec4(2.0*clip.w/float(spScreen.x),0,0,0)));
    vec4 y=spInverseView*(spInverseProjection*(clip+vec4(0,2.0*clip.w/float(spScreen.y),0,0)));
    if(abs(x.w)<.000001||abs(y.w)<.000001)return 0;
    float footprint=max(length(x.xyz/x.w-p),length(y.xyz/y.w-p));
    vec3 toCamera=spInverseView[3].xyz-p;
    float facing=abs(dot(n,normalize(toCamera)));
    // A grazing foreground silhouette must not acquire an unbounded footprint
    // and masquerade as a distant wall junction. Missing such grazing junction
    // refinement is conservative; geometric coverage repair remains available.
    return footprint/max(facing,.25);
}
// Coverage alone does not measure how well coarse probes represent a smooth
// but curved surface. Test the same compatible, visible coarse support against
// an angular and a world-space tangent-plane error budget. This runs once per
// otherwise covered subcell, rather than adding four probe reads to every pixel
// in the representative classification scan. Flat surfaces have zero error.
float SPGiCoarseCurvatureError(ivec2 pixel,vec3 p,vec3 n,float depth,float planeBudget) {
    ivec2 base=SPCornerBase(pixel);vec2 f=SPBilinearFractions(pixel,base);
    float mass=0,normalError=0,planeError=0;
    for(int c=0;c<4;++c) {
        ivec2 corner=SPCorner(base,c);
        vec4 probe=SPPosition(spUniform,corner),nd=SPNormalDepth(spUniform,corner);
        float bilinear=(c%2==0?1.0-f.x:f.x)*(c/2==0?1.0-f.y:f.y);
        float weight=bilinear*SPGiAllocationWeight(pixel,p,n,depth,probe,nd);
        if(weight<=0)continue;
        vec3 pn=normalize(nd.xyz),delta=probe.xyz-p;
        float plane=max(abs(dot(delta,n)),abs(dot(delta,pn)));
        mass+=weight;
        normalError+=weight*max(0.0,1.0-dot(n,pn));
        planeError+=weight*plane*plane;
    }
    if(mass<SP_MIN_WEIGHT)return 0.0;
    float angular=normalError/(mass*max(1.0-spCurvature.x,.0000001));
    float plane=sqrt(max(planeError/mass,0.0))/planeBudget;
    return max(angular,plane);
}
float SPGiIntersectionStrength(ivec2 pixel,vec3 p,vec3 n,float depth) {
    if(!SPGiRefinementEnabled())return 0;
    int radius=max(1,int(ceil(spRefinement.y*float(spGrid.w))));
    const ivec2 directions[8]=ivec2[8](ivec2(-1,0),ivec2(1,0),ivec2(0,-1),ivec2(0,1),
        ivec2(-1,-1),ivec2(1,-1),ivec2(-1,1),ivec2(1,1));
    float strength=0,worldRadius=SPGiWorldPixelFootprint(p,n)*float(radius+2);
    for(int tap=0;tap<8;++tap) {
        vec3 other,otherNormal;float otherDepth;
        if(!SPSurface(pixel+directions[tap]*radius,other,otherNormal,otherDepth))continue;
        vec3 delta=other-p;
        float relativePlane=max(abs(dot(delta,n)),abs(dot(delta,otherNormal)))/max(min(depth,otherDepth),.0001);
        float agreement=dot(n,otherNormal);
        if(agreement>=spRefinement.w&&relativePlane<=spOptions.x)continue;
        vec3 tangentNormal=otherNormal-n*agreement;
        float denominator=dot(tangentNormal,tangentNormal);if(denominator<.0001)continue;
        vec3 intersection=p-tangentNormal*(dot(p-other,otherNormal)/denominator);
        vec3 otherIntersection=other-(n-otherNormal*agreement)*(dot(other-p,n)/denominator);
        float otherRadius=SPGiWorldPixelFootprint(other,otherNormal)*float(radius+2);
        // Intersect finite observed patches, not just their infinite planes.
        // A separated foreground/back wall depth edge belongs to the coverage
        // allocator, even if its tangent planes happen to intersect on screen.
        if(length(intersection-p)>worldRadius*1.25||
            length(otherIntersection-other)>otherRadius*1.25||
            length(delta)>1.5*(worldRadius+otherRadius))continue;
        vec4 clip=spViewProjection*vec4(intersection,1);if(clip.w<=0)continue;
        vec2 intersectionPixel=(clip.xy/clip.w*.5+.5)*vec2(spScreen.xy)-.5;
        float distance=length(intersectionPixel-vec2(pixel));
        strength=max(strength,1.0-smoothstep(0.0,float(radius),distance));
    }
    return strength;
}
// Both slots execute the same deterministic classification, so paired position
// and normal texels cannot disagree and no pass reads concurrent spawn results.
// A representative remains an actual GBuffer sample, never an averaged point
// lying inside a curved surface or between separated foreground/background.
vec4 SPGiSelectRepresentative(ivec2 tile,int sampleIndex,bool normalPart) {
    ivec2 cells=SPGiSurfaceLayout();int cellIndex=sampleIndex/2,representative=sampleIndex%2;
    ivec2 cell=ivec2(cellIndex%cells.x,cellIndex/cells.x),begin,end;
    SPGiCellBounds(tile,cell,cells,begin,end);
    if(any(greaterThanEqual(begin,end)))return vec4(0);
    vec2 center=(vec2(begin+end)-1.0)*.5;
    ivec2 seedPixel=ivec2(floor(center+.5));vec3 seedP,seedN;float seedDepth;
    bool haveSeed=SPSurface(seedPixel,seedP,seedN,seedDepth);
    if(!haveSeed) {
        float closest=1e30;
        for(int y=begin.y;y<end.y;++y)for(int x=begin.x;x<end.x;++x) {
            ivec2 at=ivec2(x,y);vec3 p,n;float depth;if(!SPSurface(at,p,n,depth))continue;
            float distance=dot(vec2(at)-center,vec2(at)-center);
            if(distance<closest){closest=distance;seedPixel=at;seedP=p;seedN=n;seedDepth=depth;haveSeed=true;}
        }
    }
    if(!haveSeed)return vec4(0);
    vec2 positionSum[2];positionSum[0]=vec2(0);positionSum[1]=vec2(0);
    ivec2 counts=ivec2(0);vec2 minCoverage=vec2(1);
    bool curvatureEnabled=representative==0&&SPGiCurvatureEnabled();
    float minimumNormalAgreement=1.0,maximumPlaneError=0;
    vec3 secondP=vec3(0),secondN=vec3(0);float secondDepth=0,secondScore=1e30;
    for(int y=begin.y;y<end.y;++y)for(int x=begin.x;x<end.x;++x) {
        ivec2 at=ivec2(x,y);vec3 p,n;float depth;if(!SPSurface(at,p,n,depth))continue;
        float compatible=SPGiClusterPlaneWeight(p,n,depth,vec4(seedP,1),vec4(seedN,seedDepth));
        int cluster=compatible>0?0:1;
        // A secondary fragment never emits the primary cluster. Avoid its
        // unused coarse lookups; the primary fragment retains the complete
        // second-seed classification, including possible third-surface pixels.
        if(representative==1&&cluster==0)continue;
        float coverage=SPUniformCoverage(at,p,n,depth);
        positionSum[cluster]+=vec2(at);++counts[cluster];minCoverage[cluster]=min(minCoverage[cluster],coverage);
        if(curvatureEnabled&&cluster==0) {
            minimumNormalAgreement=min(minimumNormalAgreement,dot(n,seedN));
            vec3 delta=p-seedP;
            maximumPlaneError=max(maximumPlaneError,max(abs(dot(delta,n)),abs(dot(delta,seedN))));
        }
        if(cluster==1) {
            float score=coverage+1e-7*dot(vec2(at)-center,vec2(at)-center);
            if(score<secondScore){secondScore=score;secondP=p;secondN=n;secondDepth=depth;}
        }
    }
    bool multipleSurfaces=secondDepth>0;
    if(representative==1&&!multipleSurfaces)return vec4(0);
    float curvatureActivation=0.0;
    if(curvatureEnabled&&!multipleSurfaces&&minCoverage.x>=SP_MIN_WEIGHT) {
        float planeBudget=max(SPGiWorldPixelFootprint(seedP,seedN)*spCurvature.y,.000001);
        float error=max(max(1.0-minimumNormalAgreement,0.0)/max(1.0-spCurvature.x,.0000001),
            maximumPlaneError/planeBudget);
        // Past the upper ramp endpoint coarse support cannot change activation.
        if(error<1.5)error=max(error,SPGiCoarseCurvatureError(seedPixel,seedP,seedN,seedDepth,planeBudget));
        curvatureActivation=smoothstep(.5,1.5,error);
    }
    // Homogeneous covered cells do not consume ray work. A real junction can
    // still request the first representative without inventing a second face.
    if(representative==0&&!multipleSurfaces&&minCoverage.x>=SP_MIN_WEIGHT&&curvatureActivation<=.0001) {
        float strength=SPGiIntersectionStrength(seedPixel,seedP,seedN,seedDepth);
        if(SPGiRefinementConfidence(strength)<=.0001)
            return normalPart?vec4(seedN,seedDepth):vec4(seedP,1);
    }
    vec2 clusterCenter=positionSum[representative]/float(max(counts[representative],1));
    vec3 selectedP=vec3(0),selectedN=vec3(0);float selectedDepth=0,closest=1e30;
    ivec2 selectedPixel=seedPixel;
    // The rounded centroid is the closest integer sample globally. If it belongs
    // to this surface, the full search must choose it as well. ceil(c-.5) keeps
    // the original row-major lower-coordinate winner on exact half-pixel ties.
    ivec2 rounded=clamp(ivec2(ceil(clusterCenter-.5)),begin,end-1);
    vec3 roundedP,roundedN;float roundedDepth;
    if(SPSurface(rounded,roundedP,roundedN,roundedDepth)) {
        bool primary=SPGiClusterPlaneWeight(roundedP,roundedN,roundedDepth,vec4(seedP,1),vec4(seedN,seedDepth))>0;
        bool target=(representative==0)==primary;
        if(representative==1)target=target&&SPGiClusterPlaneWeight(roundedP,roundedN,roundedDepth,vec4(secondP,1),vec4(secondN,secondDepth))>0;
        if(target){selectedPixel=rounded;selectedP=roundedP;selectedN=roundedN;selectedDepth=roundedDepth;}
    }
    if(selectedDepth<=0) {
        for(int y=begin.y;y<end.y;++y)for(int x=begin.x;x<end.x;++x) {
            ivec2 at=ivec2(x,y);vec3 p,n;float depth;if(!SPSurface(at,p,n,depth))continue;
            bool primary=SPGiClusterPlaneWeight(p,n,depth,vec4(seedP,1),vec4(seedN,seedDepth))>0;
            if((representative==0)!=primary)continue;
            if(representative==1&&SPGiClusterPlaneWeight(p,n,depth,vec4(secondP,1),vec4(secondN,secondDepth))<=0)continue;
            float distance=dot(vec2(at)-clusterCenter,vec2(at)-clusterCenter);
            if(distance<closest){closest=distance;selectedPixel=at;selectedP=p;selectedN=n;selectedDepth=depth;}
        }
    }
    if(selectedDepth<=0)return vec4(0);
    float strength=0,state=1;
    // Distinct foreground/background representatives are coverage obligations,
    // not fictitious infinite-plane junctions. Each side is shaded separately.
    if(multipleSurfaces||minCoverage[representative]<SP_MIN_WEIGHT)state=2;
    else {
        strength=SPGiIntersectionStrength(selectedPixel,selectedP,selectedN,selectedDepth);
        if(SPGiRefinementConfidence(strength)>.0001)state=3.0+strength;
        else if(curvatureActivation>.0001)state=5.0+curvatureActivation;
    }
    return normalPart?vec4(selectedN,selectedDepth):vec4(selectedP,state);
}

)GLSL"; }
// UE5.7.1 LumenScreenProbeGather.usf:142-201,213-234,1695-1700,
// 1717-1778 and 1798-1915. These weights and candidates are independent of
// this renderer's legacy cosine/refinement surface-representative policy.
inline std::string UePlacementGLSL(){return R"GLSL(
float SPUePlaneWeight(vec3 p,vec3 n,float depth,vec4 probe){
    if(probe.w<.5)return 0.0;
    return exp2(-200.0*abs(dot(probe.xyz-p,n))/depth);
}
float SPUeUniformCoverage(ivec2 pixel,vec3 p,vec3 n,float depth){
    ivec2 base=SPCornerBase(pixel),full=clamp(pixel-SPJitter(),ivec2(0),spScreen.xy-1);
    vec2 f=vec2(full-base*spGrid.w+1)/float(spGrid.w+2);float total=0;
    for(int c=0;c<4;++c){ivec2 tile=SPCorner(base,c);vec4 nd=SPNormalDepth(spUniform,tile);
        float bilinear=(c%2==0?1-f.x:f.x)*(c/2==0?1-f.y:f.y);
        if(nd.w>0)total+=bilinear*SPUePlaneWeight(p,n,depth,SPPosition(spUniform,tile));}
    return total;
}
float SPUeAdaptiveWeight(ivec2 pixel,vec3 p,vec3 n,float depth,ivec2 otherPixel,vec4 other){
    vec2 delta=abs(vec2(otherPixel)-vec2(pixel));
    return SPUePlaneWeight(p,n,depth,other)*(1.0-clamp(min(delta.x,delta.y)/float(spGrid.w),0.0,1.0));
}
void SPUeMarkCandidate(ivec2 tile,int sampleIndex,out vec4 position,out vec4 normalDepth){
    position=vec4(0);normalDepth=vec4(0);if(spOptions.z<.5)return;
    ivec2 pixel=SPCandidatePixel(tile,sampleIndex);vec3 p,n;float depth;
    if(!SPSurface(pixel,p,n,depth))return;
    position=vec4(p,SPUeUniformCoverage(pixel,p,n,depth)<.01?2.0:1.0);normalDepth=vec4(n,depth);
}
bool SPUeSpawnCandidate(ivec2 tile,int sampleIndex,vec4 position,vec4 normalDepth){
    if(spOptions.z<.5||position.w<1.5)return false;
    ivec2 pixel=SPCandidatePixel(tile,sampleIndex),base=SPCornerBase(pixel);vec4 weights=vec4(0);
    for(int c=0;c<4;++c){ivec2 neighbor=SPCorner(base,c);
        for(int previous=0;previous<16;++previous){if(previous>=sampleIndex)break;
            vec4 other=SPPosition(spMarked,SPSlot(neighbor,previous));if(other.w<1.5)continue;
            weights[c]=max(weights[c],SPUeAdaptiveWeight(pixel,position.xyz,normalDepth.xyz,normalDepth.w,
                SPCandidatePixel(neighbor,previous),other));
            if(dot(weights,vec4(1))>=.01)return false;}}
    return true;
}
)GLSL";}
inline std::string UniformShader(){return Common()+R"GLSL(
void main(){ivec2 texel=ivec2(gl_FragCoord.xy),tile=ivec2(texel.x/2,texel.y);
    ivec2 pixel=SPUniformPixel(tile);vec3 p,n;float depth;bool valid=SPSurface(pixel,p,n,depth);
    if(texel.x%2!=0){outColor=valid?vec4(n,depth):vec4(0);return;}
    float state=valid?1.0+SPGiIntersectionStrength(pixel,p,n,depth):1.0;
    outColor=!valid?vec4(0):vec4(p,state);}
)GLSL";}
inline std::string MarkShader(bool uePlacement=false){
    if(uePlacement)return Common()+UePlacementGLSL()+R"GLSL(
void main(){ivec2 texel=ivec2(gl_FragCoord.xy),slot=ivec2(texel.x/2,texel.y);
    ivec2 tile=ivec2(slot.x/spGrid.z,slot.y);int sampleIndex=slot.x%spGrid.z;
    vec4 position,normalDepth;SPUeMarkCandidate(tile,sampleIndex,position,normalDepth);
    outColor=texel.x%2==0?position:normalDepth;}
)GLSL";
    return Common()+R"GLSL(
void main(){ivec2 texel=ivec2(gl_FragCoord.xy),slot=ivec2(texel.x/2,texel.y);
    ivec2 tile=ivec2(slot.x/spGrid.z,slot.y);int sampleIndex=slot.x%spGrid.z;
    if((spTracing.w&2)!=0) {
        if(spOptions.z<.5&&spScreen.w!=1){outColor=vec4(0);return;}
        outColor=SPGiSelectRepresentative(tile,sampleIndex,texel.x%2!=0);return;
    }
    ivec2 pixel=SPCandidatePixel(tile,sampleIndex);vec3 p,n;float depth;
    if(!SPSurface(pixel,p,n,depth)){outColor=vec4(0);return;}
    float state=SPUniformCoverage(pixel,p,n,depth)<SP_MIN_WEIGHT?2.0:1.0;
    outColor=texel.x%2==0?vec4(p,state):vec4(n,depth);}
)GLSL";}
inline std::string SpawnShader(bool uePlacement=false){
    if(uePlacement)return Common()+UePlacementGLSL()+R"GLSL(
void main(){ivec2 texel=ivec2(gl_FragCoord.xy),slot=ivec2(texel.x/2,texel.y);
    ivec2 tile=ivec2(slot.x/spGrid.z,slot.y);int sampleIndex=slot.x%spGrid.z;
    vec4 position=SPPosition(spMarked,slot),normalDepth=SPNormalDepth(spMarked,slot);
    bool place=SPUeSpawnCandidate(tile,sampleIndex,position,normalDepth);
    outColor=!place?vec4(0):(texel.x%2==0?vec4(position.xyz,1):normalDepth);}
)GLSL";
    return Common()+R"GLSL(
void main(){ivec2 texel=ivec2(gl_FragCoord.xy),slot=ivec2(texel.x/2,texel.y);
    ivec2 tile=ivec2(slot.x/spGrid.z,slot.y);int sampleIndex=slot.x%spGrid.z;
    vec4 p=SPPosition(spMarked,slot),nd=SPNormalDepth(spMarked,slot);
    if(spOptions.z<.5||p.w<1.5){outColor=vec4(0);return;}
    if((spTracing.w&2)!=0) {
        // Both deterministic surface representatives own fixed slots. Never
        // suppress one using concurrently written spawn results; this preserves
        // two-sided coverage without a transitive suppression hole or a race.
        outColor=texel.x%2==0?vec4(p.xyz,p.w>2.5?p.w-1.0:1.0):nd;return;
    }
    ivec2 pixel=SPCandidatePixel(tile,sampleIndex),base=SPCornerBase(pixel);
    vec4 weights=vec4(0);bool place=true;
    // UE checks lower-index marked candidates, rather than recursively reading
    // concurrently written spawn decisions. This keeps the pass race-free.
    for(int c=0;c<4&&place;++c){ivec2 neighbor=SPCorner(base,c);
        for(int previous=0;previous<16;++previous){if(previous>=sampleIndex)break;
            ivec2 otherSlot=SPSlot(neighbor,previous);vec4 other=SPPosition(spMarked,otherSlot);
            if(other.w<1.5)continue;
            vec4 otherNormal=(spTracing.w&2)!=0?SPNormalDepth(spMarked,otherSlot):vec4(0);
            weights[c]=max(weights[c],SPAdaptiveWeight(pixel,p.xyz,nd.xyz,nd.w,SPCandidatePixel(neighbor,previous),other,otherNormal));
            if(dot(weights,vec4(1))>=SP_MIN_WEIGHT){place=false;break;}}
    }
    outColor=!place?vec4(0):(texel.x%2==0?vec4(p.xyz,1):nd);}
)GLSL";}
inline std::string CoverageShader(){return Common()+R"GLSL(
void main(){ivec2 pixel=ivec2(gl_FragCoord.xy);vec3 p,n;float depth;
    vec4 scene=texelFetch(spInput,pixel,0);
    if(!SPSurface(pixel,p,n,depth)){outColor=scene;return;}
    vec4 weights=SPUniformWeights(pixel,p,n,depth);float uniformWeight=dot(weights,vec4(1));
    if(SPGiRefinementEnabled()) {
        vec4 adaptiveLighting[4];vec4 uw,aw;float unrefined;
        SPGiReconstructionStencil(pixel,p,n,depth,spUniform,spSpawned,spUniform,false,adaptiveLighting,uw,aw,unrefined);
        float total=dot(uw+aw,vec4(1));
        vec3 tint=total<.01?vec3(.75,.03,.04):
            (dot(aw,vec4(1))>.0001?vec3(1,.30,.03):vec3(.04,.22,.60));
        outColor=vec4(mix(scene.rgb,tint,.65),scene.a);return;
    }
    if((spTracing.w&2)!=0) {
        float adaptive=0;
        if(spOptions.z>.5&&uniformWeight<SP_MIN_WEIGHT) {
            ivec2 tile=pixel/spGrid.w;
            for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x) {
                ivec2 neighbor=tile+ivec2(x,y);
                if(any(lessThan(neighbor,ivec2(0)))||any(greaterThanEqual(neighbor,spGrid.xy)))continue;
                for(int s=0;s<16;++s){if(s>=spGrid.z)break;ivec2 slot=SPSlot(neighbor,s);
                    adaptive=max(adaptive,SPGiAdaptiveWeight(pixel,p,n,depth,SPPosition(spSpawned,slot),SPNormalDepth(spSpawned,slot)));}
            }
        }
        vec3 tint=uniformWeight>=SP_MIN_WEIGHT?vec3(.04,.22,.60):
            (adaptive>=SP_MIN_WEIGHT?vec3(1,.30,.03):vec3(.75,.03,.04));
        outColor=vec4(mix(scene.rgb,tint,.65),scene.a);return;
    }
    ivec2 base=SPCornerBase(pixel);
    if(spOptions.z>.5)for(int c=0;c<4;++c){if(weights[c]>=SP_MIN_WEIGHT)continue;ivec2 neighbor=SPCorner(base,c);
        for(int s=0;s<16;++s){if(s>=spGrid.z)break;ivec2 slot=SPSlot(neighbor,s);vec4 probe=SPPosition(spSpawned,slot);
            vec4 nd=(spTracing.w&2)!=0?SPNormalDepth(spSpawned,slot):vec4(0);
            weights[c]=max(weights[c],SPAdaptiveWeight(pixel,p,n,depth,SPCandidatePixel(neighbor,s),probe,nd));}}
    vec3 tint=uniformWeight>=SP_MIN_WEIGHT?vec3(.04,.22,.60):
        (dot(weights,vec4(1))>=SP_MIN_WEIGHT?vec3(1,.30,.03):vec3(.75,.03,.04));
    outColor=vec4(mix(scene.rgb,tint,.65),scene.a);}
)GLSL";}
inline std::string MarkerVertexShader(){return std::string("#version 450 core\n")+Constants()+R"GLSL(
layout(location=0) in vec2 position;layout(location=1) in vec2 uv;
layout(binding=0) uniform sampler2D spAtlas;
out vec2 markerPosition;flat out vec3 markerColor;
flat out vec2 markerSphere;flat out vec4 markerDepthProjection;
flat out int markerProbe;
void main(){ivec2 entries=textureSize(spAtlas,0)/ivec2(2,1);
    ivec2 slot=ivec2(gl_InstanceID%entries.x,gl_InstanceID/entries.x);
    vec4 p=texelFetch(spAtlas,ivec2(slot.x*2,slot.y),0),nd=texelFetch(spAtlas,ivec2(slot.x*2+1,slot.y),0);
    markerPosition=position;markerColor=spOptions.w<.5?vec3(.04,.34,1):vec3(1,.32,.03);
    markerProbe=gl_InstanceID+(spOptions.w>.5?spTraceAtlas.z:0);
    markerSphere=vec2(0);markerDepthProjection=vec4(0);
    if(spScreen.w==1&&spOptions.w>.5)markerColor=p.w>1.5?vec3(1,.20,.02):vec3(.06,.85,.24);
    if(p.w<.5){gl_Position=vec4(2,2,2,1);return;}
    // Display a sphere tangent to the stored surface position. Its curved front
    // depth keeps sloped surfaces from cutting a constant-depth disk in half.
    // The atlas position stays unchanged, including while placement is frozen.
    vec4 probeClip=spViewProjection*vec4(p.xyz,1);
    if(probeClip.w<=0){gl_Position=vec4(2,2,2,1);return;}
    float radiusWorld=2.0*probeClip.w*spOptions.y*abs(spInverseProjection[1][1])/float(spScreen.y);
    vec3 world=p.xyz+nd.xyz*radiusWorld;
    vec4 clip=spViewProjection*vec4(world,1);
    if(clip.w<=0){gl_Position=vec4(2,2,2,1);return;}
    markerSphere=vec2((spView*vec4(world,1)).z,radiusWorld);
    mat4 projection=spViewProjection*spInverseView;
    // Include the W terms as well, so perspective and orthographic cameras use
    // the same depth reconstruction without assuming clipW == -viewZ.
    markerDepthProjection=vec4(projection[2][2],projection[3][2],projection[2][3],projection[3][3]);
    clip.xy+=position*spOptions.y*2.0/vec2(spScreen.xy)*clip.w;gl_Position=clip;
}
)GLSL";}
inline std::string MarkerFragmentShader(const std::string& traceGLSL={}){return std::string("#version 450 core\n")+Constants()+traceGLSL+R"GLSL(
in vec2 markerPosition;flat in vec3 markerColor;
flat in vec2 markerSphere;flat in vec4 markerDepthProjection;
flat in int markerProbe;
layout(location=0) out vec4 outColor;
void main(){float radiusSquared=dot(markerPosition,markerPosition);if(radiusSquared>1)discard;
    float sphereFront=sqrt(max(0.0,1.0-radiusSquared));
    float viewZ=markerSphere.x+markerSphere.y*sphereFront;
    float clipZ=markerDepthProjection.x*viewZ+markerDepthProjection.y;
    float clipW=markerDepthProjection.z*viewZ+markerDepthProjection.w;if(clipW<=0)discard;
    gl_FragDepth=clamp((clipZ/clipW)*.5+.5,0.0,1.0);
    float shading=.4+.6*max(dot(normalize(vec3(markerPosition,sphereFront)),normalize(vec3(-.3,.5,.8))),0.0);
    vec3 color=markerColor*shading;
    if(spTracing.w!=0&&spScreen.w!=1) {
        vec3 worldNormal=normalize(mat3(spInverseView)*vec3(markerPosition,sphereFront));
        int rayCount=spTracing.x*spTracing.x,bestRay=0;float bestDot=-2;
        for(int ray=0;ray<256;++ray){if(ray>=rayCount)break;float score=dot(worldNormal,STReadRay(markerProbe,ray,3).xyz);
            if(score>bestDot){bestDot=score;bestRay=ray;}}
        int address=(markerProbe*rayCount+bestRay)*4;
        vec4 result=texelFetch(stRays,ivec2(address%spTraceAtlas.x,address/spTraceAtlas.x),0);
        color=STDiagnosticColor(result.rgb,result.a);
        if(markerProbe==spTraceDisplay.y&&radiusSquared>.78)color=vec3(1.5);
        else if((spTracing.w&2)!=0&&radiusSquared>.60)color=markerColor;
    }
    outColor=vec4(color,1);}
)GLSL";}
} // namespace Render::ScreenProbePlacementDetail
