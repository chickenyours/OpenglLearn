#pragma once
#include <string>

namespace Render::SurfaceCacheDetail {
// Metadata.w is an exact finite integer: accepted mask[3:0], readable[4],
// UE four-bit FastUpdate[8:5], confidence UNORM15[23:9]. GI.a keeps float count.
inline std::string UeLumenHighTemporalMetadataGLSL(){return R"GLSL(
bool UHMetadataValid(float value){
    return !isnan(value)&&!isinf(value)&&value>=0.0&&value<=16777215.0&&value==floor(value);
}
uint UHMetadataBits(float value){return UHMetadataValid(value)?uint(value):0u;}
uint UHMetadataAccepted(float value){return UHMetadataBits(value)&15u;}
bool UHMetadataReadable(float value){return (UHMetadataBits(value)&16u)!=0u;}
float UHMetadataFast(float value){return float((UHMetadataBits(value)>>5u)&15u)/15.0;}
float UHMetadataConfidence(float value){return float((UHMetadataBits(value)>>9u)&32767u)/32767.0;}
float UHPackMetadata(uint accepted,bool readable,float fast,float confidence){
    if(isnan(fast)||isinf(fast))fast=0.0;
    // UE LumenScreenProbeGatherTemporal.usf:354-355/367-369. Only exact zero
    // encodes as not moving. Four-bit fast, without quantizing GI frame count.
    uint quantized=uint(ceil(clamp(fast,0.0,1.0)*15.0));
    // Preserve public current-confidence magnitude to UNORM15 precision.
    // ceil keeps every positive source readable. All 24 carrier bits are exact
    // in RGBA32F; decode uint(value), never round via value+.5 near 2^24.
    readable=readable&&!isnan(confidence)&&!isinf(confidence)&&confidence>0.0;
    uint confidenceBits=readable?uint(ceil(clamp(confidence,0.0,1.0)*32767.0)):0u;
    return float((accepted&15u)|(readable?16u:0u)|(quantized<<5u)|(confidenceBits<<9u));
}
)GLSL";}
// Source: UE_5.7 Engine/Shaders/Private/Lumen/LumenMaterial.ush:87-99,
// StochasticLighting/StochasticLightingUpsample.ush:16-71, and
// StochasticLightingTileClassification.usf:147-152,278-380,704-711.
// declarations=false emits only the shared UBO/functions. The consumer must
// supply samplers/aliases uhPositions,uhNormals,uhBlueNoiseScalar,uhBlueNoiseVec2.
inline std::string UeLumenHighSamplingGLSL(bool declarations=true,unsigned scalarSlot=6,unsigned vec2Slot=7) {
    std::string s;
    if(declarations) s=
        "layout(binding=2) uniform sampler2D uhPositions;\n"
        "layout(binding=3) uniform sampler2D uhNormals;\n"
        "layout(binding="+std::to_string(scalarSlot)+") uniform sampler2D uhBlueNoiseScalar;\n"
        "layout(binding="+std::to_string(vec2Slot)+") uniform sampler2D uhBlueNoiseVec2;\n";
    return s+R"GLSL(
layout(std140,binding=14) uniform UeLumenHighSamplingData {
    mat4 uhInverseView;
    mat4 uhInverseProjection;
    mat4 uhView;
    ivec4 uhExtent; // full width/height, ceil(full/2) width/height
    uvec4 uhFrame;  // actual GI/AO sampling sequence, committed history valid
};
const float UHPI=3.14159265358979323846;
bool UHFinite(float x){return !isnan(x)&&!isinf(x);}
bool UHFinite3(vec3 x){return !any(isnan(x))&&!any(isinf(x));}
// FastMathThirdParty.ush:108-114 (the same approximation used by UE AO).
float UHAcosFast(float inputValue){
    float x=clamp(abs(inputValue),0.0,1.0);
    float result=(-.156583*x+.5*UHPI)*sqrt(1.0-x);
    return inputValue>=0.0?result:UHPI-result;
}
ivec2 UHDownsampleJitter(ivec2 halfPixel){
    uvec2 cell=uvec2(halfPixel)&uvec2(1);
    uint linear=(cell.x+2u*cell.y+uhFrame.x)&3u;
    return ivec2((linear&2u)!=0u?1:0,(linear&1u)!=0u?0:1);
}
ivec2 UHSourceFullResPixel(ivec2 halfPixel){
    return clamp(halfPixel*2+UHDownsampleJitter(halfPixel),ivec2(0),uhExtent.xy-1);
}
ivec2 UHBlueNoiseCoordinate(ivec2 pixel){
    uvec3 wrapped=uvec3(uvec2(pixel),uhFrame.x)&uvec3(127u,127u,63u);
    return ivec2(wrapped.x,wrapped.z*128u+wrapped.y);
}
float UHBlueNoiseScalar(ivec2 fullPixel){
    return texelFetch(uhBlueNoiseScalar,UHBlueNoiseCoordinate(fullPixel),0).x;
}
vec2 UHBlueNoiseVec2(ivec2 fullPixel){
    return texelFetch(uhBlueNoiseVec2,UHBlueNoiseCoordinate(fullPixel),0).xy;
}
bool UHGeometry(ivec2 pixel,out vec3 position,out vec3 worldNormal){
    vec4 p=texelFetch(uhPositions,pixel,0),n=texelFetch(uhNormals,pixel,0);
    position=p.xyz;worldNormal=n.xyz;
    if(p.z>=0.0||n.w<.5||!UHFinite3(p.xyz)||!UHFinite3(n.xyz)||dot(n.xyz,n.xyz)<.5)return false;
    worldNormal=normalize(worldNormal);return true;
}
vec4 UHUpsampleWeights(ivec2 fullPixel){
    vec3 p,n;
    if(!UHGeometry(fullPixel,p,n))return vec4(0);
    vec3 nv=normalize(mat3(uhView)*n);
    ivec2 base=ivec2(floor((vec2(fullPixel)+.5)*.5-.5));
    ivec2 screenOffset=fullPixel-base*2;
    const ivec2 offsets[4]=ivec2[4](ivec2(0),ivec2(1,0),ivec2(0,1),ivec2(1));
    vec4 weights=vec4(0);
    for(int i=0;i<4;++i){
        ivec2 corner=base+offsets[i];
        if(any(lessThan(corner,ivec2(0)))||any(greaterThanEqual(corner,uhExtent.zw)))continue;
        vec3 q,qn;if(!UHGeometry(UHSourceFullResPixel(corner),q,qn))continue;
        ivec2 sampleOffset=UHDownsampleJitter(corner)+offsets[i]*2-screenOffset;
        vec2 triangle=vec2(2)-abs(vec2(sampleOffset));
        float relativePlane=abs(dot(q-p,nv))/max(-p.z,1e-20);
        float depthWeight=exp2(-5000.0*relativePlane*relativePlane);
        float normalWeight=1.0-clamp(UHAcosFast(clamp(dot(qn,n),0.0,1.0)),0.0,1.0);
        weights[i]=triangle.x*triangle.y*depthWeight*normalWeight*normalWeight;
    }
    return weights;
}
// Expected reconstruction for a display chain without the final TAA/TSR
// that normally resolves UE's stochastic corner selection. Keep the original
// UE triangle/plane/normal weights and support threshold. The base is exactly
// the one used by UHUpsampleWeights, including the first row/column; do not use
// the truncating (fullPixel-1)/2 expression for these four corner addresses.
bool UHReconstructionWeights(ivec2 fullPixel,out ivec2 base,out vec4 normalizedWeights){
    base=ivec2(floor((vec2(fullPixel)+.5)*.5-.5));
    vec4 weights=UHUpsampleWeights(fullPixel);
    float mass=dot(weights,vec4(1));
    normalizedWeights=mass>0.0?weights/mass:vec4(0);
    return mass>=.01;
}
bool UHStochasticUpsample(ivec2 fullPixel,out ivec2 halfPixel){
    vec4 weights=UHUpsampleWeights(fullPixel);
    vec4 prefix=vec4(weights.x,weights.x+weights.y,weights.x+weights.y+weights.z,dot(weights,vec4(1)));
    prefix/=max(prefix.w,.00001);
    float randomValue=UHBlueNoiseScalar(fullPixel);
    ivec2 offset=randomValue<=prefix.x?ivec2(0):randomValue<=prefix.y?ivec2(1,0):
        randomValue<=prefix.z?ivec2(0,1):ivec2(1);
    // Match the integer expression in LumenScreenProbeGatherTemporal.usf:469,
    // including its clamped edge behavior; GLSL integer division truncates.
    halfPixel=clamp((fullPixel-1)/2+offset,ivec2(0),uhExtent.zw-1);
    return dot(weights,vec4(1))>=.01;
}
)GLSL";
}

