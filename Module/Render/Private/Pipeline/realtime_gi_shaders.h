#pragma once
#include "Render/Public/Pipeline/realtime_gi.h"
#include "Render/Public/Pipeline/sky_light.h"
namespace Render::PipelineDetail {
inline std::string RealtimeGiRayDirectionsGLSL() {
    return R"GLSL(
vec3 RtRayDirection(int ray,int probe) {
    float y=1-2*(float(ray)+.5)/float(rtOptions.x);
    // World-probe quadrature must stay fixed between updates. Rotating only
    // 64 rays changes hit/sky coverage and makes static radiance look reactive.
    // Different probes retain different phases without a temporal rotation.
    float phi=float(ray)*2.39996323+fract(float(probe)*.506577813)*6.283185307;
    float radius=sqrt(max(0.0,1-y*y));return vec3(radius*cos(phi),y,radius*sin(phi));
}
bool RtActive(int index) { return (index-rtOptions.y+int(rtCounts.w))%int(rtCounts.w)<rtOptions.z; }
)GLSL";
}
inline std::string RealtimeGiGeometryGLSL() {
    return R"GLSL(
#ifndef RT_GEOMETRY_SLOT
#define RT_GEOMETRY_SLOT 0
#endif
layout(binding=RT_GEOMETRY_SLOT) uniform sampler2D rtScene;
#ifdef RENDER_NATIVE_RAY_QUERY
layout(set=2,binding=0) uniform accelerationStructureEXT rtAcceleration;
#endif
layout(std140,binding=3) uniform RtPointLights {
    vec4 rtPointPositions[4],rtPointColors[4],rtUnusedAmbient,rtUnusedOutput,rtUnusedSpecular;
};
struct RtAreaLight {vec4 center,u,v,radiance;};
layout(std140,binding=8) uniform RtAreas {RtAreaLight rtArea[4];};
vec4 RtGeometry(int index) {return texelFetch(rtScene,ivec2(index%rtGeometry.y,index/rtGeometry.y),0);}
bool RtEmissionTwoSided(vec4 emission) {return (int(emission.a)&1)!=0;}
bool RtEmissionAnalytic(vec4 emission) {return (int(emission.a)&2)!=0;}
vec3 RtTransportEmission(vec4 emission) {return RtEmissionAnalytic(emission)?vec3(0):emission.rgb;}
bool RtBox(vec3 lo,vec3 hi,vec3 p,vec3 d,float distance) {
    float near=0,far=distance;
    for(int a=0;a<3;++a) {
        if(abs(d[a])<1e-8) {if(p[a]<lo[a]||p[a]>hi[a])return false;continue;}
        float x=(lo[a]-p[a])/d[a],y=(hi[a]-p[a])/d[a];
        near=max(near,min(x,y));far=min(far,max(x,y));if(near>far)return false;
    }
    return true;
}
bool RtIntersect(vec3 p,vec3 d,inout float distance,out vec3 normal,out int triangle,out bool front) {
    triangle=-1;normal=vec3(0);front=true;
    if(rtGeometry.z==0||distance<=1e-5)return false;
#ifdef RENDER_NATIVE_RAY_QUERY
    rayQueryEXT query;
    rayQueryInitializeEXT(query,rtAcceleration,gl_RayFlagsOpaqueEXT,255,p,1e-5,d,distance);
    while(rayQueryProceedEXT(query)) {}
    if(rayQueryGetIntersectionTypeEXT(query,true)==gl_RayQueryCommittedIntersectionNoneEXT)return false;
    distance=rayQueryGetIntersectionTEXT(query,true);
    triangle=int(rayQueryGetIntersectionPrimitiveIndexEXT(query,true));
    int offset=rtGeometry.x+triangle*5;
    vec3 a=RtGeometry(offset).xyz;
    normal=normalize(cross(RtGeometry(offset+1).xyz-a,RtGeometry(offset+2).xyz-a));
    front=dot(normal,d)<0;return true;
#else
    int stack[32];stack[0]=0;int size=1;
    for(int visit=0;visit<16384&&size>0;++visit) {
        int node=stack[--size];vec4 lo=RtGeometry(node*2),hi=RtGeometry(node*2+1);
        if(!RtBox(lo.xyz,hi.xyz,p,d,distance))continue;
        if(hi.w>=0) {stack[size++]=int(lo.w);stack[size++]=int(hi.w);continue;}
        for(int i=0;i<int(-hi.w);++i) {
            int t=int(lo.w)+i,offset=rtGeometry.x+t*5;
            vec3 a=RtGeometry(offset).xyz,ab=RtGeometry(offset+1).xyz-a,ac=RtGeometry(offset+2).xyz-a;
            vec3 q=cross(d,ac);float det=dot(ab,q);if(abs(det)<1e-9)continue;
            vec3 rel=p-a;float u=dot(rel,q)/det;if(u<0||u>1)continue;
            vec3 cr=cross(rel,ab);float v=dot(d,cr)/det;if(v<0||u+v>1)continue;
            float h=dot(ac,cr)/det;if(h<1e-5||h>=distance)continue;
            distance=h;normal=normalize(cross(ab,ac));triangle=t;front=dot(normal,d)<0;
        }
    }
    return triangle>=0;
#endif
}
bool RtVisible(vec3 p,vec3 N,vec3 L,float distance) {
    distance=max(0.0,distance-rtTrace.y*2);vec3 normal;int triangle;bool front;
    return !RtIntersect(p+N*rtTrace.y,L,distance,normal,triangle,front);
}
float RtSpotCone(vec3 sourceToReceiver) {
    float phase=clamp((dot(rtSpotDirectionOuter.xyz,sourceToReceiver)-rtSpotDirectionOuter.w)/
        max(rtSpotIntensityInner.w-rtSpotDirectionOuter.w,1e-6),0.0,1.0);
    return phase*phase*(3.0-2.0*phase);
}
vec3 RtDirect(vec3 p,vec3 N) {
    vec3 result=vec3(0),L=-rtSunDirectionIntensity.xyz;float cosine=max(0.0,dot(N,L));
    if(rtSunDirectionIntensity.w>0&&cosine>0&&RtVisible(p,N,L,rtTrace.x))
        result+=rtSunColor.rgb*(rtSunDirectionIntensity.w*cosine);
    for(int i=0;i<4;++i) {
        if(all(lessThanEqual(rtPointColors[i].rgb,vec3(0))))continue;
        vec3 delta=rtPointPositions[i].xyz-p;float distance=length(delta);if(distance<1e-5)continue;
        L=delta/distance;cosine=max(0.0,dot(N,L));
        // Stop at the finite source's surface, rather than its enclosed center.
        float sourceRadius=max(rtPointColors[i].w,0);
        if(cosine>0&&RtVisible(p,N,L,distance-sourceRadius))result+=rtPointColors[i].rgb*cosine/max(max(distance*distance,sourceRadius*sourceRadius),.0001);
    }
    if(any(greaterThan(rtSpotIntensityInner.rgb,vec3(0)))) {
        vec3 delta=rtSpotPositionRadius.xyz-p;float distance=length(delta);
        if(distance>1e-5) {
            L=delta/distance;cosine=max(0.0,dot(N,L));float beam=RtSpotCone(-L),radius=rtSpotPositionRadius.w;
            if(cosine>0&&beam>0&&RtVisible(p,N,L,distance-radius))
                result+=rtSpotIntensityInner.rgb*(cosine*beam/max(max(distance*distance,radius*radius),.0001));
        }
    }
    for(int i=0;i<4;++i) {
        if(rtArea[i].center.w<.5)continue;
        vec3 crossUV=cross(rtArea[i].u.xyz,rtArea[i].v.xyz);float cellArea=length(crossUV);vec3 normal=crossUV/cellArea;
        for(int y=0;y<2;++y)for(int x=0;x<2;++x) {
            vec3 delta=rtArea[i].center.xyz+rtArea[i].u.xyz*(float(x)-.5)+rtArea[i].v.xyz*(float(y)-.5)-p;
            float distance=length(delta);if(distance<1e-5)continue;L=delta/distance;
            float receiver=max(0.0,dot(N,L)),face=dot(normal,-L),emitter=rtArea[i].radiance.w>.5?abs(face):max(face,0.0);
            if(receiver>0&&emitter>0&&RtVisible(p,N,L,distance))
                result+=rtArea[i].radiance.rgb*(cellArea*receiver*emitter/max(distance*distance,.0001));
        }
    }
    return result;
}
)GLSL";
}
inline std::string RealtimeGiTraceShader() {
    return std::string("#version 450 core\n")+RealtimeGiQueryGLSL()+SkyLightGLSL()+RealtimeGiRayDirectionsGLSL()+RealtimeGiGeometryGLSL()+R"GLSL(
layout(location=0) out vec4 outColor;
void main() {
    ivec2 pixel=ivec2(gl_FragCoord.xy);int ray=pixel.x,probe=pixel.y;
    if(ray>=rtOptions.x||probe>=int(rtCounts.w)||!RtActive(probe))discard;
    vec3 p=RtPosition(probe),d=RtRayDirection(ray,probe),normal;float distance=rtTrace.x;int triangle;bool front;
    vec3 radiance=vec3(0);
    if(RtIntersect(p,d,distance,normal,triangle,front)) {
        vec3 point=p+d*distance,N=front?normal:-normal;int offset=rtGeometry.x+triangle*5;
        vec4 emission=RtGeometry(offset+4);vec3 rho=min(RtGeometry(offset+3).rgb,vec3(.95));
        if(front||RtEmissionTwoSided(emission))radiance=RtTransportEmission(emission);
        if(any(greaterThan(rho,vec3(0)))) {
            radiance+=rho*RtDirect(point,N)/3.14159265359;
            if(rtRuntime.x<.5&&rtTrace.w>0) {
                vec3 previous;float sky;EvaluateRealtimeGi(point,N,N,1,previous,sky);
                radiance+=rho*previous*rtTrace.w;
            }
        }
    } else if(skyOptions.x>.5) radiance=SkyRadiance(d,0)*skyOptions.y*skyOptions.z;
    outColor=vec4(clamp(radiance,vec3(0),vec3(60000)),front?distance:-distance);
}
)GLSL";
}
inline std::string RealtimeGiIntegrateShader() {
    return std::string("#version 450 core\n")+RealtimeGiQueryGLSL()+RealtimeGiRayDirectionsGLSL()+R"GLSL(
layout(binding=0) uniform sampler2D rtRays;
layout(location=0) out vec4 outColor;
void main() {
    ivec2 pixel=ivec2(gl_FragCoord.xy);int probe=pixel.x,row=pixel.y;
    if(probe>=int(rtCounts.w)){outColor=vec4(0);return;}
    vec4 old=rtRuntime.x>.5?vec4(0):RtCache(probe,row);
    if(!RtActive(probe)){outColor=old;return;}
    // Visibility depends on immutable geometry and fixed directions, not on
    // lighting. Initialize it once per scene/grid/ray reset; relighting must
    // never perturb distance weights or toggle probe classification.
    if(rtRuntime.x<.5&&rtRuntime.w<1.5&&row>=9) {
        outColor=old;if(row==25)outColor.w=old.w+1;return;
    }
    vec4 value=vec4(0);float backfaces=0,weight=0;vec3 dc=vec3(0);
    vec3 bin=row>=9&&row<25?RtDecodeOct(vec2(float((row-9)%4)+.5,float((row-9)/4)+.5)/4):vec3(0);
    for(int ray=0;ray<rtOptions.x;++ray) {
        vec4 rayValue=texelFetch(rtRays,ivec2(ray,probe),0);vec3 d=RtRayDirection(ray,probe);float distance=abs(rayValue.a);
        dc+=rayValue.rgb*.282095;
        backfaces+=rayValue.a<0?1:0;
        if(row<9) {
            float b[9];RtBasis(d,b);value.rgb+=rayValue.rgb*b[row];
            value.a+=(distance>=rtTrace.x*.999?1:0)*b[row];
        } else if(row<25) {
            float w=pow(max(0.0,dot(d,bin)),8);value.xy+=vec2(distance,distance*distance)*w;weight+=w;
        }
    }
    float history=rtRuntime.x>.5?0:rtTrace.z;
    vec3 newDC=dc*(12.566370614/float(rtOptions.x)),oldDC=RtCache(probe,0).rgb;
    if(rtRuntime.x<.5) {
        float relative=length(newDC-oldDC)/max(length(newDC),.05);
        float reactive=clamp((relative-.05)/.25,0,1);
        history=mix(history,min(history,.3),reactive);
    }
    if(row<9) {
        float convolution=row==0?1:(row<4?2.0/3.0:.25);
        value*=12.566370614/float(rtOptions.x);value.rgb*=convolution;
        if(rtRuntime.x<.5&&rtRuntime.w<1.5)value.a=old.a;
    } else if(row<25) {
        value=weight>1e-8?value/weight:vec4(rtTrace.x,rtTrace.x*rtTrace.x,0,0);
        history=rtRuntime.x>.5||rtRuntime.w>1.5?0:rtTrace.z;
    } else {
        float fraction=backfaces/float(rtOptions.x);
        outColor=vec4(fraction>.6?0:1,fraction,0,old.w+1);return;
    }
    outColor=vec4(mix(value.rgb,old.rgb,history),value.a);
}
)GLSL";
}
} // namespace Render::PipelineDetail
