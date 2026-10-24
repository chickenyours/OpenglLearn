#pragma once
#include <array>
#include <cstddef>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <stdexcept>
#include "pipeline_resource_builder.h"
#include "Render/Public/Compute/gpu_compute.h"
#include "surface_cache_spatial_shaders.h"
#include "surface_cache_native_spatial_shaders.h"
#include "ue_lumen_high_short_range_ao_shaders.h"

namespace Render::SurfaceCacheDetail {
// UE5.7 SHCommon.ush:232-248,291-315; MonteCarlo.ush:159-209.
// Format 1 is SH3 convolution written to a 6x6 equal-area octahedron with
// a one-texel mirrored border, not the legacy four-coefficient direction fit.
inline std::string UeLumenHighProbeMathGLSL(){return R"GLSL(
const float UPPI=3.14159265358979323846;
vec3 UPEqualArea(vec2 uv){
    uv=2.0*uv-1.0;float d=1.0-abs(uv.x)-abs(uv.y),r=1.0-abs(d);
    float phi=r==0.0?0.0:(UPPI/4.0)*((abs(uv.y)-abs(uv.x))/r+1.0);
    float f=r*sqrt(2.0-r*r);
    return vec3(f*sign(uv.x)*abs(cos(phi)),f*sign(uv.y)*abs(sin(phi)),sign(d)*(1.0-r*r));
}
vec2 UPInverseEqualArea(vec3 direction){
    direction=normalize(direction);vec3 a=abs(direction);float r=sqrt(1.0-a.z);
    float x=min(a.x,a.y)/(max(a.x,a.y)+5.42101086243e-20);
    float phi=.0419038818029165735901852432784-.0251390972343483509333252996350*x;
    phi=.0881770664775316294736387951347+phi*x;
    phi=-.247333733281268944196501420480+phi*x;
    phi=.0061572017898280213493197203466+phi*x;
    phi=.636226545274016134946890922156+phi*x;
    phi=.00000406758566246788489601959989+phi*x;
    phi=a.x<a.y?1.0-phi:phi;vec2 uv=vec2(r-phi*r,phi*r);
    if(direction.z<0.0)uv=1.0-uv.yx;
    uv=uintBitsToFloat(floatBitsToUint(uv)^(floatBitsToUint(direction.xy)&uvec2(0x80000000u)));
    return uv*.5+.5;
}
float UPBasis(int k,vec3 v){
    if(k==0)return .282095;
    if(k==1)return -.488603*v.y;if(k==2)return .488603*v.z;if(k==3)return -.488603*v.x;
    if(k==4)return 1.092548*v.x*v.y;if(k==5)return -1.092548*v.y*v.z;
    if(k==6)return .315392*(3.0*v.z*v.z-1.0);if(k==7)return -1.092548*v.x*v.z;
    return .546274*(v.x*v.x-v.y*v.y);
}
ivec2 UPWrapBorder(ivec2 p){
    if(p.x<1){p.x=1-p.x;p.y=7-p.y;}if(p.x>=7){p.x=7-(p.x-6);p.y=7-p.y;}
    if(p.y<1){p.y=1-p.y;p.x=7-p.x;}if(p.y>=7){p.y=7-(p.y-6);p.x=7-p.x;}
    return p-1;
}
)GLSL";}

// RayInfo prefix shared with the default UE importance producer. BN uses
// the actual view-frame modulo 8, independent of any history reset.
inline std::string UeLumenHighProbeRayDataGLSL(){return R"GLSL(
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
)GLSL";}
inline std::string UeLumenHighProbeNeighborsShader(bool diagnostics=false){
    auto s=NativeProbeSpatialNeighborsShader(diagnostics);
    const std::string entry=diagnostics?"void NSNativeNeighborMain() {":"void main() {";
    const auto a=s.find(entry),b=s.find("\n}\n",a);
    if(a==std::string::npos||b==std::string::npos)throw std::logic_error("UE neighbor entry changed");
    s.replace(a,b+3-a,UeLumenHighProbeRayDataGLSL()+R"GLSL(
layout(binding=11) uniform sampler2D uiNeighborProbeInfo;
void UI_NEIGHBOR_MAIN() {
    ivec2 pixel=ivec2(gl_FragCoord.xy);int width=min(spGrid.x*SF_NEIGHBOR_COUNT,8192);
    int address=pixel.x+pixel.y*width,pair=address%SF_NEIGHBOR_COUNT,probe=address/SF_NEIGHBOR_COUNT;
    outColor=vec4(0);if(probe>=spTraceAtlas.w||pair>=12)return;
    if(pair>=4&&texelFetch(uiNeighborProbeInfo,ivec2(probe%spGrid.x,probe/spGrid.x),0).x<=.01)return;
    vec4 p=SFProbe(probe,0),nd=SFProbe(probe,1);if(!SFProbeValid(p,nd))return;
    const ivec2 offsets[12]=ivec2[12](ivec2(-1,0),ivec2(1,0),ivec2(0,-1),ivec2(0,1),
        ivec2(-2,0),ivec2(2,0),ivec2(0,-2),ivec2(0,2),
        ivec2(-1,1),ivec2(1,1),ivec2(-1,-1),ivec2(1,-1));
    ivec2 tile=UITile(p.xyz)+offsets[pair];
    if(any(lessThan(tile,ivec2(0)))||any(greaterThanEqual(tile,spGrid.xy)))return;
    int chosen=tile.x+tile.y*spGrid.x;vec4 other=SFProbe(chosen,0),otherNd=SFProbe(chosen,1);
    // UE Filtering.usf:292-317 filters a full-sphere radiance field. The
    // receiver normal is used later to convolve irradiance; matching probe
    // normals or imposing the legacy double-plane gate here cuts valid
    // curved/corner support before UE's hit-parallax contact test can run.
    // Cache the original UE position weight once for all 64 bins / 3 passes.
    if(!SFProbeValid(other,otherNd)||isnan(nd.w)||isinf(nd.w)||
       isnan(otherNd.w)||isinf(otherNd.w))return;
    float relative=abs(otherNd.w-nd.w)/nd.w;
    float positionWeight=exp2(-1000.0*relative*relative);
    if(positionWeight<.0001)return;
    // Project adapter: retain the exact safe-origin visibility segment. This
    // remains geometry certification, independent of radiance/normal weights.
    if(sfOptions.z!=0&&!SFPairVisible(probe,chosen,p,nd,other,otherNd))return;
    outColor=vec4(float(chosen+1),positionWeight,0,0);
}
)GLSL");
    const auto marker=s.find("UI_NEIGHBOR_MAIN");s.replace(marker,16,diagnostics?"NSNativeNeighborMain":"main");
    return s;
}
inline std::string UeLumenHighProbeSpatialShader(){
    auto s=SpatialFilterShader(true);
    const auto replace=[&](const std::string& a,const std::string& b){
        const auto i=s.find(a);if(i==std::string::npos)throw std::logic_error("UE fixed gather spatial source changed");
        s.replace(i,a.size(),b);
    };
    auto a=s.find("vec4 SFRay(int probe,int ray,int part) {"),b=s.find("bool SFReadable",a);
    if(a==std::string::npos||b==std::string::npos)throw std::logic_error("UE gather metadata source changed");
    s.replace(a,b-a,UeLumenHighProbeMathGLSL()+UeLumenHighProbeRayDataGLSL()+R"GLSL(
layout(binding=11) uniform sampler2D uiProbeInfo;
vec4 SFRay(int probe,int ray,int part){
    int address=probe*64+ray,width=spTraceAtlas.x/4;ivec2 at=ivec2(address%width,address/width);
    if(part==0)return texelFetch(sfInputRays,at,0);
    if(part==1)return vec4(0,0,0,texelFetch(sfRayMetadata,at,0).x);
    if(part==3)return vec4(UPEqualArea((vec2(ray%8,ray/8)+UINoise(UITile(SFProbe(probe,0).xyz),uint(uiTraceInfo.z)))/8.0),0);
    return vec4(0);
}
)GLSL");
    a=s.find("        float cosine=dot(direction,normal);if(cosine<=0)continue;");
    b=s.find("        if(closestRay<0)continue;",a);
    if(a==std::string::npos||b==std::string::npos)throw std::logic_error("UE angular grid source changed");
    s.replace(a,b-a,"        int closestRay=ray;\n");
    replace("    if(state==13)return spTraceParams.x;\n    return max(SFRay(probe,ray,1).w,0.0);","    return SFRay(probe,ray,1).w;");
    replace("if(probe>=spTraceAtlas.w||!SFReadable(state)||!SFFinite(outColor.rgb))return;","if(probe>=spTraceAtlas.w||!SFFinite(outColor.rgb))return;");
    replace("    vec3 sum=max(outColor.rgb,vec3(0));float totalWeight=1.0;",
        "    vec3 sum=ownDistance>=0?max(outColor.rgb,vec3(0)):vec3(0);float totalWeight=ownDistance>=0?1.0:0.0;\n"
        "    float moving=texelFetch(uiProbeInfo,ivec2(probe%spGrid.x,probe/spGrid.x),0).x;");
    a=s.find("    int localAddress=probe*SF_NEIGHBOR_COUNT+8;");
    b=s.find("        int address=probe*SF_NEIGHBOR_COUNT+pair;",a);
    if(a==std::string::npos||b==std::string::npos)throw std::logic_error("UE filter stencil changed");
    s.replace(a,b-a,"    for(int pair=0;pair<12;++pair) {\n        if(pair>=4&&moving<=.01)break;\n");
    replace("if(!SFReadable(neighborState)||!SFFinite(value.rgb))continue;",
        "if(SFHitDistance(index,closestRay,neighborState)<0||!SFFinite(value.rgb))continue;");
    replace("float distance=min(SFHitDistance(index,closestRay,neighborState),ownDistance);",
        "float distance=SFHitDistance(index,closestRay,neighborState);if(ownDistance>=0)distance=min(distance,ownDistance);");
    // UE uses the surface world positions in the parallax filter; tracing
    // still uses the certified biased origins in the immutable ray tuples.
    replace("traceOrigin=safe.xyz;","/* UE filter WorldPosition is unshifted. */");
    replace("neighborPosition=safe.xyz;","/* UE neighbor WorldPosition is unshifted. */");
    replace("if(lengthSquared<.00000001)continue;","if(lengthSquared<.00000001&&moving<=.01)continue;");
    replace("float angle=acos(clamp(dot(toNeighborHit,direction)*inversesqrt(lengthSquared),-1.0,1.0));",
        "float angle=moving>.01?0.0:acos(clamp(dot(toNeighborHit,direction)*inversesqrt(lengthSquared),-1.0,1.0));");
    replace("float weight=neighbor.y*max(0.0,1.0-angle/max(sfFilter.y,.000001));",
        "float weight=neighbor.y*(moving>.01?1.0:max(0.0,1.0-angle/radians(10.0)));");
    replace("outColor.rgb=mix(outColor.rgb,sum/totalWeight,clamp(sfFilter.w,0.0,1.0));",
        "outColor.rgb=totalWeight>0?sum/totalWeight:vec3(0);");
    return s;
}
// UE Filtering.usf:618-645,986-1045. One 64-thread group per actual probe;
// coefficients retain all 64 directions in the denominator, including black
// and unreadable cache hits. Moving is a geometric trace flag, never RGB energy.
inline std::string UeLumenHighProbeProjectCommonGLSL(){
    return std::string("#version 450 core\n")+ScreenProbeTraceDetail::ConstantsGLSL()+
        SurfaceCacheDataGLSL()+SpatialProbeGLSL()+UeLumenHighProbeMathGLSL()+R"GLSL(
layout(local_size_x=64) in;
layout(binding=0) uniform sampler2D upRadiance;
layout(binding=2) uniform sampler2D upPositions;
layout(binding=6) uniform sampler2D upTrace;
layout(binding=10) uniform sampler2D upOrigins;
layout(binding=13) uniform sampler2D upCardMetadata;
layout(std140,binding=13) uniform UeProbeMotionData {ivec4 upMotionInfo;mat4 upPrevFromCurrent[256];};
layout(rgba32f,set=3,binding=0) uniform writeonly image2D upSH;
layout(rgba32f,set=3,binding=1) uniform writeonly image2D upInfo;
shared vec3 upSum[9][64];
shared vec3 upMeta[64];
vec4 UPRaw(int probe,int ray,int part){
    int at=(probe*64+ray)*4+part;return texelFetch(upTrace,ivec2(at%spTraceAtlas.x,at/spTraceAtlas.x),0);
}
bool UPReadable(int s){return s==1||s==5||s==9||s==10||s==13;}
int UPHitOwner(int triangle){
    if(triangle<0||triangle>=scGeometry.w)return -1;
    int at=scAtlas.w*12+triangle/4;
    return int(texelFetch(upCardMetadata,ivec2(at%scAtlas.x,at/scAtlas.x),0)[triangle%4]);
}
float UPWorldSpeed(vec3 world,int owner){
    if(upMotionInfo.y==0||owner<0||owner>=upMotionInfo.x)return 0.0;
    return length(world-(upPrevFromCurrent[owner]*vec4(world,1)).xyz);
}
int UPProbeOwner(vec3 world){
    vec4 clip=spViewProjection*vec4(world,1);if(clip.w<=0)return -1;
    ivec2 pixel=ivec2(floor((clip.xy/clip.w*.5+.5)*vec2(spScreen.xy)));
    if(any(lessThan(pixel,ivec2(0)))||any(greaterThanEqual(pixel,spScreen.xy)))return -1;
    float owner=texelFetch(upPositions,pixel,0).w;
    return owner>=1&&owner==floor(owner)&&owner<=float(upMotionInfo.x)?int(owner)-1:-1;
}
)GLSL";
}