// FurthestHZB mip0 is the 2x2 furthest device-depth reduction. The project
// stores positive linear view distance instead: max is the equivalent operator.
// Missing/background contributes far depth, exactly the non-occluding horizon.
inline std::string UeLumenHighFurthestHzbShader(){
    return std::string("#version 450 core\nin vec2 vUV;layout(location=0) out vec4 outDepth;\n")+
        UeLumenHighSamplingGLSL()+R"GLSL(
void main(){
    ivec2 base=ivec2(gl_FragCoord.xy)*2;
    float furthest=0.0;
    for(int y=0;y<2;++y)for(int x=0;x<2;++x){
        ivec2 pixel=clamp(base+ivec2(x,y),ivec2(0),uhExtent.xy-1);
        vec3 p,n;float depth=UHGeometry(pixel,p,n)?-p.z:1e20;
        furthest=max(furthest,depth);
    }
    outDepth=vec4(furthest,0,0,1);
}
)GLSL";
}

// High: scalar horizon search, 2 slices * 3 steps * both directions.
// LumenScreenSpaceBentNormal.usf:131-139,211-273,280-419.
// LumenScreenSpaceBentNormal.cpp:104-133 and BaseScalability.ini High overrides.
// The 64px extent is Gather.cpp:2620 (High probe downsample32 * 2).
inline std::string UeLumenHighShortRangeAOShader(){
    return std::string("#version 450 core\nin vec2 vUV;layout(location=0) out float outAO;\n")+
        UeLumenHighSamplingGLSL()+R"GLSL(
layout(binding=0) uniform sampler2D uhFurthestHzb;
vec3 UHViewPosition(vec2 uv,float positiveDepth){
    vec4 ray=uhInverseProjection*vec4(uv*2.0-1.0,1,1);
    vec3 p=ray.xyz/ray.w;return p*(positiveDepth/max(-p.z,1e-20));
}
void UHUpdateHorizon(vec2 sampleNdc,vec3 center,vec3 viewVector,
    float inverseForeground,float lowHorizon,inout float horizon){
    if(any(lessThanEqual(sampleNdc,vec2(-1)))||any(greaterThanEqual(sampleNdc,vec2(1))))return;
    vec2 uv=sampleNdc*.5+.5;
    ivec2 hz=clamp(ivec2(uv*vec2(uhExtent.zw)),ivec2(0),uhExtent.zw-1);
    float depth=texelFetch(uhFurthestHzb,hz,0).x;
    vec3 delta=UHViewPosition(uv,depth)-center;
    float distanceToSample=length(delta);
    if(!(distanceToSample>0.0)||!UHFinite(distanceToSample))return;
    float newHorizon=dot(delta/distanceToSample,viewVector);
    float reject=clamp(pow(abs(delta.z)*inverseForeground,1.5),0.0,1.0);
    horizon=max(horizon,mix(newHorizon,lowHorizon,reject));
}
void main(){
    ivec2 halfPixel=ivec2(gl_FragCoord.xy),pixel=UHSourceFullResPixel(halfPixel);
    vec3 p,n;outAO=1.0;if(!UHGeometry(pixel,p,n))return;
    vec2 screenNdc=(vec2(pixel)+.5)/vec2(uhExtent.xy)*2.0-1.0;
    vec3 center=UHViewPosition((vec2(pixel)+.5)/vec2(uhExtent.xy),-p.z);
    vec3 viewVector=-normalize(center),viewNormal=normalize(mat3(uhView)*n);
    vec2 randomValue=UHBlueNoiseVec2(pixel);
    float visibility=0.0,inverseForeground=1.0/(-p.z*.3);
    for(int slice=0;slice<2;++slice){
        float phi=(float(slice)+randomValue.x)*.5*UHPI;
        vec3 direction=vec3(cos(phi),sin(phi),0);
        vec3 orthogonal=direction-dot(direction,viewVector)*viewVector;
        vec3 axis=cross(orthogonal,viewVector);
        float axisLength=length(axis);if(axisLength<=1e-20){visibility+=1.0;continue;}
        axis/=axisLength;
        vec3 projectedNormal=viewNormal-axis*dot(viewNormal,axis);
        float projectedLength=length(projectedNormal);
        if(projectedLength<=1e-20)continue;
        float cosNormal=clamp(dot(projectedNormal,viewVector)/projectedLength,0.0,1.0);
        float normalAngle=sign(dot(orthogonal,projectedNormal))*UHAcosFast(cosNormal);
        float low0=cos(normalAngle+.5*UHPI),low1=cos(normalAngle-.5*UHPI);
        float horizon0=low0,horizon1=low1;
        for(int step=0;step<3;++step){
            float noise=fract(randomValue.y+float(slice+step*3)*.6180339887498948482);
            float fraction=(float(step)+noise)/3.0;
            vec2 radius=fraction*fraction*64.0/vec2(uhExtent.xy);
            vec2 offset=2.0*(radius+1.0/vec2(uhExtent.xy))*direction.xy;
            UHUpdateHorizon(screenNdc+offset,center,viewVector,inverseForeground,low0,horizon0);
            UHUpdateHorizon(screenNdc-offset,center,viewVector,inverseForeground,low1,horizon1);
        }
        float h0=-UHAcosFast(horizon1),h1=UHAcosFast(horizon0);
        float arc0=(cosNormal+2.0*h0*sin(normalAngle)-cos(2.0*h0-normalAngle))*.25;
        float arc1=(cosNormal+2.0*h1*sin(normalAngle)-cos(2.0*h1-normalAngle))*.25;
        visibility+=projectedLength*(arc0+arc1);
    }
    outAO=clamp(visibility*.5,.03,1.0);
}
)GLSL";
}

