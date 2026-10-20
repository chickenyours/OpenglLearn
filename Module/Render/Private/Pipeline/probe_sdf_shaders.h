#pragma once
#include "Render/Private/Pipeline/screen_probe_trace_shaders.h"
#include "Render/Private/Pipeline/surface_cache_shaders.h"
#include <string>

namespace Render::ProbeSdfDetail {
inline std::string ConstantsGLSL() { return R"GLSL(
layout(std140,binding=10) uniform ProbeSdfData {
    vec4 psGlobalOrigin,psGlobalSpacing;
    ivec4 psGlobalCounts,psAtlas,psGeometry;
    vec4 psTrace;
    ivec4 psDisplay;
    vec4 psSlice;
    ivec4 psFlags;
    vec4 psClipOrigins[4],psClipSpacing[4];
    ivec4 psClipCounts[4],psClipScroll[4];
    ivec4 psClipmap;
    ivec4 psClipRanges[4];
    ivec4 psRangeAtlas;
    vec4 psBoundLines[48];
};
)GLSL"; }

inline std::string SamplingGLSL() { return R"GLSL(
layout(binding=6) uniform sampler2D psMeshes;
layout(binding=7) uniform sampler2D psObjects;
layout(binding=8) uniform sampler2D psGlobal;
layout(binding=11) uniform sampler2D psRanges;
bool PSFinite(vec3 value) { return !any(isnan(value))&&!any(isinf(value)); }
vec4 PSObject(int object,int part) {
    int index=object*psFlags.z+part;
    return texelFetch(psObjects,ivec2(index%psAtlas.y,index/psAtlas.y),0);
}
float PSBoxDistance(vec3 p,vec3 low,vec3 high) {
    return length(max(max(low-p,p-high),vec3(0)));
}
float PSMeshTexel(ivec3 cell,ivec3 counts,int offset) {
    int index=offset+(cell.z*counts.y+cell.y)*counts.x+cell.x;
    return texelFetch(psMeshes,ivec2(index%psAtlas.x,index/psAtlas.x),0).x;
}
float PSMeshSample(int object,vec3 p,out vec3 localPosition,out vec3 clampedPosition) {
    mat4 inverseTransform=mat4(PSObject(object,0),PSObject(object,1),PSObject(object,2),PSObject(object,3));
    vec4 origin=PSObject(object,4),spacing=PSObject(object,5),countsScale=PSObject(object,6);
    ivec3 counts=max(ivec3(countsScale.xyz),ivec3(1));
    localPosition=(inverseTransform*vec4(p,1)).xyz;
    vec3 grid=clamp((localPosition-origin.xyz)/spacing.xyz,vec3(0),vec3(counts-1));
    clampedPosition=origin.xyz+grid*spacing.xyz;
    ivec3 cell=ivec3(floor(grid));vec3 f=fract(grid);
    float result=0;
    for(int z=0;z<2;++z)for(int y=0;y<2;++y)for(int x=0;x<2;++x) {
        vec3 weight=mix(1.0-f,f,vec3(x,y,z));
        result+=PSMeshTexel(min(cell+ivec3(x,y,z),counts-1),counts,int(origin.w))*weight.x*weight.y*weight.z;
    }
    return result;
}
float PSObjectDistance(int object,vec3 p,bool conservative,inout float fetches) {
    vec3 localPosition,clampedPosition;
    // Optional per-asset thin-surface coverage. Storage remains the original
    // sampled distance; tracing consumes the explicitly expanded iso field.
    float rawDistance=PSMeshSample(object,p,localPosition,clampedPosition),surfaceBias=PSObject(object,10).w;fetches+=8;
    float distance=rawDistance-surfaceBias;
    vec4 countsScale=PSObject(object,6),errors=PSObject(object,9);
    if(length(localPosition-clampedPosition)>1e-6) {
        // Extrapolating the clamped value by ADDING the outside distance can
        // overestimate a corner's true distance. These two terms are rigorous
        // lower bounds; the world AABB remains useful under nonuniform scale.
        float lower=max(PSBoxDistance(localPosition,PSObject(object,7).xyz,PSObject(object,8).xyz),
            max(0.0,rawDistance-errors.x-length(localPosition-clampedPosition)));
        distance=max(max(0.0,lower-surfaceBias)*countsScale.w,
            max(0.0,PSBoxDistance(p,PSObject(object,10).xyz,PSObject(object,11).xyz)-surfaceBias*countsScale.w));
    } else distance*=countsScale.w;
    return conservative?max(0.0,distance-errors.y):distance;
}
float PSMeshDistance(vec3 p,inout float fetches) {
    float distance=1e20;
    for(int object=0;object<psAtlas.z;++object) {
        float box=max(0.0,PSBoxDistance(p,PSObject(object,10).xyz,PSObject(object,11).xyz)-PSObject(object,10).w*PSObject(object,6).w);
        // World AABB distance is itself a lower bound on any enclosed mesh.
        // Taking its maximum with the sampled lower bound permits this cheap
        // object rejection without weakening the conservative advance.
        if(box>=distance)continue;
        distance=min(distance,max(box,PSObjectDistance(object,p,true,fetches)));
        if(distance<=psTrace.z)return distance;
    }
    return distance;
}
int PSGlobalLevelCount() { return psClipmap.x>0?clamp(psClipmap.x,1,4):1; }
bool PSGlobalLevelReady(int level) {
    return level>=0&&level<PSGlobalLevelCount()&&(psClipmap.x==0||psClipScroll[level].w!=0);
}
vec3 PSGlobalLevelOrigin(int level) { return psClipmap.x>0?psClipOrigins[level].xyz:psGlobalOrigin.xyz; }
vec3 PSGlobalLevelSpacing(int level) { return psClipmap.x>0?psClipSpacing[level].xyz:psGlobalSpacing.xyz; }
ivec3 PSGlobalLevelCounts(int level) { return psClipmap.x>0?psClipCounts[level].xyz:psGlobalCounts.xyz; }
float PSGlobalLevelError(int level) { return psClipmap.x>0?psClipSpacing[level].w:psTrace.w; }
void PSGlobalLevelBounds(int level,out vec3 low,out vec3 high) {
    vec3 spacing=PSGlobalLevelSpacing(level);ivec3 counts=PSGlobalLevelCounts(level);
    // Use a conservative one-node core, following Lumen's filtering-footprint
    // guard principle. Lookup and half-voxel gradients remain in this level.
    float guard=psClipmap.x>0?1.0:0.0;
    low=PSGlobalLevelOrigin(level)+spacing*guard;
    high=PSGlobalLevelOrigin(level)+spacing*(vec3(counts-1)-guard);
}
bool PSGlobalContains(int level,vec3 p) {
    if(!PSGlobalLevelReady(level))return false;
    vec3 low,high;PSGlobalLevelBounds(level,low,high);
    return all(greaterThanEqual(p,low))&&all(lessThan(p,high));
}
int PSGlobalSelectLevel(vec3 p) {
    if(psClipmap.x==0)return 0;
    for(int level=0;level<4;++level)if(PSGlobalContains(level,p))return level;
    return -1;
}
int PSGlobalViewLevel(vec3 p) {
    if(psClipmap.x>0&&psClipmap.y>=0)return PSGlobalContains(psClipmap.y,p)?psClipmap.y:-1;
    return PSGlobalSelectLevel(p);
}
int PSGlobalOuterLevel(bool viewSelection) {
    if(psClipmap.x>0&&viewSelection&&psClipmap.y>=0)return PSGlobalLevelReady(psClipmap.y)?psClipmap.y:-1;
    for(int level=3;level>=0;--level)if(PSGlobalLevelReady(level))return level;
    return -1;
}
vec4 PSGlobalTexelLevel(int level,ivec3 cell) {
    ivec3 counts=PSGlobalLevelCounts(level);cell=clamp(cell,ivec3(0),counts-1);
    int offset=0;
    if(psClipmap.x>0) {
        // Wrap EACH logical corner into the toroidal physical atlas. Logical
        // N-1 and zero are never interpolated as adjacent world-space nodes.
        cell+=psClipScroll[level].xyz;
        cell-=ivec3(greaterThanEqual(cell,counts))*counts;offset=psClipCounts[level].w;
    }
    int index=offset+(cell.z*counts.y+cell.y)*counts.x+cell.x;
    ivec2 address=psGlobalCounts.w==1024?ivec2(index&1023,index>>10):ivec2(index%psGlobalCounts.w,index/psGlobalCounts.w);
    return texelFetch(psGlobal,address,0);
}
float PSGlobalSampleLevel(int level,vec3 p) {
    if(!PSGlobalLevelReady(level))return 1e20;
    ivec3 counts=PSGlobalLevelCounts(level);
    vec3 grid=clamp((p-PSGlobalLevelOrigin(level))/PSGlobalLevelSpacing(level),vec3(0),vec3(counts-1));
    ivec3 cell=ivec3(floor(grid));vec3 f=fract(grid);float result=0;
    for(int z=0;z<2;++z)for(int y=0;y<2;++y)for(int x=0;x<2;++x) {
        vec3 weight=mix(1.0-f,f,vec3(x,y,z));
        result+=PSGlobalTexelLevel(level,min(cell+ivec3(x,y,z),counts-1)).x*weight.x*weight.y*weight.z;
    }
    return result;
}
vec4 PSGlobalTexel(ivec3 cell) { return PSGlobalTexelLevel(0,cell); }
float PSGlobalSample(vec3 p) {
    int level=PSGlobalSelectLevel(p);return level>=0?PSGlobalSampleLevel(level,p):1e20;
}
bool PSGlobalRayBox(vec3 p,vec3 d,vec3 low,vec3 high,inout float near,inout float far) {
    for(int axis=0;axis<3;++axis) {
        if(abs(d[axis])<1e-9){if(p[axis]<low[axis]||p[axis]>high[axis])return false;continue;}
        float a=(low[axis]-p[axis])/d[axis],b=(high[axis]-p[axis])/d[axis];
        near=max(near,min(a,b));far=min(far,max(a,b));if(near>far)return false;
    }
    return true;
}
float PSGlobalNudge(int level) {
    vec3 spacing=PSGlobalLevelSpacing(max(level,0));return max(.00002,min(spacing.x,min(spacing.y,spacing.z))*.0001);
}
float PSGlobalTransition(vec3 p,vec3 d,int current,bool viewSelection) {
    if(psClipmap.x==0)return 1e20;
    float limit=1e20;
    if(current>=0) {
        vec3 low,high;PSGlobalLevelBounds(current,low,high);float near=0,far=1e20;
        if(PSGlobalRayBox(p,d,low,high,near,far))limit=max(0.0,far);
    }
    int maximum=current<0?PSGlobalLevelCount():current;
    if(viewSelection&&psClipmap.y>=0)maximum=current<0?psClipmap.y+1:0;
    for(int level=0;level<4;++level) {
        if(level>=maximum||!PSGlobalLevelReady(level)||(viewSelection&&psClipmap.y>=0&&level!=psClipmap.y))continue;
        vec3 low,high;PSGlobalLevelBounds(level,low,high);float near=0,far=1e20;
        if(PSGlobalRayBox(p,d,low,high,near,far)) {
            if(near>0)limit=min(limit,near);
            // The core excludes its high face. After reaching that face from
            // outside along a negative ray, selection still returns the coarse
            // level and ray-box entry is exactly zero. Enter with one nudge
            // instead of skipping the finer core. Test the point ahead so a
            // ray tangent to the excluded face cannot loop on zero transitions.
            else if(PSGlobalContains(level,p+d*PSGlobalNudge(level)))limit=0;
        }
    }
    return limit;
}
)GLSL"; }

inline std::string FallbackShader(bool safeOrigins=false) {
    return std::string("#version 450 core\n#define PS_SAFE_ORIGINS ")+(safeOrigins?"1\n":"0\n")+
        ScreenProbeTraceDetail::ConstantsGLSL()+ConstantsGLSL()+SamplingGLSL()+SurfaceCacheDetail::LookupGLSL()+R"GLSL(
in vec2 vUV;layout(location=0) out vec4 outColor;
layout(binding=0) uniform sampler2D psScreenRays;
layout(binding=1) uniform sampler2D psSceneColor;
layout(binding=2) uniform sampler2D psViewPositions;
layout(binding=3) uniform sampler2D psWorldNormals;
layout(binding=4) uniform sampler2D psUniform;
layout(binding=5) uniform sampler2D psSpawned;
layout(binding=9) uniform sampler2D psTriangles;
#if PS_SAFE_ORIGINS
layout(binding=10) uniform sampler2D psSafeOrigins;
#endif

vec4 PSRayTexel(int ray,int part) {
    int index=ray*4+part;
    return texelFetch(psScreenRays,ivec2(index%spTraceAtlas.x,index/spTraceAtlas.x),0);
}
vec4 PSProbe(int probe,int part) {
    bool uniformProbe=probe<spTraceAtlas.z;
    int slot=uniformProbe?probe:probe-spTraceAtlas.z;
    int entriesWide=uniformProbe?spGrid.x:spGrid.x*spGrid.z;
    ivec2 texel=ivec2((slot%entriesWide)*2+part,slot/entriesWide);
    return uniformProbe?texelFetch(psUniform,texel,0):texelFetch(psSpawned,texel,0);
}
vec4 PSGeometry(int index) {
    return texelFetch(psTriangles,ivec2(index%psGeometry.y,index/psGeometry.y),0);
}
bool PSRayBox(vec3 origin,vec3 direction,vec3 low,vec3 high,inout float near,inout float far) {
    for(int axis=0;axis<3;++axis) {
        if(abs(direction[axis])<1e-8) {
            if(origin[axis]<low[axis]||origin[axis]>high[axis])return false;
            continue;
        }
        float a=(low[axis]-origin[axis])/direction[axis],b=(high[axis]-origin[axis])/direction[axis];
        near=max(near,min(a,b));far=min(far,max(a,b));if(near>far)return false;
    }
    return true;
}
bool PSRefine(vec3 origin,vec3 direction,float minimum,inout float maximum,out int triangle,out vec3 normal,inout float fetches,out bool exhausted) {
    triangle=-1;normal=vec3(0);exhausted=false;
    if(psGeometry.z==0||maximum<=minimum)return false;
    int stack[64];stack[0]=0;int size=1;
    for(int visit=0;visit<16384&&size>0;++visit) {
        int node=stack[--size];vec4 low=PSGeometry(node*2),high=PSGeometry(node*2+1);fetches+=2;
        float near=minimum,far=maximum;
        if(!PSRayBox(origin,direction,low.xyz,high.xyz,near,far))continue;
        if(high.w>=0) {
            if(size>61){exhausted=true;return false;}
#if PS_SAFE_ORIGINS
            // Visit the nearer child first so a certified close hit tightens
            // maximum before the farther subtree is visited. Keep the exact
            // triangle predicate and the legacy traversal variant unchanged.
            int left=int(low.w),right=int(high.w);
            vec4 leftLow=PSGeometry(left*2),leftHigh=PSGeometry(left*2+1);
            vec4 rightLow=PSGeometry(right*2),rightHigh=PSGeometry(right*2+1);fetches+=4;
            float leftNear=minimum,leftFar=maximum,rightNear=minimum,rightFar=maximum;
            bool leftHit=PSRayBox(origin,direction,leftLow.xyz,leftHigh.xyz,leftNear,leftFar);
            bool rightHit=PSRayBox(origin,direction,rightLow.xyz,rightHigh.xyz,rightNear,rightFar);
            if(leftHit&&rightHit) {
                stack[size++]=leftNear<rightNear?right:left;
                stack[size++]=leftNear<rightNear?left:right;
            } else if(leftHit)stack[size++]=left;
            else if(rightHit)stack[size++]=right;
#else
            stack[size++]=int(low.w);stack[size++]=int(high.w);continue;
#endif
            continue;
        }
        for(int i=0;i<int(-high.w);++i) {
            int index=int(low.w)+i,offset=psGeometry.x+index*5;
            vec3 a=PSGeometry(offset).xyz,ab=PSGeometry(offset+1).xyz-a,ac=PSGeometry(offset+2).xyz-a;fetches+=3;
            vec3 q=cross(direction,ac);float determinant=dot(ab,q);
            if(abs(determinant)<1e-9)continue;
            vec3 relative=origin-a;float u=dot(relative,q)/determinant;if(u<0||u>1)continue;
            vec3 r=cross(relative,ab);float v=dot(direction,r)/determinant;if(v<0||u+v>1)continue;
            float distance=dot(ac,r)/determinant;
            if(distance<max(minimum,1e-5)||distance>maximum)continue;
            maximum=distance;triangle=index;normal=normalize(cross(ab,ac));
        }
    }
    if(size>0)exhausted=true;
    return triangle>=0;
}
bool PSMarch(vec3 origin,vec3 direction,float start,float finish,bool globalField,
    inout float distance,inout int steps,inout float fetches,out int triangle,out vec3 normal,out bool exhausted) {
    triangle=-1;normal=vec3(0);exhausted=false;
    float at=start;
    for(int iteration=0;iteration<512;++iteration) {
        if(at>=finish-1e-6){distance=finish;return false;}
        if(steps>=int(psTrace.y)){distance=at;exhausted=true;return false;}
        ++steps;
        float safe,transition=1e20,error=psTrace.w;int level=0;
        if(globalField) {
            vec3 point=origin+direction*at;level=PSGlobalSelectLevel(point);
            transition=PSGlobalTransition(point,direction,level,false);
            if(level<0) {
                if(transition>1e19){distance=finish;return false;}
                at=min(finish,at+transition+PSGlobalNudge(0));continue;
            }
            if(transition<=PSGlobalNudge(level)) {at=min(finish,at+transition+PSGlobalNudge(level));continue;}
            error=PSGlobalLevelError(level);
            safe=max(0.0,PSGlobalSampleLevel(level,point)-error);fetches+=8;
        }
        else safe=PSMeshDistance(origin+direction*at,fetches);
        if(isnan(safe)||isinf(safe)){distance=at;exhausted=true;return false;}
        if(safe>psTrace.z){at=min(finish,at+min(safe,transition));continue;}
        // A near-zero conservative bound is not a geometric hit. Refine only
        // this local candidate window using the triangle hierarchy. If empty,
        // the exact query certifies the interval and marching can pass it.
        if(!globalField)error=max(psGlobalSpacing.w,psTrace.z);
        float window=max(4.0*psTrace.z,2.0*error+4.0*psTrace.z);
        float end=min(finish,at+min(window,transition)),candidate=end;bool refineExhausted;
        if(PSRefine(origin,direction,at,candidate,triangle,normal,fetches,refineExhausted)) {
            distance=candidate;return true;
        }
        if(refineExhausted){distance=at;exhausted=true;return false;}
        at=end;
    }
    distance=at;exhausted=true;return false;
}
bool PSReadRadiance(vec3 world,out vec3 radiance,out ivec2 pixel,out bool invalidRadiance) {
    radiance=vec3(0);pixel=ivec2(-1);invalidRadiance=false;
    vec4 clip=spViewProjection*vec4(world,1);
    if(clip.w<=0)return false;
    vec3 ndc=clip.xyz/clip.w;
    if(any(greaterThanEqual(abs(ndc),vec3(1))))return false;
    pixel=ivec2(floor((ndc.xy*.5+.5)*vec2(spScreen.xy)));
    vec4 geometry=texelFetch(psViewPositions,pixel,0),normal=texelFetch(psWorldNormals,pixel,0);
    if(geometry.w<.5||normal.w<.5||!PSFinite(geometry.xyz)||!PSFinite(normal.xyz)||dot(normal.xyz,normal.xyz)<.5)return false;
    vec3 sourceWorld=(spInverseView*vec4(geometry.xyz,1)).xyz;
    vec4 corner=spInverseProjection*vec4(ndc.xy+2.0/vec2(spScreen.xy),ndc.z,1);corner/=corner.w;
    vec3 cornerWorld=(spInverseView*vec4(corner.xyz,1)).xyz;
    float footprint=max(length(cornerWorld-world),.0001);
    // A hidden surface may project into a valid pixel. Require its world-space
    // point and tangent plane to agree with the visible GBuffer surface first.
    if(length(sourceWorld-world)>footprint*1.5+psTrace.z||
        abs(dot(sourceWorld-world,normalize(normal.xyz)))>max(psTrace.z*2.0,.0002))return false;
    ivec2 colorSize=textureSize(psSceneColor,0);
    if(any(greaterThanEqual(pixel,colorSize)))return false;
    radiance=texelFetch(psSceneColor,pixel,0).rgb;
    if(!PSFinite(radiance)){invalidRadiance=true;radiance=vec3(0);return false;}
    return true;
}
void PSTrace(int rayGlobal,out vec4 color,out vec4 hit,out vec4 metadata,out vec4 directionData) {
    color=PSRayTexel(rayGlobal,0);hit=PSRayTexel(rayGlobal,1);
    metadata=PSRayTexel(rayGlobal,2);directionData=PSRayTexel(rayGlobal,3);
    bool cacheOnlyRadiance=(spTracing.w&2)!=0;
    if(psAtlas.w==0||int(color.w+.5)==0)return;
    bool confirmScreenHit=false;
#if PS_SAFE_ORIGINS
    // Captured depth supplies a finite visibility hint, not a certified nearest
    // ray intersection. In acute gaps it may skip a nearby face or select a
    // neighbouring pixel's bright surface. Safe GI confirms that hint against
    // triangles before assigning an owner or treating its light as readable.
    confirmScreenHit=cacheOnlyRadiance&&int(color.w+.5)==1;
#endif
    if(int(color.w+.5)==1&&!confirmScreenHit) {
        // Screen geometry is a visibility accelerator. Diffuse GI must read
        // the same view-independent outgoing radiance as off-screen hits,
        // rather than borrowing specular highlights from the scene HDR.
        if(cacheOnlyRadiance) {
            ivec2 pixel=ivec2(metadata.xy);vec3 value;
            vec4 hitNormal=texelFetch(psWorldNormals,pixel,0);
            if(psAtlas.w>=3&&hitNormal.w>.5&&PSFinite(hitNormal.xyz)&&
                dot(hitNormal.xyz,hitNormal.xyz)>.5&&SCSampleLighting(hit.xyz,normalize(hitNormal.xyz),value)) {
                color=PSFinite(value)?vec4(value,1):vec4(0,0,0,7);
            } else color=vec4(0,0,0,17);
        }
        return;
    }
    int raysPerProbe=spTracing.x*spTracing.x,probe=rayGlobal/raysPerProbe;
    if(probe>=spTraceAtlas.w)return;
    vec4 position=PSProbe(probe,0),normalDepth=PSProbe(probe,1);
    vec3 direction=directionData.xyz,normal=normalDepth.xyz;
    if(position.w<.5)return;
    if(!PSFinite(position.xyz)||!PSFinite(direction)||!PSFinite(normal)||dot(direction,direction)<.5||dot(normal,normal)<.5) {
        color=vec4(0,0,0,16);return;
    }
    normal=normalize(normal);direction=normalize(direction);
    // The original screen trace also uses status five for a HIT backface.
    // Preserve only launch-hemisphere rejection; outward failures still retry.
    if(dot(direction,normal)<=(cacheOnlyRadiance?0.0:.02))return;
#if PS_SAFE_ORIGINS
    vec4 safeOrigin=texelFetch(psSafeOrigins,ivec2(probe%spGrid.x,probe/spGrid.x),0);
    if(!(safeOrigin.w>0)||!PSFinite(safeOrigin.xyz)){color=vec4(0,0,0,16);return;}
    vec3 origin=safeOrigin.xyz;
#else
    float bias=spTraceParams.z;
    vec4 clip=spViewProjection*vec4(position.xyz,1);
    if(clip.w>0) {
        vec3 ndc=clip.xyz/clip.w;
        vec4 corner=spInverseProjection*vec4(ndc.xy+1.0/vec2(spScreen.xy),ndc.z,1);corner/=corner.w;
        vec3 cornerWorld=(spInverseView*vec4(corner.xyz,1)).xyz;
        if(PSFinite(cornerWorld))bias=max(bias,2.0*abs(dot(cornerWorld-position.xyz,normal)));
    }
    vec3 origin=position.xyz+normal*bias;
#endif
    float nearEnd=clamp(psTrace.x,0.0,spTraceParams.x),distance=0,fetches=0;
    int steps=0,triangle=-1;vec3 hitNormal;bool exhausted=false,found=false,globalHit=false;
#if PS_SAFE_ORIGINS
    if(confirmScreenHit) {
        float screenDepth=max(-(spView*vec4(hit.xyz,1)).z,0.0);
        float tolerance=max(spTraceParams.y+spTraceParams.w*screenDepth,4.0*psTrace.z);
        distance=min(spTraceParams.x,max(hit.w,0.0)+tolerance);
        bool refineExhausted;
        found=PSRefine(origin,direction,0.0,distance,triangle,hitNormal,fetches,refineExhausted);
        exhausted=refineExhausted;
        // A partial traversal cannot certify that its candidate is the first
        // hit. Keep it unknown rather than leaking that candidate's radiance.
        if(exhausted)found=false;
        else if(!found)distance=0.0;
    }
#endif
    if(!found&&!exhausted&&psFlags.x!=0&&nearEnd>0)
        found=PSMarch(origin,direction,0,nearEnd,false,distance,steps,fetches,triangle,hitNormal,exhausted);
    if(!found&&!exhausted&&psFlags.y!=0) {
        float globalStart=psFlags.x!=0?nearEnd:0.0,near=globalStart,far=spTraceParams.x;
        int outer=PSGlobalOuterLevel(false);vec3 low=vec3(0),high=vec3(0);
        if(outer>=0)PSGlobalLevelBounds(outer,low,high);
        if(outer<0||!PSRayBox(origin,direction,low,high,near,far)||far<=near){color=vec4(0,0,0,14);distance=min(near,spTraceParams.x);}
        else {
            found=PSMarch(origin,direction,near,far,true,distance,steps,fetches,triangle,hitNormal,exhausted);globalHit=found;
            if(!found&&!exhausted)color=vec4(0,0,0,near>globalStart+.0001||far<spTraceParams.x-.0001?14:13);
        }
    } else if(!found&&!exhausted)color=vec4(0,0,0,13);
    if(exhausted)color=vec4(0,0,0,15);
    hit=vec4(origin+direction*distance,distance);metadata=vec4(-1,-1,float(steps),float(triangle));directionData=vec4(direction,fetches);
    if(!found)return;
    vec3 radiance=vec3(0);ivec2 pixel=ivec2(-1);bool invalidRadiance=false;
    bool readable=false;
    if(!cacheOnlyRadiance)readable=PSReadRadiance(hit.xyz,radiance,pixel,invalidRadiance);
    // A one-sided diffuse back face emits known black. It is not a cache hole.
    if(cacheOnlyRadiance&&dot(hitNormal,-direction)<=0)readable=true;
    // Cached outgoing radiance remains available outside the current view.
    // The packed triangle owner prevents borrowing a neighbouring object's page.
    if(!readable&&psAtlas.w>=3&&dot(hitNormal,-direction)>0) {
        int owner=SCTriangleOwner(triangle);
        if(owner>=0&&SCSampleLightingObject(hit.xyz,hitNormal,owner,radiance)) {
            pixel=ivec2(-1);invalidRadiance=!PSFinite(radiance);
            readable=!invalidRadiance;if(invalidRadiance)radiance=vec3(0);
        }
    }
    metadata.xy=readable?vec2(pixel):vec2(-1);
    color=vec4(readable?radiance:vec3(0),invalidRadiance?7:(globalHit?(readable?10:12):(readable?9:11)));
}
void main() {
    ivec2 pixel=ivec2(gl_FragCoord.xy);int texel=pixel.y*spTraceAtlas.x+pixel.x;
    vec4 color,hit,metadata,directionData;PSTrace(texel/4,color,hit,metadata,directionData);
    outColor=texel%4==0?color:(texel%4==1?hit:(texel%4==2?metadata:directionData));
}
)GLSL";
}

