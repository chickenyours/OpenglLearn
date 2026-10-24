#pragma once
#include "surface_cache_native_screen_probe_shaders.h"
#include "surface_cache_transport_deferred_shaders.h"
#include "ue_lumen_hit_scene_color_shaders.h"

namespace Render::SurfaceCacheDetail {
// UE 5.7.1: LumenScreenProbeGather.cpp 588-655, LumenRadianceCacheUpdate.ush,
// LumenRadianceCacheUpdate.usf and LumenRadianceCacheInterpolation.ush.
// Engine centimetres are converted to this renderer's metres once on the CPU.
// The cache budget is normal-probe cost, independent of the screen-probe count.
inline std::string UeRadianceCacheConstantsGLSL(){return R"GLSL(
layout(std140,binding=13) uniform UeRadianceCacheData {
    uvec4 urFrame; // frame, reset, normal-equivalent cost budget, query diagnostics
    vec4 urCorner[4]; // world corner.xyz, cell size
    vec4 urPreviousCorner[4];
    vec4 urCamera; // camera.xyz, existing scene trace distance
};
const uint UR_GRID=48u,UR_CELLS=110592u,UR_TOTAL_CELLS=442368u,UR_CAPACITY=16384u;
const uint UR_COST_DOWN=1u,UR_COST_NORMAL=4u,UR_COST_SUPER=16u;
bool URFinite(vec3 p){return !any(isnan(p))&&!any(isinf(p));}
uint URCellIndex(ivec3 p,int level){return uint(level)*UR_CELLS+uint(p.x)+48u*(uint(p.y)+48u*uint(p.z));}
ivec3 URCellCoord(uint flatIndex){uint local=flatIndex%UR_CELLS;return ivec3(local%48u,(local/48u)%48u,local/2304u);}
bool URInside(ivec3 p){return all(greaterThanEqual(p,ivec3(0)))&&all(lessThan(p,ivec3(48)));}
vec3 URWorld(ivec3 p,int level){return urCorner[level].xyz+(vec3(p)+.5)*urCorner[level].w;}
int URClipmap(vec3 p,float dither){
    for(int level=0;level<4;++level){vec3 q=(p-urCorner[level].xyz)/urCorner[level].w;
        vec3 edge=min(clamp(q-.5,0.0,1.0),clamp(vec3(47.5)-q,0.0,1.0));
        if(min(edge.x,min(edge.y,edge.z))>dither)return level;}
    return -1;
}
int URClipmap(vec3 p){return URClipmap(p,0.0);}
float URProbeTMin(int level){return urCorner[level].w*1.7320508075688772;}
uint URTraceCost(vec3 p){return distance(p,urCamera.xyz)>=40.0?UR_COST_DOWN:UR_COST_NORMAL;}
uint URPriority(uint traced,uint used,int level){
    if(traced==0u)return 0u;
    float importance=float(used>traced?used-traced:1u)/float(level+1);
    return 15u-uint(clamp(log2(importance),0.0,14.0));
}
vec3 UREquiArea(vec2 uv){
    uv=2.0*uv-1.0;float d=1.0-abs(uv.x)-abs(uv.y),r=1.0-abs(d);
    float phi=r==0.0?0.0:.7853981633974483*((abs(uv.y)-abs(uv.x))/r+1.0),f=r*sqrt(max(0.0,2.0-r*r));
    return vec3(f*sign(uv.x)*abs(cos(phi)),f*sign(uv.y)*abs(sin(phi)),sign(d)*(1.0-r*r));
}
vec2 URInverseEquiArea(vec3 direction){
    direction=normalize(direction);vec3 a=abs(direction);float r=sqrt(max(0.0,1.0-a.z));
    float x=min(a.x,a.y)/(max(a.x,a.y)+5.42101086243e-20);
    float phi=.0419038818029165736-.025139097234348351*x;
    phi=.08817706647753163+phi*x;phi=-.24733373328126894+phi*x;
    phi=.006157201789828021+phi*x;phi=.6362265452740161+phi*x;phi=.000004067585662467885+phi*x;
    phi=a.x<a.y?1.0-phi:phi;vec2 uv=vec2(r-phi*r,phi*r);
    if(direction.z<0.0)uv=1.0-uv.yx;
    uv=uintBitsToFloat(floatBitsToUint(uv)^(floatBitsToUint(direction.xy)&uvec2(0x80000000u)));
    return uv*.5+.5;
}
ivec2 URWrapBorder(ivec2 p,int resolution){
    if(p.x<1){p.x=1-p.x;p.y=resolution-1-p.y;}
    if(p.x>=resolution-1){p.x=(resolution-1)-(p.x-(resolution-2));p.y=resolution-1-p.y;}
    if(p.y<1){p.y=1-p.y;p.x=resolution-1-p.x;}
    if(p.y>=resolution-1){p.y=(resolution-1)-(p.y-(resolution-2));p.x=resolution-1-p.x;}
    return p-1;
}
)GLSL";}
inline std::string UeRadianceCacheBuffersGLSL(){return R"GLSL(
layout(std430,set=2,binding=0) buffer URControl {uint urControl[];};
layout(std430,set=2,binding=1) buffer URCurrent {uint urCurrent[];};
layout(std430,set=2,binding=2) readonly buffer URPrevious {uint urPrevious[];};
layout(std430,set=2,binding=3) buffer URProbeState {uvec4 urState[];}; // last used, last traced, resolution, alive
layout(std430,set=2,binding=4) buffer URFreeList {uint urFree[];};
layout(std430,set=2,binding=5) buffer URTraceQueue {uvec4 urQueue[];}; // float-bit xyz / probe|level<<24|down<<31
layout(std430,set=2,binding=6) buffer URUsedMarks {uint urUsed[];};
uint URInvocation(){return gl_GlobalInvocationID.x+gl_GlobalInvocationID.y*gl_NumWorkGroups.x*64u;}
)GLSL";}

// UE ScreenProbeGather AdaptiveProbeAllocationFraction=.5. Keep this
// renderer's public fixed logical IDs, while applying the same global append
// capacity before any ray is launched. Atomic order supplies no new priority.
inline std::string UeLumenAdaptiveAllocationShader(bool reset=false){
    auto source=std::string("#version 450 core\n")+ScreenProbeTraceDetail::ConstantsGLSL()+R"GLSL(
layout(std430,set=2,binding=0) buffer UAControl {uint uaControl[];};
)GLSL";
    if(reset)return source+R"GLSL(
layout(local_size_x=64) in;
void main(){if(gl_GlobalInvocationID.x!=0u)return;
    uaControl[52]=0u;uaControl[53]=0u;uaControl[54]=0u;uaControl[55]=uint(spTraceAtlas.z)/2u;
}
)GLSL";
    return source+R"GLSL(