// AO is temporally accumulated separately from GI. The shared metadata comes
// from the *actual* GI temporal decision, not an independently invented EMA:
// xy=continuous previous FULL history pixel, z=actual current Alpha,
// w packs accepted corner mask, current readability and UE four-bit FastUpdate.
// Invalid current has w=0/z=1; history rejection retains current bits with mask0.
// UE GatherTemporal.usf:297-346,609-630; history scalar storage PF_R8.
inline std::string UeLumenHighShortRangeAOTemporalShader(){
    return std::string("#version 450 core\nin vec2 vUV;layout(location=0) out float outAO;\n")+
        UeLumenHighSamplingGLSL()+UeLumenHighTemporalMetadataGLSL()+R"GLSL(
layout(binding=0) uniform sampler2D uhRawAO;
layout(binding=1) uniform sampler2D uhAOHistory;
layout(binding=4) uniform sampler2D uhGITemporalMetadata;
void main(){
    ivec2 pixel=ivec2(gl_FragCoord.xy),currentBase;
    vec3 p,n;outAO=1.0;if(!UHGeometry(pixel,p,n))return;
    vec4 reconstructionWeights;
    // GI consumes this same support decision in its Alpha/accepted history.
    // AO uses the expectation of those same valid corners, rather than adding
    // another stochastic display sample that requires a following TAA pass.
    UHReconstructionWeights(pixel,currentBase,reconstructionWeights);
    float currentAO=0.0,first=0.0,second=0.0,neighborhoodMass=0.0;
    // Mix the original radius-one (3x3) moments around each reconstruction
    // corner. Their 4x4 union reads each AO texel once; the four current values
    // are reused here. Sigma scale remains the original UE value of one.
    // Each union row has only three distinct corner-support combinations.
    // Keep corner additions in the original i=0,1,2,3 order, including the
    // four-corner center sum; no per-texel corner tests or dynamic corner load.
    float topMiddle=reconstructionWeights.x+reconstructionWeights.y;
    float bottomMiddle=reconstructionWeights.z+reconstructionWeights.w;
    float centerLeft=reconstructionWeights.x+reconstructionWeights.z;
    float centerMiddle=(topMiddle+reconstructionWeights.z)+reconstructionWeights.w;
    float centerRight=reconstructionWeights.y+reconstructionWeights.w;
    vec4 topRow=vec4(reconstructionWeights.x,topMiddle,topMiddle,reconstructionWeights.y);
    vec4 centerRow=vec4(centerLeft,centerMiddle,centerMiddle,centerRight);
    vec4 bottomRow=vec4(reconstructionWeights.z,bottomMiddle,bottomMiddle,reconstructionWeights.w);
    for(int y=-1;y<=2;++y){
        vec4 momentRow=y<0?topRow:(y>1?bottomRow:centerRow);
        for(int x=-1;x<=2;++x){
            ivec2 offset=ivec2(x,y);
            float value=texelFetch(uhRawAO,clamp(currentBase+offset,ivec2(0),uhExtent.zw-1),0).x;
            float momentWeight=momentRow[x+1];
            first+=value*momentWeight;second+=value*value*momentWeight;
            neighborhoodMass+=momentWeight;
            if(x>=0&&x<=1&&y>=0&&y<=1)currentAO+=value*reconstructionWeights[x+2*y];
        }
    }
    if(neighborhoodMass>0.0){first/=neighborhoodMass;second/=neighborhoodMass;}
    else {currentAO=1.0;first=1.0;second=1.0;}
    outAO=currentAO;
    vec4 metadata=texelFetch(uhGITemporalMetadata,pixel,0);
    if(uhFrame.y==0u||any(isnan(metadata))||any(isinf(metadata)))return;
    uint accepted=UHMetadataAccepted(metadata.w);
    if(accepted==0u)return;
    ivec2 base=ivec2(floor(metadata.xy));vec2 f=fract(metadata.xy);
    vec4 weights=vec4((1.0-f.x)*(1.0-f.y),f.x*(1.0-f.y),(1.0-f.x)*f.y,f.x*f.y);
    const ivec2 offsets[4]=ivec2[4](ivec2(0),ivec2(1,0),ivec2(0,1),ivec2(1));
    float history=0.0,mass=0.0;
    for(int i=0;i<4;++i){
        if((accepted&(1u<<uint(i)))==0u)continue;
        ivec2 q=base+offsets[i];
        if(any(lessThan(q,ivec2(0)))||any(greaterThanEqual(q,uhExtent.xy)))continue;
        float value=texelFetch(uhAOHistory,q,0).x;if(!UHFinite(value))continue;
        history+=value*weights[i];mass+=weights[i];
    }
    if(mass<=0.0)return;
    history/=mass;
    float sigma=sqrt(max(second-first*first,0.0)); // UE NeighborhoodClampScale=1.
    history=clamp(history,first-sigma,first+sigma);
    outAO=mix(history,currentAO,clamp(metadata.z,0.0,1.0));
}
)GLSL";
}