// Filtering.usf CompositeTracesWithScatter: stochastic bin assignment,
// exact integer accumulation scales, minimum logical transport hit distance, no averaging
// by the number of samples landing in a bin. This is image data only.
inline std::string UeLumenHighProbeScatterShader(){
    auto s=UeLumenHighProbeProjectCommonGLSL();
    const auto a=s.find("layout(rgba32f,set=3,binding=0) uniform writeonly image2D upSH;");
    const auto b=s.find("vec4 UPRaw",a);
    if(a==std::string::npos||b==std::string::npos)throw std::logic_error("UE scatter common changed");
    s.replace(a,b-a,R"GLSL(
layout(rgba16f,set=3,binding=0) uniform writeonly image2D uiScatterRaw;
layout(rgba32f,set=3,binding=1) uniform writeonly image2D uiScatterAux;
layout(rgba32f,set=3,binding=2) uniform writeonly image2D uiScatterProbeInfo;
shared uint uiAccum[64][6];
shared vec3 upMeta[64];
)GLSL");
    return s+UeLumenHighProbeRayDataGLSL()+R"GLSL(
ivec2 UIWrapGather(ivec2 p){
    if(p.x<1){p.x=1-p.x;p.y=9-p.y;}if(p.x>=9){p.x=9-(p.x-8);p.y=9-p.y;}
    if(p.y<1){p.y=1-p.y;p.x=9-p.x;}if(p.y>=9){p.y=9-(p.y-8);p.x=9-p.x;}
    return p-1;
}
void main(){
    uint lane=gl_LocalInvocationIndex;int ray=int(lane);
    int probe=int(gl_WorkGroupID.y)*spGrid.x+int(gl_WorkGroupID.x);
    ivec2 probePixel=ivec2(probe%spGrid.x,probe/spGrid.x);
    int address=probe*64+ray,width=spTraceAtlas.x/4;ivec2 outAt=ivec2(address%width,address/width);
    vec4 pp=vec4(0),nd=vec4(0),safe=vec4(0);
    if(probe<spTraceAtlas.w){pp=SFProbe(probe,0);nd=SFProbe(probe,1);safe=texelFetch(upOrigins,probePixel,0);}
    bool valid=probe<spTraceAtlas.w&&SFProbeValid(pp,nd)&&safe.w>0&&SFFinite(safe.xyz);
    if(!valid){
        imageStore(uiScatterRaw,outAt,vec4(0));imageStore(uiScatterAux,outAt,vec4(-1,0,0,0));
        if(lane==0u)imageStore(uiScatterProbeInfo,probePixel,vec4(0));return;
    }
    for(int k=0;k<5;++k)uiAccum[lane][k]=0u;
    uiAccum[lane][5]=floatBitsToUint(spTraceParams.x);
    barrier();
    vec4 color=UPRaw(probe,ray,0),hit=UPRaw(probe,ray,1),metadata=UPRaw(probe,ray,2);
    int status=int(color.w+.5);bool readable=UPReadable(status)&&SFFinite(color.rgb);
    bool knownHit=metadata.w>=0&&status!=15&&status!=16&&SFFinite(hit.xyz)&&hit.w>0;
    float moving=0.0;
    if(knownHit&&upMotionInfo.y!=0){
        int owner=UPHitOwner(int(metadata.w+.5)),sourceOwner=UPProbeOwner(pp.xyz);
        if(owner>=0&&owner<upMotionInfo.x&&sourceOwner>=0)
            moving=abs(UPWorldSpeed(pp.xyz,sourceOwner)-UPWorldSpeed(hit.xyz,owner))/max(nd.w,1.0)>.005?1.0:0.0;
    }
    ivec2 cell;int mipSize;UIDecode(UIRayInfo(probe,ray),cell,mipSize);
    // The producer emits only original UE RayInfo coordinates / mip levels.
    if(mipSize>0&&all(lessThan(cell,ivec2(mipSize)))){
        float ratio=8.0/float(mipSize),sampleWeight=ratio*ratio;
        vec3 lighting=readable?max(color.rgb,vec3(0))*sampleWeight:vec3(0);
        float peak=max(lighting.x,max(lighting.y,lighting.z));if(peak>10.0)lighting*=10.0/peak;
        ivec2 tile=UITile(pp.xyz);uint rayFrame=uint(uiTraceInfo.z);
        vec2 gatherCoord=(vec2(cell)+UINoise(tile,rayFrame))*ratio-.5;
        vec2 randomValue=UINoise(tile,rayFrame+31u);
        ivec2 quantized=ivec2(floor(gatherCoord))+ivec2(greaterThanEqual(fract(gatherCoord),randomValue));
        ivec2 wrapped=UIWrapGather(quantized+1);int target=wrapped.y*8+wrapped.x;
        float maxPerThread=float(0xffffffffu)/64.0;
        uvec3 quantizedLighting=uvec3(lighting*(maxPerThread/10.0));
        atomicAdd(uiAccum[target][0],quantizedLighting.x);atomicAdd(uiAccum[target][1],quantizedLighting.y);
        atomicAdd(uiAccum[target][2],quantizedLighting.z);atomicExchange(uiAccum[target][3],1u);
        atomicAdd(uiAccum[target][4],uint(moving*sampleWeight*maxPerThread));
        // UE raw part3.w follows the radiance representation's logical hit:
        // accepted screen RGB uses its ScreenHit distance; native hit.xyzw is
        // retained separately for movement and the independent geometry oracle.
        float logicalDistance=UPRaw(probe,ray,3).w;
        bool logicalHit=knownHit&&!isnan(logicalDistance)&&!isinf(logicalDistance)&&logicalDistance>0;
        float distance=logicalHit?min(logicalDistance,spTraceParams.x):spTraceParams.x;
        atomicMin(uiAccum[target][5],floatBitsToUint(distance));
    }
    upMeta[lane]=vec3(0,readable?1.0:0.0,knownHit?1.0/hit.w:0.0);
    barrier();
    float inverseMax=64.0/float(0xffffffffu);
    bool anySample=uiAccum[lane][3]>0u;
    vec3 lighting=vec3(uiAccum[lane][0],uiAccum[lane][1],uiAccum[lane][2])*(10.0*inverseMax);
    float binMoving=clamp(float(uiAccum[lane][4])*inverseMax,0.0,1.0);
    imageStore(uiScatterRaw,outAt,vec4(lighting,anySample?1.0:0.0));
    imageStore(uiScatterAux,outAt,vec4(anySample?uintBitsToFloat(uiAccum[lane][5]):-1.0,binMoving,anySample?1.0:0.0,0));
    upMeta[lane].x=binMoving;barrier();
    for(uint stride=32u;stride>0u;stride>>=1u){if(lane<stride)upMeta[lane]+=upMeta[lane+stride];barrier();}
    if(lane==0u)imageStore(uiScatterProbeInfo,probePixel,vec4(upMeta[0]/64.0,1));
}
)GLSL";
}
inline std::string UeLumenHighProbeProjectShader(){
    // After scatter and spatial filtering, these are the original fixed gather
    // bins, not the jittered physical ray slots in the immutable packed trace.
    return UeLumenHighProbeProjectCommonGLSL()+R"GLSL(
layout(binding=11) uniform sampler2D uiScatterProbeInfo;
void main(){
    uint lane=gl_LocalInvocationIndex;int ray=int(lane);
    int probe=int(gl_WorkGroupID.y)*spGrid.x+int(gl_WorkGroupID.x);
    ivec2 p=ivec2(probe%spGrid.x,probe/spGrid.x);vec4 info=texelFetch(uiScatterProbeInfo,p,0);
    if(probe>=spTraceAtlas.w||info.w<=0){
        if(lane==0u){for(int k=0;k<9;++k)imageStore(upSH,ivec2(p.x*9+k,p.y),vec4(0));imageStore(upInfo,p,vec4(0));}return;
    }
    int at=probe*64+ray,width=spTraceAtlas.x/4;
    vec3 radiance=texelFetch(upRadiance,ivec2(at%width,at/width),0).rgb;
    if(!SFFinite(radiance))radiance=vec3(0);
    vec3 direction=UPEqualArea((vec2(ray%8,ray/8)+.5)/8.0);
    for(int k=0;k<9;++k)upSum[k][lane]=max(radiance,vec3(0))*UPBasis(k,direction);
    barrier();
    for(uint stride=32u;stride>0u;stride>>=1u){if(lane<stride)for(int k=0;k<9;++k)upSum[k][lane]+=upSum[k][lane+stride];barrier();}
    if(lane==0u){
        for(int k=0;k<9;++k)imageStore(upSH,ivec2(p.x*9+k,p.y),vec4(upSum[k][0]/64.0,info.y));
        imageStore(upInfo,p,info);
    }
}
)GLSL";
}
inline std::string UeLumenHighProbeIrradianceShader(){
    return std::string("#version 450 core\n")+ScreenProbeTraceDetail::ConstantsGLSL()+UeLumenHighProbeMathGLSL()+R"GLSL(
in vec2 vUV;layout(location=0) out vec4 outColor;
layout(binding=0) uniform sampler2D upSH;
layout(binding=1) uniform sampler2D upInfo;
void main(){
    ivec2 pixel=ivec2(gl_FragCoord.xy),p=pixel/8,cell=pixel%8;
    int probe=p.x+p.y*spGrid.x;outColor=vec4(0);
    vec4 info=texelFetch(upInfo,p,0);if(probe>=spTraceAtlas.w||info.w<=0)return;
    vec3 n=UPEqualArea((vec2(UPWrapBorder(cell))+.5)/6.0),sum=vec3(0);
    for(int k=0;k<9;++k){
        float transfer=UPBasis(k,n)*(k==0?UPPI:k<4?2.0*UPPI/3.0:UPPI/4.0);
        sum+=texelFetch(upSH,ivec2(p.x*9+k,p.y),0).rgb*transfer;
    }
    outColor=vec4(max(4.0*UPPI*sum,vec3(0)),info.y);
}
)GLSL";
}