layout(local_size_x=8,local_size_y=8) in;
layout(rgba32f,set=3,binding=0) uniform image2D uaSpawned;
shared uint uaGroupCount,uaGroupBase;
void main(){uint lane=gl_LocalInvocationIndex;if(lane==0u)uaGroupCount=0u;barrier();
    // UE's 8x8 group owns 2x2 uniform tiles with 4x4 candidates per tile.
    uvec2 tile=gl_WorkGroupID.xy*2u+gl_LocalInvocationID.xy/4u;
    uint candidate=gl_LocalInvocationID.x%4u+4u*(gl_LocalInvocationID.y%4u);
    bool validTile=all(lessThan(tile,uvec2(spGrid.xy)));ivec2 at=ivec2(2u*(tile.x*uint(spGrid.z)+candidate),tile.y);
    vec4 position=vec4(0);if(validTile)position=imageLoad(uaSpawned,at);
    bool place=validTile&&position.w>=.5;uint localIndex=0u;if(place)localIndex=atomicAdd(uaGroupCount,1u);
    barrier();if(lane==0u)uaGroupBase=atomicAdd(uaControl[52],uaGroupCount);barrier();
    if(place){uint index=uaGroupBase+localIndex;
        if(index<uaControl[55])atomicAdd(uaControl[53],1u);
        else{position.w=0;imageStore(uaSpawned,at,position);atomicAdd(uaControl[54],1u);}}
}
)GLSL";
}

