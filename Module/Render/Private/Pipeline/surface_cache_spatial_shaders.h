#pragma once
#include <string>
#include "Render/Public/Pipeline/surface_cache_gi.h"
#include "screen_probe_trace_shaders.h"
#include "screen_probe_placement_shaders.h"
#include "surface_cache_shaders.h"

namespace Render::SurfaceCacheDetail {
inline std::string SpatialConstantsGLSL() {return "\nconst int SF_NEIGHBOR_COUNT="+
    std::to_string(SurfaceCacheSpatialNeighborCount)+";\n"+R"GLSL(
layout(std140,binding=14) uniform SpatialFilterData {
    ivec4 sfOptions; // pass index, half kernel size, visibility test, safe-origin atlas active
    vec4 sfFilter;  // relative-depth scale, hit angle radians, normal cosine, strength
    vec4 sfParams;  // origin bias, relative symmetric-plane threshold, numeric endpoint epsilon, local adaptive neighbors
};
)GLSL";}

inline std::string SpatialProbeGLSL() {return R"GLSL(
layout(binding=3) uniform sampler2D sfUniform;
layout(binding=4) uniform sampler2D sfSpawned;
bool SFFinite(vec3 v) {return !any(isnan(v))&&!any(isinf(v));}
vec4 SFProbe(int probe,int part) {
    bool uniformProbe=probe<spTraceAtlas.z;
    int slot=uniformProbe?probe:probe-spTraceAtlas.z;
    int columns=spGrid.x*(uniformProbe?1:spGrid.z);
    ivec2 at=ivec2(2*(slot%columns)+part,slot/columns);
    return uniformProbe?texelFetch(sfUniform,at,0):texelFetch(sfSpawned,at,0);
}
bool SFProbeValid(vec4 p,vec4 nd) {
    return p.w>.5&&nd.w>0&&SFFinite(p.xyz)&&SFFinite(nd.xyz)&&dot(nd.xyz,nd.xyz)>.5;
}
)GLSL";}

// UE LumenScreenProbeFiltering.usf: GetFilterPositionWeight and
// GatherNeighborRadiance. Cache the geometry and optional visibility result
// per probe pair, rather than re-tracing that segment for every angular cell.
// This is important for software BVH tracing on the 64/256-ray probe grids.
// The first eight entries retain the original remote stencil. Eight additional
// entries contain every live same-tile surface representative in a fixed local
// cell stencil, never a nearest/top-one winner. A linear table keeps its texture
// width bounded independently of the probe grid. x=index+1, y=weight.
inline std::string SpatialNeighborsShader() {
    return std::string("#version 450 core\n")+ScreenProbeTraceDetail::ConstantsGLSL()+
        ScreenProbePlacementDetail::CurvatureWeightGLSL()+
        SpatialConstantsGLSL()+SpatialProbeGLSL()+LookupGLSL()+GeometryGLSL()+R"GLSL(
in vec2 vUV;layout(location=0) out vec4 outColor;
layout(binding=10) uniform sampler2D sfSafeOrigins;
float SFGeometryWeight(vec4 p,vec4 nd,vec4 other,vec4 otherNd) {
    if(!SFProbeValid(other,otherNd))return 0;
    vec3 n=normalize(nd.xyz),nn=normalize(otherNd.xyz);
    float agreement=dot(n,nn);if(agreement<sfFilter.z)return 0;
    // Our curved nearest-stratum warp is not measure preserving. Repeating it
    // across three passes can leave a probe-dependent dark bias even after
    // the Monte Carlo noise converges. In stationary high-confidence history,
    // reuse only the exact one-to-one local angular frame; retain ordinary
    // spatial filtering for flat walls and all the existing contact filters.
    if((int(spRefinement.x+.5)&16)!=0&&
        (agreement<=1.0-1e-6||(abs(n.z)<.99)!=(abs(nn.z)<.99)))return 0;
    vec3 delta=other.xyz-p.xyz;
    float depth=max(min(nd.w,otherNd.w),.0001);
    bool curvedWeights=SPGiCurvatureAwareWeightsEnabled();
    float plane=(curvedWeights?SPGiMidpointPlaneDistance(delta,n,nn):
        max(abs(dot(delta,n)),abs(dot(delta,nn))))/depth;
    if(plane>sfParams.y)return 0;
    float relativeDepth=abs(nd.w-otherNd.w)/depth;
    float positionWeight=exp2(-sfFilter.x*relativeDepth*relativeDepth);
    // The hard normal/plane gates protect sharp geometry; the smooth terms
    // prevent an accepted but marginal candidate from dominating a corner.
    float activation=other.w>=4.0?clamp(other.w-4.0,0.0,1.0):1.0;
    if(curvedWeights) {
        float curved=SPGiCurvedNormalWeight(agreement,sfFilter.z)*
            exp2(-plane*plane/max(sfParams.y*sfParams.y,.00000001))*SPGiCurvedPlaneBoundaryWeight(plane,sfParams.y);
        float strictPlane=max(abs(dot(delta,n)),abs(dot(delta,nn)))/depth;
        float strict=strictPlane>sfParams.y?0.0:pow(max(agreement,0.0),8.0)*
            exp2(-strictPlane*strictPlane/max(sfParams.y*sfParams.y,.00000001));
        return activation*positionWeight*mix(strict,curved,SPGiCurvedNormalSupport(n,nn));
    }
    return activation*positionWeight*pow(max(agreement,0.0),8.0)*
        exp2(-plane*plane/max(sfParams.y*sfParams.y,.00000001));
}
bool SFPairVisible(int probe,int otherProbe,vec4 p,vec4 nd,vec4 other,vec4 otherNd) {
    float bias=max(sfParams.x,.00001);
    vec3 a=p.xyz+normalize(nd.xyz)*bias,b=other.xyz+normalize(otherNd.xyz)*bias;
    float endpointEpsilon=.0001;
    if(sfOptions.w!=0) {
        // Reuse the exact two lighting-ray origins. Reapplying the old normal
        // bias here could push an already limited displacement through a nearby
        // wall again and prevent otherwise valid spatial reuse in acute gaps.
        vec4 safeA=texelFetch(sfSafeOrigins,ivec2(probe%spGrid.x,probe/spGrid.x),0);
        vec4 safeB=texelFetch(sfSafeOrigins,ivec2(otherProbe%spGrid.x,otherProbe/spGrid.x),0);
        if(!(safeA.w>0)||!(safeB.w>0)||!SFFinite(safeA.xyz)||!SFFinite(safeB.xyz))return false;
        a=safeA.xyz;b=safeB.xyz;
        float scale=max(1.0,max(max(abs(a.x),max(abs(a.y),abs(a.z))),max(abs(b.x),max(abs(b.y),abs(b.z)))));
        endpointEpsilon=max(max(sfParams.z,.00001),8.0*1.1920928955078125e-7*scale);
    }
    vec3 delta=b-a;float distance=length(delta);
    // A tiny gap cannot establish visibility by skipping the BVH query. Keep
    // the legacy shortcut only when the safe-origin feature is disabled.
    if(sfOptions.w!=0) {if(distance<=2.0*endpointEpsilon)return false;}
    else if(distance<=bias*.25)return true;
    vec3 unused;int triangle;bool exhausted;
    float maximum=max(distance-endpointEpsilon,0.0);
    bool blocked=SCIntersect(a,delta/distance,maximum,unused,triangle,exhausted,true);
    // An exhausted traversal cannot establish visibility.
    return !blocked&&!exhausted;
}
ivec2 SFLocalCellLayout() {
    int cells=spGrid.z/2,columns=cells>=8?4:2;
    return ivec2(columns,cells/columns);
}
vec2 SFScreenPixel(vec3 p) {
    vec4 clip=spViewProjection*vec4(p,1);
    return clip.w>0?(clip.xy/clip.w*.5+.5)*vec2(spScreen.xy)-.5:vec2(-1e9);
}
float SFLocalBasis(float value) {
    float a=abs(value);
    if(a<.5)return .75-a*a;
    float edge=max(0.0,1.5-a);return .5*edge*edge;
}
float SFLocalKernel(vec4 p,vec4 other) {
    vec2 cellStep=vec2(spGrid.w)/vec2(SFLocalCellLayout());
    vec2 delta=(SFScreenPixel(other.xyz)-SFScreenPixel(p.xyz))/cellStep;
    // Normalize co-located support to one. The actual projected positions,
    // including moved surface representatives, determine this continuous C1
    // distance kernel; nominal subcell centers cannot create a bright winner.
    return SFLocalBasis(delta.x)*SFLocalBasis(delta.y)/(.75*.75);
}
int SFLocalCandidates(int probe,ivec2 tile,vec4 p,int wanted,out int chosen) {
    chosen=-1;
    if(sfParams.w<.5||spOptions.z<.5)return 0;
    ivec2 cells=SFLocalCellLayout();int base=spTraceAtlas.z+(tile.y*spGrid.x+tile.x)*spGrid.z;
    bool adaptiveProbe=probe>=spTraceAtlas.z;
    ivec2 cell;
    if(adaptiveProbe) {
        int index=((probe-spTraceAtlas.z)%spGrid.z)/2;
        cell=ivec2(index%cells.x,index/cells.x);
    } else {
        ivec2 pixel=clamp(ivec2(floor(SFScreenPixel(p.xyz)+.5))-tile*spGrid.w,ivec2(0),ivec2(spGrid.w-1));
        cell=min(pixel*cells/spGrid.w,cells-1);
    }
    // 2x1 / 2x2 / 4x2 layouts have at most four cells in this cardinal stencil.
    // For an adaptive receiver, removing itself leaves room for its uniform
    // same-tile probe: at most eight local candidates in every supported layout.
    int count=0;
    if(adaptiveProbe) {
        int baseProbe=tile.x+tile.y*spGrid.x;
        if(SFProbeValid(SFProbe(baseProbe,0),SFProbe(baseProbe,1))) {
            if(count==wanted)chosen=baseProbe;
            ++count;
        }
    }
    for(int candidate=0;candidate<16;++candidate) {
        if(candidate>=spGrid.z)break;
        int cellIndex=candidate/2;ivec2 otherCell=ivec2(cellIndex%cells.x,cellIndex/cells.x);
        if(abs(otherCell.x-cell.x)+abs(otherCell.y-cell.y)>1)continue;
        int index=base+candidate;
        if(index==probe||SFProbe(index,0).w<.5)continue;
        if(count==wanted)chosen=index;
        ++count;
    }
    return count;
}
void main() {
    ivec2 pixel=ivec2(gl_FragCoord.xy);
    int tableWidth=min(spGrid.x*SF_NEIGHBOR_COUNT,8192);
    int address=pixel.x+pixel.y*tableWidth;
    int pair=address%SF_NEIGHBOR_COUNT,probe=address/SF_NEIGHBOR_COUNT;
    outColor=vec4(0);
    if(probe>=spTraceAtlas.w||sfFilter.w<=0)return;
    vec4 p=SFProbe(probe,0),nd=SFProbe(probe,1);
    if(!SFProbeValid(p,nd))return;
    int tileIndex=probe<spTraceAtlas.z?probe:(probe-spTraceAtlas.z)/spGrid.z;
    ivec2 tile=ivec2(tileIndex%spGrid.x,tileIndex/spGrid.x);
    if(pair>=8) {
        int chosen;int count=SFLocalCandidates(probe,tile,p,pair-8,chosen);
        // Count active candidates before geometry/visibility gates, so each
        // pair performs at most one BVH query. Rejected entries keep xy=0;
        // this conservative bound still skips the local ray loop entirely for
        // tiles with no adaptive support and skips unused trailing entries.
        if(pair==8)outColor.z=float(count);
        if(chosen<0)return;
        vec4 other=SFProbe(chosen,0),otherNd=SFProbe(chosen,1);
        float weight=SFGeometryWeight(p,nd,other,otherNd)*SFLocalKernel(p,other);
        if(weight<.0001)return;
        if(sfOptions.z!=0&&!SFPairVisible(probe,chosen,p,nd,other,otherNd))return;
        outColor.xy=vec2(float(chosen+1),weight);return;
    }
    if(pair>=4*sfOptions.y)return;
    const ivec2 offsets[4]=ivec2[4](ivec2(-1,0),ivec2(1,0),ivec2(0,-1),ivec2(0,1));
    // Lumen repeats the same adjacent-probe stencil on each filter pass.
    // Keeping the stride fixed also avoids jumping across a thin occluder.
    int radius=pair/4+1;
    ivec2 neighborTile=tile+offsets[pair%4]*radius;
    if(any(lessThan(neighborTile,ivec2(0)))||any(greaterThanEqual(neighborTile,spGrid.xy)))return;
    int chosen=neighborTile.x+neighborTile.y*spGrid.x;
    vec4 other=SFProbe(chosen,0),otherNd=SFProbe(chosen,1);
    float weight=SFGeometryWeight(p,nd,other,otherNd);
    float bestDistance=dot(other.xyz-p.xyz,other.xyz-p.xyz);
    if(spOptions.z>.5)for(int candidate=0;candidate<16;++candidate) {
        if(candidate>=spGrid.z)break;
        int index=spTraceAtlas.z+(neighborTile.y*spGrid.x+neighborTile.x)*spGrid.z+candidate;
        vec4 ap=SFProbe(index,0),an=SFProbe(index,1);
        float w=SFGeometryWeight(p,nd,ap,an),distance=dot(ap.xyz-p.xyz,ap.xyz-p.xyz);
        if(w>weight+.000001||(w>0&&abs(w-weight)<=.000001&&distance<bestDistance)) {
            chosen=index;other=ap;otherNd=an;weight=w;bestDistance=distance;
        }
    }
    if(weight<.0001)return;
    if(sfOptions.z!=0&&!SFPairVisible(probe,chosen,p,nd,other,otherNd))return;
    outColor=vec4(float(chosen+1),weight,0,0);
}
)GLSL";
}

