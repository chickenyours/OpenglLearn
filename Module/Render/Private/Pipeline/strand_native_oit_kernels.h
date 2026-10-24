#pragma once
#include "Render/Private/Pipeline/strand_native_oit_shaders.h"
#include "Render/Private/Pipeline/strand_curve_shaders.h"

namespace Render::PipelineDetail {
inline std::string StrandNativeOitWorkGLSL(){return R"GLSL(
struct NativeLine {vec4 a,b,color0,color1,motion0,motion1;uvec4 tiles;};
layout(std430,set=2,binding=0) buffer NativeLines {NativeLine nativeLines[];};
layout(std430,set=2,binding=1) buffer NativeTiles {uvec4 nativeTiles[];};
layout(std430,set=2,binding=2) coherent buffer NativePairs {uint nativePairs[];};
layout(std430,set=2,binding=3) buffer NativeGlobal {uvec2 nativeGlobal[];};
layout(std430,set=2,binding=4) buffer NativeMotion {vec4 nativeMotion[];};
layout(std430,set=2,binding=5) buffer NativeMeta {uint nativeMeta[];};
layout(push_constant) uniform NativeDispatch {
    uvec4 nativeExtent; // width,height,tilesX,tilesY
    uvec4 nativeWork; // pieces,pairCapacity,innerLayers,flags:curve/motion
    vec4 nativeParameters; // objectOpacity,depthThreshold,bucketScale,bucketOffset
    vec4 nativeForward;
};
uint NativeLinearInvocation(){return gl_GlobalInvocationID.x+gl_GlobalInvocationID.y*gl_NumWorkGroups.x*gl_WorkGroupSize.x;}
)GLSL";}

inline std::string StrandNativeOitProjectShader(){
    auto curve=StrandCurveGLSL();
    StrandCurveReplace(curve,"StrandCurveInput()","StrandCurveInput(uint inputPiece,uint endpoint)");
    StrandCurveReplace(curve,"uint(gl_InstanceID)","inputPiece");
    StrandCurveReplace(curve,"gl_VertexID","int(endpoint)");
    return std::string(R"GLSL(#version 450 core
layout(local_size_x=64) in;
layout(std140,binding=0) uniform StrandView {
    mat4 vp;vec4 camera,viewport;mat4 previousVP,currentUnjitteredVP,previousUnjitteredVP;
    vec4 previousCamera,motionState;
};
layout(std140,binding=1) uniform StrandObject {mat4 model;};
layout(std140,binding=8) uniform StrandMotionObject {mat4 previousModel;vec4 motionObject;};
layout(std140,binding=2) uniform StrandMaterial {
    vec4 rootColor,tipColor,surface,volume,raster,fiber,scattering,shadows,ibl;
    vec4 colorWeights,colorOffsets,colorVariation,nativeMoments,spatialIncident;
    vec4 opaqueShadowParameters;
};
layout(binding=6) uniform sampler2D endpointColors;
)GLSL")+curve+StrandNativeOitWorkGLSL()+R"GLSL(
vec3 NativeColor(uint i){uint w=uint(textureSize(endpointColors,0).x);return texelFetch(endpointColors,ivec2(i%w,i/w),0).rgb;}
vec3 NativeRelativeUnproject(vec3 ndc){
    vec4 p=strandCurveInverseRasterProjection*vec4(ndc,1.0);
    return (p.xyz-camera.xyz*p.w)/p.w;
}
void NativeEndpoint(uint piece,uint endpoint,out vec4 projected,out vec3 shade,out vec4 motion,out float cameraDepth){
    StrandCurveVertex v=StrandCurveInput(piece,endpoint);
    if((nativeWork.w&1u)==0u){
        vec4 record=StrandCurveRead(strandCurveOffsets.w+piece);
        uvec4 topology=uvec4(StrandCurveRead(strandCurveOffsets.z+uint(record.x)));
        uint point=endpoint==0u?topology.y:topology.z;
        v.positionRadius=StrandCurveRead(point);v.previousPositionRadius=StrandCurveRead(strandCurveOffsets.x+point);
        v.t=float(endpoint);
    }
    vec3 world=(model*vec4(v.positionRadius.xyz,1.0)).xyz;
    vec3 old=(previousModel*vec4(v.previousPositionRadius.xyz,1.0)).xyz;
    mat4 projection=vp;float scale=max(length(model[0].xyz),max(length(model[1].xyz),length(model[2].xyz)));
    float radius=abs(v.positionRadius.w);
    bool prior=motionState.x>.5&&motionObject.x>.5;
    if(prior&&motionState.z>0.0){
        float time=1.0-clamp(motionState.z,0.0,1.0);world=mix(old,world,time);
        float oldScale=max(length(previousModel[0].xyz),max(length(previousModel[1].xyz),length(previousModel[2].xyz)));
        scale=mix(oldScale,scale,time);radius=mix(abs(v.previousPositionRadius.w),radius,time);
        projection=previousUnjitteredVP*(1.0-time)+currentUnjitteredVP*time;
        for(int c=0;c<4;++c)projection[c].xy+=vec2(previousCamera.w,motionState.w)*projection[c].w;
    }
    vec4 clip=projection*vec4(world,1.0);vec3 ndc=clip.xyz/clip.w;
    float width=max(2.0*radius*volume.y,shadows.z);if(shadows.w>0.0)width=min(width,shadows.w);
    // Source diagonal unprojection is at xy=0, with reciprocal resolution,
    // not at the actual strand point and not the usual two/W horizontal step.
    vec3 u=NativeRelativeUnproject(vec3(0,0,ndc.z));
    vec3 q=NativeRelativeUnproject(vec3(1.0/float(nativeExtent.x),-1.0/float(nativeExtent.y),ndc.z));
    float pixelWidth=max(width*scale*inversesqrt(max(dot(q-u,q-u),1e-30)),1.1920928955078125e-7);
    // Map through the same canonical host Z01 as the opaque attachment. The
    // parenthesization matters for strict equal-depth rejection in float32.
    projected=vec4((ndc.xy*vec2(.5,-.5)+.5)*vec2(nativeExtent.xy),1.0-(ndc.z*.5+.5),pixelWidth);
    shade=mix(NativeColor(v.shadeStart),NativeColor(v.shadeEnd),v.t);
    cameraDepth=abs(dot(world-camera.xyz,nativeForward.xyz));
    vec4 oldClip=previousVP*vec4(old,1.0);vec3 oldNdc=oldClip.xyz/max(abs(oldClip.w),1e-20);
    // Host temporal consumer takes absolute previous canonical UV/depth.
    // Keep source MV's interpolated centerline delta and half2 quantization.
    vec2 delta=(oldNdc.xy-ndc.xy)*.5;
    delta=unpackHalf2x16(packHalf2x16(delta));
    motion=prior&&oldClip.w>0.0?vec4(delta,oldNdc.z*.5+.5,1.0):vec4(0.0);
    if(clip.w<=0.0||any(isnan(projected))||any(isinf(projected)))projected.z=-1.0;
}
void main(){
    uint i=NativeLinearInvocation();if(i>=nativeWork.x)return;
    NativeLine s;float d0,d1;
    NativeEndpoint(i,0u,s.a,s.color0.rgb,s.motion0,d0);NativeEndpoint(i,1u,s.b,s.color1.rgb,s.motion1,d1);
    // coarseRasterMixSubD computes one camera-depth bucket from ORIGINAL raw
    // control endpoints before its child loop; all children inherit it.
    vec4 child=StrandCurveRead(strandCurveOffsets.w+i);
    uvec4 topology=uvec4(StrandCurveRead(strandCurveOffsets.z+uint(child.x)));
    vec3 raw0=(model*vec4(StrandCurveRead(topology.y).xyz,1.0)).xyz;
    vec3 raw1=(model*vec4(StrandCurveRead(topology.z).xyz,1.0)).xyz;
    vec3 origin=camera.xyz;
    if(motionState.x>.5&&motionObject.x>.5&&motionState.z>0.0){
        float time=1.0-clamp(motionState.z,0.0,1.0);
        vec3 old0=(previousModel*vec4(StrandCurveRead(strandCurveOffsets.x+topology.y).xyz,1.0)).xyz;
        vec3 old1=(previousModel*vec4(StrandCurveRead(strandCurveOffsets.x+topology.z).xyz,1.0)).xyz;
        raw0=mix(old0,raw0,time);raw1=mix(old1,raw1,time);origin=mix(previousCamera.xyz,camera.xyz,time);
    }
    d0=abs(dot(raw0-origin,nativeForward.xyz));d1=abs(dot(raw1-origin,nativeForward.xyz));
    // Boundary scale/offset are explicit host bounds, not recovered scene constants.
    s.color0.a=float(max(uint(clamp(d0*nativeParameters.z-nativeParameters.w,0.0,1.0)*23.0),
                            uint(clamp(d1*nativeParameters.z-nativeParameters.w,0.0,1.0)*23.0)));
    s.color1.a=0.0;s.tiles=uvec4(1,1,0,0);
    if(min(s.a.z,s.b.z)>0.0&&max(s.a.z,s.b.z)<1.0){
        vec2 low=floor(min(s.a.xy,s.b.xy))-vec2(1.0),high=ceil(max(s.a.xy,s.b.xy))+vec2(1.0);
        if(all(greaterThanEqual(high,vec2(0)))&&all(lessThan(low,vec2(nativeExtent.xy)))){
            s.tiles=uvec4(clamp(ivec2(floor(low/16.0)),ivec2(0),ivec2(nativeExtent.zw)-1),
                          clamp(ivec2(floor(high/16.0)),ivec2(0),ivec2(nativeExtent.zw)-1));
        }
    }
    nativeLines[i]=s;
}
)GLSL";
}

// All scratch is GPU-owned. The tile prefix reserves power-of-two ranges;
// deterministic bitonic sorting orders (source bucket, original child ordinal).
// An exhausted range uses a complete ordered scan, never drops a contribution.
inline std::string StrandNativeOitSchedulingShader(unsigned stage){
    std::string source="#version 450 core\nlayout(local_size_x="+std::to_string(stage==3?1:256)+") in;\n";
    source+=StrandNativeOitWorkGLSL();
    if(stage==0)source+=R"GLSL(
void main(){uint i=NativeLinearInvocation();
    if(i<nativeExtent.z*nativeExtent.w)nativeTiles[i]=uvec4(0);
    if(i<nativeExtent.x*nativeExtent.y){for(uint k=0u;k<4u;++k)nativeGlobal[i*4u+k]=uvec2(0u,0x3c00u);nativeMotion[i]=vec4(0);}
    if(i<8u)nativeMeta[i]=0u;
})GLSL";
    if(stage==1)source+=R"GLSL(
void main(){uint i=NativeLinearInvocation();if(i>=nativeWork.x)return;uvec4 t=nativeLines[i].tiles;
    for(uint y=t.y;y<=t.w;++y)for(uint x=t.x;x<=t.z;++x)atomicAdd(nativeTiles[y*nativeExtent.z+x].x,1u);
})GLSL";
    if(stage==2)source+=R"GLSL(