// Stages 0..7: clear, mark-needed, reuse, allocate/histogram, budget,
// select-update, indirect arguments, publish interpolation indirection.
inline std::string UeRadianceCacheUpdateShader(unsigned stage){
    auto source=std::string("#version 450 core\n")+UeRadianceCacheConstantsGLSL()+UeRadianceCacheBuffersGLSL();
    // gl_GlobalInvocationID is only reachable from compute programs.
    source+="\nlayout(local_size_x=64) in;\n";
    if(stage==0)return source+R"GLSL(
void main(){uint id=URInvocation();
    if(id<UR_TOTAL_CELLS){urCurrent[id]=0u;urUsed[id]=0u;}
    if(urFrame.y!=0u&&id<UR_CAPACITY)urState[id]=uvec4(0);
    if(id<64u){if((id<52u||id>55u)&&(id>1u||urFrame.y!=0u))urControl[id]=0u;
        if(id==9u||id==10u||id==49u||id==50u)urControl[id]=1u;
        if(id==60u)urControl[id]=65536u;}
}
)GLSL";
    if(stage==1)return source+ScreenProbeTraceDetail::ConstantsGLSL()+R"GLSL(
layout(binding=4) uniform sampler2D urUniform;
layout(binding=5) uniform sampler2D urSpawned;
layout(binding=10) uniform sampler2D urSafeOrigins;
vec4 URSource(uint probe){bool uniformProbe=probe<uint(spTraceAtlas.z);int slot=int(probe)-(uniformProbe?0:spTraceAtlas.z);
    int columns=spGrid.x*(uniformProbe?1:spGrid.z);ivec2 at=ivec2(2*(slot%columns),slot/columns);
    return uniformProbe?texelFetch(urUniform,at,0):texelFetch(urSpawned,at,0);}
void main(){uint probe=URInvocation();if(probe>=uint(spTraceAtlas.w))return;
    vec4 p=URSource(probe),safe=texelFetch(urSafeOrigins,ivec2(probe%uint(spGrid.x),probe/uint(spGrid.x)),0);
    if(p.w<.5||safe.w<=0||!URFinite(p.xyz)||!URFinite(safe.xyz))return;
    int level=URClipmap(p.xyz,.01);if(level<0)return;
    ivec3 first=ivec3(floor((p.xyz-urCorner[level].xyz)/urCorner[level].w-.5));
    for(int z=0;z<2;++z)for(int y=0;y<2;++y)for(int x=0;x<2;++x){ivec3 q=first+ivec3(x,y,z);
        if(URInside(q))atomicOr(urUsed[URCellIndex(q,level)],1u);}
}
)GLSL";
    if(stage==2)return source+R"GLSL(
void main(){uint old=URInvocation();if(urFrame.y!=0u||old>=UR_TOTAL_CELLS)return;
    uint encoded=urPrevious[old];if(encoded==0u||encoded>UR_CAPACITY)return;
    uint probe=encoded-1u;int level=int(old/UR_CELLS);ivec3 previous=URCellCoord(old);
    vec3 p=urPreviousCorner[level].xyz+(vec3(previous)+.5)*urPreviousCorner[level].w;
    ivec3 now=ivec3(floor((p-urCorner[level].xyz)/urCorner[level].w));bool reuse=false;
    if(URInside(now)){uint cell=URCellIndex(now,level);bool used=urUsed[cell]!=0u;
        if(used||urFrame.x-urState[probe].x<8u){urCurrent[cell]=encoded;reuse=true;
            if(used)urState[probe].x=urFrame.x;atomicAdd(urControl[6],1u);}}
    if(!reuse){urState[probe]=uvec4(0);uint freeIndex=atomicAdd(urControl[1],1u);
        if(freeIndex<UR_CAPACITY)urFree[freeIndex]=probe;else atomicAdd(urControl[7],1u);}
}
)GLSL";
    if(stage==3)return source+R"GLSL(