inline std::string SliceDisplayShader() {
    return std::string("#version 450 core\n")+ScreenProbeTraceDetail::ConstantsGLSL()+ConstantsGLSL()+SamplingGLSL()+R"GLSL(
in vec2 vUV;layout(location=0) out vec4 outColor;
layout(binding=0) uniform sampler2D psScene;
vec3 PSSlicePoint(vec2 uv,vec3 low,vec3 high) {
    vec3 p=mix(low,high,vec3(uv.x,psSlice.x,uv.y));
    if(psDisplay.z==0)p=mix(low,high,vec3(psSlice.x,uv.x,uv.y));
    else if(psDisplay.z==2)p=mix(low,high,vec3(uv.x,uv.y,psSlice.x));
    return p;
}
vec3 PSDistanceColor(float distance,float cellSize) {
    vec3 inside=vec3(.04,.22,.85),outside=vec3(.9,.36,.06);
    float magnitude=1.0-exp(-abs(distance)/max(cellSize*3,.001));
    vec3 color=mix(vec3(.95),distance<0?inside:outside,magnitude);
    float contours=abs(fract(abs(distance)/max(cellSize,.001))-.5);
    return color*(contours<.025?.65:1.0);
}
void main() {
    ivec2 pixel=ivec2(gl_FragCoord.xy);outColor=texelFetch(psScene,pixel,0);
    if(psAtlas.w==0||psDisplay.x==0)return;
    float side=psSlice.y>0?psSlice.y:clamp(float(spScreen.y)*.35,96.0,240.0);
    vec2 low=vec2(16.0),local=vec2(pixel)+.5-low;
    if(any(lessThan(local,vec2(-3)))||any(greaterThanEqual(local,vec2(side+3))))return;
    if(any(lessThan(local,vec2(0)))||any(greaterThanEqual(local,vec2(side)))){outColor=vec4(.12,.14,.18,1);return;}
    vec2 uv=local/side;float distance,cellSize;
    if(psDisplay.x==1) {
        int level=psClipmap.x>0?max(psClipmap.y,0):0;
        if(!PSGlobalLevelReady(level))for(int candidate=0;candidate<4;++candidate)
            if(PSGlobalLevelReady(candidate)){level=candidate;break;}
        vec3 origin=PSGlobalLevelOrigin(level),spacing=PSGlobalLevelSpacing(level);
        vec3 world=PSSlicePoint(uv,origin,origin+spacing*vec3(PSGlobalLevelCounts(level)-1));
        distance=PSGlobalSampleLevel(level,world);cellSize=max(spacing.x,max(spacing.y,spacing.z));
    } else {
        int object=clamp(psDisplay.y,0,max(psAtlas.z-1,0));
        vec4 origin=PSObject(object,4),spacing=PSObject(object,5),counts=PSObject(object,6);
        vec3 p=PSSlicePoint(uv,origin.xyz,origin.xyz+spacing.xyz*(counts.xyz-1));
        ivec3 cell=clamp(ivec3(round((p-origin.xyz)/spacing.xyz)),ivec3(0),ivec3(counts.xyz)-1);
        distance=PSMeshTexel(cell,ivec3(counts.xyz),int(origin.w))-PSObject(object,10).w;
        cellSize=max(spacing.x,max(spacing.y,spacing.z));
    }
    outColor=vec4(PSDistanceColor(distance,cellSize),1);
}
)GLSL";
}

