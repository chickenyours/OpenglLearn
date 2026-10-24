#pragma once
#include <string>

namespace Render::PipelineDetail {
// Normal, non-stencil fineRasterOIT4/8 arithmetic. A pixel-owner dispatch may
// call these functions without uint64 atomics. That is a host scheduling
// choice: native successful operations use full64 atomicMax/CAS in the tile
// and high32-only ordering globally. Native CAS retry losses are not emulated.
inline std::string StrandNativeOitMathGLSL(){return R"GLSL(
const uvec2 StrandNativeEmpty=uvec2(0u,0x3c00u); // low32, high32
float StrandNativeAdd(float a,float b){precise float q=a+b;return q;}
float StrandNativeMul(float a,float b){precise float q=a*b;return q;}
vec3 StrandNativeScale(float a,vec3 b){return vec3(StrandNativeMul(a,b.x),StrandNativeMul(a,b.y),StrandNativeMul(a,b.z));}
vec3 StrandNativeOver(vec3 a,float t,vec3 b){return vec3(StrandNativeAdd(a.x,StrandNativeMul(t,b.x)),
    StrandNativeAdd(a.y,StrandNativeMul(t,b.y)),StrandNativeAdd(a.z,StrandNativeMul(t,b.z)));}
bool StrandNativeIsEmpty(uvec2 p){return p.y==0x3c00u;}
bool StrandNativeFullLess(uvec2 a,uvec2 b){return a.y<b.y||(a.y==b.y&&a.x<b.x);}
uint StrandNativeRgbPack(vec3 c){
    uint r=packHalf2x16(vec2(c.r,0.0)),g=packHalf2x16(vec2(c.g,0.0)),b=packHalf2x16(vec2(c.b,0.0));
    return ((r>>4u)&0x7ffu)|((g<<7u)&0x3ff800u)|((b>>5u)<<22u);
}
vec3 StrandNativeRgbUnpack(uint p){return vec3(unpackHalf2x16((p<<4u)&0x7ff0u).x,
    unpackHalf2x16((p>>7u)&0x7ff0u).x,unpackHalf2x16((p>>22u)<<5u).x);}
uint StrandNativeTransmissionByte(float t){return uint(StrandNativeAdd(StrandNativeMul(sqrt(t),255.0),.5));}
float StrandNativeTransmission(uint p){float q=StrandNativeMul(float(p&255u),0.003921568859368563);return StrandNativeMul(q,q);}
uvec2 StrandNativePack(float reverseZ,vec3 premultiplied,float t){return uvec2(StrandNativeRgbPack(premultiplied),
    ((floatBitsToUint(reverseZ)<<2u)&0xffffff00u)|StrandNativeTransmissionByte(t));}
void StrandNativeUnpack(uvec2 p,out vec3 c,out float t,out float reverseZ){
    bool empty=StrandNativeIsEmpty(p);c=empty?vec3(0.0):StrandNativeRgbUnpack(p.x);
    t=empty?1.0:StrandNativeTransmission(p.y);reverseZ=empty?0.0:uintBitsToFloat((p.y>>2u)&0x3fffffc0u);
}
uvec2 StrandNativeMerge(uvec2 front,uvec2 back){
    vec3 a,b;float ta,tb,za,zb;StrandNativeUnpack(front,a,ta,za);StrandNativeUnpack(back,b,tb,zb);
    return uvec2(StrandNativeRgbPack(StrandNativeOver(a,ta,b)),
        (front.y&0xffffff00u)|StrandNativeTransmissionByte(StrandNativeMul(ta,tb)));
}
// count is4 or8. The last slot is a re-quantized tail, not a weighted mean.
void StrandNativeInnerInsert(inout uvec2 layers[8],uint count,uvec2 node){
    for(uint i=0u;i<count;++i){if(StrandNativeFullLess(layers[i],node)){uvec2 old=layers[i];layers[i]=node;node=old;}}
    if(!StrandNativeIsEmpty(node))layers[count-1u]=StrandNativeMerge(layers[count-1u],node);
}
// Call only for a nonempty object aggregate. Equal high words retain incoming
// order; packed RGB is intentionally NOT a secondary global sorting key.
void StrandNativeGlobalInsert(inout uvec2 layers[4],uvec2 node){
    for(uint i=0u;i<4u;++i){if(node.y>layers[i].y||StrandNativeIsEmpty(node)){
        uvec2 old=layers[i];layers[i]=node;node=old;if(StrandNativeIsEmpty(layers[i]))return;}}
    if(!StrandNativeIsEmpty(node))layers[3]=StrandNativeMerge(layers[3],node);
}
void StrandNativeResolve(in uvec2 layers[8],uint count,out vec3 c,out float t,out float nearest){
    c=vec3(0.0);t=1.0;nearest=0.0;
    for(uint i=0u;i<count;++i){if(StrandNativeIsEmpty(layers[i]))break;vec3 q;float u,z;StrandNativeUnpack(layers[i],q,u,z);
        if(i==0u)nearest=z;c=StrandNativeOver(c,t,q);t=StrandNativeMul(t,u);}
}
void StrandNativeGlobalResolve(in uvec2 layers[4],out vec3 c,out float t,out float nearest){
    c=vec3(0.0);t=1.0;nearest=0.0;
    for(uint i=0u;i<4u;++i){if(StrandNativeIsEmpty(layers[i]))break;vec3 q;float u,z;StrandNativeUnpack(layers[i],q,u,z);
        if(i==0u)nearest=z;c=StrandNativeOver(c,t,q);t=StrandNativeMul(t,u);}
}
// Apply only after the persistent inner layers have been resolved. Native
// bucket-close (T<=.01 ->T0 and stop farther buckets) is the caller's policy.
void StrandNativeOpacity(inout vec3 c,inout float t,float g){
    c=StrandNativeScale(g,c);t=clamp(StrandNativeAdd(StrandNativeAdd(t,1.0),-g),0.0,1.0);
}
struct StrandNativeWuSegment {vec2 p0,p1;vec2 width,reverseZ;vec3 shade0,shade1;};
bool StrandNativeWuDeposit(float major,float minor,float width,float z,vec3 shade,float progress,bool steep,
    ivec2 pixel,float opaqueReverseZ,out vec4 ct,out float depth,out float t){
    ct=vec4(0.0,0.0,0.0,1.0);depth=0.0;t=0.0;
    if(any(lessThan(pixel,ivec2(0)))||any(greaterThanEqual(pixel,ivec2(16)))||!(z<1.0&&opaqueReverseZ<z))return false;
    int targetMajor=steep?pixel.y:pixel.x,targetMinor=steep?pixel.x:pixel.y;
    if(major!=float(targetMajor))return false;
    float low=floor(minor),fraction=StrandNativeAdd(minor,-low),weight;
    if(float(targetMinor)==low)weight=StrandNativeAdd(1.0,-fraction);
    else if(float(targetMinor)==low+1.0)weight=fraction;else return false;
    float alpha=StrandNativeMul(weight,clamp(width,0.0,1.0));
    ct=vec4(StrandNativeScale(alpha,shade),StrandNativeAdd(1.0,-alpha));depth=z;t=progress;return true;
}
// Inputs are already projected into THIS tile's0..15 coordinate system, with
// native full-width diagonal-unprojection and native decoded endpoint shade.
// Endpoint ordering, roundEven, first-sample asymmetry and f32 increment order
// are source arithmetic. Evaluating this sequence per owned pixel is host work.
// A zero-alpha accepted deposit is kept: source can still insert its depth.
bool StrandNativeWuSample(StrandNativeWuSegment s,ivec2 pixel,float opaqueReverseZ,
    out vec4 ct,out float depth,out float progress){
    ct=vec4(0.0,0.0,0.0,1.0);depth=0.0;progress=0.0;
    bool steep=abs(StrandNativeAdd(s.p1.y,-s.p0.y))>abs(StrandNativeAdd(s.p1.x,-s.p0.x));
    float minor0=steep?s.p0.x:s.p0.y,major0=steep?s.p0.y:s.p0.x;
    float minor1=steep?s.p1.x:s.p1.y,major1=steep?s.p1.y:s.p1.x;
    if(major0>major1){float q=minor0;minor0=minor1;minor1=q;q=major0;major0=major1;major1=q;
        s.width=s.width.yx;s.reverseZ=s.reverseZ.yx;vec3 c=s.shade0;s.shade0=s.shade1;s.shade1=c;}
    float dm=StrandNativeAdd(major1,-major0);
    precise float slope=dm!=0.0?StrandNativeAdd(minor1,-minor0)/dm:1.0;
    float start=roundEven(major0),end=roundEven(major1);
    float y=StrandNativeAdd(minor0,StrandNativeMul(slope,StrandNativeAdd(start,-major0)));
    if(start>=-1.0&&StrandNativeWuDeposit(start,y,s.width.x,s.reverseZ.x,s.shade0,0.0,steep,pixel,opaqueReverseZ,ct,depth,progress))return true;
    y=StrandNativeAdd(y,slope);float m=StrandNativeAdd(start,1.0),jump=0.0;
    if(!(y>=-1.0||m>=-1.0))jump=ceil(min((-1.0-y)/abs(slope),-2.0-start));
    precise float inverse=1.0/StrandNativeAdd(end,-start);
    float t=jump!=0.0?StrandNativeMul(jump,inverse):0.0;
    y=StrandNativeAdd(y,StrandNativeMul(jump,slope));m=StrandNativeAdd(m,jump);
    float targetMajor=float(steep?pixel.y:pixel.x);
    while(m<min(16.0,end)&&m<=targetMajor){
        t=StrandNativeAdd(t,inverse);
        float width=StrandNativeAdd(s.width.x,StrandNativeMul(t,StrandNativeAdd(s.width.y,-s.width.x)));
        float z=StrandNativeAdd(s.reverseZ.x,StrandNativeMul(t,StrandNativeAdd(s.reverseZ.y,-s.reverseZ.x)));
        vec3 shade=vec3(StrandNativeAdd(s.shade0.x,StrandNativeMul(t,StrandNativeAdd(s.shade1.x,-s.shade0.x))),
            StrandNativeAdd(s.shade0.y,StrandNativeMul(t,StrandNativeAdd(s.shade1.y,-s.shade0.y))),
            StrandNativeAdd(s.shade0.z,StrandNativeMul(t,StrandNativeAdd(s.shade1.z,-s.shade0.z))));
        if(StrandNativeWuDeposit(m,y,width,z,shade,t,steep,pixel,opaqueReverseZ,ct,depth,progress))return true;
        y=StrandNativeAdd(y,slope);float next=StrandNativeAdd(m,1.0);
        if(next<=m)break; // finite host guard outside the native screen domain
        m=next;
    }
    return false;
}
bool StrandNativeWuNode(StrandNativeWuSegment s,ivec2 pixel,float opaqueReverseZ,out uvec2 node){
    vec4 ct;float z,t;bool accepted=StrandNativeWuSample(s,pixel,opaqueReverseZ,ct,z,t);
    node=accepted?StrandNativePack(z,ct.rgb,ct.a):StrandNativeEmpty;return accepted;
}
)GLSL";}
} // namespace Render::PipelineDetail