void main(){uint i=NativeLinearInvocation();if(i<nativeWork.y)nativePairs[i]=0xffffffffu;}
)GLSL";
    if(stage==3)source+=R"GLSL(
void main(){uint cursor=0u;for(uint i=0u;i<nativeExtent.z*nativeExtent.w;++i){uint n=nativeTiles[i].x,p=1u;
    if(n==0u){nativeTiles[i]=uvec4(0);continue;}while(p<n)p<<=1u;
    nativeMeta[0]+=n;nativeMeta[2]=max(nativeMeta[2],n);
    if(p>nativeWork.y-cursor){nativeTiles[i].y=0xffffffffu;nativeTiles[i].z=0u;++nativeMeta[1];}
    else{nativeTiles[i].y=cursor;nativeTiles[i].z=p;cursor+=p;}
    nativeTiles[i].w=0u;
}nativeMeta[3]=cursor;}
)GLSL";
    if(stage==4)source+=R"GLSL(
void main(){uint i=NativeLinearInvocation();if(i>=nativeWork.x)return;uvec4 t=nativeLines[i].tiles;
    uint key=(uint(nativeLines[i].color0.a)<<27u)|i;
    for(uint y=t.y;y<=t.w;++y)for(uint x=t.x;x<=t.z;++x){uint tile=y*nativeExtent.z+x;
        uint j=atomicAdd(nativeTiles[tile].w,1u);uint offset=nativeTiles[tile].y;
        if(offset!=0xffffffffu&&j<nativeTiles[tile].z)nativePairs[offset+j]=key;}
})GLSL";
    if(stage==5)source+=R"GLSL(