uint URAllocate(){uint count=urControl[1];
    while(count>0u){uint before=atomicCompSwap(urControl[1],count,count-1u);if(before==count)return urFree[count-1u];count=before;}
    uint next=urControl[0];while(next<UR_CAPACITY){uint before=atomicCompSwap(urControl[0],next,next+1u);if(before==next)return next;next=before;}
    return UR_CAPACITY;
}
void main(){uint cell=URInvocation();if(cell>=UR_TOTAL_CELLS)return;int level=int(cell/UR_CELLS);
    uint encoded=urCurrent[cell];
    if(encoded==0u&&urUsed[cell]!=0u){uint probe=URAllocate();if(probe>=UR_CAPACITY){atomicAdd(urControl[7],1u);return;}
        encoded=probe+1u;urCurrent[cell]=encoded;urState[probe]=uvec4(urFrame.x,0,0,1);atomicAdd(urControl[5],1u);}
    if(encoded==0u)return;uint probe=encoded-1u;uvec4 state=urState[probe];
    uint priority=URPriority(state.y,state.x,level),cost=URTraceCost(URWorld(URCellCoord(cell),level));
    atomicAdd(urControl[16u+priority],cost);if(priority==0u)atomicAdd(urControl[35],UR_COST_DOWN);
    if(urUsed[cell]!=0u)atomicAdd(urControl[38],1u);
}
)GLSL";
    if(stage==4)return source+R"GLSL(
void main(){if(URInvocation()!=0u)return;uint sum=0u,bucket=0u,remaining=urFrame.z;
    for(;bucket<16u;++bucket){uint cost=urControl[16u+bucket];if(sum+cost>=urFrame.z){remaining=urFrame.z-sum;break;}sum+=cost;}
    urControl[32]=bucket;urControl[33]=remaining;
}
)GLSL";
    if(stage==5)return source+R"GLSL(
void main(){uint cell=URInvocation();if(cell>=UR_TOTAL_CELLS)return;uint encoded=urCurrent[cell];if(encoded==0u)return;
    uint probe=encoded-1u;int level=int(cell/UR_CELLS);vec3 p=URWorld(URCellCoord(cell),level);
    uvec4 state=urState[probe];uint priority=URPriority(state.y,state.x,level),cost=URTraceCost(p),last=urControl[32];
    if(priority>last)return;bool down=cost==UR_COST_DOWN;
    if(priority==last&&last>0u){uint allocated=atomicAdd(urControl[34],cost);if(allocated+cost>urControl[33])return;}
    else if(priority==0u){uint minimum=atomicAdd(urControl[35],cost-UR_COST_DOWN);if(minimum+cost-UR_COST_DOWN>urFrame.z)down=true;}
    uint index=atomicAdd(urControl[2],1u);if(index>=UR_CAPACITY){atomicAdd(urControl[7],1u);return;}
    urQueue[index]=uvec4(floatBitsToUint(p),probe|(uint(level)<<24u)|(down?0x80000000u:0u));
    urState[probe].y=urFrame.x;urState[probe].z=down?8u:16u;
    atomicAdd(urControl[down?4:3],1u);atomicAdd(urControl[14],cost);atomicAdd(urControl[41],down?UR_COST_DOWN:UR_COST_NORMAL);
    atomicAdd(urControl[42],down?64u:256u);
    if(urCamera.w>URProbeTMin(level)+1e-5)atomicAdd(urControl[13],down?64u:256u);
}
)GLSL";
    if(stage==6)return source+R"GLSL(
void main(){if(URInvocation()!=0u)return;
    uint traces=min(urControl[2],UR_CAPACITY),groups=traces*4u;
    urControl[8]=min(groups,65535u);urControl[9]=max((groups+65534u)/65535u,1u);urControl[10]=1u;
    groups=(traces*324u+63u)/64u;
    urControl[48]=min(groups,65535u);urControl[49]=max((groups+65534u)/65535u,1u);urControl[50]=1u;
}
)GLSL";
    if(stage==7)return source+R"GLSL(
layout(rgba32f,set=3,binding=0) uniform writeonly image2D urIndirectionOutput;
void main(){uint cell=URInvocation();if(cell>=UR_TOTAL_CELLS)return;uint encoded=urCurrent[cell];float valid=0;
    if(encoded>0u&&encoded<=UR_CAPACITY)valid=urState[encoded-1u].y>0u?1.0:0.0;
    imageStore(urIndirectionOutput,ivec2(cell%1024u,cell/1024u),vec4(float(encoded),valid,0,0));
}
)GLSL";
    throw std::logic_error("Unknown UE radiance-cache update stage");
}