// Filter directional incoming radiance before cosine integration, receiver
// albedo, reconstruction and temporal accumulation. Our direction bins are
// cosine-hemisphere strata (UE uses world-space octahedral bins). Reproject a
// world direction into each neighbor's local grid instead of assuming that
// equal local indices represent equal world directions. Actual sample angles
// choose the nearest directional measurement; the spatial hit-angle test uses
// a common world cone, as UE does after scattering traces into its gather grid.
// Preserve original trace status and all hit/direction metadata: spatial
// filtering never manufactures a valid ray out of an unreadable cache hole.
inline std::string SpatialFilterShader() {
    return std::string("#version 450 core\n")+ScreenProbeTraceDetail::ConstantsGLSL()+
        SpatialConstantsGLSL()+SpatialProbeGLSL()+R"GLSL(
in vec2 vUV;layout(location=0) out vec4 outColor;
layout(binding=0) uniform sampler2D sfInputRays;
layout(binding=5) uniform sampler2D sfNeighbors;
layout(binding=10) uniform sampler2D sfSafeOrigins;
vec4 SFRay(int probe,int ray,int part) {
    int address=(probe*spTracing.x*spTracing.x+ray)*4+part;
    return texelFetch(sfInputRays,ivec2(address%spTraceAtlas.x,address/spTraceAtlas.x),0);
}
bool SFReadable(int state) {return state==1||state==9||state==10||state==5||state==13;}
float SFHitDistance(int probe,int ray,int state) {
    // A completed empty ray is known black at the maximum trace distance;
    // treating it as a zero-length hit would reject every neighboring ray.
    if(state==13)return spTraceParams.x;
    return max(SFRay(probe,ray,1).w,0.0);
}
void main() {
    ivec2 pixel=ivec2(gl_FragCoord.xy);
    int address=pixel.x+pixel.y*spTraceAtlas.x,part=address%4;
    outColor=texelFetch(sfInputRays,pixel,0);
    if(part!=0||sfFilter.w<=0)return;
    int count=spTracing.x*spTracing.x,globalRay=address/4;
    int probe=globalRay/count,ray=globalRay%count,state=int(outColor.w+.5);
    if(probe>=spTraceAtlas.w||!SFReadable(state)||!SFFinite(outColor.rgb))return;
    vec4 position=SFProbe(probe,0),nd=SFProbe(probe,1);
    vec3 direction=SFRay(probe,ray,3).xyz;
    if(!SFProbeValid(position,nd)||!SFFinite(direction)||dot(direction,direction)<.5)return;
    vec3 traceOrigin=position.xyz;
    if(sfOptions.w!=0) {
        vec4 safe=texelFetch(sfSafeOrigins,ivec2(probe%spGrid.x,probe/spGrid.x),0);
        if(!(safe.w>0)||!SFFinite(safe.xyz))return;
        traceOrigin=safe.xyz;
    }
    direction=normalize(direction);
    float ownDistance=SFHitDistance(probe,ray,state);
    vec3 sum=max(outColor.rgb,vec3(0));float totalWeight=1.0;
    int tableWidth=textureSize(sfNeighbors,0).x;
    int localAddress=probe*SF_NEIGHBOR_COUNT+8;
    int localCount=clamp(int(texelFetch(sfNeighbors,ivec2(localAddress%tableWidth,localAddress/tableWidth),0).z+.5),0,8);
    for(int pair=0;pair<SF_NEIGHBOR_COUNT;++pair) {
        if(pair<8&&pair>=4*sfOptions.y)continue;
        if(pair>=8&&pair>=8+localCount)break;
        int address=probe*SF_NEIGHBOR_COUNT+pair;
        ivec2 at=ivec2(address%tableWidth,address/tableWidth);
        vec4 neighbor=texelFetch(sfNeighbors,at,0);
        if(neighbor.x<.5||neighbor.y<.0001)continue;
        int index=int(neighbor.x+.5)-1;
        vec3 neighborPosition=SFProbe(index,0).xyz,normal=normalize(SFProbe(index,1).xyz);
        if(sfOptions.w!=0) {
            vec4 safe=texelFetch(sfSafeOrigins,ivec2(index%spGrid.x,index/spGrid.x),0);
            if(!(safe.w>0)||!SFFinite(safe.xyz))continue;
            neighborPosition=safe.xyz;
        }
        float cosine=dot(direction,normal);if(cosine<=0)continue;
        // Inverse of STCosineRayDirection, using exactly the same local frame.
        // The four closest strata also account for independent per-probe
        // jitter and for the tangent-axis switch near abs(normal.z)==.99.
        vec3 axis=abs(normal.z)<.99?vec3(0,0,1):vec3(0,1,0);
        vec3 tangent=normalize(cross(axis,normal)),bitangent=cross(normal,tangent);
        vec2 lateral=vec2(dot(direction,tangent),dot(direction,bitangent));
        float azimuthCoordinate=dot(lateral,lateral)>1e-12?
            fract(atan(lateral.y,lateral.x)/6.283185307179586):0.0;
        vec2 square=vec2(clamp(1.0-cosine*cosine,0.0,1.0),
            azimuthCoordinate);
        ivec2 first=ivec2(floor(square*float(spTracing.x)-.5));
        vec3 ownNormal=normalize(nd.xyz);int closestRay=-1;
        if(dot(ownNormal,normal)>1.0-1e-6&&
            (abs(ownNormal.z)<.99)==(abs(normal.z)<.99)) {
            // Equal local frames already share the same directional strata.
            // Keep that mapping one-to-one: nearest jittered-ray selection
            // would duplicate some samples and omit others, biasing planar
            // energy even before the actual spatial weights are applied.
            closestRay=ray;
        } else {
            float bestDirection=-1;
            for(int sampleIndex=0;sampleIndex<4;++sampleIndex) {
                int radial=clamp(first.x+sampleIndex%2,0,spTracing.x-1);
                int azimuth=(first.y+sampleIndex/2+spTracing.x)%spTracing.x;
                int neighborRay=radial+azimuth*spTracing.x;
                vec3 neighborDirection=SFRay(index,neighborRay,3).xyz;
                if(!SFFinite(neighborDirection)||dot(neighborDirection,neighborDirection)<.5)continue;
                neighborDirection=normalize(neighborDirection);
                float agreement=dot(neighborDirection,direction);
                if(agreement>bestDirection){bestDirection=agreement;closestRay=neighborRay;}
            }
        }
        if(closestRay<0)continue;
        vec4 value=SFRay(index,closestRay,0);int neighborState=int(value.w+.5);
        // Select angular support before examining readability, so an unknown
        // closest sample cannot be replaced by a farther bright measurement.
        if(!SFReadable(neighborState)||!SFFinite(value.rgb))continue;
        // UE filters a common directional gather grid. Independent sampling
        // jitter belongs to angular resampling above, not the contact gate:
        // otherwise even co-located equal-normal probes reject most neighbors.
        // A long neighboring hit must still not wash out a near contact shadow.
        float distance=min(SFHitDistance(index,closestRay,neighborState),ownDistance);
        // Hit distances are measured from the shared tracing origins. Using
        // unshifted surface positions here mixes two coordinate baselines,
        // especially when acute-gap clipping gives neighbors different bias.
        vec3 toNeighborHit=neighborPosition+direction*distance-traceOrigin;
        float lengthSquared=dot(toNeighborHit,toNeighborHit);
        if(lengthSquared<.00000001)continue;
        float angle=acos(clamp(dot(toNeighborHit,direction)*inversesqrt(lengthSquared),-1.0,1.0));
        float weight=neighbor.y*max(0.0,1.0-angle/max(sfFilter.y,.000001));
        sum+=max(value.rgb,vec3(0))*weight;totalWeight+=weight;
    }
    outColor.rgb=mix(outColor.rgb,sum/totalWeight,clamp(sfFilter.w,0.0,1.0));
}
)GLSL";
}
} // namespace Render::SurfaceCacheDetail
