#pragma once
#include "surface_cache_transport_deferred_shaders.h"
#include <stdexcept>

namespace Render::SurfaceCacheDetail {
inline void TransportComputeReplace(std::string& source,const std::string& from,const std::string& to) {
    const auto at=source.find(from);
    if(at==std::string::npos||source.find(from,at+from.size())!=std::string::npos)
        throw std::logic_error("Transport compute source changed");
    source.replace(at,from.size(),to);
}
inline std::string TransportComputeRadianceFormat(std::string source,bool halfRadiance) {
    if(halfRadiance){
        TransportComputeReplace(source,"layout(rgba32f,set=3,binding=0) uniform writeonly image2D wcRaw;",
            "layout(rgba16f,set=3,binding=0) uniform writeonly image2D wcRaw;");
        TransportComputeReplace(source,"layout(rgba32f,set=3,binding=2) uniform writeonly image2D wcBase;",
            "layout(rgba16f,set=3,binding=2) uniform writeonly image2D wcBase;");
    }
    return source;
}
// Shared control layout follows the existing compact SDF dispatch contract:
// [0] queued receivers, [1..3] VkDispatchIndirectCommand at byte offset 4,
// [4] queue capacity, [5] consumed receivers, [15] overflow/invalid queue IDs.
// Every queued record is one original full-resolution uint pixel ID. It never
// changes the source-origin work, pixel hash, direction loop or reduction order.
inline std::string TransportReceiverCompactResetShader(){return R"GLSL(#version 450 core
layout(local_size_x=64) in;
layout(std430,set=2,binding=0) buffer WGCompactControl {uint wcWords[];};
layout(push_constant) uniform WGCompactReset {uint capacity;} wcReset;
void main(){uint at=gl_GlobalInvocationID.x;
    if(at<16u)wcWords[at]=(at==2u||at==3u)?1u:(at==4u?wcReset.capacity:0u);
}
)GLSL";}
// No AS or lighting samples occur in this pass. Set 0 uses UBO slots 12/14;
// set 1 uses sampled slots 2/3. Set 2 binds control/queue at 0/1, and set 3
// binds raw/mask/base/classification outputs at 0/1/2/3. Raw/base optionally
// use RGBA16F; mask/classification always retain RGBA32F.
inline std::string TransportReceiverRasterCoreClassificationShader(){
    auto source=TransportReceiverClassificationShader();
    // The existing queue selector remains unchanged. Y separately carries the
    // strict 3cm predicate; larger-grid footprint repair and unknown owners
    // must not accidentally become forced contact cores.
    TransportComputeReplace(source,
        "    outColor=vec4(WGReceiverDetail(pixel,p,n,own)?1:0,0,0,0);",
        "    outColor=vec4(WGReceiverDetail(pixel,p,n,own)?1:0,WGNearRiskWithin(p,n,own,.03)?1:0,0,0);");
    return source;
}
inline std::string TransportReceiverCompactClassifyShader(bool halfRadiance=false,bool forceRasterCore=false,bool deferredQueue=false){
    auto source=(forceRasterCore||deferredQueue)?TransportReceiverRasterCoreClassificationShader():TransportReceiverClassificationShader();
    TransportComputeReplace(source,"in vec2 vUV;layout(location=0) out vec4 outColor;",
        "vec4 outColor;bool wcValidReceiver=false;");
    TransportComputeReplace(source,"void main(){ivec2 pixel=ivec2(gl_FragCoord.xy);outColor=vec4(0);",
        "void WGClassifyReceiver(ivec2 pixel){outColor=vec4(0);wcValidReceiver=false;");
    TransportComputeReplace(source,"    int own=v.w>=1&&v.w==floor(v.w)?int(v.w)-1:-1;",
        "    wcValidReceiver=true;int own=v.w>=1&&v.w==floor(v.w)?int(v.w)-1:-1;");
    source=TransportComputeRadianceFormat(source+R"GLSL(
layout(local_size_x=64) in;
layout(std430,set=2,binding=0) buffer WGCompactControl {uint wcWords[];};
layout(std430,set=2,binding=1) buffer WGCompactQueue {uint wcPixels[];};
layout(rgba32f,set=3,binding=0) uniform writeonly image2D wcRaw;
layout(rgba32f,set=3,binding=1) uniform writeonly image2D wcMask;
layout(rgba32f,set=3,binding=2) uniform writeonly image2D wcBase;
layout(rgba32f,set=3,binding=3) uniform writeonly image2D wcClassification;
shared uint wcGroupPending,wcGroupBase;
void main(){
    uint local=gl_LocalInvocationIndex,id=gl_GlobalInvocationID.x,capacity=wcWords[4],rank=0u;
    if(local==0u){wcGroupPending=0u;wcGroupBase=0u;}barrier();
    bool pending=false;
    if(id<capacity&&id<uint(scScreen.x)*uint(scScreen.y)){
        ivec2 pixel=ivec2(id%uint(scScreen.x),id/uint(scScreen.x));
        WGClassifyReceiver(pixel);pending=outColor.x>.5;
        // Initialize every physical output, even when the queue is empty or
        // shrinks. Unselected valid receivers keep the fragment core's exact
        // placeholder; failed source anchors are written only by the consumer.
        imageStore(wcRaw,pixel,vec4(0));imageStore(wcBase,pixel,vec4(0));
        imageStore(wcMask,pixel,wcValidReceiver?vec4(.25,1,0,0):vec4(0));
        imageStore(wcClassification,pixel,outColor);
        if(pending)rank=atomicAdd(wcGroupPending,1u);
    }
    barrier();
    if(local==0u){
        wcGroupBase=atomicAdd(wcWords[0],wcGroupPending);
        if(wcGroupPending>0u)atomicMax(wcWords[1],(min(wcGroupBase+wcGroupPending,capacity)+63u)/64u);
    }
    barrier();
    if(pending){uint at=wcGroupBase+rank;
        if(at<capacity)wcPixels[at]=id;else atomicAdd(wcWords[15],1u);
    }
}
)GLSL",halfRadiance);
    if(deferredQueue)TransportComputeReplace(source,"WGClassifyReceiver(pixel);pending=outColor.x>.5;","WGClassifyReceiver(pixel);pending=false;");
    return source;
}
// Expand only the scheduling selector. Two bounded separable paths replace a
// 15x15 search; every intermediate pixel must stay on the same visible surface.
// The horizontal scratch stores original X in Z. The vertical output restores
// Y verbatim and marks newly selected halo pixels in Z, with W remaining zero.
inline std::string TransportReceiverSupportHaloShader(bool vertical){
    auto source=TransportReceiverRasterCoreClassificationShader();
    const auto main=source.find("in vec2 vUV;layout(location=0) out vec4 outColor;");
    if(main==std::string::npos)throw std::logic_error("Receiver halo classification prefix changed");
    source.resize(main);
    source+=R"GLSL(
layout(local_size_x=64) in;
layout(binding=5) uniform sampler2D whClassification;
layout(rgba32f,set=3,binding=0) uniform writeonly image2D whOutput;
bool WHGeometry(vec4 view,vec4 normal){
    return view.w>=1.0&&view.w==floor(view.w)&&view.w<=float(scAtlas.w/6)&&view.z<0&&normal.w>.5&&
        WGFinite(view.xyz)&&WGFinite(normal.xyz)&&dot(normal.xyz,normal.xyz)>.5;
}
bool WHSelected(ivec2 pixel,ivec2 axis){
    if(texelFetch(whClassification,pixel,0).x>.5)return true;
    vec4 view=texelFetch(wgPositions,pixel,0),normal=texelFetch(wgNormals,pixel,0);
    if(!WHGeometry(view,normal))return false;
    vec3 p=(scInverseView*vec4(view.xyz,1)).xyz,n=normalize(normal.xyz);
    if(!WGFinite(p))return false;
    for(int sign=-1;sign<=1;sign+=2)for(int step=1;step<=7;++step){
        ivec2 at=pixel+axis*(sign*step);
        if(any(lessThan(at,ivec2(0)))||any(greaterThanEqual(at,scScreen.xy)))break;
        vec4 other=texelFetch(wgPositions,at,0),nd=texelFetch(wgNormals,at,0);
        if(other.w!=view.w||!WHGeometry(other,nd))break;
        vec3 q=(scInverseView*vec4(other.xyz,1)).xyz,nn=normalize(nd.xyz),delta=q-p;
        float alignment=clamp(dot(n,nn),-1.0,1.0);
        float curve=.75*length(delta)*sqrt(max(0.0,2.0*(1.0-alignment)));
        if(!WGFinite(q)||alignment<.9659258263||max(abs(dot(delta,n)),abs(dot(delta,nn)))>max(.0003,curve))break;
        if(texelFetch(whClassification,at,0).x>.5)return true;
    }
    return false;
}
)GLSL";
    if(!vertical)return source+R"GLSL(
void main(){uint id=gl_GlobalInvocationID.x;
    if(id>=uint(scScreen.x)*uint(scScreen.y))return;
    ivec2 pixel=ivec2(id%uint(scScreen.x),id/uint(scScreen.x));vec4 old=texelFetch(whClassification,pixel,0);
    imageStore(whOutput,pixel,vec4(WHSelected(pixel,ivec2(1,0))?1:0,old.y,old.x,0));
}
)GLSL";
    return source+R"GLSL(