inline std::string UeRadianceCacheTraceShader(bool diagnostics=false){
    auto source=std::string("#version 450 core\n")+LookupGLSL()+NativeScreenProbeIntersectionGLSL(diagnostics)+
        UeRadianceCacheConstantsGLSL()+UeRadianceCacheBuffersGLSL();
    if(diagnostics)source+="layout(std430,set=2,binding=7) buffer URActualTraceAudit {uvec4 urActualTraceAudit[];};\n";
    source+=R"GLSL(
layout(local_size_x=64) in;
layout(rgba16f,set=3,binding=0) uniform writeonly image2D urRawOutput;
layout(rgba32f,set=3,binding=1) uniform writeonly image2D urDepthOutput;
void main(){uint logical=URInvocation(),index=logical/256u,ray=logical%256u;
    if(index>=min(urControl[2],UR_CAPACITY))return;
    uvec4 packed=urQueue[index];uint probe=packed.w&0x00ffffffu;int level=int((packed.w>>24u)&127u),resolution=(packed.w&0x80000000u)!=0u?8:16;
    if(ray>=uint(resolution*resolution))return;
    ivec2 texel=ivec2(int(ray)%resolution,int(ray)/resolution),atlas=ivec2(int(probe&127u)*16,int(probe>>7u)*16)+texel;
    vec3 p=uintBitsToFloat(packed.xyz),direction=normalize(UREquiArea((vec2(texel)+.5)/float(resolution)));
    float tmin=URProbeTMin(level),maximum=urCamera.w,hitDistance=maximum;vec4 value=vec4(0,0,0,1);
    float nativeDistance=maximum-tmin;int triangle=-1;bool found=false,exhausted=false,frontFace=false,actualTrace=false;
)GLSL";
    if(diagnostics)source+="    scTraceDiag[0]=uvec4(0);scTraceDiag[1]=uvec4(0);\n";
    source+=R"GLSL(
    if(maximum>tmin+1e-5){vec3 origin=p+direction*tmin,normal;actualTrace=true;
        found=SCNativeIntersect(origin,direction,nativeDistance,normal,triangle,exhausted,false);
        frontFace=found&&dot(normal,-direction)>0;
        if(exhausted){value=vec4(0);hitDistance=0;}
        else if(frontFace){vec3 radiance;int owner=SCTriangleOwner(triangle);
            value=owner>=0&&SCSampleLightingObject(origin+direction*nativeDistance,normal,owner,radiance)&&URFinite(radiance)?vec4(max(radiance,vec3(0)),1):vec4(0);}
        if(found&&!exhausted)hitDistance=nativeDistance+tmin;
    }
    imageStore(urRawOutput,atlas,value);
    imageStore(urDepthOutput,atlas,vec4(hitDistance));
)GLSL";
    if(diagnostics)source+=R"GLSL(
    atomicAdd(urControl[12],scTraceDiag[0].x);atomicAdd(urControl[40],value.a<=0?1u:0u);
    bool nearHit=actualTrace&&found&&!exhausted&&nativeDistance<=.2,uniformSample=actualTrace&&(logical%16u)==0u;
    if(nearHit||uniformSample){uint index=atomicAdd(urControl[56],1u);
        if(nearHit)atomicAdd(urControl[58],1u);if(uniformSample)atomicAdd(urControl[59],1u);
        uint flags=(found?1u:0u)|(exhausted?2u:0u)|(value.a>0?4u:0u)|(frontFace?8u:0u)|
            (nearHit?16u:0u)|(uniformSample?32u:0u)|(scTraceDiag[0].x<<8u);
        if(index<urControl[60])urActualTraceAudit[index]=uvec4(logical,floatBitsToUint(nativeDistance),uint(triangle),flags);
        else atomicAdd(urControl[57],1u);
    }
)GLSL";
    return source+"}\n";
}
// UE fixes the one-texel octahedral border at mip zero. A downsampled probe is
// reconstructed from its actual 8x8 trace, never re-traced to fill a 16x16 atlas.
inline std::string UeRadianceCacheResolveShader(){return std::string("#version 450 core\n")+
    UeRadianceCacheConstantsGLSL()+UeRadianceCacheBuffersGLSL()+R"GLSL(
layout(local_size_x=64) in;
layout(binding=0) uniform sampler2D urRawAtlas;
layout(binding=6) uniform sampler2D urDepthAtlas;
layout(rgba16f,set=3,binding=0) uniform writeonly image2D urFinalOutput;
vec4 URRaw(uint probe,ivec2 at,int resolution){
    at=URWrapBorder(at+ivec2(1),resolution+2);
    return texelFetch(urRawAtlas,ivec2(int(probe&127u)*16,int(probe>>7u)*16)+at,0);
}
vec4 URRawDirection(uint probe,vec2 uv,int resolution){vec2 at=uv*float(resolution)-.5;ivec2 first=ivec2(floor(at));vec2 f=fract(at);
    return mix(mix(URRaw(probe,first,resolution),URRaw(probe,first+ivec2(1,0),resolution),f.x),
        mix(URRaw(probe,first+ivec2(0,1),resolution),URRaw(probe,first+ivec2(1,1),resolution),f.x),f.y);}
float URDepth(uint probe,vec3 direction,int resolution){ivec2 at=clamp(ivec2(URInverseEquiArea(direction)*float(resolution)),ivec2(0),ivec2(resolution-1));
    return texelFetch(urDepthAtlas,ivec2(int(probe&127u)*16,int(probe>>7u)*16)+at,0).x;}
void main(){uint logical=URInvocation(),index=logical/324u,part=logical%324u;if(index>=min(urControl[2],UR_CAPACITY))return;
    uvec4 packed=urQueue[index];uint probe=packed.w&0x00ffffffu;int level=int((packed.w>>24u)&127u),resolution=(packed.w&0x80000000u)!=0u?8:16;
    ivec2 finalAt=ivec2(part%18u,part/18u),source=URWrapBorder(finalAt,18);
    vec2 uv=(vec2(source)+.5)/16.0;vec3 direction=UREquiArea(uv),p=uintBitsToFloat(packed.xyz);
    vec4 value=URRawDirection(probe,uv,resolution);float total=1.0,hitDistance=URDepth(probe,direction,resolution);
    ivec3 cell=ivec3(floor((p-urCorner[level].xyz)/urCorner[level].w));float offset=2.0*URProbeTMin(level);
    // UE FilterProbeRadianceWithGatherCS: six adjacent probes, symmetric
    // starting-point depth tests, center-clamped hit distance, 0.2 degree gate.
    for(int neighbor=0;neighbor<6;++neighbor){ivec3 delta=ivec3(0);delta[neighbor/2]=(neighbor&1)==0?-1:1;ivec3 q=cell+delta;
        if(!URInside(q))continue;uint encoded=urCurrent[URCellIndex(q,level)];if(encoded==0u)continue;
        uint other=encoded-1u;if(urState[other].y==0u)continue;int otherResolution=int(urState[other].z);
        vec3 otherP=URWorld(q,level),toOther=otherP+offset*direction-p,toCenter=p+offset*direction-otherP;
        float centerDepth=URDepth(probe,toOther,resolution),otherDepth=URDepth(other,toCenter,otherResolution);
        if(centerDepth*centerDepth<dot(toOther,toOther)||otherDepth*otherDepth<dot(toCenter,toCenter))continue;
        float depth=min(URDepth(other,direction,otherResolution),hitDistance);vec3 toHit=otherP+direction*depth-p;
        float hitLength=length(toHit);if(hitLength<=1e-8)continue;
        float angle=acos(clamp(dot(toHit,direction)/hitLength,-1.0,1.0));float weight=1.0-clamp(angle/.003490658503988659,0.0,1.0);
        value+=URRawDirection(other,uv,otherResolution)*weight;total+=weight;
    }
    value/=total;
    imageStore(urFinalOutput,ivec2(int(probe&127u)*18,int(probe>>7u)*18)+finalAt,value);
}
)GLSL";}

