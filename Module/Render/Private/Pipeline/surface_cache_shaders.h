#pragma once
#include <string>
#include "Render/Public/Pipeline/area_lights.h"
#include "Render/Public/Pipeline/software_bvh_visibility.h"
#include "ue_lumen_card_shadow_visibility_shaders.h"
namespace Render::SurfaceCacheDetail {
inline std::string LookupGLSL(bool reuseReceiverLookup=false,bool fastReceiverAddressingOnly=false) {auto source=SurfaceCacheDataGLSL()+R"GLSL(
layout(binding=12) uniform sampler2D scAttributes;
layout(binding=13) uniform sampler2D scMetadata;
layout(binding=14) uniform sampler2D scLighting;
vec4 SCMeta(int card,int part);
vec4 SCAttribute(int index) {
    vec4 value=texelFetch(scAttributes,ivec2(index%scAtlas.x,index/scAtlas.x),0);
    // The optional fourth plane is the captured material shading normal.
    // Keep the original geometry normal and albedo planes unchanged: lookup,
    // layer rejection and safe ray launches must still use the real face.
    if(index>=3*scAtlas.y&&index<4*scAtlas.y&&(int(scParameters.w+.5)&768)==768&&value.w>=.5&&scAtlas.w>0) {
        int texel=index-3*scAtlas.y;if(texel>=scAtlas.z)return value;
        int card=texel/(scAtlas.z/scAtlas.w);
        mat3 worldToLocal=mat3(SCMeta(card,0).xyz,SCMeta(card,1).xyz,SCMeta(card,2).xyz);
        return vec4(normalize(transpose(worldToLocal)*value.xyz),value.w);
    }
    if((int(scParameters.w+.5)&256)==0||index>=2*scAtlas.y||value.w<.5||scAtlas.w<=0)return value;
    int texel=index%scAtlas.y;if(texel>=scAtlas.z)return value;
    int card=texel/(scAtlas.z/scAtlas.w),objectFirst=card-card%6;
    if(index<scAtlas.y) {
        mat4 localToWorld=mat4(SCMeta(objectFirst,11),SCMeta(objectFirst+1,11),SCMeta(objectFirst+2,11),SCMeta(objectFirst+3,11));
        return vec4((localToWorld*value).xyz,value.w);
    }
    mat3 worldToLocal=mat3(SCMeta(card,0).xyz,SCMeta(card,1).xyz,SCMeta(card,2).xyz);
    return vec4(normalize(transpose(worldToLocal)*value.xyz),value.w);
}
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
)GLSL";
    if(!reuseReceiverLookup&&!fastReceiverAddressingOnly)return source;
    // Receiver-only execution variant. Keep the original lookup above as the
    // source for disabled/default consumers and preserve every card, anchor,
    // tap and accumulation decision. Immutable atlas values can be carried
    // from the already selected anchor instead of fetching them again.
    const auto replace=[&](const std::string& from,const std::string& to) {
        const auto at=source.find(from);if(at!=std::string::npos)source.replace(at,from.size(),to);
    };
    replace("layout(binding=14) uniform sampler2D scLighting;",R"GLSL(layout(binding=14) uniform sampler2D scLighting;
ivec2 SCAtlasAddress(int index) {
    if(scAtlas.x==1024&&index>=0)return ivec2(index&1023,index>>10);
    return ivec2(index%scAtlas.x,index/scAtlas.x);
})GLSL");
    const std::string oldAddress="ivec2(index%scAtlas.x,index/scAtlas.x)",newAddress="SCAtlasAddress(index)";
    // Leave the legacy fallback expression inside the helper untouched.
    auto at=source.find("vec4 SCAttribute(");
    while((at=source.find(oldAddress,at))!=std::string::npos) {
        source.replace(at,oldAddress.size(),newAddress);at+=newAddress.size();
    }
    replace("ivec2(anchor%scAtlas.x,anchor/scAtlas.x)","SCAtlasAddress(anchor)");
    // Address-only experiment retains the original anchor lookup/fetches and
    // does not carry extra position/normal values through the lookup function.
    // The earlier context-plus-address variant keeps priority when requested.
    if(!reuseReceiverLookup)return source;
    replace("bool SCLookupObject(vec3 p,vec3 n,int object,out int result) {",R"GLSL(bool SCLookupObjectContext(vec3 p,vec3 n,int object,out int result,out vec4 resultPosition,out vec4 resultNormal) {
    resultPosition=vec4(0);resultNormal=vec4(0);)GLSL");
    replace("float best=1e30;int selected=-1;","float best=1e30;int selected=-1;vec4 selectedPosition=vec4(0),selectedNormal=vec4(0);");
    replace("if(distance<best){best=distance;selected=index;}",
        "if(distance<best){best=distance;selected=index;selectedPosition=position;selectedNormal=normal;}");
    replace("if(selected>=0){result=selected;return true;}",
        "if(selected>=0){result=selected;resultPosition=selectedPosition;resultNormal=selectedNormal;return true;}");
    replace("bool SCLookup(vec3 p,vec3 n,out int result){",R"GLSL(bool SCLookupObject(vec3 p,vec3 n,int object,out int result) {
    vec4 position,normal;return SCLookupObjectContext(p,n,object,result,position,normal);
}
bool SCLookup(vec3 p,vec3 n,out int result){)GLSL");
    replace("radiance=vec3(0);coverage=0;int anchor;\n    if(!SCLookupObject(p,n,object,anchor))return false;",
        "radiance=vec3(0);coverage=0;int anchor;vec4 anchorPosition,anchorNormal;\n    if(!SCLookupObjectContext(p,n,object,anchor,anchorPosition,anchorNormal))return false;");
    replace("    vec4 anchorNormal=SCAttribute(scAtlas.y+anchor),anchorPosition=SCAttribute(anchor);\n","");
    return source;
}
inline std::string GeometryGLSL(bool hardwareVisibility=false,bool hardwareIntersection=false,
    bool reuseClosestBounds=false,bool fastGeometryAddressing=false){
    return SoftwareBvhVisibilityGLSL(9,reuseClosestBounds,fastGeometryAddressing)+
        ((hardwareVisibility||hardwareIntersection)?NativeSurfaceCacheIntersectionGLSL():std::string{})+
        (hardwareVisibility?"\n#define SC_DIRECT_VISIBLE SCNativeVisible\n":"\n#define SC_DIRECT_VISIBLE SCVisible\n")+
        AreaLightSamplingGLSL()+R"GLSL(
layout(std140,binding=3) uniform SCPointLights {
    vec4 scPointPositions[4],scPointColors[4],scUnusedAmbient,scUnusedOutput,scUnusedSpecular;
};
struct SCAreaLight {vec4 center,u,v,radiance;};
layout(std140,binding=8) uniform SCAreaLights {SCAreaLight scAreaLights[4];};
vec3 SCDirect(vec3 p,vec3 n) {
    vec3 irradiance=vec3(0);vec3 d=-scSunDirectionIntensity.xyz;float cosine=max(dot(n,d),0.0);
    if(scSunDirectionIntensity.w>0&&cosine>0&&SC_DIRECT_VISIBLE(p,n,d,scParameters.x))
        irradiance+=scSunColor.rgb*(scSunDirectionIntensity.w*cosine);
    for(int i=0;i<4;++i) {
        if(all(lessThanEqual(scPointColors[i].rgb,vec3(0))))continue;
        vec3 delta=scPointPositions[i].xyz-p;float distance=length(delta);if(distance<1e-5)continue;
        d=delta/distance;cosine=max(dot(n,d),0.0);float radius=max(scPointColors[i].w,0.0);
        if(cosine>0&&SC_DIRECT_VISIBLE(p,n,d,distance-radius))
            irradiance+=scPointColors[i].rgb*cosine/max(max(distance*distance,radius*radius),.0001);
    }
    if(any(greaterThan(scSpotIntensityInner.rgb,vec3(0)))) {
        vec3 delta=scSpotPositionRadius.xyz-p;float distance=length(delta);
        if(distance>1e-5) {
            d=delta/distance;cosine=max(dot(n,d),0.0);
            float beam=clamp((dot(scSpotDirectionOuter.xyz,-d)-scSpotDirectionOuter.w)/max(scSpotIntensityInner.w-scSpotDirectionOuter.w,1e-6),0.0,1.0);
            beam=beam*beam*(3-2*beam);float radius=scSpotPositionRadius.w;
            if(cosine>0&&beam>0&&SC_DIRECT_VISIBLE(p,n,d,distance-radius))
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
        int grid=abs(light.u.w)>6?8:4;float cellArea=4*quarterArea/float(grid*grid);
        for(int y=0;y<grid;++y)for(int x=0;x<grid;++x) {
            vec2 parameter=AreaLightSampleParameters(x,y,grid,light.u.w<0);
            vec3 delta=light.center.xyz+U*parameter.x+V*parameter.y-p;
            float distanceSquared=max(dot(delta,delta),max(cellArea*1e-8,1e-12)),distance=sqrt(distanceSquared);
            d=delta/distance;float receiver=max(dot(n,d),0),face=dot(emitterNormal,-d);
            float geometricHorizon=smoothstep(0.0,.05,dot(n,d));
            float emitter=light.radiance.w>.5?abs(face):max(face,0.0);
            if(receiver>0&&emitter>0&&SC_DIRECT_VISIBLE(p,n,d,distance))
                irradiance+=light.radiance.rgb*(cellArea*receiver*emitter*geometricHorizon/distanceSquared);
        }
    }
    return irradiance;
}
)GLSL";}
inline std::string UeCardLightingGeometryGLSL(bool hardwareVisibility) {
    auto geometry=GeometryGLSL(hardwareVisibility);
    const auto first=geometry.find("vec3 SCDirect(vec3 p,vec3 n) {");
    if(first==std::string::npos)throw std::logic_error("Surface cache direct lighting source anchor missing");
    // Derive the material-normal variant from the original function. Leave
    // SCDirect verbatim for legacy lighting and WGUnoccluded extraction.
    auto shaded=geometry.substr(first);
    const auto replaceAll=[&](const std::string& from,const std::string& to) {
        std::size_t at=0;bool found=false;
        while((at=shaded.find(from,at))!=std::string::npos) {
            shaded.replace(at,from.size(),to);at+=to.size();found=true;
        }
        if(!found)throw std::logic_error("Surface cache shading normal source anchor missing");
    };
    replaceAll("vec3 SCDirect(vec3 p,vec3 n)","vec3 SCDirectShaded(vec3 p,vec3 shadeN,vec3 geoN)");
    replaceAll("dot(n,","dot(shadeN,");
    replaceAll("SC_DIRECT_VISIBLE(p,n,","UECardDirectVisible(p,geoN,");
    // UE evaluates irradiance with the material normal. A separate flat-face
    // horizon would reintroduce triangle steps into a smooth material normal.
    // Keep the geometric normal in the unchanged shadow visibility calls.
    replaceAll("            float geometricHorizon=smoothstep(0.0,.05,dot(shadeN,d));\n", "");
    replaceAll("cellArea*receiver*emitter*geometricHorizon/distanceSquared",
        "cellArea*receiver*emitter/distanceSquared");
    // Carry a scalar visibility for primary pixel-normal reconstruction. RGB
    // remains outgoing radiance for every existing GI consumer. Accumulate
    // both terms in the original area-light loop with the same shadow call.
    replaceAll("vec3 irradiance=vec3(0);", "ueCardUnoccludedIrradiance=vec3(0);ueCardShadowIrradiance=vec3(0);vec3 irradiance=vec3(0);");
    replaceAll("            if(receiver>0&&emitter>0&&UECardDirectVisible(p,geoN,d,distance))\n"
        "                irradiance+=light.radiance.rgb*(cellArea*receiver*emitter/distanceSquared);",
        "            if(receiver>0&&emitter>0){\n"
        "                vec3 energy=light.radiance.rgb*(cellArea*receiver*emitter/distanceSquared);\n"
        "                ueCardUnoccludedIrradiance+=energy;\n"
        "                if(UECardDirectVisible(p,geoN,d,distance)){irradiance+=energy;ueCardShadowIrradiance+=energy;}\n"
        "            }");
    return geometry+UeLumenCardShadowVisibilityGLSL(hardwareVisibility)+
        "\nvec3 ueCardUnoccludedIrradiance=vec3(0),ueCardShadowIrradiance=vec3(0);\n"+shaded;
}
inline std::string LightingShader(bool hardwareVisibility=false,bool diagnostics=false,bool ueCardLighting=false){
    if(ueCardLighting) {
        auto geometry=UeCardLightingGeometryGLSL(hardwareVisibility);
        if(diagnostics)geometry=InstrumentSurfaceCacheTraceDiagnosticsGLSL(std::move(geometry));
        return std::string("#version 450 core\n")+LookupGLSL()+geometry+
            (diagnostics?"\nin vec2 vUV;layout(location=0) out vec4 outColor;layout(location=1) out vec4 outQueries;\n":
                "\nin vec2 vUV;layout(location=0) out vec4 outColor;\n")+
            "void main(){int index=int(gl_FragCoord.y)*scAtlas.x+int(gl_FragCoord.x);\n"+
            (diagnostics?"    SCTraceDiagReset();outQueries=vec4(0,0,0,1);\n":"")+R"GLSL(
    outColor=vec4(0);if(index>=scAtlas.z)return;
    vec4 p=SCAttribute(index),n=SCAttribute(scAtlas.y+index);if(p.w<.5)return;
    int card=int(n.w+.5)-1;
    vec3 geoN=normalize(n.xyz),shadeN=geoN;
    if((int(scParameters.w+.5)&512)!=0) {
        vec4 shading=SCAttribute(3*scAtlas.y+index);
        float lengthSquared=dot(shading.xyz,shading.xyz);
        if(shading.w>=.5&&!any(isnan(shading.xyz))&&!any(isinf(shading.xyz))&&lengthSquared>1e-12)
            shadeN=normalize(shading.xyz);
    }
    vec3 rho=SCAttribute(2*scAtlas.y+index).rgb;
    vec3 direct=SCDirectShaded(p.xyz,shadeN,geoN);
    if(ueCardDirectInputsReadable){
        vec3 luma=vec3(.2126,.7152,.0722);float unoccluded=dot(ueCardUnoccludedIrradiance,luma);
        float visibility=unoccluded>0?clamp(dot(ueCardShadowIrradiance,luma)/max(unoccluded,1e-6),0.0,1.0):0.0;
        // Alpha 0 remains unknown; 1 is legacy; [2,3] carries versioned V.
        outColor=vec4(clamp(rho*direct/3.14159265359+SCMeta(card,8).rgb,vec3(0),vec3(60000)),2.0+visibility);
    }
)GLSL"+(diagnostics?"    outQueries=vec4(float(scTraceDiag[0].x),float(scTraceDiag[1].y),1,1);\n":"")+"}\n";
    }
    // The diagnostic variant observes the same SCDirect invocation that writes
    // the lighting atlas. The ordinary source below is kept unchanged.
    if(diagnostics)return std::string("#version 450 core\n")+LookupGLSL()+
        InstrumentSurfaceCacheTraceDiagnosticsGLSL(GeometryGLSL(hardwareVisibility))+R"GLSL(