inline std::string HitVertexShader() {
    return std::string("#version 450 core\n")+ScreenProbeTraceDetail::ConstantsGLSL()+ConstantsGLSL()+
        ScreenProbeTraceDetail::MarkerTraceGLSL()+R"GLSL(
layout(location=0) in vec2 position;layout(location=1) in vec2 uv;
layout(binding=1) uniform sampler2D psHitNormals;
layout(binding=9) uniform sampler2D psHitGeometry;
out vec2 hitMarkerPosition;flat out vec3 hitMarkerColor;
flat out vec2 hitMarkerSphere;flat out vec4 hitMarkerDepthProjection;
vec4 PSHitGeometry(int index) {
    return texelFetch(psHitGeometry,ivec2(index%psGeometry.y,index/psGeometry.y),0);
}
void main() {
    hitMarkerPosition=position;hitMarkerColor=vec3(0);hitMarkerSphere=vec2(0);hitMarkerDepthProjection=vec4(0);
    gl_Position=vec4(2,2,2,1);
    int probe=spTraceDisplay.y,ray=gl_InstanceID;
    if(probe<0||probe>=spTraceAtlas.w||ray>=spTracing.x*spTracing.x)return;
    vec4 color=STReadRay(probe,ray,0);int state=int(color.w+.5);
    if(state!=1&&state!=17&&(state<9||state>12))return;
    vec3 hit=STReadRay(probe,ray,1).xyz;vec4 metadata=STReadRay(probe,ray,2);
    vec3 normal;
    if(state==1||state==17) {
        ivec2 pixel=ivec2(metadata.xy);
        if(any(lessThan(pixel,ivec2(0)))||any(greaterThanEqual(pixel,textureSize(psHitNormals,0))))return;
        normal=texelFetch(psHitNormals,pixel,0).xyz;
    } else {
        int triangle=int(metadata.w+.5);
        if(triangle<0||triangle>=psGeometry.w)return;
        int offset=psGeometry.x+triangle*5;vec3 a=PSHitGeometry(offset).xyz;
        normal=cross(PSHitGeometry(offset+1).xyz-a,PSHitGeometry(offset+2).xyz-a);
    }
    if(any(isnan(hit))||any(isinf(hit))||dot(normal,normal)<1e-12)return;
    normal=normalize(normal);
    // Tangent sphere markers remain attached to the actual hit point. Face
    // their small offset toward the viewer; scene depth still hides endpoints
    // behind the current visible receiver, including unreadable hidden hits.
    vec3 camera=spInverseView[3].xyz;if(dot(normal,camera-hit)<0)normal=-normal;
    vec4 hitClip=spViewProjection*vec4(hit,1);if(hitClip.w<=0)return;
    const float radiusPixels=2.5;
    float radiusWorld=2.0*hitClip.w*radiusPixels*abs(spInverseProjection[1][1])/float(spScreen.y);
    vec3 center=hit+normal*radiusWorld;vec4 clip=spViewProjection*vec4(center,1);
    if(clip.w<=0)return;
    hitMarkerColor=STDiagnosticColor(color.rgb,color.w);
    hitMarkerSphere=vec2((spView*vec4(center,1)).z,radiusWorld);
    mat4 projection=spViewProjection*spInverseView;
    hitMarkerDepthProjection=vec4(projection[2][2],projection[3][2],projection[2][3],projection[3][3]);
    clip.xy+=position*radiusPixels*2.0/vec2(spScreen.xy)*clip.w;gl_Position=clip;
}
)GLSL";
}

inline std::string HitFragmentShader() {
    return std::string("#version 450 core\n")+R"GLSL(
in vec2 hitMarkerPosition;flat in vec3 hitMarkerColor;
flat in vec2 hitMarkerSphere;flat in vec4 hitMarkerDepthProjection;
layout(location=0) out vec4 outColor;
void main() {
    float radiusSquared=dot(hitMarkerPosition,hitMarkerPosition);if(radiusSquared>1)discard;
    float front=sqrt(max(0.0,1.0-radiusSquared));
    float viewZ=hitMarkerSphere.x+hitMarkerSphere.y*front;
    float clipZ=hitMarkerDepthProjection.x*viewZ+hitMarkerDepthProjection.y;
    float clipW=hitMarkerDepthProjection.z*viewZ+hitMarkerDepthProjection.w;if(clipW<=0)discard;
    gl_FragDepth=clamp((clipZ/clipW)*.5+.5,0.0,1.0);
    float shade=.65+.35*max(dot(normalize(vec3(hitMarkerPosition,front)),normalize(vec3(-.3,.5,.8))),0.0);
    outColor=vec4(hitMarkerColor*shade,1);
}
)GLSL";
}
} // namespace Render::ProbeSdfDetail