inline std::string UeLumenHighProbeGatherShader(){
    // Geometry-only compatibility is inherited; evaluate the four-probe
    // octahedral irradiance with UE non-stochastic interpolation weights.
    return std::string("#version 450 core\n")+ScreenProbeTraceDetail::ConstantsGLSL()+SurfaceCacheDataGLSL()+R"GLSL(
layout(binding=0) uniform sampler2D uhBlueNoiseScalar;
layout(binding=1) uniform sampler2D uhBlueNoiseVec2;
layout(binding=2) uniform sampler2D uhPositions;
layout(binding=3) uniform sampler2D uhNormals;
#define SPGI_SURFACE_POSITIONS uhPositions
float UPCurvedSupport(int probe,vec3 p,vec3 n,float depth);
#define SPGI_CURVED_SUPPORT(probe,p,n,depth) UPCurvedSupport(probe,p,n,depth)
)GLSL"+ScreenProbePlacementDetail::SamplingGLSL()+UeLumenHighSamplingGLSL(false)+UeLumenHighProbeMathGLSL()+R"GLSL(
in vec2 vUV;layout(location=0) out vec4 outColor;layout(location=1) out float outMoving;
layout(binding=5) uniform sampler2D upUniform;
layout(binding=6) uniform sampler2D upSpawned;
layout(binding=7) uniform sampler2D upIrradiance;
layout(binding=8) uniform sampler2D upInfo;
layout(binding=11) uniform sampler2D upOrigins;
vec4 UPProbe(int probe,int part){
    bool uniformProbe=probe<spTraceAtlas.z;int slot=uniformProbe?probe:probe-spTraceAtlas.z;
    int columns=spGrid.x*(uniformProbe?1:spGrid.z);ivec2 at=ivec2(2*(slot%columns)+part,slot/columns);
    return uniformProbe?texelFetch(upUniform,at,0):texelFetch(upSpawned,at,0);
}
ivec2 UPProbePixel(vec3 world){
    vec4 clip=spViewProjection*vec4(world,1);
    return clip.w>0?ivec2(floor((clip.xy/clip.w*.5+.5)*vec2(spScreen.xy))):ivec2(-1);
}
float UPCurvedSupport(int probe,vec3 p,vec3 n,float depth){
    float inverseDistance=texelFetch(upInfo,ivec2(probe%spGrid.x,probe/spGrid.x),0).z;
    return SPGiCurvedHitSupport(UPProbe(probe,0).xyz-p,n,inverseDistance);
}
bool UPCandidate(ivec2 pixel,vec3 p,vec3 n,float depth,float owner,int probe){
    if(probe<0||probe>=spTraceAtlas.w)return false;
    vec4 pp=UPProbe(probe,0),nd=UPProbe(probe,1),origin=texelFetch(upOrigins,ivec2(probe%spGrid.x,probe/spGrid.x),0);
    // UE Gather.usf:142-201,213-234,237-294 evaluates a full-sphere
    // radiance probe using the receiver's plane and normal. A probe need not
    // belong to the same object or have the receiver's shading normal.
    // The original UE weights are evaluated below, after candidate validation.
    // Project adapter: retain certified current source geometry and bounds;
    // these are validity checks, not the legacy hemisphere/cluster weights.
    if(!(pp.w>=.5)||!UHFinite(pp.w)||!UHFinite3(pp.xyz)||
       !(nd.w>0)||!UHFinite(nd.w)||!UHFinite3(nd.xyz)||dot(nd.xyz,nd.xyz)<.5||
       !(origin.w>0)||!UHFinite(origin.w)||!UHFinite3(origin.xyz))return false;
    vec4 sourceClip=spViewProjection*vec4(pp.xyz,1);
    if(!UHFinite3(sourceClip.xyz)||!UHFinite(sourceClip.w)||!(sourceClip.w>0))return false;
    vec2 sourceUv=sourceClip.xy/sourceClip.w*.5+.5;
    if(any(isnan(sourceUv))||any(isinf(sourceUv))||any(lessThan(sourceUv,vec2(0)))||
       any(greaterThanEqual(sourceUv,vec2(1))))return false;
    ivec2 source=ivec2(floor(sourceUv*vec2(spScreen.xy)));
    vec4 sourceView=texelFetch(uhPositions,source,0),sourceNormal=texelFetch(uhNormals,source,0);
    return UHFinite3(sourceView.xyz)&&UHFinite(sourceView.w)&&sourceView.z<0&&
        sourceView.w>=1&&sourceView.w==floor(sourceView.w)&&sourceView.w<=float(scAtlas.w/6)&&
        UHFinite3(sourceNormal.xyz)&&UHFinite(sourceNormal.w)&&sourceNormal.w>=.5&&dot(sourceNormal.xyz,sourceNormal.xyz)>=.5;
}
vec3 UPIrradiance(int probe,vec2 uv,out vec4 info){
    ivec2 p=ivec2(probe%spGrid.x,probe/spGrid.x);
    info=texelFetch(upInfo,p,0);
    // UE Gather.usf:831-837 uses the existing bilinear-clamped sampler.
    // This RGBA16F target is already Linear. The one-texel mirrored border
    // contains the whole footprint, including normal UV exactly zero/one.
    vec2 atlasUv=(vec2(p*8)+uv*6.0+1.0)/vec2(textureSize(upIrradiance,0));
    return textureLod(upIrradiance,atlasUv,0.0).rgb;
}

// Gather.usf:153-200,213-294,1153-1178,1195-1238. High uses
// interpolation depth exponent -200 and one full-width jitter of the probe grid.
void main(){
    outColor=vec4(0);outMoving=0;
    ivec2 pixel=UHSourceFullResPixel(ivec2(gl_FragCoord.xy));
    vec4 view=texelFetch(uhPositions,pixel,0),normal=texelFetch(uhNormals,pixel,0);
    if(view.w<1||view.w!=floor(view.w)||view.w>float(scAtlas.w/6)||view.z>=0||
        normal.w<.5||!UHFinite3(view.xyz)||!UHFinite3(normal.xyz)||dot(normal.xyz,normal.xyz)<.5)return;
    vec3 p=(spInverseView*vec4(view.xyz,1)).xyz,n=normalize(normal.xyz);float depth=-view.z;
    float jitterWidth=mix(1.0,.5,clamp((depth-5.0)/5.0,0.0,1.0));
    vec2 offset=(UHBlueNoiseVec2(pixel)*2.0-1.0)*float(spGrid.w)*jitterWidth;
    ivec2 jittered=ivec2(clamp(vec2(pixel)+offset,vec2(0),vec2(spScreen.xy-1)));
    vec4 jv=texelFetch(uhPositions,jittered,0);vec3 jp=(spInverseView*vec4(jv.xyz,1)).xyz;
    float relative=abs(dot(jp-p,n))/max(depth,1e-20);
    // UE Gather.usf:1166-1178 tests the receiver plane, not primitive identity.
    // Coplanar physical slabs may have distinct owners. Keep source validity
    // and the exact original plane/depth gate, then use the original footprint.
    if(jv.w<1||!UHFinite(jv.w)||jv.w!=floor(jv.w)||jv.w>float(scAtlas.w/6)||
       jv.z>=0||!UHFinite3(jp)||exp2(-1000000.0*relative*relative)<=.01)offset=vec2(0);
    ivec2 full=ivec2(clamp(vec2(pixel-SPJitter())+offset,vec2(0),vec2(spScreen.xy-1)));
    ivec2 base=min(full/spGrid.w,max(spGrid.xy-2,ivec2(0)));
    // The host's fixed resident grid omits UE's guard allocation at its edge.
    // Clamp the final edge footprint instead of extrapolating negative weights.
    vec2 f=clamp(vec2(full-base*spGrid.w+1)/float(spGrid.w+2),vec2(0),vec2(1));
    ivec4 probes;vec4 weights=vec4(0);
    for(int corner=0;corner<4;++corner){
        ivec2 tile=SPCorner(base,corner);int chosen=tile.x+tile.y*spGrid.x;
        float bilinear=(corner%2==0?1.0-f.x:f.x)*(corner/2==0?1.0-f.y:f.y);
        if(UPCandidate(pixel,p,n,depth,view.w,chosen))
            weights[corner]=bilinear*exp2(-200.0*abs(dot(UPProbe(chosen,0).xyz-p,n))/depth);
        if(weights[corner]<.01&&spOptions.z>.5){
            for(int candidate=0;candidate<16;++candidate){
                if(candidate>=spGrid.z)break;
                int probe=spTraceAtlas.z+(tile.y*spGrid.x+tile.x)*spGrid.z+candidate;
                if(!UPCandidate(pixel,p,n,depth,view.w,probe))continue;
                vec3 pp=UPProbe(probe,0).xyz;ivec2 screen=UPProbePixel(pp);
                vec2 distance=abs(vec2(screen-pixel));
                float newWeight=exp2(-200.0*abs(dot(pp-p,n))/depth)*
                    (1.0-clamp(min(distance.x,distance.y)/float(spGrid.w),0.0,1.0));
                if(newWeight>weights[corner]){weights[corner]=newWeight;chosen=probe;}
            }
        }
        probes[corner]=chosen;
    }

    float total=dot(weights,vec4(1));if(total<.0001)return;
    // UE STOCHASTIC_PROBE_INTERPOLATION=0: Gather.usf:904-916,1425-1433.
    // Evaluate the same four-corner distribution directly. This renderer has
    // no final TAA/TSR stage to resolve the additional single-probe CDF noise.
    // Keep the geometric denominator: unknown/valid-black angles contribute
    // black rather than lending their probability mass to a brighter probe.
    vec2 irradianceUv=UPInverseEqualArea(n);vec3 lighting=vec3(0);
    float readable=0.0,moving=0.0;
    for(int corner=0;corner<4;++corner){
        if(weights[corner]<=0.0)continue;
        vec4 info;vec3 value=UPIrradiance(probes[corner],irradianceUv,info);
        if(!UHFinite3(value)||!UHFinite(info.x)||!UHFinite(info.y))continue;
        float weight=weights[corner]/total;
        lighting+=value*weight;readable+=info.y*weight;moving+=info.x*weight;
    }
    outColor=vec4(lighting*(scParameters.z/UPPI),clamp(readable,0.0,1.0));
    outMoving=total>=.01&&readable>0?max(moving,.004):0.0;
}
)GLSL";
}
} // namespace Render::SurfaceCacheDetail

