#pragma once
#include <glm/glm.hpp>
#include <string>
namespace Render::PipelineDetail {
struct alignas(16) SkinDiffusionConstants {
    glm::mat4 inverseProjection{1},projection{1};
    glm::vec4 viewport{0}; // width, height, direction.xy
    glm::vec4 radius{0}; // world sigma, blend, normal cosine, plane-distance rejection
    glm::vec4 channels{1,.45f,.25f,64}; // RGB sigmas, support cap in pixels
    glm::vec4 texturing{.5f,1,0,0}; // local albedo exponent, first convolution
    glm::vec4 radial{20,1,0,0}; // disk samples, world units per meter
};
inline std::string SkinDiffusionBlurShader() {
    return R"GLSL(#version 450 core
layout(binding=0) uniform sampler2D sourceDiffuse;
layout(binding=1) uniform sampler2D skinProfile;
layout(binding=2) uniform sampler2D sourceDepth;
layout(binding=3) uniform sampler2D skinAlbedo;
layout(std140,binding=5) uniform SkinDiffusionData {
    mat4 inverseProjection; mat4 projection;
    vec4 viewport; vec4 radius; vec4 channels; vec4 texturing; vec4 radial;
};
in vec2 vUV;
layout(location=0) out vec4 outColor;
vec3 decodeNormal(vec2 p) {
    vec3 n=vec3(p*2.0-1.0,1.0-abs(p.x*2.0-1.0)-abs(p.y*2.0-1.0));
    if(n.z<0.0)n.xy=(1.0-abs(n.yx))*mix(vec2(-1.0),vec2(1.0),greaterThanEqual(n.xy,vec2(0.0)));
    return normalize(n);
}
vec3 viewPosition(ivec2 p) {
    vec2 uv=(vec2(p)+.5)/viewport.xy;
    vec4 h=inverseProjection*vec4(uv*2.0-1.0,texelFetch(sourceDepth,p,0).r*2.0-1.0,1.0);
    return h.xyz/h.w;
}
vec3 transportInput(ivec2 p) {
    vec3 radiance=texelFetch(sourceDiffuse,p,0).rgb;
    if(texturing.y>.5) {
        // Source capture is incident diffuse lighting, independent of local
        // painted RGB. Split coloring at entry/exit without ever dividing black.
        vec3 localColor=clamp(texelFetch(skinAlbedo,p,0).rgb,vec3(0.0),vec3(1.0));
        vec3 entryColor=texturing.x>=1.0?vec3(1.0):pow(localColor,vec3(1.0-texturing.x));
        radiance*=entryColor;
    }
    return radiance;
}
void main() {
    ivec2 size=ivec2(viewport.xy),p=clamp(ivec2(vUV*viewport.xy),ivec2(0),size-1);
    vec4 center=texelFetch(sourceDiffuse,p,0),profile=texelFetch(skinProfile,p,0);
    if(profile.b<=0.0 || profile.a<=0.0 || radius.x<=0.0){outColor=vec4(transportInput(p),center.a);return;}
    vec3 position=viewPosition(p),normal=decodeNormal(profile.rg);
    vec4 a=projection*vec4(position,1.0);
    vec4 b=projection*vec4(position+vec3(viewport.zw*radius.x,0.0),1.0);
    float worldPixels=length((b.xy/b.w-a.xy/a.w)*.5*viewport.xy);
    vec3 sigma=max(worldPixels*channels.rgb,vec3(.001));
    float support=min(3.0*max(sigma.r,max(sigma.g,sigma.b)),channels.w);
    // Integrate a normalized RGB Gaussian reconstruction. The source mask
    // and material-boundary tag are retained, not a recovered profile kernel.
    vec3 sum=vec3(0.0),weights=vec3(0.0);
    for(int i=-12;i<=12;++i) {
        float offset=support*float(i)/12.0;
        ivec2 q=p+ivec2(round(viewport.zw*offset));
        if(any(lessThan(q,ivec2(0)))||any(greaterThanEqual(q,size)))continue;
        vec4 other=texelFetch(skinProfile,q,0);
        if(other.b<=0.0 || other.a!=profile.a)continue;
        vec3 otherNormal=decodeNormal(other.rg);
        if(dot(normal,otherNormal)<radius.z)continue;
        vec3 displacement=viewPosition(q)-position;
        float separation=max(abs(dot(displacement,normal)),abs(dot(displacement,otherNormal)));
        if(separation>radius.x*radius.w+1e-6)continue;
        // The source mask also gates cross-receiver transport. Zero masks can
        // neither donate nor receive energy; normalize accepted samples only.
        vec3 weight=exp(-.5*offset*offset/(sigma*sigma))*min(profile.b,other.b);
        sum+=transportInput(q)*weight;
        weights+=weight;
    }
    outColor=vec4(sum/max(weights,vec3(1e-12)),center.a);
}
)GLSL";
}
// A bounded raster adaptation of the radial profile found in the shipped
// SSSSSMainPass. Selection of that runtime branch/profile remains unconfirmed.
// The source uses blue-noise rotation, tile dispatch and temporal variance;
// this deterministic gather keeps the recovered inverse CDF/radial weight law.
inline std::string SkinDiffusionBurleyShader() {
    std::string shader=SkinDiffusionBlurShader();
    const auto main=shader.find("void main() {");
    shader.resize(main);
    shader+=R"GLSL(
vec2 sourceBurleySample(float u,float distance) {
    // DXIL %534..559: t=1-u, cubic inverse-CDF, radius and inverse radial PDF*2PI.
    float t=1.0-u,a=4.0*t;
    float g=1.0+a*(2.0*t+sqrt(1.0+4.0*t*t));
    float c=exp2(-log2(g)/3.0),b=1.0+c+c*c*g;
    float r=distance*2.079441547393799*log2(b/a);
    float inversePdf=distance*25.132741928100586*b*b*b/(a*(a*a+b*b));
    return vec2(r,inversePdf);
}
void main() {
    ivec2 size=ivec2(viewport.xy),p=clamp(ivec2(vUV*viewport.xy),ivec2(0),size-1);
    vec4 center=texelFetch(sourceDiffuse,p,0),profile=texelFetch(skinProfile,p,0);
    // Preserve six passes: the first convolution prepares entry coloration;
    // the second performs a genuinely radial 2D gather, never two 1D Burley filters.
    if(texturing.y>.5) {outColor=vec4(transportInput(p),center.a);return;}
    if(profile.b<=0.0 || profile.a<=0.0 || radius.x<=0.0){outColor=center;return;}
    vec3 position=viewPosition(p),normal=decodeNormal(profile.rg);
    float unitsPerMeter=max(radial.y,1e-6);
    vec3 distanceMm=max(radius.x*1000.0/unitsPerMeter*channels.rgb,vec3(1e-5));
    float maxDistance=max(distanceMm.r,max(distanceMm.g,distanceMm.b));
    vec4 a=projection*vec4(position,1.0);
    vec4 bx=projection*vec4(position+vec3(radius.x,0.0,0.0),1.0);
    vec4 by=projection*vec4(position+vec3(0.0,radius.x,0.0),1.0);
    vec2 pixelsPerWorld=vec2(length((bx.xy/bx.w-a.xy/a.w)*.5*viewport.xy),
        length((by.xy/by.w-a.xy/a.w)*.5*viewport.xy))/max(radius.x,1e-12);
    vec3 sum=center.rgb*1e-4,weights=vec3(1e-4);
    int count=clamp(int(radial.x),8,64);
    for(int i=0;i<64;++i) {
        if(i>=count)break;
        vec2 sampleData=sourceBurleySample((float(i)+.5)/float(count),maxDistance);
        // Host adaptation: deterministic full-circle golden-angle quadrature.
        // Source random rotation/history has not been reproduced here.
        float angle=float(i)*2.399963229728653;
        vec2 offset=vec2(cos(angle),sin(angle))*sampleData.x*.001*unitsPerMeter*pixelsPerWorld;
        if(length(offset)>channels.w)continue;
        ivec2 q=p+ivec2(round(offset));
        if(any(lessThan(q,ivec2(0)))||any(greaterThanEqual(q,size)))continue;
        vec4 other=texelFetch(skinProfile,q,0);
        if(other.b<=0.0||other.a!=profile.a)continue;
        vec3 otherNormal=decodeNormal(other.rg),otherPosition=viewPosition(q);
        if(dot(normal,otherNormal)<radius.z)continue;
        vec3 displacement=otherPosition-position;
        float separation=max(abs(dot(displacement,normal)),abs(dot(displacement,otherNormal)));
        if(separation>radius.x*radius.w+1e-6)continue;
        // Source SubSurfaceParam stores radial camera distance in half precision.
        // Keep full precision here and convert its difference from world to mm.
        float deltaMm=(length(otherPosition)-length(position))*1000.0/unitsPerMeter;
        float r=sqrt(sampleData.x*sampleData.x+deltaMm*deltaMm);
        // DXIL %625..650: exp2(-log2(e)*r/(3*d)), cube+first power,
        // inverse radial PDF*2PI and saturate, then RGB-normalized accumulation.
        vec3 e=exp2(-.48089799284935*r/distanceMm);
        vec3 w=clamp((e+e*e*e)*sampleData.y,vec3(0.0),vec3(1.0))*min(profile.b,other.b);
        sum+=texelFetch(sourceDiffuse,q,0).rgb*w;weights+=w;
    }
    outColor=vec4(sum/max(weights,vec3(1e-12)),center.a);
}
)GLSL";
    return shader;
}
inline std::string SkinDiffusionComposeShader() {
    return R"GLSL(#version 450 core
layout(binding=0) uniform sampler2D originalDiffuse;
layout(binding=1) uniform sampler2D diffusedDiffuse;
layout(binding=2) uniform sampler2D skinProfile;
layout(binding=3) uniform sampler2D skinAlbedo;
layout(std140,binding=5) uniform SkinDiffusionData {
    mat4 inverseProjection; mat4 projection; vec4 viewport; vec4 radius; vec4 channels; vec4 texturing; vec4 radial;
};
in vec2 vUV;
layout(location=0) out vec4 outColor;
void main() {
    ivec2 size=textureSize(originalDiffuse,0);
    ivec2 p=clamp(ivec2(vUV*vec2(size)),ivec2(0),size-1);
    float mask=texelFetch(skinProfile,p,0).b;
    vec3 localColor=clamp(texelFetch(skinAlbedo,p,0).rgb,vec3(0.0),vec3(1.0));
    vec3 exitColor=texturing.x<=0.0?vec3(1.0):pow(localColor,vec3(texturing.x));
    vec3 difference=texelFetch(diffusedDiffuse,p,0).rgb*exitColor-texelFetch(originalDiffuse,p,0).rgb*localColor;
    // ONE/ONE addition to existing HDR: only diffuse changes, alpha/specular
    // and every non-skin receiver remain intact. Negative corrections are valid.
    outColor=vec4(difference*mask*radius.y,0.0);
}
)GLSL";
}
} // namespace Render::PipelineDetail