layout(std430,set=2,binding=0) buffer WGCompactControl {uint wcWords[];};
layout(std430,set=2,binding=1) buffer WGCompactQueue {uint wcPixels[];};
shared uint wcGroupPending,wcGroupBase;
void main(){
    uint local=gl_LocalInvocationIndex,id=gl_GlobalInvocationID.x,capacity=wcWords[4],rank=0u;
    if(local==0u){wcGroupPending=0u;wcGroupBase=0u;}barrier();
    bool pending=false;
    if(id<capacity&&id<uint(scScreen.x)*uint(scScreen.y)){
        ivec2 pixel=ivec2(id%uint(scScreen.x),id/uint(scScreen.x));vec4 old=texelFetch(whClassification,pixel,0);
        pending=WHSelected(pixel,ivec2(0,1));
        imageStore(whOutput,pixel,vec4(pending?1:0,old.y,pending&&old.z<.5?1:0,0));
        if(pending)rank=atomicAdd(wcGroupPending,1u);
    }
    barrier();
    if(local==0u){wcGroupBase=atomicAdd(wcWords[0],wcGroupPending);
        if(wcGroupPending>0u)atomicMax(wcWords[1],(min(wcGroupBase+wcGroupPending,capacity)+63u)/64u);}
    barrier();
    if(pending){uint at=wcGroupBase+rank;if(at<capacity)wcPixels[at]=id;else atomicAdd(wcWords[15],1u);}
}
)GLSL";
}
// The consumer is generated from the same coherent core shader. Only its
// fragment IO/entry point and the already-completed classifier lookup change.
// Bind control/queue SSBOs at set 2 bindings 0/1, nativeScene through the frame
// pipeline descriptor, and raw/mask/base outputs at set 3 bindings 0..2.
// Raw/base optionally use RGBA16F; mask always retains RGBA32F.
// Required UBO slots: 9/12/14/15. Required sampled slots: 2/3/4/9/12/13/14/15.
inline std::string TransportReceiverCompactTraceShader(bool halfRadiance=false,bool deferredMix=false){
    auto source=deferredMix?TransportDeferredResolveShader(1):TransportResolveShader(false,1);
    TransportComputeReplace(source,"in vec2 vUV;layout(location=0) out vec4 outColor;","vec4 outColor;");
    TransportComputeReplace(source,"layout(location=1) out vec4 wgNearMask;layout(location=2) out vec4 wgBase;",
        "vec4 wgNearMask,wgBase;");
    TransportComputeReplace(source,"void main(){ivec2 pixel=ivec2(gl_FragCoord.xy);outColor=vec4(0);",
        "void WGTraceReceiver(ivec2 pixel){outColor=vec4(0);");
    TransportComputeReplace(source,"    if(texelFetch(wgScene,pixel,0).x<.5){wgNearMask=vec4(.25,1,0,0);return;}","");
    return TransportComputeRadianceFormat(source+R"GLSL(
layout(local_size_x=64) in;
layout(std430,set=2,binding=0) buffer WGCompactControl {uint wcWords[];};
layout(std430,set=2,binding=1) readonly buffer WGCompactQueue {uint wcPixels[];};
layout(rgba32f,set=3,binding=0) uniform writeonly image2D wcRaw;
layout(rgba32f,set=3,binding=1) uniform writeonly image2D wcMask;
layout(rgba32f,set=3,binding=2) uniform writeonly image2D wcBase;
void main(){
    uint index=gl_GlobalInvocationID.x,pending=min(wcWords[0],wcWords[4]);
    if(index>=pending)return;
    uint id=wcPixels[index];
    if(id>=uint(scScreen.x)*uint(scScreen.y)){atomicAdd(wcWords[15],1u);return;}
    ivec2 pixel=ivec2(id%uint(scScreen.x),id/uint(scScreen.x));
    WGTraceReceiver(pixel);
    imageStore(wcRaw,pixel,outColor);imageStore(wcMask,pixel,wgNearMask);imageStore(wcBase,pixel,wgBase);
    if(gl_LocalInvocationIndex==0u)atomicAdd(wcWords[5],min(64u,pending-index));
}
)GLSL",halfRadiance);
}
}