inline std::string UeRadianceCacheReadGLSL(){return UeRadianceCacheConstantsGLSL()+R"GLSL(
layout(binding=0) uniform sampler2D urRadianceAtlas;
layout(binding=1) uniform sampler2D urIndirection;
vec4 URMapped(ivec3 at,int level){if(!URInside(at))return vec4(0);uint flatIndex=URCellIndex(at,level);
    return texelFetch(urIndirection,ivec2(flatIndex%1024u,flatIndex/1024u),0);}
bool URCoverage(vec3 p,out int level,out ivec3 first,out float cutoff){
    level=URClipmap(p);first=ivec3(0);cutoff=urCamera.w;if(level<0)return false;
    first=ivec3(floor((p-urCorner[level].xyz)/urCorner[level].w-.5));
    for(int z=0;z<2;++z)for(int y=0;y<2;++y)for(int x=0;x<2;++x){vec4 mapped=URMapped(first+ivec3(x,y,z),level);
        if(mapped.x<1||mapped.x>float(UR_CAPACITY)||mapped.y<.5)return false;}
    cutoff=URProbeTMin(level)+urCorner[level].w*1.7320508075688772;
    return cutoff<urCamera.w;
}
vec4 URSampleProbe(uint probe,vec3 direction){vec2 at=URInverseEquiArea(direction)*16.0+.5;ivec2 first=ivec2(floor(at));vec2 f=fract(at);
    ivec2 origin=ivec2(int(probe&127u)*18,int(probe>>7u)*18);
    return mix(mix(texelFetch(urRadianceAtlas,origin+first,0),texelFetch(urRadianceAtlas,origin+first+ivec2(1,0),0),f.x),
        mix(texelFetch(urRadianceAtlas,origin+first+ivec2(0,1),0),texelFetch(urRadianceAtlas,origin+first+ivec2(1,1),0),f.x),f.y);
}
vec4 URSample(vec3 p,vec3 direction,int level,ivec3 first){
    vec3 f=fract((p-urCorner[level].xyz)/urCorner[level].w-.5);vec4 sum=vec4(0);
    float radius=1.5*URProbeTMin(level);
    for(int z=0;z<2;++z)for(int y=0;y<2;++y)for(int x=0;x<2;++x){ivec3 q=first+ivec3(x,y,z);vec4 mapped=URMapped(q,level);
        uint probe=uint(mapped.x)-1u;vec3 center=URWorld(q,level),delta=p-center;
        float b=dot(delta,direction),t=-b+sqrt(max(0.0,b*b-dot(delta,delta)+radius*radius));
        vec3 corrected=p+direction*t-center;float correction=t*t/(radius*max(dot(corrected,direction),1e-20));
        vec4 value=URSampleProbe(probe,corrected);value.rgb*=correction;
        float weight=(x==0?1.0-f.x:f.x)*(y==0?1.0-f.y:f.y)*(z==0?1.0-f.z:f.z);sum+=value*weight;
    }
    return URFinite(sum.rgb)&&!isnan(sum.a)&&!isinf(sum.a)?sum:vec4(0);
}
)GLSL";}
inline std::string UeLumenHighScreenProbeTraceShader(bool diagnostics=false){
    auto source=NativeScreenProbeTraceShader(diagnostics);
    TransportDeferredReplace(source,"void NSNativeTrace(int probe,int ray,out vec4 color,out vec4 hit,out vec4 metadata,out vec4 directionData) {",
        UeRadianceCacheReadGLSL()+UeLumenScreenTraceGLSL()+R"GLSL(
layout(binding=7) uniform sampler2D uiBlueNoiseVec2;
layout(binding=8) uniform sampler2D uiRayInfos;
layout(std140,binding=15) uniform UeProbeImportanceData {ivec4 uiTraceInfo;};
ivec2 UITile(vec3 world){
    vec4 clip=spViewProjection*vec4(world,1);
    ivec2 pixel=ivec2(floor((clip.xy/clip.w*.5+.5)*vec2(spScreen.xy)));
    uint phase=uint(spScreen.z)&7u;
    ivec2 jitter=ivec2(vec2(float(phase)/8.0,float(bitfieldReverse(phase)>>16)/65536.0)*float(spGrid.w));
    return ivec2((uvec2(pixel)-uvec2(jitter))/uint(spGrid.w));
}
vec2 UINoise(ivec2 tile,uint frame){
    uvec3 at=uvec3(uvec2(tile),frame)&uvec3(127u,127u,63u);
    return texelFetch(uiBlueNoiseVec2,ivec2(at.x,at.z*128u+at.y),0).xy;
}
uint UIRayInfo(int probe,int ray){
    ivec2 at=ivec2(probe%spGrid.x,probe/spGrid.x)*8+ivec2(ray%8,ray/8);
    return floatBitsToUint(texelFetch(uiRayInfos,at,0).x);
}
void UIDecode(uint info,out ivec2 cell,out int mipSize){
    cell=ivec2(int(info&63u),int((info>>6)&63u));
    mipSize=uiTraceInfo.x>>int((info>>12)&15u);
}

vec3 UITraceDirection(int probe,int ray,vec3 world){
    ivec2 cell;int mipSize;UIDecode(UIRayInfo(probe,ray),cell,mipSize);
    if(mipSize<=0||any(greaterThanEqual(cell,ivec2(mipSize))))return vec3(0);
    return UREquiArea((vec2(cell)+UINoise(UITile(world),uint(uiTraceInfo.z)))/float(mipSize));
}
)GLSL"+"\nvoid NSNativeTrace(int probe,int ray,out vec4 color,out vec4 hit,out vec4 metadata,out vec4 directionData) {");
    TransportDeferredReplace(source,"vec3 direction=normalize(STRayDirection(ray));directionData.xyz=direction;hit.xyz=position.xyz;",
        "vec3 direction=normalize(UITraceDirection(probe,ray,position.xyz));directionData.xyz=direction;hit.xyz=position.xyz;");
    // UE's tracing octahedron is a full-sphere equal-area map. The legacy
    // cosine-hemisphere producer and its half-space gate remain untouched.
    TransportDeferredReplace(source,"    if(dot(direction,normal)<=(cosineHemisphere?0.0:.02)){color.w=5;return;}","");
    TransportDeferredReplace(source,"    vec3 origin=safeOrigin.xyz,hitNormal;float distance=spTraceParams.x;",
        "    int urLevel;ivec3 urFirst;float urCutoff;bool urCovered=URCoverage(position.xyz,urLevel,urFirst,urCutoff);\n"
        "    float uhscNearLimit=urCovered?min(spTraceParams.x,urCutoff):spTraceParams.x;\n"
        "    UeScreenTraceCandidate uhscCandidate;bool uhscHasCandidate=UHSCTraceScreenCandidate(probe,ray,position.xyz,normal,direction,uhscNearLimit,uhscCandidate);\n"
        "    float uhscQueryLimit=uhscHasCandidate?spTraceParams.x:uhscNearLimit;\n"
        "    vec3 origin=safeOrigin.xyz,hitNormal;float distance=uhscQueryLimit;");
    // UE-only part3.w is logical transport distance, not native geometry-fetch
    // telemetry. Native hit.xyzw, direction.xyz and query metadata remain exact.
    // A near-query miss completed by the far cache still has max logical range.
    TransportDeferredReplace(source,"directionData.w=nsGeometryFetches;",
        "directionData.w=found&&!exhausted?distance:spTraceParams.x;");
    TransportDeferredReplace(source,"    if(!found){color.w=13;return;}",
        "    if(!found){if(urCovered){vec4 cached=URSample(position.xyz,direction,urLevel,urFirst);\n"
        "        color=cached.a>=.99999?vec4(max(cached.rgb,vec3(0)),13):vec4(0,0,0,11);hit=vec4(origin+direction*spTraceParams.x,spTraceParams.x);\n"
        "    }else color.w=13;return;}");
    // UE's HW Surface Cache self-feedback guard is 10cm (not an AO strength).
    // A valid screen candidate is first, while native geometry AND readable SC
    // remain mandatory. The original SCNativeIntersect logical call, precise
    // retry and actual initialization-count instrumentation remain in place.
    TransportDeferredReplace(source,"    color=NSFinite(radiance)?vec4(radiance,9):vec4(0,0,0,7);",
        "    if(NSFinite(radiance)){if(distance<.1)radiance=vec3(0);\n"
        "        if(uhscHasCandidate&&UHSCMatchNativeHit(uhscCandidate,hit.xyz,owner)){radiance=uhscCandidate.radiance;directionData.w=uhscCandidate.logicalDistance;}}\n"
        "    color=NSFinite(radiance)?vec4(radiance,9):vec4(0,0,0,7);");
    if(diagnostics){
        // Radiance status also describes the far-cache result. Preserve the
        // actual near query interval separately for the existing untimed oracle.
        TransportDeferredReplace(source,"metadata=vec4(-1,-1,1,float(triangle));",
            "metadata=vec4(-1,-1,uhscQueryLimit,float(triangle));");
        TransportDeferredReplace(source,"hit=vec4(origin+direction*spTraceParams.x,spTraceParams.x);",
            "hit=vec4(origin+direction*distance,distance);");
    }
    return source;
}
} // namespace Render::SurfaceCacheDetail