// DeferredShadingCommon.ush:1270-1275 and DiffuseIndirectComposite.usf:187.
// Input GI history is unmodulated E/pi, with A=GI frame count. Apply rho and AO
// only at final composite; coverage comes from the metadata confidence field, never the count.
// AO never contaminates the RGB GI history or its sample count/readability.
inline std::string UeLumenHighShortRangeAOCompositeShader(){
    return std::string("#version 450 core\n")+UeLumenHighTemporalMetadataGLSL()+R"GLSL(
in vec2 vUV;layout(location=0) out vec4 outGI;
layout(binding=0) uniform sampler2D uhTemporalGI;
layout(binding=1) uniform sampler2D uhTemporalAO;
layout(binding=2) uniform sampler2D uhDiffuseColor;
layout(binding=3) uniform sampler2D uhGIHistoryAux;
vec3 UHAOMultiBounce(vec3 albedo,float ao){
    vec3 a=2.0404*albedo-.3324,b=-4.7951*albedo+.6417,c=2.7552*albedo+.6903;
    return max(vec3(ao),((ao*a+b)*ao+c)*ao);
}
void main(){
    ivec2 pixel=ivec2(gl_FragCoord.xy);
    vec4 gi=texelFetch(uhTemporalGI,pixel,0);float ao=texelFetch(uhTemporalAO,pixel,0).x;
    vec3 diffuseColor=max(texelFetch(uhDiffuseColor,pixel,0).rgb,vec3(0));
    vec3 albedo=min(diffuseColor,vec3(.5));
    float coverage=UHMetadataConfidence(texelFetch(uhGIHistoryAux,pixel,0).w);
    outGI=vec4(gi.rgb*diffuseColor*UHAOMultiBounce(albedo,ao),coverage);
}
)GLSL";
}
} // namespace Render::SurfaceCacheDetail
