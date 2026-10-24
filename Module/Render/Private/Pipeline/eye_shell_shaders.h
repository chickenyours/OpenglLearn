#pragma once
#include <string>
namespace Render::PipelineDetail {
inline std::string EyeShellVertexShader() {
    return R"GLSL(#version 450 core
layout(location=0)in vec3 aPosition;
layout(location=1)in vec3 aNormal;
layout(location=2)in vec2 aTexCoord;
layout(location=3)in vec4 aTangent;
layout(std140,binding=0)uniform EyeShellView {mat4 viewProjection;vec4 cameraPosition;};
layout(std140,binding=1)uniform EyeShellObject {mat4 model;};
out vec3 shellWorldPosition;out vec3 shellNormal;out vec4 shellTangent;out vec2 shellUV;
void main(){
    vec4 position=model*vec4(aPosition,1.0);mat3 linear=mat3(model);
    mat3 normalMatrix=abs(determinant(linear))>1e-12?transpose(inverse(linear)):mat3(1.0);
    vec3 n=normalize(normalMatrix*aNormal);vec3 t=linear*aTangent.xyz;
    t=normalize(t-n*dot(t,n));
    shellWorldPosition=position.xyz;shellNormal=n;
    shellTangent=vec4(t,aTangent.w*(determinant(linear)<0.0?-1.0:1.0));shellUV=aTexCoord;
    gl_Position=viewProjection*position;
}
)GLSL";
}
inline std::string EyeShellCopyShader() {
    return R"GLSL(#version 450 core
layout(binding=0)uniform sampler2D sourceHDR;in vec2 vUV;layout(location=0)out vec4 outColor;
void main(){ivec2 size=textureSize(sourceHDR,0);ivec2 p=clamp(ivec2(vUV*vec2(size)),ivec2(0),size-1);
vec4 c=texelFetch(sourceHDR,p,0);if(any(isnan(c.rgb))||any(isinf(c.rgb)))c.rgb=vec3(0.0);outColor=c;}
)GLSL";
}
inline std::string EyeShellFragmentShader() {
    return R"GLSL(#version 450 core
layout(binding=0)uniform sampler2D sourceHDR;
layout(binding=1)uniform sampler2D sourceDepth;
layout(binding=2)uniform sampler2D tearlineNormal;
layout(binding=3)uniform sampler2D fakeSpecular;
layout(std140,binding=0)uniform EyeShellView {mat4 viewProjection;vec4 cameraPosition;};
layout(std140,binding=2)uniform EyeShellMaterial {
    vec4 eyeShellSurface,eyeShellOutlineArea,eyeShellOutlineSmooth,eyeShellFillArea,eyeShellFillSmooth;
    vec4 eyeShellBlurOutlineArea,eyeShellBlurOutlineSmooth,eyeShellBlurFillArea,eyeShellBlurFillSmooth;
    vec4 eyeShellSpecular,eyeShellFake,eyeShellCavity,eyeShellDebug;
};
layout(std140,binding=3)uniform EyeShellPass {
    mat4 inverseProjection,view,projection;
    vec4 viewport; // xy=inverse extent, z=near plane, w=source-to-world scale
    vec4 sunDirectionIntensity,sunColor;
    vec4 lightPositions[4],lightColors[4];
};
in vec3 shellWorldPosition;in vec3 shellNormal;in vec4 shellTangent;in vec2 shellUV;
layout(location=0)out vec4 outColor;
const float ShellPi=3.141592653589793;
vec3 shellNormalize(vec3 n,vec3 fallback){float l=dot(n,n);return l>1e-12?n*inversesqrt(l):fallback;}
vec2 shellClampUV(sampler2D map,vec2 uv){vec2 h=.5/vec2(textureSize(map,0));return clamp(uv,h,vec2(1.0)-h);}
vec3 shellSampleHDR(vec2 uv){vec3 c=texture(sourceHDR,shellClampUV(sourceHDR,uv)).rgb;return any(isnan(c))||any(isinf(c))?vec3(0.0):max(c,vec3(0.0));}
float shellDenominator(float value){return value<0.0?min(value,-1e-5):max(value,1e-5);}
// Direct transcription of source DXIL %296..393. UV0 is the original source
// top-row convention. Keep outline and fill responses separate until max/min.
float EyeShellShape(vec2 uv,vec4 outlineArea,vec4 outlineSmooth,vec4 fillArea,vec4 fillSmooth){
    vec4 edges=vec4(1.0-uv.y,uv.y,1.0-uv.x,uv.x);
    vec4 fills=vec4(uv.y,1.0-uv.y,uv.x,1.0-uv.x);
    vec4 outline;
    vec4 fill;
    for(int i=0;i<4;++i){
        outline[i]=max(0.0,edges[i]-outlineArea[i])/shellDenominator((1.0-outlineArea[i])*outlineSmooth[i]);
        fill[i]=(min(1.0,fills[i]/shellDenominator(fillArea[i]))+fillSmooth[i]-1.0)/shellDenominator(fillSmooth[i]);
    }
    return clamp(max(max(max(outline.x,outline.y),max(outline.z,outline.w)),min(min(fill.x,fill.y),min(fill.z,fill.w))),0.0,1.0);
}
vec3 shellViewPosition(vec2 uv,float depth){vec4 p=inverseProjection*vec4(uv*2.0-1.0,depth*2.0-1.0,1.0);return p.xyz/p.w;}
float shellDepth(vec2 uv){return texture(sourceDepth,shellClampUV(sourceDepth,uv)).r;}
vec3 shellSpecular(vec3 n,vec3 v,vec3 l,vec3 radiance,float roughness,float reflectance){
    float nl=max(dot(n,l),0.0),nv=max(dot(n,v),0.0);if(nl<=0.0||nv<=0.0)return vec3(0.0);
    vec3 h=shellNormalize(l+v,n);float nh=max(dot(n,h),0.0),vh=max(dot(v,h),0.0);
    float a2=pow(roughness,4.0),den=nh*nh*(a2-1.0)+1.0;
    float d=a2/(ShellPi*max(den*den,1e-12));float k=pow(roughness+1.0,2.0)/8.0;
    float g=(nl/max(nl*(1.0-k)+k,1e-6))*(nv/max(nv*(1.0-k)+k,1e-6));
    float f=reflectance+(1.0-reflectance)*pow(1.0-vh,5.0);
    return radiance*(d*g*f/max(4.0*nv,1e-4));
}
void main(){
    // The source samples its tearline normal before depth eligibility. Capture
    // UV derivatives before the early return so mip selection is defined at
    // pixels straddling that depth boundary in the host reconstruction.
    vec2 normalDx=dFdx(shellUV),normalDy=dFdy(shellUV);
    ivec2 pixel=ivec2(gl_FragCoord.xy);vec2 screenUV=gl_FragCoord.xy*viewport.xy;
    vec4 original=texelFetch(sourceHDR,pixel,0);vec3 base=shellSampleHDR(screenUV);
    float depth=texelFetch(sourceDepth,pixel,0).r;
    vec3 shellView=(view*vec4(shellWorldPosition,1.0)).xyz;
    float scale=max(viewport.w,1e-5),shellDistance=-shellView.z;
    float gap=-shellViewPosition(screenUV,depth).z-shellDistance;
    float depthUnits=max(eyeShellCavity.w,1e-5); // Explicit host assumption; runtime ZToLinear units were not captured.
    // Source exact eligibility: underlying linear depth is strictly behind the
    // shell and less than CheckDepthDifference away. Outside: HDR copy identity.
    if(depth>=.999999 || !(gap>0.0 && gap<eyeShellSurface.x*scale/depthUnits)) {outColor=vec4(base,original.a);return;}
    float shape=EyeShellShape(shellUV,eyeShellOutlineArea,eyeShellOutlineSmooth,eyeShellFillArea,eyeShellFillSmooth);
    float visibility=clamp(1.0-eyeShellSurface.y+shape,0.0,1.0);visibility*=visibility;
    float blurShape=EyeShellShape(shellUV,eyeShellBlurOutlineArea,eyeShellBlurOutlineSmooth,eyeShellBlurFillArea,eyeShellBlurFillSmooth);
    float blurBlend=1.0-blurShape;
    float cavity=clamp((min(1.0,shellUV.y/shellDenominator(eyeShellCavity.x))+eyeShellCavity.y-1.0)/shellDenominator(eyeShellCavity.y),0.0,1.0);
    // Source uses (3-viewDepth)*8 radial pairs and 1/i weights. Convert scene
    // units using model scale; cap at32 pairs for edited/nonphysical materials.
    float sourceDistance=shellDistance*depthUnits/scale,range=3.0-sourceDistance;
    int pairs=clamp(int(floor(max(range,0.0)*8.0)),0,32);
    vec2 ray=vec2(2.0*shellUV.x-1.0,1.0-2.0*shellUV.y);
    // Source projection-element / near-depth blur scaling. Exact engine packed
    // projection-element convention is unknown; projection[1][1] is the host
    // raster equivalent. Clamp the edited footprint to32 screen pixels.
    float radius=projection[1][1]*.0001/max(viewport.z*depthUnits/scale,1e-5)*gl_FragCoord.z*max(eyeShellSurface.z,0.0)/max(range,1e-4);
    vec2 stepUV=ray*radius;float pixels=length(stepUV/viewport.xy);
    if(pixels>32.0)stepUV*=32.0/pixels;
    vec3 sum=base;float weight=1.0;float reject=length(base)*max(eyeShellSurface.w,0.0);
    if(blurBlend>0.0&&pairs>0&&eyeShellSurface.z>0.0)for(int i=1;i<=32;++i){
        if(i>pairs)break;float inverseWeight=1.0/float(i);weight+=2.0*inverseWeight;
        vec3 a=shellSampleHDR(screenUV+float(i)*stepUV)*inverseWeight;
        vec3 b=shellSampleHDR(screenUV-float(i)*stepUV)*inverseWeight;
        sum+=(length(a)>reject?base:a)+(length(b)>reject?base:b);
    }
    vec3 color=mix(base,sum/weight,blurBlend);
    vec3 ng=shellNormalize(shellNormal,vec3(0.0,0.0,1.0));
    vec3 t=shellNormalize(shellTangent.xyz-ng*dot(ng,shellTangent.xyz),vec3(1.0,0.0,0.0));
    vec3 b=shellNormalize(cross(ng,t)*shellTangent.w,vec3(0.0,1.0,0.0));
    vec3 encoded=textureGrad(tearlineNormal,shellClampUV(tearlineNormal,shellUV),normalDx,normalDy).rgb;
    encoded=mix(vec3(.5,.5,1.0),encoded,eyeShellCavity.z);
    vec3 n=shellNormalize(mat3(t,b,ng)*(encoded*2.0-254.0/255.0),ng);
    vec3 v=shellNormalize(cameraPosition.xyz-shellWorldPosition,ng);
    vec3 specular=vec3(0.0);float roughness=clamp(eyeShellSpecular.x,.045,1.0),f0=clamp(eyeShellSpecular.y,0.0,1.0);
    // Host direct-light GGX adapter. The source additionally gathers scene
    // normals, BRDF LUTs, area/shadow lighting and reflection; those are omitted.
    if(sunDirectionIntensity.w>0.0)specular+=shellSpecular(n,v,-shellNormalize(sunDirectionIntensity.xyz,vec3(0.0,-1.0,0.0)),max(sunColor.rgb,vec3(0.0))*sunDirectionIntensity.w,roughness,f0);
    for(int i=0;i<4;++i){vec3 delta=lightPositions[i].xyz-shellWorldPosition;float distance2=max(dot(delta,delta),max(lightColors[i].w*lightColors[i].w,1e-4));
        specular+=shellSpecular(n,v,shellNormalize(delta,n),max(lightColors[i].rgb,vec3(0.0))/distance2,roughness,f0);}
    float specScale=blurBlend*blurBlend*visibility*min(cavity,clamp(eyeShellSpecular.y*125.0-.25,0.0,1.0))/ShellPi;
    specular*=specScale;
    if(any(greaterThan(specular,vec3(max(eyeShellSpecular.w,0.0))))&& !any(isnan(specular))&&!any(isinf(specular)))color+=specular;
    if(eyeShellFake.x!=0.0){vec2 uv=shellUV/shellDenominator(eyeShellFake.w)+eyeShellFake.yz;
        color+=vec3(textureGrad(fakeSpecular,shellClampUV(fakeSpecular,uv),normalDx/shellDenominator(eyeShellFake.w),normalDy/shellDenominator(eyeShellFake.w)).r*eyeShellFake.x);}
    color*=visibility;
    if(eyeShellDebug.w!=0.0)color=mix(color,n,blurBlend);
    else if(eyeShellDebug.z!=0.0)color=mix(color,vec3(0.0,0.0,1.0),1.0-cavity);
    else if(eyeShellDebug.y!=0.0)color=mix(color,vec3(0.0,1.0,0.0),blurBlend);
    else if(eyeShellDebug.x!=0.0)color=mix(color,vec3(1.0,0.0,0.0),1.0-visibility);
    if(any(isnan(color))||any(isinf(color)))color=vec3(0.0);
    outColor=vec4(clamp(color,vec3(0.0),vec3(60000.0)),original.a);
}
)GLSL";
}
} // namespace Render::PipelineDetail
