#pragma once
#include <string>

namespace Render::ScreenProbePlacementDetail {
inline std::string Constants() { return R"GLSL(
layout(std140,binding=9) uniform PlacementData {
    mat4 spInverseView; mat4 spInverseProjection; mat4 spViewProjection; mat4 spView;
    ivec4 spGrid; ivec4 spScreen; vec4 spOptions;
    ivec4 spTracing; vec4 spTraceParams; ivec4 spTraceAtlas; ivec4 spTraceDisplay;
};
)GLSL"; }
// Placement and final reconstruction must use exactly the same jittered tile
// stencil and surface-validity test. Otherwise the allocator can declare a
// curved surface covered by a probe the final lighting pass must reject.
inline std::string SamplingGLSL() {return R"GLSL(
vec2 SPHammersley(uint index,uint count,uvec2 seed) {
    return vec2(fract(float(index)/float(count)+float(seed.x)/65536.0),
        float((bitfieldReverse(index)>>16)^seed.y)/65536.0);
}
uvec2 SPSeed(ivec2 tile) {
    uvec3 v=uvec3(tile,spScreen.z)*1664525u+1013904223u;
    v.x+=v.y*v.z;v.y+=v.z*v.x;v.z+=v.x*v.y;
    v.x+=v.y*v.z;v.y+=v.z*v.x;v.z+=v.x*v.y;
    return (v>>16).xy;
}
ivec2 SPJitter(){return ivec2(SPHammersley(uint(spScreen.z),8u,uvec2(0))*float(spGrid.w));}
ivec2 SPUniformPixel(ivec2 tile){return min(tile*spGrid.w+SPJitter(),spScreen.xy-1);}
ivec2 SPCandidatePixel(ivec2 tile,int sampleIndex) {
    return tile*spGrid.w+SPJitter()+ivec2(clamp(
        SPHammersley(uint(sampleIndex),uint(spGrid.z),SPSeed(tile))*float(spGrid.w),
        vec2(0),vec2(spGrid.w-1)));
}
ivec2 SPSlot(ivec2 tile,int sampleIndex){return ivec2(tile.x*spGrid.z+sampleIndex,tile.y);}
ivec2 SPCornerBase(ivec2 pixel) {
    ivec2 full=clamp(pixel-SPJitter(),ivec2(0),spScreen.xy-1);
    return min(full/spGrid.w,max(spGrid.xy-2,ivec2(0)));
}
ivec2 SPCorner(ivec2 base,int corner){return min(base+ivec2(corner%2,corner/2),spGrid.xy-1);}
vec2 SPBilinearFractions(ivec2 pixel,ivec2 base) {
    ivec2 full=clamp(pixel-SPJitter(),ivec2(0),spScreen.xy-1);
    vec2 f=vec2(full-base*spGrid.w+1)/float(spGrid.w+2);
    return (spTracing.w&2)!=0?clamp(f,vec2(0),vec2(1)):f;
}
float SPGiPlaneWeight(vec3 p,vec3 n,float depth,vec4 probe,vec4 normalDepth) {
    if(probe.w<.5||normalDepth.w<=0||dot(normalDepth.xyz,normalDepth.xyz)<.5)return 0;
    if(any(isnan(probe.xyz))||any(isinf(probe.xyz))||
        any(isnan(normalDepth.xyz))||any(isinf(normalDepth.xyz)))return 0;
    vec3 pn=normalize(normalDepth.xyz);float alignment=dot(n,pn);if(alignment<=.6)return 0;
    vec3 delta=probe.xyz-p;
    float plane=max(abs(dot(delta,n)),abs(dot(delta,pn)));
    float relative=plane/max(min(depth,normalDepth.w),.0001);
    // Values beneath the allocation threshold are missing coverage, not a
    // tiny confidence to normalize back into a full-strength light sample.
    if(relative>spOptions.x)return 0;
    float weight=exp2(-6.643856189774725*relative/spOptions.x)*pow(alignment,8.0);
    return weight>=.01?weight:0;
}
)GLSL";}
inline std::string Common() { return std::string("#version 450 core\n")+Constants()+SamplingGLSL()+R"GLSL(
in vec2 vUV;
layout(location=0) out vec4 outColor;
layout(binding=0) uniform sampler2D spInput;
layout(binding=1) uniform sampler2D spNormals;
layout(binding=2) uniform sampler2D spDepth;
layout(binding=3) uniform sampler2D spGeometry;
layout(binding=4) uniform sampler2D spUniform;
layout(binding=5) uniform sampler2D spMarked;
layout(binding=6) uniform sampler2D spSpawned;
const float SP_MIN_WEIGHT=.01;
bool SPFinite(vec3 p){return !any(isnan(p))&&!any(isinf(p));}
vec4 SPPosition(sampler2D atlas,ivec2 slot){return texelFetch(atlas,ivec2(slot.x*2,slot.y),0);}
vec4 SPNormalDepth(sampler2D atlas,ivec2 slot){return texelFetch(atlas,ivec2(slot.x*2+1,slot.y),0);}
bool SPSurface(ivec2 pixel,out vec3 p,out vec3 n,out float depth) {
    p=vec3(0);n=vec3(0);depth=0;
    if(any(lessThan(pixel,ivec2(0)))||any(greaterThanEqual(pixel,spScreen.xy)))return false;
    vec4 geometry=texelFetch(spGeometry,pixel,0),normal=texelFetch(spNormals,pixel,0);
    if(geometry.w<.5||normal.w<.5||!SPFinite(geometry.xyz)||!SPFinite(normal.xyz)||dot(normal.xyz,normal.xyz)<.5)return false;
    p=(spInverseView*vec4(geometry.xyz,1)).xyz;n=normalize(normal.xyz);depth=max(abs(geometry.z),.0001);
    return SPFinite(p);
}
float SPPlaneWeight(vec3 p,vec3 n,float depth,vec4 probe,vec4 normalDepth) {
    if((spTracing.w&2)!=0)return SPGiPlaneWeight(p,n,depth,probe,normalDepth);
    if(probe.w<.5)return 0;
    return exp2(log2(SP_MIN_WEIGHT)*abs(dot(probe.xyz-p,n))/max(depth,.0001)/spOptions.x);
}
vec4 SPUniformWeights(ivec2 pixel,vec3 p,vec3 n,float depth) {
    ivec2 base=SPCornerBase(pixel);vec2 f=SPBilinearFractions(pixel,base);vec4 weights=vec4(0);
    for(int c=0;c<4;++c){float b=(c%2==0?1-f.x:f.x)*(c/2==0?1-f.y:f.y);
        ivec2 corner=SPCorner(base,c);
        vec4 nd=(spTracing.w&2)!=0?SPNormalDepth(spUniform,corner):vec4(0);
        weights[c]=b*SPPlaneWeight(p,n,depth,SPPosition(spUniform,corner),nd);}
    return weights;
}
float SPUniformCoverage(ivec2 pixel,vec3 p,vec3 n,float depth){return dot(SPUniformWeights(pixel,p,n,depth),vec4(1));}
float SPAdaptiveWeight(ivec2 pixel,vec3 p,vec3 n,float depth,ivec2 probePixel,vec4 probe,vec4 normalDepth) {
    vec2 delta=abs(vec2(probePixel-pixel));
    float support=(spTracing.w&2)!=0?max(delta.x,delta.y):min(delta.x,delta.y);
    return SPPlaneWeight(p,n,depth,probe,normalDepth)*(1-clamp(support/float(spGrid.w),0,1));
}
)GLSL"; }
inline std::string UniformShader(){return Common()+R"GLSL(
void main(){ivec2 texel=ivec2(gl_FragCoord.xy),tile=ivec2(texel.x/2,texel.y);
    vec3 p,n;float depth;bool valid=SPSurface(SPUniformPixel(tile),p,n,depth);
    outColor=!valid?vec4(0):(texel.x%2==0?vec4(p,1):vec4(n,depth));}
)GLSL";}
inline std::string MarkShader(){return Common()+R"GLSL(
void main(){ivec2 texel=ivec2(gl_FragCoord.xy),slot=ivec2(texel.x/2,texel.y);
    ivec2 tile=ivec2(slot.x/spGrid.z,slot.y);int sampleIndex=slot.x%spGrid.z;
    ivec2 pixel=SPCandidatePixel(tile,sampleIndex);vec3 p,n;float depth;
    if(!SPSurface(pixel,p,n,depth)){outColor=vec4(0);return;}
    float state=SPUniformCoverage(pixel,p,n,depth)<SP_MIN_WEIGHT?2.0:1.0;
    outColor=texel.x%2==0?vec4(p,state):vec4(n,depth);}
)GLSL";}
inline std::string SpawnShader(){return Common()+R"GLSL(
void main(){ivec2 texel=ivec2(gl_FragCoord.xy),slot=ivec2(texel.x/2,texel.y);
    ivec2 tile=ivec2(slot.x/spGrid.z,slot.y);int sampleIndex=slot.x%spGrid.z;
    vec4 p=SPPosition(spMarked,slot),nd=SPNormalDepth(spMarked,slot);
    if(spOptions.z<.5||p.w<1.5){outColor=vec4(0);return;}
    ivec2 pixel=SPCandidatePixel(tile,sampleIndex),base=SPCornerBase(pixel);
    vec4 weights=vec4(0);bool place=true;
    // UE checks lower-index marked candidates, rather than recursively reading
    // concurrently written spawn decisions. This keeps the pass race-free.
    for(int c=0;c<4&&place;++c){ivec2 neighbor=SPCorner(base,c);
        for(int previous=0;previous<16;++previous){if(previous>=sampleIndex)break;
            ivec2 otherSlot=SPSlot(neighbor,previous);vec4 other=SPPosition(spMarked,otherSlot);
            if(other.w<1.5)continue;
            vec4 otherNormal=(spTracing.w&2)!=0?SPNormalDepth(spMarked,otherSlot):vec4(0);
            weights[c]=max(weights[c],SPAdaptiveWeight(pixel,p.xyz,nd.xyz,nd.w,SPCandidatePixel(neighbor,previous),other,otherNormal));
            if(dot(weights,vec4(1))>=SP_MIN_WEIGHT){place=false;break;}}
    }
    outColor=!place?vec4(0):(texel.x%2==0?vec4(p.xyz,1):nd);}
)GLSL";}
inline std::string CoverageShader(){return Common()+R"GLSL(
void main(){ivec2 pixel=ivec2(gl_FragCoord.xy);vec3 p,n;float depth;
    vec4 scene=texelFetch(spInput,pixel,0);
    if(!SPSurface(pixel,p,n,depth)){outColor=scene;return;}
    vec4 weights=SPUniformWeights(pixel,p,n,depth);float uniformWeight=dot(weights,vec4(1));
    ivec2 base=SPCornerBase(pixel);
    if(spOptions.z>.5)for(int c=0;c<4;++c){if(weights[c]>=SP_MIN_WEIGHT)continue;ivec2 neighbor=SPCorner(base,c);
        for(int s=0;s<16;++s){if(s>=spGrid.z)break;ivec2 slot=SPSlot(neighbor,s);vec4 probe=SPPosition(spSpawned,slot);
            vec4 nd=(spTracing.w&2)!=0?SPNormalDepth(spSpawned,slot):vec4(0);
            weights[c]=max(weights[c],SPAdaptiveWeight(pixel,p,n,depth,SPCandidatePixel(neighbor,s),probe,nd));}}
    vec3 tint=uniformWeight>=SP_MIN_WEIGHT?vec3(.04,.22,.60):
        (dot(weights,vec4(1))>=SP_MIN_WEIGHT?vec3(1,.30,.03):vec3(.75,.03,.04));
    outColor=vec4(mix(scene.rgb,tint,.65),scene.a);}
)GLSL";}
inline std::string MarkerVertexShader(){return std::string("#version 450 core\n")+Constants()+R"GLSL(
layout(location=0) in vec2 position;layout(location=1) in vec2 uv;
layout(binding=0) uniform sampler2D spAtlas;
out vec2 markerPosition;flat out vec3 markerColor;
flat out vec2 markerSphere;flat out vec4 markerDepthProjection;
flat out int markerProbe;
void main(){ivec2 entries=textureSize(spAtlas,0)/ivec2(2,1);
    ivec2 slot=ivec2(gl_InstanceID%entries.x,gl_InstanceID/entries.x);
    vec4 p=texelFetch(spAtlas,ivec2(slot.x*2,slot.y),0),nd=texelFetch(spAtlas,ivec2(slot.x*2+1,slot.y),0);
    markerPosition=position;markerColor=spOptions.w<.5?vec3(.04,.34,1):vec3(1,.32,.03);
    markerProbe=gl_InstanceID+(spOptions.w>.5?spTraceAtlas.z:0);
    markerSphere=vec2(0);markerDepthProjection=vec4(0);
    if(spScreen.w==1&&spOptions.w>.5)markerColor=p.w>1.5?vec3(1,.20,.02):vec3(.06,.85,.24);
    if(p.w<.5){gl_Position=vec4(2,2,2,1);return;}
    // Display a sphere tangent to the stored surface position. Its curved front
    // depth keeps sloped surfaces from cutting a constant-depth disk in half.
    // The atlas position stays unchanged, including while placement is frozen.
    vec4 probeClip=spViewProjection*vec4(p.xyz,1);
    if(probeClip.w<=0){gl_Position=vec4(2,2,2,1);return;}
    float radiusWorld=2.0*probeClip.w*spOptions.y*abs(spInverseProjection[1][1])/float(spScreen.y);
    vec3 world=p.xyz+nd.xyz*radiusWorld;
    vec4 clip=spViewProjection*vec4(world,1);
    if(clip.w<=0){gl_Position=vec4(2,2,2,1);return;}
    markerSphere=vec2((spView*vec4(world,1)).z,radiusWorld);
    mat4 projection=spViewProjection*spInverseView;
    // Include the W terms as well, so perspective and orthographic cameras use
    // the same depth reconstruction without assuming clipW == -viewZ.
    markerDepthProjection=vec4(projection[2][2],projection[3][2],projection[2][3],projection[3][3]);
    clip.xy+=position*spOptions.y*2.0/vec2(spScreen.xy)*clip.w;gl_Position=clip;
}
)GLSL";}
inline std::string MarkerFragmentShader(const std::string& traceGLSL={}){return std::string("#version 450 core\n")+Constants()+traceGLSL+R"GLSL(
in vec2 markerPosition;flat in vec3 markerColor;
flat in vec2 markerSphere;flat in vec4 markerDepthProjection;
flat in int markerProbe;
layout(location=0) out vec4 outColor;
void main(){float radiusSquared=dot(markerPosition,markerPosition);if(radiusSquared>1)discard;
    float sphereFront=sqrt(max(0.0,1.0-radiusSquared));
    float viewZ=markerSphere.x+markerSphere.y*sphereFront;
    float clipZ=markerDepthProjection.x*viewZ+markerDepthProjection.y;
    float clipW=markerDepthProjection.z*viewZ+markerDepthProjection.w;if(clipW<=0)discard;
    gl_FragDepth=clamp((clipZ/clipW)*.5+.5,0.0,1.0);
    float shading=.4+.6*max(dot(normalize(vec3(markerPosition,sphereFront)),normalize(vec3(-.3,.5,.8))),0.0);
    vec3 color=markerColor*shading;
    if(spTracing.w!=0&&spScreen.w!=1) {
        vec3 worldNormal=normalize(mat3(spInverseView)*vec3(markerPosition,sphereFront));
        int rayCount=spTracing.x*spTracing.x,bestRay=0;float bestDot=-2;
        for(int ray=0;ray<256;++ray){if(ray>=rayCount)break;float score=dot(worldNormal,STReadRay(markerProbe,ray,3).xyz);
            if(score>bestDot){bestDot=score;bestRay=ray;}}
        int address=(markerProbe*rayCount+bestRay)*4;
        vec4 result=texelFetch(stRays,ivec2(address%spTraceAtlas.x,address/spTraceAtlas.x),0);
        color=STDiagnosticColor(result.rgb,result.a);
        if(markerProbe==spTraceDisplay.y&&radiusSquared>.78)color=vec3(1.5);
    }
    outColor=vec4(color,1);}
)GLSL";}
} // namespace Render::ScreenProbePlacementDetail