namespace Render::PipelineDetail {
struct alignas(16) UeLumenHighProbeMotionConstants {
    glm::ivec4 info{0};
    std::array<glm::mat4,256> previousFromCurrent{};
};
static_assert(offsetof(UeLumenHighProbeMotionConstants,previousFromCurrent)==16);
static_assert(sizeof(UeLumenHighProbeMotionConstants)==16+256*64);
template<class Target> struct UeLumenHighScreenProbeGatherState {
    std::shared_ptr<const void> programsOwner,targetsOwner;
    std::shared_ptr<Compute::Device> computeOwner;
    RenderResourceHandle<PipelineSpec> neighborsProgram,neighborsDiagnosticProgram,spatialProgram,irradianceProgram,gatherProgram;
    RenderResourceHandle<UniformBufferSpec> spatialBuffer,motionBuffer;
    Compute::PipelineHandle scatterProgram,projectProgram;
    std::array<Target,2> radiance;
    Target neighbors,scatterRaw,scatterAux,scatterProbeInfo,sh,probeInfo,irradiance,halfGi,lightIsMoving;
    UeLumenHighProbeMotionConstants motion;
    std::uint64_t motionCacheGeneration=0,motionGeometryGeneration=0;
    bool motionSnapshotPrepared=false;
};
} // namespace Render::PipelineDetail
