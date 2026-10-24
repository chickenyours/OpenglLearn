#pragma once
#include "Render/Private/Pipeline/temporal_effects_shaders.h"
namespace Render::PipelineDetail {
// Host temporal adapter for a separate premultiplied hair layer. Motion comes
// from LAST EXECUTED per-fiber positions; opaque surfaces keep camera reprojection.
inline std::string HairTemporalShader(){return TemporalEffectsDetail::FragmentPreamble()+R"GLSL(
vec4 HairPremultipliedTexel(ivec2 pixel){
    vec4 c=texelFetch(currentColor,clamp(pixel,ivec2(0),textureSize(currentColor,0)-1),0);
    float a=(isnan(c.a)||isinf(c.a))?0.0:clamp(c.a,0.0,1.0);
    return vec4(FiniteHDR(c.rgb)*a,a);
}
vec4 HairCurrent(vec2 uv){
    // Filter coverage and premultiplied energy together. Filtering straight RGB
    // first darkens subpixel strands and admits RGB from transparent texels.
    vec2 p=uv*vec2(textureSize(currentColor,0))-.5;
    ivec2 base=ivec2(floor(p));vec2 f=fract(p);
    return mix(mix(HairPremultipliedTexel(base),HairPremultipliedTexel(base+ivec2(1,0)),f.x),
               mix(HairPremultipliedTexel(base+ivec2(0,1)),HairPremultipliedTexel(base+ivec2(1,1)),f.x),f.y);
}
void main(){
    vec2 rawUV=SafeUV(currentColor,vUV+CurrentJitterUV());
    vec4 current=HairCurrent(rawUV);
    if(temporal.w<.5||temporal.x<=0.0){outColor=current;return;}
    ivec2 size=textureSize(currentDepth,0),pixel=DepthPixel(currentDepth,rawUV);
    vec4 previous=texelFetch(opaqueColor,pixel,0); // slot4 = resolved strand motion
    if(previous.w<.5&&current.a<=.00001){
        // A one-pixel velocity dilation covers changing subpixel filter support.
        // It operates only on this hair layer, so it cannot drag the face color.
        float best=0.0;
        for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){
            ivec2 p=clamp(pixel+ivec2(x,y),ivec2(0),size-1);
            vec4 candidate=texelFetch(opaqueColor,p,0);
            float a=texelFetch(currentColor,p,0).a;
            float score=a/(1.0+float(x*x+y*y));
            if(candidate.w>.5&&score>best){
                best=score;previous=candidate;
                previous.xy+=rawUV-(vec2(p)+.5)/vec2(size);
            }
        }
    }
    if(previous.w<.5||!Inside(previous.xy)||any(isnan(previous))||any(isinf(previous))){outColor=current;return;}
    vec3 oldWorld;
    if(!Reconstruct(previousInverseViewProjection,previous.xy,previous.z,oldWorld)){outColor=current;return;}
    vec4 stableClip=previousUnjitteredViewProjection*vec4(oldWorld,1.0);
    if(stableClip.w<=1e-8){outColor=current;return;}
    vec2 historyUV=stableClip.xy/stableClip.w*.5+.5;
    if(!Inside(historyUV)){outColor=current;return;}
    ivec2 oldPixel=DepthPixel(historyDepth,previous.xy);
    float oldDepth=texelFetch(historyDepth,oldPixel,0).r;
    if(oldDepth>=.999999){outColor=current;return;}
    vec2 oldPixelUV=(vec2(oldPixel)+.5)/vec2(textureSize(historyDepth,0));
    vec3 expected,actual;
    if(!Reconstruct(previousInverseViewProjection,oldPixelUV,previous.z,expected)||
       !Reconstruct(previousInverseViewProjection,oldPixelUV,oldDepth,actual)){outColor=current;return;}
    float tolerance=max(temporal.y,0.0)+max(temporal.z,0.0)*length(oldWorld-cameraPosition.xyz);
    if(length(expected-actual)>tolerance){outColor=current;return;}
    vec4 old=ReadColor(historyColor,historyUV);
    // History is normally produced by this pass, but a poisoned texel must
    // never propagate a NaN opacity into the separate hair composite.
    if(any(isnan(old))||any(isinf(old))){outColor=current;return;}
    old.a=clamp(old.a,0.0,1.0);
    if(old.a<=.00001){outColor=current;return;}
    vec4 lower=vec4(1e30),upper=vec4(-1e30);
    vec2 texel=1.0/vec2(textureSize(currentColor,0));
    for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){
        vec4 c=HairCurrent(rawUV+vec2(x,y)*texel);lower=min(lower,c);upper=max(upper,c);
    }
    old=clamp(old,lower,upper);
    float speed=length((historyUV-vUV)/max(screen.xy,vec2(1e-8)));
    // Hair-only contract: reactive.z enables this host reconstruction guard;
    // reactive.w is the full-response speed in stable-grid pixels per frame.
    // Static/subpixel history retains the original coverage accumulation.
    float motionResponse=reactive.z>.5?smoothstep(1.0,max(reactive.w,1.001),speed):0.0;
    if(motionResponse>0.0){
        // Moving coverage cannot keep an old silhouette after that strand has
        // left the pixel. Preserve straight history hue while constraining its
        // premultiplied energy and alpha together, including the A0 edge case.
        float coverage=mix(old.a,current.a,motionResponse);
        old.rgb*=coverage/max(old.a,.00001);old.a=coverage;
    }
    float weight=clamp(temporal.x,0.0,.98);
    if(current.a>.01&&old.a>.01){
        vec3 a=current.rgb/current.a,b=old.rgb/old.a;
        float lumaA=dot(a,vec3(.2126,.7152,.0722)),lumaB=dot(b,vec3(.2126,.7152,.0722));
        weight*=1.0-.85*clamp(abs(lumaA-lumaB)/max(max(lumaA,lumaB),.01),0.0,1.0);
    }
    if(motionResponse>0.0&&current.a>.00001&&old.a>.00001){
        // Equal-luminance chroma/highlight changes evade a luminance-only
        // limiter. Cover very transparent HDR strands as well: low alpha does
        // not imply low radiance. Coherent moving color keeps its old weight.
        vec3 a=current.rgb/current.a,b=old.rgb/old.a;
        float colorChange=clamp(length(a-b)/max(max(length(a),length(b)),.01),0.0,1.0);
        weight*=1.0-motionResponse*colorChange;
    }
    weight*=mix(1.0,.65,clamp(speed/32.0,0.0,1.0));
    vec4 result=mix(current,old,weight);
    result.a=clamp(result.a,0.0,1.0);
    outColor=vec4(clamp(FiniteHDR(result.rgb),vec3(0.0),vec3(60000.0*result.a)),result.a);
}
)GLSL";}
inline std::string HairTemporalCompositeShader(){return R"GLSL(#version 450 core
layout(binding=0) uniform sampler2D backgroundColor;
layout(binding=1) uniform sampler2D premultipliedHair;
in vec2 vUV;layout(location=0) out vec4 outColor;
void main(){vec4 h=texture(premultipliedHair,vUV);vec3 b=texture(backgroundColor,vUV).rgb;
outColor=vec4(h.rgb+(1.0-clamp(h.a,0.0,1.0))*b,1.0);}
)GLSL";}
}
