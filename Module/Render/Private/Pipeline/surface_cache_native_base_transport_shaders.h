#pragma once
#include "surface_cache_transport_compute_shaders.h"
#include "surface_cache_native_fresh_shaders.h"

namespace Render::SurfaceCacheDetail {
// Receiver variants for a current-frame native screen-probe base. The world
// transport/card solution is not a lighting input to these variants. Source
// certification, the core/lattice physical selection and all actual directions
// are inherited from the corresponding transport shader unchanged.
inline std::string NativeRasterCoreReadGLSL(unsigned binding,bool analytic) {
    if(!analytic){const std::string sampler=binding==5?"wgScene":"nbCoreClassification";
        return "layout(binding="+std::to_string(binding)+") uniform sampler2D "+sampler+";\n"+
            "bool NBForceRasterCore(ivec2 pixel){return texelFetch("+sampler+",pixel,0).y>.5;}\n";}
    // Full-resolution reference draws and untimed diagnostic replay do not
    // depend on the sparse classifier. Re-evaluate the original predicate,
    // retaining the captured receiver's certified object, rather than a card
    // lookup's fallback owner.
    return R"GLSL(
bool NBForceRasterCore(ivec2 pixel){
    vec4 view=texelFetch(wgPositions,pixel,0),normal=texelFetch(wgNormals,pixel,0);
    if(view.w<1.0||view.w!=floor(view.w)||normal.w<.5||!WGFinite(view.xyz)||dot(normal.xyz,normal.xyz)<.5)return false;
    int object=int(view.w)-1;if(object<0||object>=scAtlas.w/6)return false;
    vec3 p=(scInverseView*vec4(view.xyz,1)).xyz,n=normalize(normal.xyz);
    return WGNearRiskWithin(p,n,object,.03);
}
)GLSL";
}
inline std::string NativeBaseTransportReadGLSL(const std::string& baseSampler,bool adaptiveFresh=false,unsigned freshBinding=11,
    bool forceRasterCore=false,unsigned coreBinding=5,bool analyticCore=false) {
    auto source=(adaptiveFresh?NativeReceiverFreshRiskGLSL(freshBinding):std::string{})+
        (forceRasterCore?NativeRasterCoreReadGLSL(coreBinding,analyticCore):std::string{})+R"GLSL(
layout(binding=8) uniform sampler2D nbSoftMask;
vec4 NBReadBase(ivec2 pixel) {
    vec4 value=texelFetch()GLSL"+baseSampler+R"GLSL(,pixel,0);
    return value.a>0&&WGFinite(value.rgb)&&!isnan(value.a)&&!isinf(value.a)?value:vec4(0);
}
float NBReadRisk(ivec2 pixel,bool baseValid) {
    if(!baseValid)return 1.0;
    float risk=texelFetch(nbSoftMask,pixel,0).x;
    return isnan(risk)||isinf(risk)?1.0:clamp(risk,0.0,1.0);
}
)GLSL";
    if(forceRasterCore)TransportDeferredReplace(source,"float NBReadRisk(ivec2 pixel,bool baseValid) {",
        "float NBReadRisk(ivec2 pixel,bool baseValid) {\n    if(NBForceRasterCore(pixel))return 1.0;");
    if(adaptiveFresh)TransportDeferredReplace(source,"    return isnan(risk)||isinf(risk)?1.0:clamp(risk,0.0,1.0);",
        "    risk=isnan(risk)||isinf(risk)?1.0:clamp(risk,0.0,1.0);return NBEffectiveMask(risk,pixel);");
    return source;
}
inline std::string NativeBaseTransportResolveShader(unsigned receiverPass,bool adaptiveFresh=false,bool forceRasterCore=false) {
    auto source=TransportDeferredResolveShader(receiverPass);
    if(forceRasterCore&&receiverPass!=3)TransportDeferredReplace(source,"layout(binding=5) uniform sampler2D wgScene;","");
    TransportDeferredReplace(source,"in vec2 vUV;layout(location=0) out vec4 outColor;",
        NativeBaseTransportReadGLSL("wgTransport",adaptiveFresh,11,forceRasterCore,5,receiverPass==3)+"\nin vec2 vUV;layout(location=0) out vec4 outColor;");
    TransportDeferredReplace(source,
        "    vec3 farValue=vec3(0);float coverage=0;bool farValid=!rasterCore&&WGSampleObject(p,n,own,farValue,coverage);\n"
        "    float risk=WGReceiverGeometryRisk(pixel,p,n,own,rasterCore,farValid);",
        "    vec4 nativeBase=NBReadBase(pixel);bool farValid=nativeBase.a>0;\n"
        "    float risk=NBReadRisk(pixel,farValid);");
    TransportDeferredReplace(source,"    if(farValid)wgBase=vec4(rho*farValue*scParameters.z,coverage);",
        "    if(farValid)wgBase=nativeBase;");
    if(adaptiveFresh){
        TransportDeferredReplace(source,"in vec2 vUV;layout(location=0) out vec4 outColor;",NativeReceiverFreshGLSL(forceRasterCore)+"\nin vec2 vUV;layout(location=0) out vec4 outColor;");
        TransportDeferredReplace(source,"count=int(wgParameters.z+.5);","count=NBReceiverRayCount(pixel);");
        TransportDeferredReplace(source,"vec3 d=WGDirection(n,ray,count,seed,uint(spScreen.z));",
            "vec3 d=NBReceiverDirection(n,ray,int(wgParameters.z+.5),seed,uint(spScreen.z));");
    }
    return source;
}

