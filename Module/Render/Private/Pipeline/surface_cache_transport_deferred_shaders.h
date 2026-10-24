#pragma once
#include "surface_cache_transport_shaders.h"
#include <stdexcept>
namespace Render::SurfaceCacheDetail {
inline void TransportDeferredReplace(std::string& source,const std::string& from,const std::string& to){
    const auto at=source.find(from);if(at==std::string::npos||source.find(from,at+from.size())!=std::string::npos)
        throw std::logic_error("Deferred transport source changed");source.replace(at,from.size(),to);
}
inline std::string TransportDeferredResolveShader(unsigned receiverPass){
    auto source=TransportResolveShader(false,receiverPass);
    TransportDeferredReplace(source,"    if(farValid&&risk<=.000001){outColor=vec4(rho*farValue*scParameters.z,coverage);return;}",
        "    if(farValid&&risk<=.000001){outColor=vec4(0);wgNearMask=vec4(0);return;}");
    TransportDeferredReplace(source,"    outColor=farValid?mix(vec4(rho*farValue*scParameters.z,coverage),current,clamp(risk,0.0,1.0)):current;",
        "    outColor=current;");
    return source;
}
inline std::string TransportDeferredReceiverFilterShader(){
    auto source=TransportReceiverFilterShader(true);
    TransportDeferredReplace(source,"void main() {","void TFCurrentMain() {");
    return source+R"GLSL(
void main(){
    TFCurrentMain();
    // Run after the complete current-only filter, including every early exit.
    if((ctOptions.w&64)==0||(ctOptions.w&4)==0)return;
    ivec2 pixel=ivec2(gl_FragCoord.xy);vec4 mask=texelFetch(ctMask,pixel,0);
    if(mask.x<0){outColor=vec4(0);return;}
    vec4 base=texelFetch(ctBase,pixel,0);
    if(!(base.a>0)||!CFFinite(base.rgb)||isnan(base.a)||isinf(base.a))base=vec4(0);
    if(!CFFinite(outColor.rgb)||isnan(outColor.a)||isinf(outColor.a))outColor=vec4(0);
    if(mask.x<=0&&mask.w<=0){outColor=base;return;}
    float risk=base.a>0?clamp(mask.y,0.0,1.0):1.0;
    outColor=mix(base,outColor,risk);
}
)GLSL";
}
inline std::string TransportDeferredReconstructionShader(){
    auto source=TransportReconstructionShader();const auto main=source.find("void main(){ivec2 pixel=ivec2(gl_FragCoord.xy);");
    if(main==std::string::npos)throw std::logic_error("Deferred reconstruction main changed");source.resize(main);
    return source+R"GLSL(
layout(location=2) out vec4 outBase;
layout(binding=9) uniform sampler2D sgBase;
layout(binding=15) uniform sampler2D sgLowBase;
bool TDFinite(vec4 value){return WGFinite(value.rgb)&&!isnan(value.a)&&!isinf(value.a);}
void main(){
    ivec2 pixel=ivec2(gl_FragCoord.xy);vec4 mask=texelFetch(sgMask,pixel,0),current=texelFetch(sgCurrent,pixel,0);
    outColor=current;outMask=mask;outBase=vec4(0);
    if(mask.x<0){outColor=vec4(0);return;}
    if(mask.w>0){outBase=texelFetch(sgBase,pixel,0);return;}
    vec4 view=texelFetch(wgPositions,pixel,0),normal=texelFetch(wgNormals,pixel,0);
    if(view.w<.5||normal.w<.5){outColor=vec4(0);outMask=vec4(0);return;}
    int grid=WGReceiverGrid(),phase=wgSchedule.x%(grid*grid);ivec2 shift=ivec2(phase%grid,phase/grid);
    if(all(equal(pixel%grid,shift))){
        vec4 low=texelFetch(sgLowCurrent,pixel/grid,0),m=texelFetch(sgLowMask,pixel/grid,0),b=texelFetch(sgLowBase,pixel/grid,0);
        if(m.x<0){outColor=vec4(0);outMask=m;return;}
        if(m.w>0||b.a>0){outColor=low;outMask=m;outBase=b;return;}
    }
    vec3 p=(scInverseView*vec4(view.xyz,1)).xyz,n=normalize(normal.xyz),albedo=texelFetch(sgAlbedo,pixel,0).rgb;
    vec2 coord=vec2(pixel-shift)/float(grid),fraction=fract(coord);ivec2 first=ivec2(floor(coord))*grid+shift;
    vec4 sum=vec4(0),baseSum=vec4(0);float mass=0,baseMass=0,riskSum=0,riskMass=0,hitDistance=scParameters.x;
    for(int y=0;y<2;++y)for(int x=0;x<2;++x){ivec2 at=first+ivec2(x,y)*grid;
        if(any(lessThan(at,ivec2(0)))||any(greaterThanEqual(at,scScreen.xy)))continue;
        float weight=(x==0?1-fraction.x:fraction.x)*(y==0?1-fraction.y:fraction.y);if(weight<=.000001)continue;
        vec4 m=texelFetch(sgMask,at,0),v=texelFetch(sgCurrent,at,0),b=texelFetch(sgBase,at,0);
        if(m.x<0)continue;
        if(m.w<=0){m=texelFetch(sgLowMask,at/grid,0);v=texelFetch(sgLowCurrent,at/grid,0);b=texelFetch(sgLowBase,at/grid,0);}
        if(m.x<0)continue;
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
        // Risk uses the entire geometric footprint, including base-only taps.
        riskSum+=clamp(m.y,0.0,1.0)*weight;riskMass+=weight;
        if(m.w>0&&TDFinite(v)){
            // Completed unknown directions retain their full geometric weight.
            sum+=vec4(v.rgb/max(sourceAlbedo,vec3(.0001)),v.a)*weight;mass+=weight;
            hitDistance=min(hitDistance,m.z);
        }
        if(b.a>0&&TDFinite(b)){baseSum+=vec4(b.rgb/max(sourceAlbedo,vec3(.0001)),b.a)*weight;baseMass+=weight;}
    }
    outColor=vec4(0);outMask=vec4(0);
    if(baseMass>0)outBase=vec4(max(albedo*baseSum.rgb/baseMass,vec3(0)),clamp(baseSum.a/baseMass,0.0,1.0));
    if(mass>0){
        outColor=vec4(max(albedo*sum.rgb/mass,vec3(0)),clamp(sum.a/mass,0.0,1.0));
        float risk=outBase.a>0?clamp(riskSum/max(riskMass,.000001),0.0,1.0):1.0;
        outMask=vec4(.25,risk,hitDistance,0);
    }else if(baseMass<=0){
        int anchor,own;vec3 lighting;float readable;
        if(WGAnchor(view,p,n,anchor,own)){wgAnchor=anchor;if(WGSampleObject(p,n,own,lighting,readable))outBase=vec4(albedo*lighting*scParameters.z,readable);}
    }
}
)GLSL";
}
}
