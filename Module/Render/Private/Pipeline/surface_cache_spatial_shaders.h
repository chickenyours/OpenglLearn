#pragma once
#include <string>
#include "screen_probe_trace_shaders.h"
#include "surface_cache_shaders.h"

namespace Render::SurfaceCacheDetail {
inline std::string SpatialConstantsGLSL() {return R"GLSL(
layout(std140,binding=14) uniform SpatialFilterData {
    ivec4 sfOptions; // pass index, half kernel size, visibility test, reserved
    vec4 sfFilter;  // relative-depth scale, hit angle radians, normal cosine, strength
    vec4 sfParams;  // origin bias, relative symmetric-plane threshold, reserved
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
// A table has eight RGBA32F texels per logical probe; x=index+1, y=weight.
inline std::string SpatialNeighborsShader() {
    return std::string("#version 450 core\n")+ScreenProbeTraceDetail::ConstantsGLSL()+
        SpatialConstantsGLSL()+SpatialProbeGLSL()+LookupGLSL()+GeometryGLSL()+R"GLSL(
in vec2 vUV;layout(location=0) out vec4 outColor;
float SFGeometryWeight(vec4 p,vec4 nd,vec4 other,vec4 otherNd) {
    if(!SFProbeValid(other,otherNd))return 0;
    vec3 n=normalize(nd.xyz),nn=normalize(otherNd.xyz);
    float agreement=dot(n,nn);if(agreement<sfFilter.z)return 0;
    vec3 delta=other.xyz-p.xyz;
    float depth=max(min(nd.w,otherNd.w),.0001);
    float plane=max(abs(dot(delta,n)),abs(dot(delta,nn)))/depth;
    if(plane>sfParams.y)return 0;
    float relativeDepth=abs(nd.w-otherNd.w)/depth;
    float positionWeight=exp2(-sfFilter.x*relativeDepth*relativeDepth);
    // The hard normal/plane gates protect sharp geometry; the smooth terms
    // prevent an accepted but marginal candidate from dominating a corner.
    return positionWeight*pow(max(agreement,0.0),8.0)*
        exp2(-plane*plane/max(sfParams.y*sfParams.y,.00000001));
}
bool SFPairVisible(vec4 p,vec4 nd,vec4 other,vec4 otherNd) {
    float bias=max(sfParams.x,.00001);
    vec3 a=p.xyz+normalize(nd.xyz)*bias,b=other.xyz+normalize(otherNd.xyz)*bias;
    vec3 delta=b-a;float distance=length(delta);
    if(distance<=bias*.25)return true;
    vec3 unused;int triangle;bool exhausted;
    float maximum=max(distance-.0001,0.0);
    bool blocked=SCIntersect(a,delta/distance,maximum,unused,triangle,exhausted,true);
    // An exhausted traversal cannot establish visibility.
    return !blocked&&!exhausted;
}
void main() {
    ivec2 pixel=ivec2(gl_FragCoord.xy);
    int pair=pixel.x%8,probe=pixel.x/8+pixel.y*spGrid.x;
    outColor=vec4(0);
    if(probe>=spTraceAtlas.w||pair>=4*sfOptions.y||sfFilter.w<=0)return;
    vec4 p=SFProbe(probe,0),nd=SFProbe(probe,1);
    if(!SFProbeValid(p,nd))return;
    int tileIndex=probe<spTraceAtlas.z?probe:(probe-spTraceAtlas.z)/spGrid.z;
    ivec2 tile=ivec2(tileIndex%spGrid.x,tileIndex/spGrid.x);
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
    if(sfOptions.z!=0&&!SFPairVisible(p,nd,other,otherNd))return;
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
    direction=normalize(direction);
    float ownDistance=SFHitDistance(probe,ray,state);
    vec3 sum=max(outColor.rgb,vec3(0));float totalWeight=1.0;
    for(int pair=0;pair<8;++pair) {
        if(pair>=4*sfOptions.y)break;
        ivec2 at=ivec2((probe%spGrid.x)*8+pair,probe/spGrid.x);
        vec4 neighbor=texelFetch(sfNeighbors,at,0);
        if(neighbor.x<.5||neighbor.y<.0001)continue;
        int index=int(neighbor.x+.5)-1;
        vec3 neighborPosition=SFProbe(index,0).xyz,normal=normalize(SFProbe(index,1).xyz);
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
        vec3 toNeighborHit=neighborPosition+direction*distance-position.xyz;
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
