#pragma once
#include <string>
#include "Render/Public/Pipeline/software_bvh_visibility.h"
namespace Render::SurfaceCacheDetail {
inline std::string LookupGLSL() {return SurfaceCacheDataGLSL()+R"GLSL(
layout(binding=12) uniform sampler2D scAttributes;
layout(binding=13) uniform sampler2D scMetadata;
layout(binding=14) uniform sampler2D scLighting;
vec4 SCAttribute(int index) {return texelFetch(scAttributes,ivec2(index%scAtlas.x,index/scAtlas.x),0);}
vec4 SCMeta(int card,int part) {int index=card*12+part;return texelFetch(scMetadata,ivec2(index%scAtlas.x,index/scAtlas.x),0);}
int SCTriangleOwner(int triangle) {
    if(triangle<0||triangle>=scGeometry.w)return -1;
    int index=scAtlas.w*12+triangle/4;
    return int(texelFetch(scMetadata,ivec2(index%scAtlas.x,index/scAtlas.x),0)[triangle%4]);
}
vec3 SCValue(int index) {return texelFetch(scLighting,ivec2(index%scAtlas.x,index/scAtlas.x),0).rgb;}
bool SCLookupObject(vec3 p,vec3 n,int object,out int result) {
    result=-1;if(scParameters.w<.5)return false;
    int first=object>=0?object*6:0,last=object>=0?min(scAtlas.w,first+6):scAtlas.w;
    for(int group=first;group<last;group+=6) {
        float padding=SCMeta(group,6).w;
        if(any(lessThan(p,SCMeta(group,9).xyz-vec3(padding)))||any(greaterThan(p,SCMeta(group,10).xyz+vec3(padding))))continue;
        // Opposite cards share a projection. Rank the three facing directions
        // once, and visit another page only when the preferred one has a hole.
        vec4 cx=SCMeta(group,6),cy=SCMeta(group+2,6),cz=SCMeta(group+4,6);
        vec3 alignment=vec3(dot(n,cx.xyz),dot(n,cy.xyz),dot(n,cz.xyz));
        ivec3 sides=ivec3(0,2,4)+ivec3(lessThan(alignment,vec3(0)));alignment=abs(alignment);
        if(alignment.x<alignment.y){float a=alignment.x;alignment.x=alignment.y;alignment.y=a;int c=sides.x;sides.x=sides.y;sides.y=c;}
        if(alignment.y<alignment.z){float a=alignment.y;alignment.y=alignment.z;alignment.z=a;int c=sides.y;sides.y=sides.z;sides.z=c;}
        if(alignment.x<alignment.y){float a=alignment.x;alignment.x=alignment.y;alignment.y=a;int c=sides.x;sides.x=sides.y;sides.y=c;}
        mat4 inverseTransform=mat4(SCMeta(group,0),SCMeta(group,1),SCMeta(group,2),SCMeta(group,3));
        vec3 local=(inverseTransform*vec4(p,1)).xyz;
        vec4 low=SCMeta(group,4),high=SCMeta(group,5);
        int resolution=int(SCMeta(group,9).w),pageSize=resolution*resolution;
        for(int rank=0;rank<3;++rank) {
            if(alignment[rank]<=.05)break;
            int side=sides[rank],axis=side/2,u=(axis+1)%3,v=(axis+2)%3;
            vec4 facing=axis==0?cx:(axis==1?cy:cz);if((side&1)!=0)facing.xyz=-facing.xyz;
            vec2 extent=max(vec2(high[u]-low[u],high[v]-low[v]),vec2(1e-8));
            vec2 uv=(vec2(local[u],local[v])-vec2(low[u],low[v]))/extent;
            if(any(lessThan(uv,vec2(0)))||any(greaterThan(uv,vec2(1))))continue;
            vec2 at=uv*float(resolution)-.5;ivec2 first=ivec2(floor(at));
            float best=1e30;int selected=-1;
            for(int y=0;y<2;++y)for(int x=0;x<2;++x) {
                ivec2 cell=clamp(first+ivec2(x,y),ivec2(0),ivec2(resolution-1));
                int index=int(high.w)+side*pageSize+cell.y*resolution+cell.x;
                vec4 position=SCAttribute(index);if(position.w<.5)continue;
                vec4 normal=SCAttribute(scAtlas.y+index);if(dot(n,normal.xyz)<.6)continue;
                vec3 delta=p-position.xyz;float depth=abs(dot(delta,facing.xyz));
                float distance=dot(delta,delta);if(depth>facing.w||distance>facing.w*facing.w*6.25)continue;
                if(distance<best){best=distance;selected=index;}
            }
            if(selected>=0){result=selected;return true;}
        }
    }
    return false;
}
bool SCLookup(vec3 p,vec3 n,out int result){return SCLookupObject(p,n,-1,result);}
// Match LumenSurfaceCacheSampling.ush: filter lighting with the bilinear
// footprint multiplied by geometric visibility. Establish coverage first with
// the original nearest query, then stay on that exact object's card. A cache
// hole must not acquire coverage from a different object or projection.
bool SCSampleLightingObject(vec3 p,vec3 n,int object,out vec3 radiance,out float coverage) {
    radiance=vec3(0);coverage=0;int anchor;
    if(!SCLookupObject(p,n,object,anchor))return false;
    vec4 anchorLighting=texelFetch(scLighting,ivec2(anchor%scAtlas.x,anchor/scAtlas.x),0);
    if(anchorLighting.w<.5||any(isnan(anchorLighting.rgb))||any(isinf(anchorLighting.rgb)))return false;
    if((int(scParameters.w+.5)&2)==0) {radiance=anchorLighting.rgb;coverage=1;return true;}
    vec4 anchorNormal=SCAttribute(scAtlas.y+anchor),anchorPosition=SCAttribute(anchor);
    int card=int(anchorNormal.w+.5)-1;
    if(card<0||card>=scAtlas.w||(object>=0&&card/6!=object))return false;
    vec4 low=SCMeta(card,4),high=SCMeta(card,5),facing=SCMeta(card,6);
    mat4 inverseTransform=mat4(SCMeta(card,0),SCMeta(card,1),SCMeta(card,2),SCMeta(card,3));
    vec3 local=(inverseTransform*vec4(p,1)).xyz;
    int axis=card%6/2,u=(axis+1)%3,v=(axis+2)%3;
    int resolution=int(SCMeta(card,9).w);
    vec2 extent=max(vec2(high[u]-low[u],high[v]-low[v]),vec2(1e-8));
    vec2 at=(vec2(local[u],local[v])-vec2(low[u],low[v]))/extent*float(resolution)-.5;
    ivec2 base=ivec2(floor(at));vec2 fraction=fract(at);
    float tolerance=max(facing.w,.00001),weightSum=0;vec3 sum=vec3(0);
    bool missingLighting=false;
    for(int y=0;y<2;++y)for(int x=0;x<2;++x) {
        float bilinear=(x==0?1-fraction.x:fraction.x)*(y==0?1-fraction.y:fraction.y);
        if(bilinear<=.000001)continue;
        // Clamp to texel centers in this card only, never the next atlas page.
        ivec2 cell=clamp(base+ivec2(x,y),ivec2(0),ivec2(resolution-1));
        int index=int(high.w)+cell.y*resolution+cell.x;
        vec4 position=SCAttribute(index),normal=SCAttribute(scAtlas.y+index);
        if(position.w<.5||dot(n,normal.xyz)<.6||int(normal.w+.5)-1!=card)continue;
        vec3 delta=p-position.xyz;float depth=abs(dot(delta,facing.xyz));
        if(depth>=tolerance||dot(delta,delta)>tolerance*tolerance*6.25)continue;
        // A projection can contain separate depth layers with similar normals.
        // Compare each tap to the covered anchor's tangent plane as well as the
        // hit plane; allow the second-order change expected on a curved mesh.
        vec3 fromAnchor=position.xyz-anchorPosition.xyz;
        float normalDifference=sqrt(max(0.0,2.0*(1-clamp(dot(normal.xyz,anchorNormal.xyz),-1.0,1.0))));
        float layerSlack=max(.0002,.05*tolerance)+.75*length(fromAnchor)*normalDifference;
        if(abs(dot(fromAnchor,anchorNormal.xyz))>layerSlack||
            max(abs(dot(delta,n)),abs(dot(delta,normal.xyz)))>tolerance)continue;
        float visibility=1-clamp((depth-.8*tolerance)/(.2*tolerance),0.0,1.0);
        float weight=bilinear*visibility;if(weight<=.000001)continue;
        vec4 lighting=texelFetch(scLighting,ivec2(index%scAtlas.x,index/scAtlas.x),0);
        // Alpha denotes captured/relit data, independently of RGB. Valid black
        // contributes its full weight; unavailable lighting stays unreadable.
        if(lighting.w<.5||any(isnan(lighting.rgb))||any(isinf(lighting.rgb))) {missingLighting=true;continue;}
        sum+=lighting.rgb*weight;weightSum+=weight;
    }
    if(missingLighting||weightSum<=.000001)return false;
    radiance=sum/weightSum;coverage=clamp(weightSum,0.0,1.0);return true;
}
bool SCSampleLightingObject(vec3 p,vec3 n,int object,out vec3 radiance) {
    float coverage;return SCSampleLightingObject(p,n,object,radiance,coverage);
}
bool SCSampleLighting(vec3 p,vec3 n,out vec3 radiance) {return SCSampleLightingObject(p,n,-1,radiance);}
)GLSL";}
inline std::string GeometryGLSL(){return SoftwareBvhVisibilityGLSL()+R"GLSL(
layout(std140,binding=3) uniform SCPointLights {
    vec4 scPointPositions[4],scPointColors[4],scUnusedAmbient,scUnusedOutput,scUnusedSpecular;
};
struct SCAreaLight {vec4 center,u,v,radiance;};
layout(std140,binding=8) uniform SCAreaLights {SCAreaLight scAreaLights[4];};
vec3 SCDirect(vec3 p,vec3 n) {
    vec3 irradiance=vec3(0);vec3 d=-scSunDirectionIntensity.xyz;float cosine=max(dot(n,d),0.0);
    if(scSunDirectionIntensity.w>0&&cosine>0&&SCVisible(p,n,d,scParameters.x))
        irradiance+=scSunColor.rgb*(scSunDirectionIntensity.w*cosine);
    for(int i=0;i<4;++i) {
        if(all(lessThanEqual(scPointColors[i].rgb,vec3(0))))continue;
        vec3 delta=scPointPositions[i].xyz-p;float distance=length(delta);if(distance<1e-5)continue;
        d=delta/distance;cosine=max(dot(n,d),0.0);float radius=max(scPointColors[i].w,0.0);
        if(cosine>0&&SCVisible(p,n,d,distance-radius))
            irradiance+=scPointColors[i].rgb*cosine/max(max(distance*distance,radius*radius),.0001);
    }
    if(any(greaterThan(scSpotIntensityInner.rgb,vec3(0)))) {
        vec3 delta=scSpotPositionRadius.xyz-p;float distance=length(delta);
        if(distance>1e-5) {
            d=delta/distance;cosine=max(dot(n,d),0.0);
            float beam=clamp((dot(scSpotDirectionOuter.xyz,-d)-scSpotDirectionOuter.w)/max(scSpotIntensityInner.w-scSpotDirectionOuter.w,1e-6),0.0,1.0);
            beam=beam*beam*(3-2*beam);float radius=scSpotPositionRadius.w;
            if(cosine>0&&beam>0&&SCVisible(p,n,d,distance-radius))
                irradiance+=scSpotIntensityInner.rgb*(cosine*beam/max(max(distance*distance,radius*radius),.0001));
        }
    }
    for(int i=0;i<4;++i) {
        SCAreaLight light=scAreaLights[i];
        if(light.center.w<.5||all(lessThanEqual(light.radiance.rgb,vec3(0))))continue;
        vec3 U=light.u.xyz,V=light.v.xyz,areaVector=cross(U,V);
        float quarterArea=length(areaVector);if(quarterArea<=1e-12)continue;
        vec3 emitterNormal=areaVector/quarterArea,toCenter=light.center.xyz-p;
        float emitterSideMax=dot(emitterNormal,-toCenter)+abs(dot(emitterNormal,U))+abs(dot(emitterNormal,V));
        if(light.radiance.w<.5&&emitterSideMax<=0)continue;
        if(dot(n,toCenter)+abs(dot(n,U))+abs(dot(n,V))<=0)continue;
        int grid=light.u.w>6?8:4;float cellArea=4*quarterArea/float(grid*grid);
        for(int y=0;y<grid;++y)for(int x=0;x<grid;++x) {
            vec2 parameter=(vec2(x,y)+.5)*(2.0/float(grid))-1;
            vec3 delta=light.center.xyz+U*parameter.x+V*parameter.y-p;
            float distanceSquared=max(dot(delta,delta),max(cellArea*1e-8,1e-12)),distance=sqrt(distanceSquared);
            d=delta/distance;float receiver=max(dot(n,d),0),face=dot(emitterNormal,-d);
            float geometricHorizon=smoothstep(0.0,.05,dot(n,d));
            float emitter=light.radiance.w>.5?abs(face):max(face,0.0);
            if(receiver>0&&emitter>0&&SCVisible(p,n,d,distance))
                irradiance+=light.radiance.rgb*(cellArea*receiver*emitter*geometricHorizon/distanceSquared);
        }
    }
    return irradiance;
}
)GLSL";}
inline std::string LightingShader(){return std::string("#version 450 core\n")+LookupGLSL()+GeometryGLSL()+R"GLSL(
in vec2 vUV;layout(location=0) out vec4 outColor;
void main(){int index=int(gl_FragCoord.y)*scAtlas.x+int(gl_FragCoord.x);
    outColor=vec4(0);if(index>=scAtlas.z)return;
    vec4 p=SCAttribute(index),n=SCAttribute(scAtlas.y+index);if(p.w<.5)return;
    int card=int(n.w+.5)-1;
    vec3 rho=SCAttribute(2*scAtlas.y+index).rgb;
    outColor=vec4(clamp(rho*SCDirect(p.xyz,normalize(n.xyz))/3.14159265359+SCMeta(card,8).rgb,vec3(0),vec3(60000)),1);
}
)GLSL";}
inline std::string GatherShader(){return std::string("#version 450 core\n")+LookupGLSL()+GeometryGLSL()+R"GLSL(
in vec2 vUV;layout(location=0) out vec4 outColor;
layout(binding=2) uniform sampler2D scViewPositions;
layout(binding=3) uniform sampler2D scWorldNormals;
layout(binding=4) uniform sampler2D scReceiverAlbedo;
uint SCHash(uint h){h=(h^(h>>16))*0x7feb352du;h=(h^(h>>15))*0x846ca68bu;return h^(h>>16);}
void main(){ivec2 pixel=ivec2(gl_FragCoord.xy);vec4 view=texelFetch(scViewPositions,pixel,0),normal=texelFetch(scWorldNormals,pixel,0);
    outColor=vec4(0);if(view.w<.5||normal.w<.5)return;
    vec3 n=normalize(normal.xyz),p=(scInverseView*vec4(view.xyz,1)).xyz;
    vec3 axis=abs(n.z)<.99?vec3(0,0,1):vec3(0,1,0),t=normalize(cross(axis,n)),b=cross(n,t);
    // The unused sun-color W carries a reference-only sampling seed. Seed zero
    // preserves the original pixel sequence exactly; screen probes use their
    // independent angular sampler and never read this value.
    uint pixelSeed=SCHash((uint(pixel.x)+uint(pixel.y)*uint(scScreen.x))^
        (floatBitsToUint(scSunColor.w)*0x9e3779b9u));
    vec3 sum=vec3(0);int readable=0;
    for(int ray=0;ray<32;++ray){if(ray>=scScreen.z)break;
        uint seed=SCHash(pixelSeed^SCHash(uint(ray)+0x9e3779b9u));
        float radial=float(seed>>13)/524288.0,azimuth=float(SCHash(seed^0x68bc21ebu)>>13)/524288.0;
        float u=(float(ray)+radial)/float(scScreen.z),phi=6.283185307*azimuth;
        vec3 d=t*(sqrt(u)*cos(phi))+b*(sqrt(u)*sin(phi))+n*sqrt(1-u),hitNormal;bool exhausted;int triangle;
        float distance=scParameters.x;
        if(SCIntersect(p+n*scParameters.y,d,distance,hitNormal,triangle,exhausted,false)) {
            // A one-sided diffuse surface emits only into its outward hemisphere.
            int owner=SCTriangleOwner(triangle);vec3 sampledLighting;
            if(!exhausted&&dot(hitNormal,-d)<=0)++readable; // Known black, not a cache hole.
            else if(!exhausted&&owner>=0&&SCSampleLightingObject(p+n*scParameters.y+d*distance,hitNormal,owner,sampledLighting))
                {sum+=sampledLighting;++readable;}
        } else if(!exhausted)++readable; // A traced empty ray is valid black.
    }
    vec3 rho=texelFetch(scReceiverAlbedo,pixel,0).rgb;
    outColor=vec4(rho*sum*(scParameters.z/float(scScreen.z)),float(readable)/float(scScreen.z));
}
)GLSL";}
inline std::string ResolveShader(bool coverage=false){return std::string("#version 450 core\n")+(coverage?"#define SC_COVERAGE 1\n":"#define SC_COVERAGE 0\n")+LookupGLSL()+R"GLSL(
in vec2 vUV;layout(location=0) out vec4 outColor;
layout(binding=0) uniform sampler2D scDirectScene;
layout(binding=1) uniform sampler2D scRawGi;
layout(binding=2) uniform sampler2D scViewPositions;
layout(binding=3) uniform sampler2D scWorldNormals;
void main(){ivec2 pixel=ivec2(gl_FragCoord.xy);vec4 raw=texelFetch(scRawGi,pixel,0),view=texelFetch(scViewPositions,pixel,0),normal=texelFetch(scWorldNormals,pixel,0);
    vec3 base=texelFetch(scDirectScene,pixel,0).rgb;outColor=vec4(base,1);
    bool valid=view.w>.5&&normal.w>.5;int index=-1;
#if !SC_COVERAGE
    if(scScreen.w==4){ivec2 size=textureSize(scLighting,0);ivec2 at=clamp(ivec2(vUV*vec2(size)),ivec2(0),size-1);outColor=texelFetch(scLighting,at,0);return;}
    if(scScreen.w==0){outColor=vec4(base+raw.rgb,1);return;}
    if(scScreen.w==2){outColor=vec4(raw.rgb,1);return;}
#endif
    bool covered=valid&&SCLookup((scInverseView*vec4(view.xyz,1)).xyz,normalize(normal.xyz),index);
    vec3 diagnostic=!valid?vec3(0):(covered?vec3(.04,.8,.18):vec3(1,.03,.65));
#if SC_COVERAGE
    outColor=vec4(diagnostic,(scScreen.w==1||scScreen.w==3||scScreen.w==4)?(covered?1:0):raw.a);return;
#else
    if(!valid)return;
    if(scScreen.w==1) {
        vec3 lighting;bool readable=SCSampleLighting((scInverseView*vec4(view.xyz,1)).xyz,normalize(normal.xyz),lighting);
        outColor=vec4(readable?lighting:diagnostic,1);
    }
    else if(scScreen.w==2)outColor=vec4(raw.rgb,1);
    else if(scScreen.w==3)outColor=vec4(diagnostic,1);
    else outColor=vec4(base+raw.rgb,1);
#endif
}
)GLSL";}
}
