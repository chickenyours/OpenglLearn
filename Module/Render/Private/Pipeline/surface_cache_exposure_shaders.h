#pragma once
#include <string>
#include "surface_cache_temporal_shaders.h"

namespace Render::SurfaceCacheDetail {
// Classification precedes this frame's receiver tracing and temporal resolve.
// Only completed history geometry and its independent exposure lifetime are
// read. No radiance or temporal accumulation count chooses extra samples.
// Geometry exposure retains its own finite lifetime; an independent tag can
// request more current rays on certified moving receivers. The later gather
// uses the unchanged geometric contact mask, unless an independent full-current
// moving tag requests a real receiver measurement across that moving surface.
inline std::string ExposureSamplingShader(bool objectMotion=false) {
    return std::string("#version 450 core\n#define SE_OBJECT_MOTION ")+(objectMotion?"1\n":"0\n")+
        TemporalConstantsGLSL()+R"GLSL(
in vec2 vUV;layout(location=0) out vec4 outColor;
layout(binding=0) uniform sampler2D seViewPositions;
layout(binding=1) uniform sampler2D seWorldNormals;
layout(binding=2) uniform sampler2D seCurrentAlbedo;
layout(binding=3) uniform sampler2D seHistoryPositions;
layout(binding=4) uniform sampler2D seHistoryNormals;
#if SE_OBJECT_MOTION
layout(binding=6) uniform sampler2D seObjectPreviousPositions;
layout(std140,binding=1) uniform ExposureObjectMotionData {mat4 seObjectPreviousNormals[64];};
#endif
layout(std140,binding=14) uniform ExposureSamplingData {
    // enabled, baseline receiver rays, exposure multiplier, packed batch caps.
    // W low four bits retain the hard cap; bits 4..7 optionally request two/four
    // batches and bit 8 requests full-current moving measurements, not new age.
    // Each added batch has the complete original angular stratum count.
    ivec4 seSampling;
};

bool SEFinite(float value) {return !isnan(value)&&!isinf(value);}
int SEMovingRayMultiplier() {
    int multiplier=(seSampling.w>>4)&15;
    return multiplier==2||multiplier==4?multiplier:1;
}

// History position alpha has two established albedo/coverage encodings.
// Compare their RGB portion only: this classification runs before the current
// receiver readability is known. Owner identity is checked independently when
// the existing receiver-motion capture is available.
bool SEMaterialCompatible(float key,vec3 albedo) {
    if(!SEFinite(key)||key<0.0||key>=16777216.0||!STFinite(albedo))return false;
    key=STBaseMaterialKey(key);
    if(key>=2097152.0&&key<4194304.0) {
        float ownerBase=32768.0*floor((key-2097152.0)/32768.0);
        int packedRgb=int(floor((key-2097152.0-ownerBase)/8.0));
        ivec3 q=ivec3(floor(clamp(albedo,vec3(0),vec3(1))*15.0+.5));
        return packedRgb==q.x+16*q.y+256*q.z;
    }
    if(key>=4.0&&key<1048580.0) {
        int packedRgb=int(floor((key-4.0)/32.0));
        ivec3 q=ivec3(floor(clamp(albedo,vec3(0),vec3(1))*31.0+.5));
        return packedRgb==q.x+32*q.y+1024*q.z;
    }
    // Legacy histories carry geometry validity only. Never infer material
    // mismatch from those non-signature values; geometric checks still apply.
    return key==1.0||key==2.0;
}

void SESetBudget(int remainingAge,bool geometryFresh) {
    // A moving-only classifier must retain the exact disabled geometry-
    // exposure result. Its later Z tag cannot create confidence or lifetime.
    if(seSampling.z<=1) {
        outColor=vec4(0,float(max(seSampling.y,1)),0,1);return;
    }
    remainingAge=clamp(remainingAge,0,3);
    float confidence=float(remainingAge)/3.0;
    int cap=clamp(seSampling.w&15,1,4);
    int multiplier=clamp(seSampling.z,1,cap);
    int batches=1;
    if(remainingAge>0)
        batches=clamp(1+(remainingAge*(multiplier-1)+2)/3,1,multiplier);
    // W is valid + two times the independent remaining lifetime. The position
    // save pass packs those two lifetime bits above its exact integer key.
    outColor=vec4(confidence,float(max(seSampling.y,1)*batches),geometryFresh?1.0:0.0,float(1+2*remainingAge));
}

void main() {
    ivec2 pixel=ivec2(gl_FragCoord.xy);
    vec4 view=texelFetch(seViewPositions,pixel,0),nd=texelFetch(seWorldNormals,pixel,0);
    vec3 world=(stCurrentInverseView*vec4(view.xyz,1)).xyz;
    // Evaluate derivatives for complete quads before validity or feature
    // branches. Their allowance is bounded below at geometry discontinuities.
    float footprint=max(length(dFdx(world)),length(dFdy(world)));
    outColor=vec4(0,float(max(seSampling.y,1)),0,0);
    if(!STGeometryValid(view,nd)||!STFinite(world)||!SEFinite(footprint))return;
    outColor.w=1.0;
    // The CPU disables cold, invalid, cut and stationary frames. Retain this
    // shader guard so those paths never fetch an uninitialised history image.
    if(seSampling.x==0||(stScreen.z&1)==0||(seSampling.z<=1&&SEMovingRayMultiplier()<=1))return;
    vec3 normal=normalize(nd.xyz),albedo=texelFetch(seCurrentAlbedo,pixel,0).rgb;
    if(!STFinite(albedo))return;
    vec3 expectedWorld=world,expectedNormal=normal;float receiverOwner=0.0;bool receiverMoved=false;
#if SE_OBJECT_MOTION
    if((stScreen.z&32768)!=0) {
        vec4 receiver=texelFetch(seObjectPreviousPositions,pixel,0);
        if(receiver.w>=1.0&&receiver.w<=64.0&&STFinite(receiver.xyz)) {
            int owner=int(receiver.w+.5)-1;
            if(seObjectPreviousNormals[owner][3].w<=1.5)receiverOwner=receiver.w;
            else {
                vec3 transformed=mat3(seObjectPreviousNormals[owner])*normal;
                if(STFinite(transformed)&&dot(transformed,transformed)>1e-12) {
                    expectedWorld=receiver.xyz;expectedNormal=normalize(transformed);receiverOwner=receiver.w;receiverMoved=true;
                }
            }
        }
    }
#endif
    vec4 clip=stPreviousViewProjection*vec4(expectedWorld,1);
    float depth=-(stPreviousView*vec4(expectedWorld,1)).z;
    if(!STFinite(clip.xyz)||!SEFinite(clip.w)||!(clip.w>0.0)||!SEFinite(depth)||!(depth>0.0)) {
        SESetBudget(3,true);return;
    }
    vec3 ndc=clip.xyz/clip.w;
    if(!STFinite(ndc)||any(lessThan(ndc,vec3(-1)))||any(greaterThan(ndc,vec3(1)))) {
        SESetBudget(3,true);return;
    }
    vec2 historyPixel=(ndc.xy*.5+.5)*vec2(stScreen.xy)-.5;
    ivec2 origin=ivec2(floor(historyPixel));vec2 fraction=fract(historyPixel);
    float baseTolerance=max(stReject.x*depth,stClamp.z);
    float distanceTolerance=baseTolerance+min(1.5*footprint,baseTolerance);
    // Use the ordinary temporal geometry rejection, then require the strict
    // same-surface support used by camera/object stability. Extra samples do
    // not relax any of those gates or move a sample onto a neighbouring face.
    float strictDistance=min(distanceTolerance,max(2.0*footprint,stClamp.z));
    float planeTolerance=max(stClamp.z,min(baseTolerance,.0005*depth));
    float normalCosine=max(stReject.y,.984807753);
    float support=0.0,ageSum=0.0;
    for(int corner=0;corner<4;++corner) {
        ivec2 offset=ivec2(corner%2,corner/2),at=origin+offset;
        if(any(lessThan(at,ivec2(0)))||any(greaterThanEqual(at,stScreen.xy)))continue;
        float weight=(offset.x==0?1.0-fraction.x:fraction.x)*(offset.y==0?1.0-fraction.y:fraction.y);
        if(!(weight>0.0))continue;
        vec4 oldPosition=texelFetch(seHistoryPositions,at,0),oldNormal=texelFetch(seHistoryNormals,at,0);
        if(!(oldPosition.w>=.5)||!STFinite(oldPosition.xyz)||!STFinite(oldNormal.xyz)||
            !SEFinite(oldNormal.w)||!(oldNormal.w>0.0)||dot(oldNormal.xyz,oldNormal.xyz)<.25||
            !SEMaterialCompatible(oldPosition.w,albedo))continue;
        if(receiverOwner>0.0&&STObjectOwner(oldPosition.w)!=receiverOwner)continue;
        vec3 historyNormal=normalize(oldNormal.xyz),delta=oldPosition.xyz-expectedWorld;
        if(dot(expectedNormal,historyNormal)<normalCosine||length(delta)>strictDistance||
            abs(oldNormal.w-depth)>distanceTolerance||
            max(abs(dot(delta,expectedNormal)),abs(dot(delta,historyNormal)))>planeTolerance)continue;
        support+=weight;
        // Read lifetime only after geometry/material/owner validation, so a
        // receiver cannot inherit the previous occluder's exposure state.
        ageSum+=weight*STExposureAge(oldPosition.w);
    }
    bool geometryFresh=support<.5;
    // New geometry starts a three-frame sampling lifetime. Accepted geometry
    // can only consume an existing lifetime, never restart one because its
    // unrelated temporal history count happens to have a small configured cap.
    int remainingAge=geometryFresh?3:clamp(int(floor(ageSum/support+.5))-1,0,2);
    SESetBudget(remainingAge,geometryFresh);
    // Bit zero remains the original geometry-fresh flag. The independent
    // moving-budget tag changes neither X/Y exposure policy nor W lifetime.
    // Only actual transform motion with strict same-owner/material support
    // qualifies. Full-current confidence is independent of geometric freshness.
    int movingMultiplier=SEMovingRayMultiplier();
    if(receiverMoved&&support>=.75&&movingMultiplier>1) {
        outColor.z+=2.0*float(movingMultiplier);
        if((seSampling.w&256)!=0)outColor.z+=16.0;
    }
}
)GLSL";
}
} // namespace Render::SurfaceCacheDetail