in vec2 vUV;layout(location=0) out vec4 outColor;layout(location=1) out vec4 outQueries;
void main(){int index=int(gl_FragCoord.y)*scAtlas.x+int(gl_FragCoord.x);
    SCTraceDiagReset();outQueries=vec4(0,0,0,1);
    outColor=vec4(0);if(index>=scAtlas.z)return;
    vec4 p=SCAttribute(index),n=SCAttribute(scAtlas.y+index);if(p.w<.5)return;
    int card=int(n.w+.5)-1;
    vec3 rho=SCAttribute(2*scAtlas.y+index).rgb;
    outColor=vec4(clamp(rho*SCDirect(p.xyz,normalize(n.xyz))/3.14159265359+SCMeta(card,8).rgb,vec3(0),vec3(60000)),1);
    outQueries=vec4(float(scTraceDiag[0].x),float(scTraceDiag[1].y),1,1);
}
)GLSL";
    return std::string("#version 450 core\n")+LookupGLSL()+GeometryGLSL(hardwareVisibility)+R"GLSL(
in vec2 vUV;layout(location=0) out vec4 outColor;
void main(){int index=int(gl_FragCoord.y)*scAtlas.x+int(gl_FragCoord.x);
    outColor=vec4(0);if(index>=scAtlas.z)return;
    vec4 p=SCAttribute(index),n=SCAttribute(scAtlas.y+index);if(p.w<.5)return;
    int card=int(n.w+.5)-1;
    vec3 rho=SCAttribute(2*scAtlas.y+index).rgb;
    outColor=vec4(clamp(rho*SCDirect(p.xyz,normalize(n.xyz))/3.14159265359+SCMeta(card,8).rgb,vec3(0),vec3(60000)),1);
}
)GLSL";}
inline std::string LightingQueryDiagnosticShader(){return std::string("#version 450 core\n")+LookupGLSL()+
    InstrumentSurfaceCacheTraceDiagnosticsGLSL(GeometryGLSL(true))+R"GLSL(
in vec2 vUV;layout(location=0) out vec4 outColor;
void main(){outColor=vec4(0);int index=int(gl_FragCoord.y)*scAtlas.x+int(gl_FragCoord.x);if(index>=scAtlas.z)return;
    vec4 p=SCAttribute(index),n=SCAttribute(scAtlas.y+index);if(p.w<.5)return;
    SCTraceDiagReset();vec3 direct=SCDirect(p.xyz,normalize(n.xyz));
    outColor=vec4(float(scTraceDiag[0].x),float(scTraceDiag[1].y),direct.x,1);
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