void main(){uint tile=gl_WorkGroupID.x+gl_WorkGroupID.y*gl_NumWorkGroups.x;
    if(tile>=nativeExtent.z*nativeExtent.w)return;uvec4 t=nativeTiles[tile];uint lane=gl_LocalInvocationID.x;
    if(t.y==0xffffffffu||t.x==0u)return;
    for(uint k=2u;k<=t.z;k<<=1u)for(uint j=k>>1u;j>0u;j>>=1u){
        for(uint i=lane;i<t.z;i+=256u){uint other=i^j;if(other>i){uint a=nativePairs[t.y+i],b=nativePairs[t.y+other];
            bool ascending=(i&k)==0u;if((ascending&&a>b)||(!ascending&&a<b)){
                nativePairs[t.y+i]=b;nativePairs[t.y+other]=a;}}}
        memoryBarrierBuffer();barrier();
    }
})GLSL";
    // Per object only reset tile metadata, retaining global object layers.
    if(stage==6)source+=R"GLSL(
void main(){uint i=NativeLinearInvocation();if(i<nativeExtent.z*nativeExtent.w)nativeTiles[i]=uvec4(0);if(i<8u)nativeMeta[i]=0u;}
)GLSL";
    return source;
}

inline std::string StrandNativeOitFineShader(){return std::string(R"GLSL(#version 450 core
layout(local_size_x=16,local_size_y=16) in;
layout(binding=3) uniform sampler2D nativeOpaqueDepth;
)GLSL")+StrandNativeOitWorkGLSL()+StrandNativeOitMathGLSL()+R"GLSL(
bool NativeClose(in uvec2 nodes[8],out vec3 c,out float t,out float z){
    StrandNativeResolve(nodes,nativeWork.z,c,t,z);if(t<=.01){t=0.0;return true;}return false;
}
void main(){
    uvec2 pixel=gl_GlobalInvocationID.xy;if(any(greaterThanEqual(pixel,nativeExtent.xy)))return;
    uvec2 tile=pixel/16u;uint tileIndex=tile.y*nativeExtent.z+tile.x;uvec4 range=nativeTiles[tileIndex];
    ivec2 imagePixel=ivec2(pixel.x,nativeExtent.y-1u-pixel.y),localPixel=ivec2(pixel%16u);
    uint outputIndex=uint(imagePixel.y)*nativeExtent.x+uint(imagePixel.x);
    float opaque=1.0-texelFetch(nativeOpaqueDepth,imagePixel,0).r;
    uvec2 inner[8];for(uint k=0u;k<8u;++k)inner[k]=StrandNativeEmpty;
    vec3 c=vec3(0);float t=1.0,z=0.0;vec4 firstMotion=vec4(0);bool motionSet=false,closed=false;
    uint lastBucket=0xffffffffu;
    // Reserved bins are sorted on GPU. Overflow bins scan every input in the
    // same bucket/ordinal order; expensive, but mathematically complete.
    bool fallback=range.y==0xffffffffu;uint iterations=fallback?nativeWork.x*24u:range.x;
    for(uint j=0u;j<iterations;++j){
        uint key=fallback?((j/nativeWork.x)<<27u)|(j%nativeWork.x):nativePairs[range.y+j];
        uint id=key&0x7ffffffu,bucket=key>>27u;NativeLine line=nativeLines[id];
        if(fallback&&(uint(line.color0.a)!=bucket||tile.x<line.tiles.x||tile.y<line.tiles.y||tile.x>line.tiles.z||tile.y>line.tiles.w))continue;
        if(lastBucket!=0xffffffffu&&bucket!=lastBucket){closed=NativeClose(inner,c,t,z);if(closed)break;}
        lastBucket=bucket;
        StrandNativeWuSegment s;s.p0=line.a.xy-vec2(tile*16u);s.p1=line.b.xy-vec2(tile*16u);
        s.width=vec2(line.a.w,line.b.w);s.reverseZ=vec2(line.a.z,line.b.z);s.shade0=line.color0.rgb;s.shade1=line.color1.rgb;
        vec4 ct;float depth,progress;
        if(StrandNativeWuSample(s,localPixel,opaque,ct,depth,progress)){
            StrandNativeInnerInsert(inner,nativeWork.z,StrandNativePack(depth,ct.rgb,ct.a));
            if(!motionSet){
                // Wu may reverse its endpoints before interpolation.
                bool steep=abs(s.p1.y-s.p0.y)>abs(s.p1.x-s.p0.x);
                bool reverse=steep?s.p0.y>s.p1.y:s.p0.x>s.p1.x;
                float u=reverse?1.0-progress:progress;
                vec4 m=mix(line.motion0,line.motion1,u);
                vec2 uv=(vec2(imagePixel)+.5)/vec2(nativeExtent.xy);
                firstMotion=vec4(uv+m.xy,m.z,m.w>.999?1.0:0.0);motionSet=true;
            }
        }
    }
    if(!closed)NativeClose(inner,c,t,z);
    if(z==0.0)return;
    StrandNativeOpacity(c,t,nativeParameters.x);
    if(!(t<1.0))return; // Source skips completely transparent object aggregates.
    uvec2 node=StrandNativePack(z,c,t),global[4];
    for(uint k=0u;k<4u;++k)global[k]=nativeGlobal[outputIndex*4u+k];
    bool nearest=node.y>global[0].y;
    StrandNativeGlobalInsert(global,node);
    for(uint k=0u;k<4u;++k)nativeGlobal[outputIndex*4u+k]=global[k];
    if(nearest){bool valid=firstMotion.w>.5&&all(greaterThanEqual(firstMotion.xyz,vec3(0)))&&all(lessThanEqual(firstMotion.xyz,vec3(1)));
        nativeMotion[outputIndex]=valid?firstMotion:vec4(0);}
}
)GLSL";}

inline std::string StrandNativeOitResolveShader(){return std::string(R"GLSL(#version 450 core
layout(local_size_x=16,local_size_y=16) in;
layout(binding=3) uniform sampler2D nativeOpaqueDepth;
layout(rgba32f,set=3,binding=0) uniform writeonly image2D nativeColor;
layout(rgba32f,set=3,binding=1) uniform writeonly image2D nativeAux;
layout(rgba32f,set=3,binding=2) uniform writeonly image2D nativeMotionOutput;
)GLSL")+StrandNativeOitWorkGLSL()+StrandNativeOitMathGLSL()+R"GLSL(
void main(){uvec2 p=gl_GlobalInvocationID.xy;if(any(greaterThanEqual(p,nativeExtent.xy)))return;
    uint index=p.y*nativeExtent.x+p.x;uvec2 nodes[4];for(uint k=0u;k<4u;++k)nodes[k]=nativeGlobal[index*4u+k];
    vec3 c;float t,z;StrandNativeGlobalResolve(nodes,c,t,z);float a=clamp(1.0-t,0.0,1.0),depth=z>0.0?1.0-z:1.0;
    float opaque=texelFetch(nativeOpaqueDepth,ivec2(p),0).r;bool write=z>0.0&&a>=nativeParameters.y;
    imageStore(nativeColor,ivec2(p),vec4(a>0.0?c/a:vec3(0),a));
    imageStore(nativeAux,ivec2(p),vec4(depth,a,write?1.0:0.0,write?min(depth,opaque):opaque));
    imageStore(nativeMotionOutput,ivec2(p),nativeMotion[index]);
}
)GLSL";}

inline std::string StrandNativeOitDepthShader(bool nearest){
    if(nearest)return R"GLSL(#version 450 core
layout(binding=0) uniform sampler2D nativeAux;
void main(){gl_FragDepth=texelFetch(nativeAux,ivec2(gl_FragCoord.xy),0).x;}
)GLSL";
    return std::string(R"GLSL(#version 450 core
layout(binding=0) uniform sampler2D nativeAux;
layout(location=0) out vec4 coverage;
void main(){vec4 a=texelFetch(nativeAux,ivec2(gl_FragCoord.xy),0);coverage=vec4(a.y,a.z,0,1);gl_FragDepth=
)GLSL")+"a.w;}";}
} // namespace Render::PipelineDetail
