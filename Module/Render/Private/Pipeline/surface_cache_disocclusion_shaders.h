#pragma once
#include <string>

namespace Render::SurfaceCacheDetail {
// Append after TemporalConstantsGLSL(), the temporal samplers and
// MotionRecoveryGLSL() (which declares contact-mask/measurement samplers 7/8).
// This helper never reads history. The caller admits only an already readable
// current receiver during camera/object motion, and keeps its actual count.
inline std::string DisocclusionRecoveryGLSL() { return R"GLSL(
float STDisocclusionHitDistance(float encoded) {
    // Only an active contact-mask pixel carries this count/distance pack.
    // Ordinary coverage outside that mask must never be decoded as distance.
    if(isnan(encoded)||isinf(encoded)||encoded<0.0||floor(encoded*.5)<1.0)return -1.0;
    float fraction=clamp(encoded-2.0*floor(encoded*.5),0.0,.99999);
    return fraction/max(1.0-fraction,.00001);
}
float STDisocclusionContactHitDistance(ivec2 pixel) {
    if((stScreen.z&2097152)!=0)return STNativeContactHitDistance(pixel);
    return STDisocclusionHitDistance(texelFetch(stRecoveryMeasurements,pixel,0).a);
}

vec3 STRestoreDisoccludedCurrent(ivec2 pixel,vec3 world,vec3 normal,float depth,float footprint,
    vec3 current,float readability,float count) {
    // 16384 is supplied only with valid history and camera/scene motion.
    // An invalid reprojection may still have count one; cold static frames are
    // excluded by the CPU flag. No sampler read occurs on the ordinary path.
    if((stScreen.z&16385)!=16385||!(count>=1.0)||count>=4.0||isinf(count)||
        !(readability>0.0)||isinf(readability)||!STFinite(current)||!STFinite(world)||
        !STFinite(normal)||dot(normal,normal)<.25||!(depth>0.0)||isinf(depth)||
        !(footprint>0.0)||isinf(footprint))return current;
    normal=normalize(normal);
    vec3 albedo=texelFetch(stCurrentAlbedo,pixel,0).rgb;
    if(!STFinite(albedo))return current;
    float pixelWorld=clamp(footprint,.000001,max(stClamp.z,.025*depth));
    float planeTolerance=max(stClamp.z,min(.0005*depth,.2*pixelWorld));
    bool contact=(stScreen.z&64)!=0,guided=contact&&(stScreen.z&4096)!=0;
    float centerMask=contact?STRecoveryContactMaskAt(pixel):0.0;
    if(isnan(centerMask)||isinf(centerMask))return current;
    centerMask=clamp(centerMask,0.0,1.0);
    float centerHit=-1.0;
    if(guided&&centerMask>.000001)
        centerHit=STDisocclusionContactHitDistance(pixel);

    const ivec2 axes[4]=ivec2[4](ivec2(1,0),ivec2(0,1),ivec2(1,1),ivec2(1,-1));
    vec3 sum=current;float mass=0.0;int acceptedPairs=0;
    for(int axis=0;axis<4;++axis) {
        vec3 endpointValues[8];float endpointWeights[8];
        for(int endpoint=0;endpoint<8;++endpoint) {
            endpointValues[endpoint]=vec3(0);endpointWeights[endpoint]=0.0;
        }
        for(int side=0;side<2;++side) {
            float pathConfidence=1.0,pathMaterial=1.0,pathMaskWeight=1.0,pathContact=centerMask;
            float pathHit=centerHit;bool completeHitGuide=centerHit>0.0;
            // Keep valid inner taps if a farther step fails. Walking the entire
            // path forbids skipping a silhouette, material seam or unknown strip.
            // Tap radii 1/2/4/8 give 32 possible current samples, not 32 histories.
            for(int step=1;step<=8;++step) {
                ivec2 offset=axes[axis]*(side==0?-step:step),at=pixel+offset;
                if(any(lessThan(at,ivec2(0)))||any(greaterThanEqual(at,stScreen.xy)))break;
                vec4 raw=texelFetch(stRawGi,at,0),position=texelFetch(stViewPositions,at,0),nd=texelFetch(stWorldNormals,at,0);
                if(!(raw.a>0.0)||isinf(raw.a)||!STFinite(raw.rgb)||!STGeometryValid(position,nd))break;
                vec3 neighborWorld=(stCurrentInverseView*vec4(position.xyz,1)).xyz,neighborNormal=normalize(nd.xyz);
                vec3 material=texelFetch(stCurrentAlbedo,at,0).rgb,delta=neighborWorld-world;
                vec3 materialDelta=abs(material-albedo);
                float materialError=max(materialDelta.x,max(materialDelta.y,materialDelta.z));
                float agreement=dot(normal,neighborNormal);
                if(!STFinite(neighborWorld)||!STFinite(material)||agreement<=.9659258263||materialError>=.04)break;
                // The midpoint tangent plane accepts a smooth curved surface;
                // both endpoints still need close normals and a short world span.
                float plane=abs(dot(delta,normalize(normal+neighborNormal)));
                float worldLimit=max(1.5*pixelWorld*(length(vec2(offset))+.75),2.0*planeTolerance);
                if(plane>=planeTolerance||length(delta)>worldLimit)break;
                pathConfidence=min(pathConfidence,min(readability,raw.a)/max(readability,raw.a));
                pathMaterial=min(pathMaterial,1.0-smoothstep(.02,.04,materialError));
                float mask=contact?STRecoveryContactMaskAt(at):0.0;
                if(isnan(mask)||isinf(mask))break;
                mask=clamp(mask,0.0,1.0);
                pathMaskWeight=min(pathMaskWeight,exp2(-8.0*abs(centerMask-mask)));
                pathContact=max(pathContact,mask);
                if(guided&&mask>.000001) {
                    float hit=STDisocclusionContactHitDistance(at);
                    completeHitGuide=completeHitGuide&&hit>0.0;
                    if(hit>0.0)pathHit=pathHit>0.0?min(pathHit,hit):hit;
                } else completeHitGuide=false;
                if(step!=1&&step!=2&&step!=4&&step!=8)continue;
                // One-pixel support is retained near a contact. Extra reach is
                // bounded by the full-budget harmonic hit distance: a short hit
                // allows a small tangent displacement only. Missing metadata
                // supplies no extra contact support. Mask confidence interpolates
                // this restriction continuously into the unmasked probe signal.
                float extraSupport=1.0;
                if(step>1&&pathContact>0.0) {
                    float hitSupport=0.0;
                    if(guided&&completeHitGuide&&pathHit>0.0) {
                        float tangent=length(delta-normal*dot(delta,normal));
                        float parallax=tangent/max(pathHit,.00001);
                        hitSupport=1.0-smoothstep(.0874886635,.1763269807,parallax);
                    }
                    extraSupport=mix(1.0,hitSupport,pathContact);
                }
                float normalWeight=smoothstep(.9659258263,.9961946981,agreement);
                float planeWeight=1.0-smoothstep(.5*planeTolerance,planeTolerance,plane);
                float weight=exp2(-dot(vec2(offset),vec2(offset))/32.0)*normalWeight*planeWeight*
                    pathConfidence*pathMaterial*pathMaskWeight*extraSupport;
                if(!(weight>0.0))continue;
                int level=step==1?0:(step==2?1:(step==4?2:3));
                endpointValues[side*4+level]=max(raw.rgb,vec3(0));endpointWeights[side*4+level]=weight;
            }
        }
        bool accepted=false;
        for(int level=0;level<4;++level) {
            // Equal weight on opposite endpoints cancels a linear lighting
            // gradient. A silhouette/contact rejection on either side drops
            // the whole pair, rather than pulling toward one surviving side.
            float pairWeight=min(endpointWeights[level],endpointWeights[4+level]);
            if(!(pairWeight>0.0))continue;
            sum+=(endpointValues[level]+endpointValues[4+level])*pairWeight;
            mass+=2.0*pairWeight;accepted=true;
        }
        if(accepted)++acceptedPairs;
    }
    if(acceptedPairs<1||mass<=.5||!STFinite(sum))return current;
    // This is a bounded current-frame estimate, never evidence of another
    // temporal sample. Known black participates normally; RGB selects no taps.
    float young=1.0-smoothstep(1.0,4.0,count);
    float strength=.9*young*smoothstep(.5,4.0,mass);
    vec3 restored=mix(current,sum/(1.0+mass),strength);
    return STFinite(restored)?restored:current;
}
)GLSL"; }
} // namespace Render::SurfaceCacheDetail
