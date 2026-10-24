#pragma once
#include <string>

namespace Render::SurfaceCacheDetail {
// Append after the temporal shader's samplers and TemporalConstantsGLSL().
// This recovers a current sample from neighboring mature histories; it does not
// certify the center's own temporal history or alter its count/readability.
inline std::string MotionRecoveryGLSL() { return R"GLSL(
layout(binding=7) uniform sampler2D stRecoveryContactMask;
layout(binding=8) uniform sampler2D stRecoveryMeasurements;
layout(binding=10) uniform sampler2D stNativeReceiverCurrent;

float STRecoveryContactMaskAt(ivec2 pixel) {
    float soft=texelFetch(stRecoveryContactMask,pixel,0).x;
    if((stScreen.z&8388608)==0||(stScreen.z&4194304)==0)return soft;
    return STNativeContactMaskUnion(soft,texelFetch(stRecoveryMeasurements,pixel,0),
        texelFetch(stNativeReceiverCurrent,pixel,0));
}

float STNativeContactHitDistance(ivec2 pixel) {
    // Native metadata stores the full-budget harmonic distance directly in Z.
    // Its W counts executed directions, not readable directions. The retained
    // current-only reconstruction supplies the latter coverage independently;
    // a known base must never make an all-unknown current budget readable.
    // Full receiver mode lacks that retained current image and conservatively
    // supplies no hit guide. The original recovery fallback remains in charge.
    if((stScreen.z&4194304)==0)return -1.0;
    vec4 metadata=texelFetch(stRecoveryMeasurements,pixel,0);
    vec4 current=texelFetch(stNativeReceiverCurrent,pixel,0);
    bool physical=metadata.x>0.0&&metadata.w>0.0;
    bool reconstructed=metadata.x==.25&&metadata.w==0.0;
    if((!physical&&!reconstructed)||!STFinite(metadata.xyz)||isnan(metadata.w)||isinf(metadata.w)||
        !(current.a>0.0)||isnan(current.a)||isinf(current.a)||!STFinite(current.rgb)||!(metadata.z>0.0))return -1.0;
    // Interpolation certifies only current support; neither this test nor the
    // guide increments history confidence or the independent direction count.
    return metadata.z;
}

float STRecoveryHitDistance(float encoded) {
    // Same integer-readable-count / fractional-distance pack as
    // CTMeasurementDistance. Only active contact-mask pixels carry this pack.
    if(isnan(encoded)||isinf(encoded)||encoded<0.0||floor(encoded*.5)<1.0)return -1.0;
    float fraction=clamp(encoded-2.0*floor(encoded*.5),0.0,.99999);
    return fraction/max(1.0-fraction,.00001);
}
float STRecoveryContactHitDistance(ivec2 pixel) {
    if((stScreen.z&2097152)!=0)return STNativeContactHitDistance(pixel);
    return STRecoveryHitDistance(texelFetch(stRecoveryMeasurements,pixel,0).a);
}
bool STRecoveryMatureHistory(ivec2 pixel,vec3 world,vec3 normal,vec3 albedo,float readability,
    float footprint,out vec3 radiance,out float support) {
    radiance=vec3(0);support=0.0;
    vec4 clip=stPreviousViewProjection*vec4(world,1);
    float depth=-(stPreviousView*vec4(world,1)).z;
    if(!STFinite(clip.xyz)||!(clip.w>0.0)||isinf(clip.w)||!(depth>0.0)||isinf(depth))return false;
    vec3 ndc=clip.xyz/clip.w;
    if(!STFinite(ndc)||any(lessThan(ndc,vec3(-1)))||any(greaterThan(ndc,vec3(1))))return false;
    vec2 previousPixel=(ndc.xy*.5+.5)*vec2(stScreen.xy)-.5;
    ivec2 origin=ivec2(floor(previousPixel));vec2 fraction=fract(previousPixel);
    float baseTolerance=max(stReject.x*depth,stClamp.z);
    float distanceTolerance=baseTolerance+min(1.5*footprint,baseTolerance);
    float strictDistance=min(distanceTolerance,max(2.0*footprint,stClamp.z));
    float strictPlane=max(stClamp.z,.0005*depth),key=STCurrentMaterialKey(pixel,albedo,readability);
    float validSupport=0.0;vec3 sum=vec3(0);
    for(int corner=0;corner<4;++corner) {
        ivec2 offset=ivec2(corner%2,corner/2),at=origin+offset;
        if(any(lessThan(at,ivec2(0)))||any(greaterThanEqual(at,stScreen.xy)))continue;
        float weight=(offset.x==0?1.0-fraction.x:fraction.x)*(offset.y==0?1.0-fraction.y:fraction.y);
        if(weight<=0.0)continue;
        vec4 value=texelFetch(stHistory,at,0),position=texelFetch(stHistoryPositions,at,0),nd=texelFetch(stHistoryNormals,at,0);
        if(!(value.a>=1.0)||isinf(value.a)||!STFinite(value.rgb)||position.w<.5||
            !STFinite(position.xyz)||!STFinite(nd.xyz)||!(nd.w>0.0)||dot(nd.xyz,nd.xyz)<.25)continue;
        vec3 previousNormal=normalize(nd.xyz),delta=position.xyz-world;
        float agreement=dot(normal,previousNormal),plane=max(abs(dot(delta,normal)),abs(dot(delta,previousNormal)));
        if(agreement<stReject.y||length(delta)>distanceTolerance||abs(nd.w-depth)>distanceTolerance||plane>baseTolerance)continue;
        validSupport+=weight;
        if(value.a<8.0||STBaseMaterialKey(position.w)!=key||agreement<=.984807753||length(delta)>strictDistance||plane>strictPlane)continue;
        sum+=value.rgb*weight;support+=weight;
    }
    if(support<.75||support<.9*validSupport)return false;
    radiance=sum/support;return STFinite(radiance);
}

vec3 STRecoverMotionCurrent(ivec2 pixel,vec3 world,vec3 normal,float depth,float footprint,
    vec3 current,float readability,bool residualOnly) {
    // The caller sets 128 only for unchanged-scene camera movement and invokes
    // this on a newly exposed/young center. A reset or object motion cannot borrow.
    if((stScreen.z&1)==0||((stScreen.z&128)==0&&!(residualOnly&&(stScreen.z&65536)!=0))||
        stDynamic.x>.5||!(readability>0.0)||!STFinite(current)||
        !STFinite(world)||!STFinite(normal)||!(depth>0.0)||isinf(depth)||
        !(footprint>0.0)||isinf(footprint))return current;
    vec3 albedo=texelFetch(stCurrentAlbedo,pixel,0).rgb;
    if(!STFinite(albedo)||dot(normal,normal)<.25)return current;
    normal=normalize(normal);
    float pixelWorld=clamp(footprint,.000001,max(stClamp.z,.025*depth));
    float planeTolerance=max(stClamp.z,min(.0005*depth,.2*pixelWorld));
    bool contact=(stScreen.z&64)!=0,guided=(stScreen.z&4096)!=0;
    if(guided&&!contact)return current;
    float centerMask=contact?STRecoveryContactMaskAt(pixel):0.0;
    if(isnan(centerMask)||isinf(centerMask))return current;
    bool centerActive=centerMask>.000001;float centerHit=-1.0;
    if(guided&&centerActive) {
        centerHit=STRecoveryContactHitDistance(pixel);
        if(!(centerHit>0.0))return current;
    }
    const ivec2 directions[8]=ivec2[8](ivec2(-1,0),ivec2(1,0),ivec2(0,-1),ivec2(0,1),
        ivec2(-1,-1),ivec2(1,-1),ivec2(-1,1),ivec2(1,1));
    vec3 sum=vec3(0);float mass=0.0;int acceptedDirections=0;
    for(int direction=0;direction<8;++direction) {
        float pathConfidence=1.0,pathMaskWeight=1.0;
        // Every intermediate pixel must remain the same current visible surface.
        // Borrow the nearest mature candidate per direction, never an entire ray.
        for(int step=1;step<=4;++step) {
            ivec2 offset=directions[direction]*step,at=pixel+offset;
            if(any(lessThan(at,ivec2(0)))||any(greaterThanEqual(at,stScreen.xy)))break;
            vec4 raw=texelFetch(stRawGi,at,0),position=texelFetch(stViewPositions,at,0),nd=texelFetch(stWorldNormals,at,0);
            if(!(raw.a>0.0)||isinf(raw.a)||!STFinite(raw.rgb)||!STGeometryValid(position,nd))break;
            vec3 neighborWorld=(stCurrentInverseView*vec4(position.xyz,1)).xyz,neighborNormal=normalize(nd.xyz);
            vec3 material=texelFetch(stCurrentAlbedo,at,0).rgb,delta=neighborWorld-world,materialDelta=abs(material-albedo);
            float agreement=dot(normal,neighborNormal),span=length(delta);
            float plane=max(abs(dot(delta,normal)),abs(dot(delta,neighborNormal)));
            float worldLimit=1.5*pixelWorld*(length(vec2(offset))+.75);
            if(!STFinite(neighborWorld)||!STFinite(material)||agreement<=.984807753||plane>planeTolerance||
                span>max(worldLimit,2.0*planeTolerance)||max(materialDelta.x,max(materialDelta.y,materialDelta.z))>.025)break;
            float mask=contact?STRecoveryContactMaskAt(at):0.0;
            if(isnan(mask)||isinf(mask))break;
            if(guided) {
                // Outside the active mask, measurement alpha is ordinary input
                // coverage, not the encoded distance. Do not decode or cross it.
                if((mask>.000001)!=centerActive)break;
                if(centerActive) {
                    float hit=STRecoveryContactHitDistance(at);
                    if(!(hit>0.0)||span>.15*min(centerHit,hit))break;
                }
            }
            pathConfidence=min(pathConfidence,min(readability,raw.a)/max(readability,raw.a));
            pathMaskWeight=min(pathMaskWeight,exp2(-8.0*abs(centerMask-mask)));
            vec3 history;float support;
            if(!STRecoveryMatureHistory(at,neighborWorld,neighborNormal,material,raw.a,pixelWorld,history,support))continue;
            float weight=exp2(-.25*dot(vec2(offset),vec2(offset)))*pow(max(agreement,0.0),32.0)*
                pathConfidence*pathMaskWeight*support;
            // Under unchanged-scene camera motion the local temporal residual
            // estimates correlated probe error. Transfer that residual rather
            // than the neighbor's lighting gradient/contact shadow itself.
            sum+=(residualOnly?history-max(raw.rgb,vec3(0)):history)*weight;
            mass+=weight;++acceptedDirections;break;
        }
    }
    if(acceptedDirections<2||mass<=.5||!STFinite(sum))return current;
    // Spatial history support is not a temporal sample at the center. Preserve
    // black values and bound influence without any RGB-dependent sample choice.
    float strength=.6*smoothstep(.5,2.0,mass);
    return residualOnly?max(current+(sum/mass)*strength,vec3(0)):mix(current,sum/mass,strength);
}
)GLSL"; }
} // namespace Render::SurfaceCacheDetail
