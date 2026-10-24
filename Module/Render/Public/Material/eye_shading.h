#pragma once

#include <string>
#include <vector>
#include "Render/Public/Material/material.h"

namespace Render::Material {

// Recovered from Diana's EyeballShader_Regolith_Master, target 2,
// DeferredPreTransform pixel DXIL. See E:/REA/pragmata/diana_full/eye_evidence.json.
// This is a raster material reconstruction, not the game's caustic/RT passes.
// Compact routing/authoring values; the dedicated pass uploads these into its
// own constant buffer. Source MDF names map component-by-component in evidence.
inline void AppendEyeShellMaterialParameters(std::vector<ParameterDesc>& parameters) {
    parameters.insert(parameters.end(), {
        {"eyeShellEnabled", ParameterType::Bool, false, "Eye-shell postprocess routing", "Eye shell"},
        {"eyeShellSurface", ParameterType::Vec4, glm::vec4(.1f,.47f,.86f,8), "Depth window / occlusion / blur / highlight rejection", "Eye shell"},
        {"eyeShellOutlineArea", ParameterType::Vec4, glm::vec4(.84f,.35f,.61f,.71f), "Outline area: top / bottom / inner / outer", "Eye shell"},
        {"eyeShellOutlineSmooth", ParameterType::Vec4, glm::vec4(1.5f,1.92f,1.54f,2.92f), "Outline smooth: top / bottom / inner / outer", "Eye shell"},
        {"eyeShellFillArea", ParameterType::Vec4, glm::vec4(1.1f,1.2f,.75f,1.06f), "Fill area: top / bottom / inner / outer", "Eye shell"},
        {"eyeShellFillSmooth", ParameterType::Vec4, glm::vec4(.9f,1.5f,1.32f,1.42f), "Fill smooth: top / bottom / inner / outer", "Eye shell"},
        {"eyeShellBlurOutlineArea", ParameterType::Vec4, glm::vec4(.8f,.9f,1.14f,.73f), "Blur outline area: top / bottom / inner / outer", "Eye shell"},
        {"eyeShellBlurOutlineSmooth", ParameterType::Vec4, glm::vec4(1,.3f,.91f,2), "Blur outline smooth: top / bottom / inner / outer", "Eye shell"},
        {"eyeShellBlurFillArea", ParameterType::Vec4, glm::vec4(.31f,.2f,.19f,.3f), "Blur fill area: top / bottom / inner / outer", "Eye shell"},
        {"eyeShellBlurFillSmooth", ParameterType::Vec4, glm::vec4(.62f,.3f,1.87f,.9f), "Blur fill smooth: top / bottom / inner / outer", "Eye shell"},
        {"eyeShellSpecular", ParameterType::Vec4, glm::vec4(.01f,.85f,2,1), "Roughness / specular / normal samples / threshold", "Eye shell"},
        {"eyeShellFake", ParameterType::Vec4, glm::vec4(0,-.31f,.03f,1.02f), "Fake intensity / offset XY / size", "Eye shell"},
        {"eyeShellCavity", ParameterType::Vec4, glm::vec4(1,.54f,.2f,1), "Cavity area / smooth / normal / source depth units", "Eye shell"},
        {"eyeShellDebug", ParameterType::Vec4, glm::vec4(0), "Source debug: occlusion / blur / cavity / normal", "Eye shell"}
    });
}

inline void AppendEyeMaterialParameters(std::vector<ParameterDesc>& parameters) {
    parameters.insert(parameters.end(), {
        {"eyeEnabled", ParameterType::Bool, false, "Source eye shading", "Eye"},
        {"eyeRefractionStrength", ParameterType::Float, 1.f, "Iris refraction / displacement", "Eye", 0.f, 2.f, true},
        // Radius, texture start/end radii, rotation in degrees.
        {"eyeIrisShape", ParameterType::Vec4, glm::vec4(.56f,0,.83f,2), "Iris radius / UV range / rotation", "Eye"},
        // Lens-depth zoom, lens distortion, iris depth, POM ray length.
        {"eyeParallax", ParameterType::Vec4, glm::vec4(.13f,0,.4f,.44f), "Lens zoom / distortion / depth / offset", "Eye"},
        // Layer count, displacement height, pupil radius, reserved.
        {"eyeDisplacement", ParameterType::Vec4, glm::vec4(16,.2f,-.05f,0), "Iris layers / height / pupil radius", "Eye"},
        {"eyePupilOffset", ParameterType::Vec2, glm::vec2(-.05f,-.03f), "Pupil center offset", "Eye"},
        // Sclera blend radius, limbus exponent, limbus radius, reserved.
        {"eyeLimbus", ParameterType::Vec4, glm::vec4(.3f,.7f,.3f,0), "Iris edge blend / limbus power / radius", "Eye"},
        {"eyeLimbusColor", ParameterType::Vec4, glm::vec4(21.f/255,21.f/255,21.f/255,1), "Limbus multiplier (source linear)", "Eye", 0.f, 1.f, true},
        {"eyeSpecularReflectance", ParameterType::Vec2, glm::vec2(.03f,.02f), "Lens / sclera normal reflectance", "Eye", 0.f, 1.f, true},
        // Spherical reflection UV offsets, intensity, source illumination cap.
        {"eyeFakeReflection", ParameterType::Vec4, glm::vec4(.23f,-.08f,10,25), "Fake reflection rotation / intensity / illumination cap", "Eye"},
        {"eyeSclera", ParameterType::Vec4, glm::vec4(-.06f,332,.3f,0), "Sclera UV offset / rotation / normal / flip", "Eye"},
        {"eyeRedness", ParameterType::Vec2, glm::vec2(.5f,.8f), "Sclera redness mask / power", "Eye"},
        {"eyeRednessColor", ParameterType::Vec4, glm::vec4(129.f/255,43.f/255,43.f/255,1), "Sclera redness source color", "Eye", 0.f, 1.f, true},
        // Off by default: source multiplies by a runtime BAB RGB light value,
        // which has not been captured. The host may supply a documented proxy.
        {"eyeReflectionIllumination", ParameterType::Vec4, glm::vec4(0), "Reflection illumination proxy (linear lux)", "Eye", 0.f, 60000.f, true}
    });
    AppendEyeShellMaterialParameters(parameters);
}

// Contract: uniforms above + existing ior. samplers remain explicit arguments.
// Packed depth: R=IrisDispMap.r, G=LensDepthMap.r, BA=source vein normal XY.
// Sclera atlas is linear RGB with fixed MDF layers baked in UV1 space.
// Call EyePrepareCoordinates BEFORE alpha/discard; it captures gradients before
// the bounded POM loop. Both color and normal must use result.irisUV.
inline std::string EyeShadingGLSL() {
    return R"GLSL(
// The host's legacy textures use Repeat. Keep these eye samples inside the
// base-level edge texels to avoid wrap bleeding at the source Clamp boundary.
// Coarse-mip edge behavior is an approximation until per-texture samplers exist.
vec2 EyeClampUV(sampler2D eyeMap,vec2 uv) {
    vec2 halfTexel=.5/vec2(max(textureSize(eyeMap,0),ivec2(1)));
    return clamp(uv,halfTexel,vec2(1.0)-halfTexel);
}
struct EyeCoordinates {
    vec2 irisUV;
    vec2 baseUV;
    float radius;
    float scleraWeight;
};
vec3 EyeSafeNormalize(vec3 n) {
    float lengthSquared=dot(n,n);
    return lengthSquared>1e-12?n*inversesqrt(lengthSquared):vec3(0,0,1);
}
vec2 EyePupilWarp(vec2 q) {
    // DXIL %540..586: radial pupil dilation plus asymmetric pupil offsets.
    vec2 centered=q-eyePupilOffset;
    float radius=length(centered);
    vec2 axis=radius>1e-6?centered/radius:vec2(0);
    float zoom=eyeParallax.x;
    float removed=min(eyeDisplacement.z*(1.0+zoom),radius);
    vec2 warped=(centered-removed*axis)/max(1.0+zoom-removed,1e-4);
    vec2 offset=eyePupilOffset/max(1.0-removed,1e-4);
    return warped/max(vec2(1.0)-sign(warped)*offset,vec2(1e-4));
}
vec2 EyeIrisUV(vec2 q) {
    vec2 p=EyePupilWarp(q);
    float angle=radians(eyeIrisShape.w), c=cos(angle), s=sin(angle);
    p=vec2(c*p.x-s*p.y,s*p.x+c*p.y);
    p*=eyeIrisShape.z-eyeIrisShape.y;
    float radius=length(p);
    if(radius>1e-6)p+=p*(eyeIrisShape.y/radius);
    return clamp(p*.5+.5,vec2(0),vec2(1));
}
EyeCoordinates EyePrepareCoordinates(vec2 uv,vec3 viewTS,sampler2D depthMap) {
    EyeCoordinates result;
    result.baseUV=uv;
    result.radius=length((2.0*uv-1.0)/max(eyeIrisShape.x,1e-4));
    result.scleraWeight=1.0;
    result.irisUV=uv;
    if(eyeEnabled==0u)return result;
    vec2 p=2.0*uv-1.0;
    vec2 q=p/max(eyeIrisShape.x,1e-4);
    vec2 lensUV=clamp(p*(1.0-eyeParallax.x)*.5+.5,vec2(0),vec2(1));
    vec2 uvDx=dFdx(uv),uvDy=dFdy(uv);
    float lens=textureGrad(depthMap,EyeClampUV(depthMap,lensUV),uvDx,uvDy).g;
    if(result.radius<1.0) {
        // Source view ray is surface-to-camera. Its coordinate convention has
        // the bitangent component negated. This is the source artist-controlled
        // angular lens approximation; IOR is used separately for specular.
        vec2 ray=vec2(viewTS.x,-viewTS.y);
        float strength=clamp(eyeRefractionStrength,0.0,2.0);
        float edge=1.0-result.radius;
        q-=strength*dot(ray,ray)*ray*(lens*eyeParallax.y+edge*edge*eyeParallax.z);
        int layers=clamp(int(round(eyeDisplacement.x)),1,64);
        float stepDepth=1.0/float(layers);
        vec2 rayStep=strength*lens*eyeParallax.w*ray*stepDepth;
        float depth=1.0,previousHeight=0.0;
        // Source %535..623, including linear intersection refinement.
        // 64 is a host safety cap; Diana's source uses 16 layers.
        for(int layer=0;layer<64;++layer) {
            vec2 sampleUV=EyeIrisUV(q);
            float height=textureGrad(depthMap,EyeClampUV(depthMap,sampleUV),uvDx,uvDy).r*max(eyeDisplacement.y,0.0);
            depth-=stepDepth;
            q-=rayStep;
            if(depth<=height||layer+1>=layers) {
                float fraction=clamp((height-depth)/max(stepDepth-previousHeight+height,1e-6),0.0,1.0);
                q+=fraction*rayStep;
                break;
            }
            previousHeight=height;
        }
        result.irisUV=EyeIrisUV(q);
        float blend=clamp(edge/max(eyeLimbus.x,1e-4),0.0,1.0);
        result.scleraWeight=.5+.5*sin((1.0-blend)*3.141592653589793-1.570796326794897);
    }
    return result;
}
vec3 EyeMixAlbedo(vec3 irisLinear,vec3 scleraLinear,EyeCoordinates coordinates) {
    float redness=clamp((length(2.0*coordinates.baseUV-1.0)+eyeRedness.x-1.0)/max(eyeRedness.x,1e-4),0.0,1.0);
    scleraLinear*=clamp(vec3(1.0)+redness*eyeRedness.y*(eyeRednessColor.rgb-1.0),vec3(0),vec3(1));
    float edge=clamp((1.0-coordinates.radius)/max(eyeLimbus.z,1e-4),0.0,1.0);
    vec3 limbus=mix(eyeLimbusColor.rgb,vec3(1),pow(edge,max(eyeLimbus.y,.001)));
    // Dynamic light-dependent LimbusShadow=.2 is not included here; the
    // source searches local lights before selecting that edge width.
    return mix(irisLinear*limbus,scleraLinear,coordinates.scleraWeight);
}
vec3 EyeTangentNormal(vec3 irisEncoded,sampler2D packedDepth,sampler2D scleraMap,vec2 scleraUV,EyeCoordinates coordinates,float strength) {
    vec3 iris=EyeSafeNormalize(irisEncoded*2.0-1.0);
    float angle=radians(eyeIrisShape.w),c=cos(angle),s=sin(angle);
    float x=c*iris.x-s*iris.y;
    // Preserve the observed DXIL sequence (the second component uses new x).
    iris=EyeSafeNormalize(vec3(x,s*x+c*iris.y,iris.z));
    iris=EyeSafeNormalize(mix(vec3(0,0,1),iris,strength));
    vec2 veinUV=vec2(fract(scleraUV.x+eyeSclera.y/360.0),scleraUV.y+eyeSclera.x);
    vec2 raw=texture(packedDepth,EyeClampUV(packedDepth,veinUV)).ba*2.0-1.0;
    float z=texture(scleraMap,EyeClampUV(scleraMap,veinUV)).a*2.0-1.0;
    float veinAngle=-scleraUV.x*6.283185307179586;
    float vc=cos(veinAngle),vs=sin(veinAngle);
    float vx=raw.x*vc-raw.y*vs;
    vec3 sclera=EyeSafeNormalize(vec3(vx,vx*vs+raw.y*vc,z));
    if(eyeSclera.w!=0.0)sclera.x=-sclera.x;
    sclera=EyeSafeNormalize(mix(vec3(0,0,1),sclera,eyeSclera.z));
    // Source normal transition uses saturate(4*scleraWeight), sharper than color.
    return EyeSafeNormalize(mix(iris,sclera,clamp(4.0*coordinates.scleraWeight,0.0,1.0)));
}
vec3 EyeDielectricF0(EyeCoordinates coordinates) {
    float index=clamp(ior,1.0,2.5);
    float reflectance=(index-1.0)/(index+1.0);
    reflectance*=reflectance;
    // Renderer adaptation: retain authored .03/.02 at source IOR=1.4 while
    // allowing physically finite IOR edits. Source stores F0 independently.
    float authored=mix(eyeSpecularReflectance.x,eyeSpecularReflectance.y,coordinates.scleraWeight);
    return vec3(clamp(authored*reflectance/(1.0/36.0),0.0,1.0));
}
vec3 EyeFakeReflection(sampler2D reflectionMap,vec3 reflectionInViewSpace) {
    vec3 r=EyeSafeNormalize(reflectionInViewSpace);
    vec2 uv=vec2(fract(atan(r.z,r.x)/6.283185307179586+.5-eyeFakeReflection.x),
        acos(clamp(r.y,-1.0,1.0))/3.141592653589793-eyeFakeReflection.y);
    vec3 illumination=clamp(eyeReflectionIllumination.rgb/max(eyeFakeReflection.w,1e-4),vec3(0),vec3(1));
    // Original FakeRelection_Map is BC1 sRGB (DXGI72). Host binds linear
    // UNORM PNGs, therefore decode explicitly just as albedo does.
    vec3 encoded=max(texture(reflectionMap,EyeClampUV(reflectionMap,uv)).rgb,vec3(0));
    vec3 color=mix(pow((encoded+.055)/1.055,vec3(2.4)),encoded/12.92,
        lessThanEqual(encoded,vec3(.04045)));
    return color*max(eyeFakeReflection.z,0.0)*illumination;
}
)GLSL";
}

} // namespace Render::Material