inline std::string NativeBaseTransportRayDiagnosticShader(bool adaptiveFresh=false,bool forceRasterCore=false,bool supportHalo=false) {
    auto source=TransportRayDiagnosticShader(true,true);
    TransportDeferredReplace(source,"in vec2 vUV;layout(location=0) out vec4 outColor;",
        NativeBaseTransportReadGLSL("wgTransport",adaptiveFresh,11,forceRasterCore,5,true)+"\nin vec2 vUV;layout(location=0) out vec4 outColor;");
    TransportDeferredReplace(source,
        "    wgAnchor=anchor;vec3 farValue;float coverage;bool farValid=!rasterCore&&WGSampleObject(p,n,own,farValue,coverage);\n"
        "    float risk=WGReceiverGeometryRisk(pixel,p,n,own,rasterCore,farValid);",
        "    wgAnchor=anchor;vec4 nativeBase=NBReadBase(pixel);bool farValid=nativeBase.a>0;\n"
        "    float risk=NBReadRisk(pixel,farValid);");
    if(adaptiveFresh){
        TransportDeferredReplace(source,"in vec2 vUV;layout(location=0) out vec4 outColor;",NativeReceiverFreshGLSL(forceRasterCore)+"\nin vec2 vUV;layout(location=0) out vec4 outColor;");
        TransportDeferredReplace(source,"if(part==2&&ray==0)","if(part==2&&ray==0&&spTraceDisplay.w==0)");
        TransportDeferredReplace(source,"    if((farValid&&risk<=.000001)||!WGReceiverScheduled(pixel,p,n,own,farValid))return;",
            "    if((farValid&&risk<=.000001)||!WGReceiverScheduled(pixel,p,n,own,farValid))return;\n"
            "    int logicalRay=ray+spTraceDisplay.w*count;if(logicalRay>=NBReceiverRayCount(pixel))return;");
        TransportDeferredReplace(source,"    if(ray==0)originQueries=uvec2(scTraceDiag[0].x,scTraceDiag[1].y);SCTraceDiagReset();",
            "    if(ray==0&&spTraceDisplay.w==0)originQueries=uvec2(scTraceDiag[0].x,scTraceDiag[1].y);SCTraceDiagReset();");
        TransportDeferredReplace(source,"    vec3 d=WGDirection(n,ray,count,seed,phase),hitNormal;int triangle;bool exhausted;",
            "    vec3 d=NBReceiverDirection(n,logicalRay,count,seed,phase),hitNormal;int triangle;bool exhausted;");
    }
    if(supportHalo){
        TransportDeferredReplace(source,"in vec2 vUV;layout(location=0) out vec4 outColor;",
            "layout(binding=5) uniform sampler2D nbTraceClassification;\nin vec2 vUV;layout(location=0) out vec4 outColor;");
        TransportDeferredReplace(source,"!WGReceiverScheduled(pixel,p,n,own,farValid)",
            "!(texelFetch(nbTraceClassification,pixel,0).x>.5||WGReceiverScheduled(pixel,p,n,own,farValid))");
    }
    return source;
}

// The queue protocol and compact launch policy are byte-for-byte the existing
// consumer's; only the deferred receiver factory supplying the body differs.
inline std::string NativeBaseTransportCompactTraceShader(bool halfRadiance=false,bool adaptiveFresh=false,bool forceRasterCore=false) {
    auto source=NativeBaseTransportResolveShader(1,adaptiveFresh,forceRasterCore);
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

inline std::string NativeBaseTransportReconstructionShader(bool adaptiveFresh=false,bool forceRasterCore=false) {
    auto source=TransportDeferredReconstructionShader();
    // Remove the unused world-card lighting sampler in this variant. Slot 11
    // becomes the full-resolution native base; slot 8 is the original soft
    // mask, rather than the unused world Bz coefficient texture.
    auto sample=TransportSampleGLSL();
    TransportDeferredReplace(sample,"layout(binding=0) uniform sampler2D wgTransport;","layout(binding=11) uniform sampler2D wgTransport;");
    TransportDeferredReplace(sample,"layout(binding=6) uniform sampler2D wgBy;","layout(binding=7) uniform sampler2D wgBy;");
    TransportDeferredReplace(sample,"layout(binding=7) uniform sampler2D wgBz;","layout(binding=8) uniform sampler2D wgBz;");
    TransportDeferredReplace(source,sample,"layout(binding=11) uniform sampler2D nbNativeBase;\n"+NativeBaseTransportReadGLSL("nbNativeBase",adaptiveFresh,1,forceRasterCore,7));
    const auto main=source.find("void main(){\n    ivec2 pixel=ivec2(gl_FragCoord.xy);");
    if(main==std::string::npos)throw std::logic_error("Native base reconstruction main changed");
    source.resize(main);
    return source+R"GLSL(
void main(){
    ivec2 pixel=ivec2(gl_FragCoord.xy);vec4 mask=texelFetch(sgMask,pixel,0),current=texelFetch(sgCurrent,pixel,0);
    outColor=current;outMask=mask;outBase=NBReadBase(pixel);
    if(mask.x<0){outColor=vec4(0);outBase=vec4(0);return;}
    if(mask.w>0){outMask.y=NBReadRisk(pixel,outBase.a>0);return;}
    vec4 view=texelFetch(wgPositions,pixel,0),normal=texelFetch(wgNormals,pixel,0);
    if(view.w<.5||normal.w<.5){outColor=vec4(0);outMask=vec4(0);outBase=vec4(0);return;}
    int grid=WGReceiverGrid(),phase=wgSchedule.x%(grid*grid);ivec2 shift=ivec2(phase%grid,phase/grid);
    if(all(equal(pixel%grid,shift))){
        vec4 low=texelFetch(sgLowCurrent,pixel/grid,0),m=texelFetch(sgLowMask,pixel/grid,0);
        if(m.x<0){outColor=vec4(0);outMask=m;outBase=vec4(0);return;}
        if(m.w>0){outColor=low;outMask=m;outMask.y=NBReadRisk(pixel,outBase.a>0);return;}
    }
    // Base and risk always belong to this receiver, never to an interpolated
    // card/lattice footprint. Only actual current measurements are rebuilt.
    float risk=NBReadRisk(pixel,outBase.a>0);
    if(outBase.a>0&&risk<=.000001){outColor=vec4(0);outMask=vec4(0);return;}
    vec3 p=(scInverseView*vec4(view.xyz,1)).xyz,n=normalize(normal.xyz),albedo=texelFetch(sgAlbedo,pixel,0).rgb;
    vec2 coord=vec2(pixel-shift)/float(grid),fraction=fract(coord);ivec2 first=ivec2(floor(coord))*grid+shift;
    vec4 sum=vec4(0);float mass=0,hitDistance=scParameters.x;
    for(int y=0;y<2;++y)for(int x=0;x<2;++x){ivec2 at=first+ivec2(x,y)*grid;
        if(any(lessThan(at,ivec2(0)))||any(greaterThanEqual(at,scScreen.xy)))continue;
        float weight=(x==0?1-fraction.x:fraction.x)*(y==0?1-fraction.y:fraction.y);if(weight<=.000001)continue;
        vec4 m=texelFetch(sgMask,at,0),v=texelFetch(sgCurrent,at,0);
        if(m.x<0)continue;
        if(m.w<=0){m=texelFetch(sgLowMask,at/grid,0);v=texelFetch(sgLowCurrent,at/grid,0);}
        if(m.x<0||m.w<=0||!TDFinite(v))continue;
        vec4 position=texelFetch(wgPositions,at,0),nd=texelFetch(wgNormals,at,0);
        if(position.w!=view.w||nd.w<.5)continue;
        vec3 q=(scInverseView*vec4(position.xyz,1)).xyz,nn=normalize(nd.xyz),delta=q-p,sourceAlbedo=texelFetch(sgAlbedo,at,0).rgb;
        float alignment=clamp(dot(n,nn),-1.0,1.0),curve=.75*length(delta)*sqrt(max(0.0,2.0*(1.0-alignment)));
        if(alignment<.95||max(abs(dot(delta,n)),abs(dot(delta,nn)))>max(.0003,curve)||
            any(greaterThan(abs(sourceAlbedo-albedo),vec3(.08)))||any(bvec3(albedo.x>.0001&&sourceAlbedo.x<.0001,albedo.y>.0001&&sourceAlbedo.y<.0001,albedo.z>.0001&&sourceAlbedo.z<.0001)))continue;
        bool clear=true;for(int step=1;step<4;++step){if(step>=grid)break;
            ivec2 middle=ivec2(floor(mix(vec2(pixel),vec2(at),float(step)/float(grid))+.5));
            vec4 mv=texelFetch(wgPositions,middle,0),mn=texelFetch(wgNormals,middle,0);
            if(mv.w!=view.w||mn.w<.5||dot(n,normalize(mn.xyz))<.95){clear=false;break;}}
        if(!clear)continue;weight*=pow(alignment,16.0);
        sum+=vec4(v.rgb/max(sourceAlbedo,vec3(.0001)),v.a)*weight;mass+=weight;
        hitDistance=min(hitDistance,m.z);
    }
    outColor=vec4(0);outMask=vec4(0);
    if(mass>0){
        outColor=vec4(max(albedo*sum.rgb/mass,vec3(0)),clamp(sum.a/mass,0.0,1.0));
        outMask=vec4(.25,risk,hitDistance,0);
    }
    // With no actual support, retain a valid native base; an unknown native
    // base stays unknown black. Never manufacture an actual measurement.
}
)GLSL";
}
} // namespace Render::SurfaceCacheDetail
