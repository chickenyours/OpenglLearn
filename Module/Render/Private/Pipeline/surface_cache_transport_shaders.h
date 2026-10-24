#pragma once
#include "surface_cache_contact_shaders.h"
#include "surface_cache_lookup_context.h"
#include "surface_cache_candidate_intersection.h"
#include "ue_lumen_card_direct_sample_shaders.h"
#include "ue_lumen_card_world_attributes_shaders.h"
namespace Render::SurfaceCacheDetail {
inline std::string TransportCommonGLSL(bool diagnostics=false,bool compactLookup=false,bool nativeCandidates=false) {
    auto geometry=GeometryGLSL(false,true,true,true);
    if(diagnostics)geometry=InstrumentSurfaceCacheTraceDiagnosticsGLSL(geometry);
    const std::string miss=diagnostics?"if(!anyHit){++scTraceDiag[0].z;return SCIntersect(p,d,maximum,n,triangle,exhausted,false);}":
        "if(!anyHit)return SCIntersect(p,d,maximum,n,triangle,exhausted,false);";
    const auto at=geometry.find(miss);if(at==std::string::npos)throw std::logic_error("Native empty segment anchor changed");
    geometry.replace(at,miss.size(),diagnostics?"if(!anyHit){++scTraceDiag[0].z;return false;}":"if(!anyHit)return false;");
    const auto function=[&](const std::string& name){const auto first=geometry.find("bool "+name+"(");
        if(first==std::string::npos)throw std::logic_error("Transport intersection helper missing");
        auto end=geometry.find('{',first);unsigned depth=1;while(++end<geometry.size()&&depth){if(geometry[end]=='{')++depth;else if(geometry[end]=='}')--depth;}
        return std::pair(first,end-first);};
    auto [softwareAt,softwareLength]=function("SCIntersect");auto strict=geometry.substr(softwareAt,softwareLength);
    const auto replaceAll=[](std::string& text,const std::string& from,const std::string& to){std::size_t pos=0;
        while((pos=text.find(from,pos))!=std::string::npos){text.replace(pos,from.size(),to);pos+=to.size();}};
    replaceAll(strict,"SCIntersect(","WGStrictIntersect(");
    replaceAll(strict,"u< -1e-6||u>1.000001","u<0||u>1");replaceAll(strict,"v< -1e-6||u+v>1.000001","v<0||u+v>1");
    // Expanded native triangles can nominate a numerically ambiguous edge.
    // Only that rare software fallback needs double predicates; the ordinary
    // native hit and all radiance arithmetic remain float. Older devices keep
    // the strict float fallback without acquiring a float64 requirement.
    auto strict64=strict;
    replaceAll(strict64,"vec3 a=SCGeometry(offset).xyz,ab=SCGeometry(offset+1).xyz-a,ac=SCGeometry(offset+2).xyz-a;",
        "int base=(int(low.w)+i)*3;dvec3 a=WGPreciseVertex(base,SCGeometry(offset).xyz),ab=WGPreciseVertex(base+1,SCGeometry(offset+1).xyz)-a,ac=WGPreciseVertex(base+2,SCGeometry(offset+2).xyz)-a;");
    replaceAll(strict64,"vec3 q=cross(d,ac);float determinant=dot(ab,q);","dvec3 q=cross(dvec3(d),ac);double determinant=dot(ab,q);");
    replaceAll(strict64,"vec3 relative=p-a;float u=", "dvec3 relative=dvec3(p)-a;double u=");
    replaceAll(strict64,"vec3 r=cross(relative,ab);float v=dot(d,r)","dvec3 r=cross(relative,ab);double v=dot(dvec3(d),r)");
    replaceAll(strict64,"float distance=dot(ac,r)","double distance=dot(ac,r)");
    replaceAll(strict64,"distance<1e-5", "distance<double(1)/double(100000)");
    replaceAll(strict64,"maximum=distance;n=normalize(cross(ab,ac));","maximum=float(distance);n=vec3(normalize(cross(ab,ac)));");
    replaceAll(strict64,"int base=(int(low.w)+i)*3;",R"GLSL(
vec3 fa=SCGeometry(offset).xyz,fb=SCGeometry(offset+1).xyz,fc=SCGeometry(offset+2).xyz;float triangleNear;
if(!WGPreciseBox(p,d,min(fa,min(fb,fc)),max(fa,max(fb,fc)),maximum,triangleNear))continue;
int base=(int(low.w)+i)*3;
)GLSL");
    replaceAll(strict64,"SCBox(","WGPreciseBox(");
    strict="#ifdef RENDER_NATIVE_FLOAT64\n"+std::string(R"GLSL(
bool WGPreciseBox(vec3 p,vec3 d,vec3 low,vec3 high,float maximum,out float near){
    vec3 margin=max(vec3(.000001),4.0*1.1920928955078125e-7*max(abs(low),abs(high)));
    return SCBox(p,d,low-margin,high+margin,maximum,near);
}
)GLSL")+strict64+"\n#else\n"+strict+"\n#endif\n";
    auto [nativeAt,nativeLength]=function("SCNativeIntersect");auto native=geometry.substr(nativeAt,nativeLength);
    replaceAll(native,"SCIntersect(","WGPreciseOrStrict(");
    replaceAll(native,"u< -1e-6||u>1.000001","u<0||u>1");replaceAll(native,"v< -1e-6||u+v>1.000001","v<0||u+v>1");
    const std::string normal="    n=normalize(cross(ab,ac));";
    const auto normalAt=native.find(normal);if(normalAt==std::string::npos)throw std::logic_error("Transport native predicate anchor changed");
    native.insert(normalAt,"    if(distance<.0001||abs(rayQueryGetIntersectionTEXT(query,true)-distance)>max(1e-7,abs(distance)*2e-6))\n"
        "        return WGPreciseOrStrict(p,d,maximum,n,triangle,exhausted,anyHit);\n");
    const std::string preciseCandidate=R"GLSL(
bool WGPreciseOrStrict(vec3 p,vec3 d,inout float maximum,out vec3 n,inout int triangle,out bool exhausted,bool anyHit){
#ifdef RENDER_NATIVE_FLOAT64
    // Validate the native candidate before considering a complete BVH walk.
    // A shared edge normally still names a real original triangle; resolving
    // that single triangle avoids millions of double leaf predicates.
    if(triangle>=0&&triangle<scGeometry.w){int offset=scGeometry.x+triangle*5,base=triangle*3;
        dvec3 a=WGPreciseVertex(base,SCGeometry(offset).xyz),ab=WGPreciseVertex(base+1,SCGeometry(offset+1).xyz)-a,
            ac=WGPreciseVertex(base+2,SCGeometry(offset+2).xyz)-a,dir=dvec3(d),q=cross(dir,ac);
        double determinant=dot(ab,q);
        if(abs(determinant)>=1e-9){dvec3 relative=dvec3(p)-a,r=cross(relative,ab);
            double u=dot(relative,q)/determinant,v=dot(dir,r)/determinant,distance=dot(ac,r)/determinant;
            if(u>=0&&v>=0&&u+v<=1&&distance>=double(1)/double(100000)&&distance<double(maximum)){
                maximum=float(distance);n=vec3(normalize(cross(ab,ac)));exhausted=false;return true;}}
    }
#endif
    return WGStrictIntersect(p,d,maximum,n,triangle,exhausted,anyHit);
}
)GLSL";
    geometry.replace(nativeAt,nativeLength,strict+"\n"+preciseCandidate+native);
    if(nativeCandidates){const auto [at,length]=function("SCNativeIntersect");
        geometry.replace(at,length,TransportCandidateIntersectionGLSL(diagnostics));}
    return (compactLookup?TransportLookupContextGLSL():LookupGLSL(true))+R"GLSL(
layout(binding=15) uniform sampler2D wgGeometryResidual;
#ifdef RENDER_NATIVE_FLOAT64
dvec3 WGPreciseVertex(int index,vec3 high){return dvec3(high)+dvec3(texelFetch(wgGeometryResidual,ivec2(index&1023,index>>10),0).xyz);}
#endif
)GLSL"+geometry+R"GLSL(
layout(std140,binding=14) uniform TransportData {
    ivec4 wgSchedule; // phase, update period (zero resets), first/last dirty card
    vec4 wgParameters; // receiver risk radius, transport directions, contact directions, transport texel stride
    vec4 wgLaunchBias;
    vec4 wgReceiverOptions; // maximum lattice grid, geometry risk mode, near-hit radius, reserved
    mat4 wgViewProjection;
    vec4 wgBounds[512];
};
layout(binding=2) uniform sampler2D wgPositions;
layout(binding=3) uniform sampler2D wgNormals;
bool WGRequested(vec3 p,vec3 n){vec3 toEye=scInverseView[3].xyz-p;vec4 clip=wgViewProjection*vec4(p,1);
    return dot(n,toEye)>=-.05*length(toEye)&&clip.w>0&&all(lessThanEqual(abs(clip.xy),vec2(1.75*clip.w)));}
bool WGVisibleSource(vec3 p,vec3 n,int card){
    vec4 clip=wgViewProjection*vec4(p,1);if(clip.w<=0)return false;vec2 uv=clip.xy/clip.w*.5+.5;
    if(any(lessThan(uv,vec2(0)))||any(greaterThanEqual(uv,vec2(1))))return false;
    ivec2 pixel=clamp(ivec2(uv*vec2(scScreen.xy)),ivec2(0),scScreen.xy-1);
    vec4 source=texelFetch(wgPositions,pixel,0),normal=texelFetch(wgNormals,pixel,0);
    if(source.w<.5||normal.w<.5||dot(n,normal.xyz)<.6)return false;
    if(source.w>=1&&source.w==floor(source.w)&&int(source.w)-1!=card/6)return false;
    vec3 q=(scInverseView*vec4(source.xyz,1)).xyz,delta=q-p;
    float normalDifference=sqrt(max(0.0,2.0*(1.0-clamp(dot(n,normalize(normal.xyz)),-1.0,1.0))));
    return length(delta)<=max(.003,.01*clip.w)&&abs(dot(delta,n))<=max(.0003,.75*length(delta)*normalDifference);
}
int WGSchedulingIndex(int card,int cell,int resolution,int stride){int grid=(resolution+stride-1)/stride;
    int tiles=(grid+7)/8;
    // Schedule coherent 8x8 tiles instead of every fourth SIMD lane.
    return card+((cell/resolution)/stride/8)*tiles+(cell%resolution)/stride/8;}
uint WGHash(uint h){h=(h^(h>>16))*0x7feb352du;h=(h^(h>>15))*0x846ca68bu;return h^(h>>16);}
vec3 WGDirection(vec3 n,int ray,int count,uint seed,uint phase) {
    vec3 axis=abs(n.z)<.99?vec3(0,0,1):vec3(0,1,0),t=normalize(cross(axis,n)),b=cross(n,t);
    float jitter=(float((WGHash(seed^WGHash(uint(ray)))+phase*0xc13fa9a9u)>>14)+.5)/262144.0;
    float azimuth=float((bitfieldReverse(uint(ray))+WGHash(seed^0x68bc21ebu)+phase*0x91e10da5u)>>8)/16777216.0;
    float radial=(float(ray)+jitter)/float(count),phi=6.28318530718*azimuth;
    return t*(sqrt(radial)*cos(phi))+b*(sqrt(radial)*sin(phi))+n*sqrt(max(0.0,1.0-radial));
}
bool WGFinite(vec3 v){return !any(isnan(v))&&!any(isinf(v));}
bool WGAnchor(vec4 view,vec3 p,vec3 n,out int anchor,out int own){
    own=view.w>=1.0&&view.w==floor(view.w)?int(view.w)-1:-1;
    if(own>=scAtlas.w/6)return false;
    if(!SCLookupObject(p,n,own,anchor))return false;
    if(own<0)own=(int(SCAttribute(scAtlas.y+anchor).w+.5)-1)/6;
    return true;
}
bool WGNearRiskWithin(vec3 p,vec3 n,int own,float radius){
    for(int object=0;object<scAtlas.w/6;++object){if(object==own)continue;
        vec3 lo=wgBounds[object*2].xyz,hi=wgBounds[object*2+1].xyz;
        vec3 delta=max(max(lo-p,p-hi),vec3(0));
        float core=radius;if(dot(delta,delta)>=core*core)continue;
        if(dot((lo+hi)*.5-p,n)+dot((hi-lo)*.5,abs(n))<=1e-5)continue;
        return true;
    }return false;
}
bool WGNearRisk(vec3 p,vec3 n,int own){return WGNearRiskWithin(p,n,own,wgParameters.x);}
int WGReceiverGrid(){return min(clamp(scScreen.x/320,1,4),int(wgReceiverOptions.x+.5));}
bool WGReceiverDetail(ivec2 pixel,vec3 p,vec3 n,int own){
    if(WGNearRiskWithin(p,n,own,.03))return true;
    if(wgLaunchBias.y<.5||wgReceiverOptions.x<3.5||!WGNearRiskWithin(p,n,own,wgParameters.x))return false;
    int grid=WGReceiverGrid(),phase=wgSchedule.x%(grid*grid);if(grid==1)return false;
    ivec2 shift=ivec2(phase%grid,phase/grid);vec2 coord=vec2(pixel-shift)/float(grid),fraction=fract(coord);
    ivec2 first=ivec2(floor(coord))*grid+shift;float mass=0;
    float owner=texelFetch(wgPositions,pixel,0).w;
    // A clipped bilinear footprint must not become a one-sided lighting
    // extrapolation after renormalization. Use geometry alone to request a
    // real receiver measurement; neither RGB nor its brightness chooses work.
    for(int y=0;y<2;++y)for(int x=0;x<2;++x){ivec2 at=first+ivec2(x,y)*grid;
        float weight=(x==0?1-fraction.x:fraction.x)*(y==0?1-fraction.y:fraction.y);if(weight<=.000001)continue;
        if(any(lessThan(at,ivec2(0)))||any(greaterThanEqual(at,scScreen.xy)))continue;
        vec4 v=texelFetch(wgPositions,at,0),nd=texelFetch(wgNormals,at,0);
        if(v.w!=owner||nd.w<.5)continue;
        vec3 nn=normalize(nd.xyz),q=(scInverseView*vec4(v.xyz,1)).xyz,delta=q-p;
        float alignment=clamp(dot(n,nn),-1.0,1.0),curve=.75*length(delta)*sqrt(max(0.0,2.0*(1.0-alignment)));
        if(alignment<.95||max(abs(dot(delta,n)),abs(dot(delta,nn)))>max(.0003,curve))continue;
        bool clear=true;for(int step=1;step<4;++step){if(step>=grid)break;
            ivec2 middle=ivec2(floor(mix(vec2(pixel),vec2(at),float(step)/float(grid))+.5));
            vec4 m=texelFetch(wgPositions,middle,0),mn=texelFetch(wgNormals,middle,0);
            if(m.w!=owner||mn.w<.5||dot(n,normalize(mn.xyz))<.95){clear=false;break;}}
        if(clear)mass+=weight;
    }
    return mass<.999;
}
bool WGReceiverScheduled(ivec2 pixel,vec3 p,vec3 n,int own,bool cacheValid){
    if(wgLaunchBias.z<.5||WGReceiverDetail(pixel,p,n,own))return true;
    int grid=WGReceiverGrid(),phase=wgSchedule.x%(grid*grid);
    return pixel.x%grid==phase%grid&&pixel.y%grid==phase/grid;
}
bool WGReceiverPassScheduled(ivec2 pixel,vec3 p,vec3 n,int own,bool cacheValid){
    if(wgLaunchBias.z<.5)return true;
    bool core=WGReceiverDetail(pixel,p,n,own);
    return wgLaunchBias.w>.5?!core:core;
}
bool WGCornerClearance(inout vec3 origin,vec3 n,int own){
    vec3 initial=origin;float scale=max(1.0,max(abs(origin.x),max(abs(origin.y),abs(origin.z))));
    float epsilon=max(.00003,16.0*1.1920928955078125e-7*scale);
    // At adjoining objects, a bias along the receiver normal alone can leave
    // the origin within t_min of a second wall. Separate from conservative
    // bounds tangentially instead of discarding a genuine close blocker or
    // changing the query interval. In this room the displacement is < .12 mm.
    for(int pass=0;pass<2;++pass)for(int object=0;object<scAtlas.w/6;++object){
        if(object==own)continue;vec3 delta=origin-clamp(origin,wgBounds[object*2].xyz,wgBounds[object*2+1].xyz);
        float distance=length(delta);if(distance<=0||distance>=epsilon)continue;
        vec3 away=delta/distance;if(dot(away,n)<-.0001)continue;
        origin+=away*(epsilon-distance);
    }
    return WGFinite(origin)&&length(origin-initial)<=4.0*epsilon;
}
bool WGWorldOrigin(vec3 p,vec3 n,int card,out vec3 origin){
    origin=p;float window=max(.0002,8.0*wgLaunchBias.x*.001),distance=2.0*window;
    vec3 hitNormal;int triangle;bool exhausted;
    // A card can contain a hidden face touching another object. Certify the
    // outward side and original owner before displacing; never launch from
    // inside overlapping lamp trims or a coincident reverse-facing layer.
    vec3 start=p+n*window;
    if(!SCNativeIntersect(start,-n,distance,hitNormal,triangle,exhausted,false)||exhausted||
        dot(hitNormal,n)<=.5||SCTriangleOwner(triangle)!=card/6||abs(distance-window)>window*.5)return false;
    vec3 anchor=start-n*distance;float clearance=wgLaunchBias.x+2e-5;
    bool blocked=SCNativeIntersect(anchor,n,clearance,hitNormal,triangle,exhausted,false);
    if(exhausted)return false;float bias=blocked?min(wgLaunchBias.x,max(0.0,clearance-2e-5)*.5):wgLaunchBias.x;
    if(bias<2e-5)return false;origin=anchor+n*bias;return WGCornerClearance(origin,n,card/6);
}
vec3 WGHitLighting(vec3 origin,vec3 direction,float distance,vec3 normal,int triangle,out bool readable) {
    readable=true;if(dot(normal,-direction)<=0)return vec3(0);
    vec3 value;int owner=SCTriangleOwner(triangle);
    readable=owner>=0&&SCSampleLightingObject(origin+direction*distance,normal,owner,value)&&WGFinite(value);
    return readable?max(value,vec3(0)):vec3(0);
}
)GLSL";}
inline std::string TransportUpdateShader(bool nativeCandidates=false) {return std::string("#version 450 core\n")+TransportCommonGLSL(false,false,nativeCandidates)+R"GLSL(
in vec2 vUV;layout(location=0) out vec4 outColor;
layout(location=1) out vec4 outBx;layout(location=2) out vec4 outBy;layout(location=3) out vec4 outBz;
layout(binding=0) uniform sampler2D wgPrevious;
layout(binding=1) uniform sampler2D wgPreviousBx;
layout(binding=6) uniform sampler2D wgPreviousBy;
layout(binding=7) uniform sampler2D wgPreviousBz;
void main(){ivec2 pixel=ivec2(gl_FragCoord.xy);int index=pixel.y*scAtlas.x+pixel.x;
    outColor=vec4(0);outBx=vec4(0);outBy=vec4(0);outBz=vec4(0);if(index>=scAtlas.z)return;
    vec4 position=SCAttribute(index),normal=SCAttribute(scAtlas.y+index);
    if(position.w<.5||dot(normal.xyz,normal.xyz)<.5)return;
    int card=int(normal.w+.5)-1;
    int resolution=int(SCMeta(card,9).w),cell=index-int(SCMeta(card,5).w),stride=int(wgParameters.w+.5);
    if((cell%resolution)%stride!=0||(cell/resolution)%stride!=0)return;
    bool dirty=card>=wgSchedule.z&&card<=wgSchedule.w;
    bool reset=wgSchedule.y==0||dirty;
    vec4 previous=reset?vec4(0):texelFetch(wgPrevious,pixel,0);
    vec4 previousBx=reset?vec4(0):texelFetch(wgPreviousBx,pixel,0),previousBy=reset?vec4(0):texelFetch(wgPreviousBy,pixel,0),previousBz=reset?vec4(0):texelFetch(wgPreviousBz,pixel,0);
    if(!WGRequested(position.xyz,normalize(normal.xyz))||!WGVisibleSource(position.xyz,normalize(normal.xyz),card)){
        if(wgSchedule.z>wgSchedule.w){outColor=previous;outBx=previousBx;outBy=previousBy;outBz=previousBz;}return;}
    if(!reset&&previous.w>0&&WGSchedulingIndex(card,cell,resolution,stride)%wgSchedule.y!=wgSchedule.x%wgSchedule.y){outColor=previous;outBx=previousBx;outBy=previousBy;outBz=previousBz;return;}
    vec3 n=normalize(normal.xyz),origin=position.xyz;
    vec3 hitNormal;int triangle;bool exhausted;
    if(!WGWorldOrigin(position.xyz,n,card,origin))return;
    vec3 sum=vec3(0),directionSum=vec3(0),momentX=vec3(0),momentY=vec3(0),momentZ=vec3(0);int farCount=0,readableCount=0,count=int(wgParameters.y+.5);
    float nearReciprocal=0,nearMass=0,minimumHit=scParameters.x,nearRadius=max(wgReceiverOptions.z,.00001);
    for(int ray=0;ray<64;++ray){if(ray>=count)break;
        vec3 d=WGDirection(n,ray,count,WGHash(uint(index)),uint(wgSchedule.x));
        float distance=scParameters.x;bool found=SCNativeIntersect(origin,d,distance,hitNormal,triangle,exhausted,false);
        directionSum+=d;vec3 radiance=vec3(0);
        if(!exhausted){++farCount;if(!found)++readableCount;
            else {
                // Geometry has its own full-direction denominator. Black,
                // back-facing and cache-unknown hits still constrain reuse.
                if(wgReceiverOptions.y>.5){minimumHit=min(minimumHit,distance);
                    if(distance<nearRadius){float nearWeight=1.0-smoothstep(.75*nearRadius,nearRadius,distance);
                        nearReciprocal+=nearWeight/max(distance,.00001);nearMass+=nearWeight;}}
                bool readable;radiance=WGHitLighting(origin,d,distance,hitNormal,triangle,readable);readableCount+=readable?1:0;}}
        sum+=radiance;momentX+=radiance*d.x;momentY+=radiance*d.y;momentZ+=radiance*d.z;
    }
    vec3 value=sum/float(count);
    float age=reset||wgSchedule.z<=wgSchedule.w?0.0:min(previous.w,16.0);
    if(readableCount==0&&farCount>0)return;
    vec3 meanDirection=directionSum/float(count);
    vec3 cx=momentX/float(count)-value*meanDirection.x,cy=momentY/float(count)-value*meanDirection.y,cz=momentZ/float(count)-value*meanDirection.z;
    vec3 cn=cx*n.x+cy*n.y+cz*n.z,bx=4.0*cx+14.0*n.x*cn,by=4.0*cy+14.0*n.y*cn,bz=4.0*cz+14.0*n.z*cn;
    vec3 projection=bx*n.x+by*n.y+bz*n.z,limit=sqrt(max(bx*bx+by*by+bz*bz,vec3(0)))+(2.0/3.0)*projection;
    vec3 scale=min(vec3(1),value/max(limit,vec3(.000001)));bx*=scale;by*=scale;bz*=scale;
    float blend=1.0/(age+1.0);outColor=vec4(mix(previous.rgb,value,blend),age+1.0);
    vec3 geometrySupport=vec3(nearReciprocal/float(count),nearMass/float(count),minimumHit);
    vec3 previousSupport=vec3(previousBx.w,previousBy.w,previousBz.w);
    geometrySupport=wgReceiverOptions.y>.5?mix(previousSupport,geometrySupport,blend):vec3(0);
    outBx=vec4(mix(previousBx.rgb,bx,blend),geometrySupport.x);
    outBy=vec4(mix(previousBy.rgb,by,blend),geometrySupport.y);
    outBz=vec4(mix(previousBz.rgb,bz,blend),geometrySupport.z);
}
)GLSL";}
inline std::string TransportFilterShader(){return std::string("#version 450 core\n")+TransportCommonGLSL()+R"GLSL(
in vec2 vUV;layout(location=0) out vec4 outColor;
layout(location=1) out vec4 outBx;layout(location=2) out vec4 outBy;layout(location=3) out vec4 outBz;
layout(binding=0) uniform sampler2D wgInput;
layout(binding=1) uniform sampler2D wgInputBx;layout(binding=6) uniform sampler2D wgInputBy;layout(binding=7) uniform sampler2D wgInputBz;
void main(){ivec2 pixel=ivec2(gl_FragCoord.xy);vec4 center=texelFetch(wgInput,pixel,0);outColor=center;outBx=vec4(0);outBy=vec4(0);outBz=vec4(0);if(center.w<=0)return;
    int index=pixel.y*scAtlas.x+pixel.x;if(index>=scAtlas.z)return;
    vec4 position=SCAttribute(index),normal=SCAttribute(scAtlas.y+index);int card=int(normal.w+.5)-1;
    int resolution=int(SCMeta(card,9).w),first=int(SCMeta(card,5).w),stride=int(wgParameters.w+.5);
    ivec2 cell=ivec2((index-first)%resolution,(index-first)/resolution);vec3 n=normalize(normal.xyz),sum=vec3(0),sumBx=vec3(0),sumBy=vec3(0),sumBz=vec3(0),sumGeometry=vec3(0);float mass=0;
    for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x){ivec2 at=cell+ivec2(x,y)*stride;
        if(any(lessThan(at,ivec2(0)))||any(greaterThanEqual(at,ivec2(resolution))))continue;
        int other=first+at.y*resolution+at.x;vec4 value=texelFetch(wgInput,ivec2(other%scAtlas.x,other/scAtlas.x),0);
        if(value.w<=0)continue;vec4 p=SCAttribute(other),nd=SCAttribute(scAtlas.y+other);if(p.w<.5||int(nd.w+.5)-1!=card)continue;
        vec3 nn=normalize(nd.xyz),delta=p.xyz-position.xyz;float alignment=clamp(dot(n,nn),-1.0,1.0);if(alignment<.9)continue;
        float curve=.75*length(delta)*sqrt(max(0.0,2.0*(1.0-alignment)));
        if(abs(dot(delta,n))>max(.0003,curve))continue;
        ivec2 address=ivec2(other%scAtlas.x,other/scAtlas.x);vec3 bx=texelFetch(wgInputBx,address,0).rgb,by=texelFetch(wgInputBy,address,0).rgb,bz=texelFetch(wgInputBz,address,0).rgb;
        vec3 evaluated=value.rgb+(2.0/3.0)*(bx*(n.x-nn.x)+by*(n.y-nn.y)+bz*(n.z-nn.z));
        float weight=exp2(-float(x*x+y*y))*pow(alignment,16.0);sum+=evaluated*weight;mass+=weight;sumBx+=bx*weight;sumBy+=by*weight;sumBz+=bz*weight;
        if(wgReceiverOptions.y>.5)sumGeometry+=vec3(texelFetch(wgInputBx,address,0).w,texelFetch(wgInputBy,address,0).w,texelFetch(wgInputBz,address,0).w)*weight;
    }
    if(mass>0){vec3 support=sumGeometry/mass;outColor=vec4(max(sum/mass,vec3(0)),center.w);
        outBx=vec4(sumBx/mass,support.x);outBy=vec4(sumBy/mass,support.y);outBz=vec4(sumBz/mass,support.z);}
}
)GLSL";}
inline std::string TransportReceiverRiskGLSL(){
    auto source=ContactMaskShader();const auto first=source.find("float CTReceiverScreenRisk("),last=source.find("\nvoid main()",first);
    if(first==std::string::npos||last==std::string::npos)throw std::logic_error("Receiver geometric-risk source changed");source=source.substr(first,last-first);
    const auto replace=[&](const std::string& a,const std::string& b){std::size_t pos=0;while((pos=source.find(a,pos))!=std::string::npos){source.replace(pos,a.size(),b);pos+=b.size();}};
    replace("CTReceiverScreenRisk","WGScreenRisk");replace("ctPositions","wgPositions");replace("ctNormals","wgNormals");replace("CTFinite","WGFinite");
    replace("ctGuide.z","1.0");replace("ctFilter.w",".00002");replace("ctParameters.x","wgParameters.x");
    return source+"\nfloat WGCurrentRisk(ivec2 pixel,vec3 p,vec3 n,int own){if(WGNearRisk(p,n,own))return 1.0;return WGScreenRisk(pixel,p,n);}\n";
}
inline std::string TransportReceiverFilterShader(bool physicalCore=false){
    auto source=ContactFilterShader();
    const auto replace=[&](const std::string& a,const std::string& b){const auto at=source.find(a);
        if(at==std::string::npos)throw std::logic_error("Receiver filter source changed");source.replace(at,a.size(),b);};
    if(!physicalCore){
        replace("texelFetch(ctMask,pixel,0).x","(texelFetch(ctMask,pixel,0).x>0.0?1.0:0.0)");
        replace("texelFetch(ctMask,at,0).x","(texelFetch(ctMask,at,0).x>0.0?1.0:0.0)");
    }else{
        replace("float originalMask=texelFetch(ctMask,pixel,0).x,mask=CTEffectiveMask(originalMask,pixel);",
            "vec4 physicalMask=texelFetch(ctMask,pixel,0);float originalMask=(ctOptions.w&256)!=0?physicalMask.y:physicalMask.x,mask=CTEffectiveMask(originalMask,pixel);");
        // Bit 256 uses the effective geometric risk in Y for mask weights;
        // source classification and physical/synthetic provenance remain X/W.
        // Bit 128 admits certified synthetic current only. Existing readable,
        // geometry/path and hit-parallax gates remain authoritative; W stays zero.
        replace("float originalCandidateMask=texelFetch(ctMask,at,0).x,candidateMask=CTEffectiveMask(originalCandidateMask,at);",
            "vec4 physicalCandidate=texelFetch(ctMask,at,0);if(physicalMask.w>0&&physicalCandidate.w<=0&&"
            "((ctOptions.w&128)==0||physicalCandidate.x!=.25||physicalCandidate.w!=0)){compatible=false;break;}\n"
            "            float originalCandidateMask=(ctOptions.w&256)!=0?physicalCandidate.y:physicalCandidate.x,candidateMask=CTEffectiveMask(originalCandidateMask,at);");
        replace("ctFilter.x*(guided?1.0:mask)","ctFilter.x");
    }
    replace("int radius=clamp(ctOptions.y,1,guided?4:2);","int radius=clamp(ctOptions.y,1,4);");
    replace("if(guided)candidatePlane=abs(dot(delta,normalize(n+neighborNormal)));","candidatePlane=abs(dot(delta,normalize(n+neighborNormal)));");
    replace("if(!compatible&&!(guided&&acceptedStep>0))continue;","if(!compatible&&acceptedStep<=0)continue;");
    replace("directions[neighbor]*(guided?acceptedStep:radius)","directions[neighbor]*acceptedStep");
    replace("int kernelRadius=guided?max(acceptedStep,1):radius;","int kernelRadius=max(acceptedStep,1);");
    const std::string anchor="            float candidateHit=hitWeight;";
    const auto at=source.find(anchor);if(at==std::string::npos)throw std::logic_error("Receiver filter support changed");
    source.insert(at+anchor.size(),R"GLSL(
            // Match the reference's pixel-scale support. Only extra reach
            // needs the current physical hit-distance/parallax certificate.
            if(!guided&&step>1){float hitDistance=min(texelFetch(ctMask,pixel,0).z,texelFetch(ctMask,at,0).z);
                float tangent=length(delta-n*dot(delta,n))+max(spTraceParams.z,0.0)*length(n-neighborNormal);
                float angle=tangent/max(hitDistance,.00001);
                candidateHit=min(hitWeight,1.0-smoothstep(.5*ctGuide.x,ctGuide.x,angle));
                if(candidateHit<=0){compatible=false;break;}}
)GLSL");return source;
}
inline std::string TransportSampleGLSL(bool coarse=true) {
    auto source=LookupGLSL();const auto begin=source.find("bool SCSampleLightingObject(");
    source=source.substr(begin);
    const auto replace=[&](const std::string& a,const std::string& b){std::size_t at=0;while((at=source.find(a,at))!=std::string::npos){source.replace(at,a.size(),b);at+=b.size();}};
    replace("SCSampleLightingObject","WGSampleObject");replace("SCSampleLighting","WGSample");replace("scLighting","wgTransport");
    replace("int anchor;\n    if(!SCLookupObject(p,n,object,anchor))return false;","int anchor=wgAnchor;\n    if(anchor<0)return false;");
    replace("radiance=vec3(0);coverage=0;","radiance=vec3(0);coverage=0;wgRiskSupport=vec3(0);");
    if(coarse){
        const auto first=source.find("    vec4 anchorLighting="),last=source.find("    vec4 anchorNormal=",first);
        if(first==std::string::npos||last==std::string::npos)throw std::logic_error("Transport anchor source changed");
        source.replace(first,last-first,"    if((int(scParameters.w+.5)&2)==0)return false;\n");
        replace("ivec2 base=ivec2(floor(at));vec2 fraction=fract(at);",
            "int stride=int(wgParameters.w+.5);at/=float(stride);ivec2 base=ivec2(floor(at));vec2 fraction=fract(at);");
        replace("ivec2 cell=clamp(base+ivec2(x,y),ivec2(0),ivec2(resolution-1));",
            "ivec2 cell=clamp(base+ivec2(x,y),ivec2(0),ivec2((resolution-1)/stride))*stride;");
        // Missing transport taps may use other current, geometrically valid
        // taps on this same covered card. This does not give surface-cache
        // holes coverage; the original object/layer anchor is still required.
        replace("{missingLighting=true;continue;}","{continue;}");
        replace("float tolerance=max(facing.w,.00001),weightSum=0;vec3 sum=vec3(0);",
            "float tolerance=max(facing.w,.00001),weightSum=0;vec3 sum=vec3(0),wgSupportSum=vec3(0);");
        replace("sum+=lighting.rgb*weight;weightSum+=weight;",R"GLSL(
        if(dot(n,normal.xyz)<.99999){ivec2 address=ivec2(index%scAtlas.x,index/scAtlas.x);
            vec3 bx=texelFetch(wgBx,address,0).rgb,by=texelFetch(wgBy,address,0).rgb,bz=texelFetch(wgBz,address,0).rgb;
            lighting.rgb=max(lighting.rgb+(2.0/3.0)*(bx*(n.x-normal.x)+by*(n.y-normal.y)+bz*(n.z-normal.z)),vec3(0));}
        if(wgReceiverOptions.y>.5){ivec2 address=ivec2(index%scAtlas.x,index/scAtlas.x);
            // The lighting validity, object/card/layer gates and geometric
            // footprint above are also the complete risk-support stencil.
            wgSupportSum+=vec3(texelFetch(wgBx,address,0).w,texelFetch(wgBy,address,0).w,texelFetch(wgBz,address,0).w)*weight;}
        sum+=lighting.rgb*weight;weightSum+=weight;
)GLSL");
        replace("radiance=sum/weightSum;coverage=clamp(weightSum,0.0,1.0);return true;",
            "radiance=sum/weightSum;coverage=clamp(weightSum,0.0,1.0);wgRiskSupport=wgSupportSum/weightSum;return true;");
    }
    return "\nlayout(binding=0) uniform sampler2D wgTransport;\nint wgAnchor=-1;\nvec3 wgRiskSupport=vec3(0);\n"+
        (coarse?std::string("layout(binding=1) uniform sampler2D wgBx;layout(binding=6) uniform sampler2D wgBy;layout(binding=7) uniform sampler2D wgBz;\n"):std::string{})+source;
}
inline std::string TransportGeometryRiskGLSL(){
    auto screen=TransportReceiverRiskGLSL();const auto first=screen.find("float WGScreenRisk(");
    if(first==std::string::npos)throw std::logic_error("Transport screen-risk helper changed");
    auto last=screen.find('{',first);unsigned depth=1;
    while(++last<screen.size()&&depth){if(screen[last]=='{')++depth;else if(screen[last]=='}')--depth;}
    screen=screen.substr(first,last-first);
    const auto replace=[&](const std::string& from,const std::string& to){std::size_t at=0;
        while((at=screen.find(from,at))!=std::string::npos){screen.replace(at,from.size(),to);at+=to.size();}};
    replace("WGScreenRisk","WGGeometryScreenRisk");replace("wgParameters.x","max(wgReceiverOptions.z,.00001)");
    return screen+R"GLSL(
float WGReceiverGeometryRisk(ivec2 pixel,vec3 p,vec3 n,int own,bool core,bool cacheValid){
    if(core)return 1.0;
    if(wgReceiverOptions.y<.5)return WGCurrentRisk(pixel,p,n,own);
    if(!cacheValid||wgAnchor<0||!WGFinite(wgRiskSupport))return 1.0;
    vec3 delta=p-SCAttribute(wgAnchor).xyz;
    float tangent=length(delta-n*dot(delta,n)),radius=max(wgReceiverOptions.z,.00001);
    // Same geometric parallax/floor roles as the original receiver mask.
    // RGB and readability never choose the strength of a valid correction.
    float risk=smoothstep(.1763269807,.3639702343,tangent*max(wgRiskSupport.x,0.0));
    float support=1.0-smoothstep(.15*radius,radius,tangent);
    risk=max(risk,clamp(wgRiskSupport.y,0.0,1.0)*support);
    float mask=smoothstep(0.0,.2,risk);
    return max(mask,WGGeometryScreenRisk(pixel,p,n));
}
)GLSL";
}
inline std::string UeLumenRectIrradianceGLSL(){return R"GLSL(
// UE5.7 RectLight.ush:56-160, CapsuleLight.ush:74-107, active branches.
// The project supplies emitted radiance and half axes; no extra area/pi scale.
float UECardSphereHorizonCosWrap(float NoL,float SinAlphaSqr){
    float SinAlpha=sqrt(SinAlphaSqr);
    if(NoL<SinAlpha){NoL=max(NoL,-SinAlpha);NoL=(SinAlpha+NoL)*(SinAlpha+NoL)/(4.0*SinAlpha);}
    return NoL;
}
vec3 UECardRectAnalytic(vec3 p,vec3 N,SCAreaLight light){
    if(light.center.w<.5||all(lessThanEqual(light.radiance.rgb,vec3(0))))return vec3(0);
    vec3 U=light.u.xyz,V=light.v.xyz,areaVector=cross(U,V);
    float quarterArea=length(areaVector);if(quarterArea<=1e-12)return vec3(0);
    vec3 emitterNormal=areaVector/quarterArea,toCenter=light.center.xyz-p;
    float emitterSideMax=dot(emitterNormal,-toCenter)+abs(dot(emitterNormal,U))+abs(dot(emitterNormal,V));
    if(light.radiance.w<.5&&emitterSideMax<=0)return vec3(0);
    if(dot(N,toCenter)+abs(dot(N,U))+abs(dot(N,V))<=0)return vec3(0);
    float extentX=length(U),extentY=length(V);
    vec3 axis0=U/extentX,axis2=-areaVector/quarterArea,toLight=light.center.xyz-p;
    float planeDistance=dot(axis2,toLight);
    if(planeDistance<=0.0){if(light.radiance.w<.5||planeDistance==0.0)return vec3(0);axis2=-axis2;}
    vec3 axis1=cross(axis2,axis0);
    vec3 localPosition=vec3(dot(axis0,toLight),dot(axis1,toLight),dot(axis2,toLight));
    float x0=localPosition.x-extentX,x1=localPosition.x+extentX;
    float y0=localPosition.y-extentY,y1=localPosition.y+extentY,z0=localPosition.z,z0Sqr=z0*z0;
    vec3 v0=vec3(x0,y0,z0),v1=vec3(x1,y0,z0),v2=vec3(x1,y1,z0),v3=vec3(x0,y1,z0);
    vec3 L0=v0*inversesqrt(dot(v0.xy,v0.xy)+z0Sqr);
    vec3 L1=v1*inversesqrt(dot(v1.xy,v1.xy)+z0Sqr);
    vec3 L2=v2*inversesqrt(dot(v2.xy,v2.xy)+z0Sqr);
    vec3 L3=v3*inversesqrt(dot(v3.xy,v3.xy)+z0Sqr);
    float c01=dot(L0,L1),c12=dot(L1,L2),c23=dot(L2,L3),c30=dot(L3,L0);
    float w01=(1.5708-.175*c01)*inversesqrt(max(c01+1.0,1e-4));
    float w12=(1.5708-.175*c12)*inversesqrt(max(c12+1.0,1e-4));
    float w23=(1.5708-.175*c23)*inversesqrt(max(c23+1.0,1e-4));
    float w30=(1.5708-.175*c30)*inversesqrt(max(c30+1.0,1e-4));
    vec3 L=cross(L1,-w01*L0+w12*L2);
    L+=cross(L3,w30*L0-w23*L2);
    L=L.x*axis0+L.y*axis1+L.z*axis2;
    float lengthSqr=dot(L,L);if(lengthSqr<=0.0)return vec3(0);
    float invLength=inversesqrt(lengthSqr),len=lengthSqr*invLength;
    L*=invLength;
    float baseIrradiance=.5*len;
    float NoL=UECardSphereHorizonCosWrap(dot(N,L),baseIrradiance/3.14159265359);
    return light.radiance.rgb*(baseIrradiance*NoL);
}
)GLSL";}
inline std::string TransportResolveShader(bool directOnly=false,unsigned receiverPass=0,bool ueDirectCardBlend=false,bool useWorldAttributes=false) {
    auto source=std::string("#version 450 core\n")+TransportCommonGLSL(false,receiverPass!=0,receiverPass!=0)+TransportSampleGLSL(!directOnly)+
        ScreenProbeTraceDetail::ConstantsGLSL()+"\n#define CT_ORIGIN_INTERSECT SCNativeIntersect\n"+
        ContactReceiverOriginGLSL()+TransportReceiverRiskGLSL()+TransportGeometryRiskGLSL()+R"GLSL(
in vec2 vUV;layout(location=0) out vec4 outColor;
layout(binding=4) uniform sampler2D wgAlbedo;
layout(binding=5) uniform sampler2D wgScene;
void main(){ivec2 pixel=ivec2(gl_FragCoord.xy);outColor=vec4(0);
    if(wgLaunchBias.w>.5){int grid=WGReceiverGrid(),phase=wgSchedule.x%(grid*grid);pixel=pixel*grid+ivec2(phase%grid,phase/grid);}
    if(any(greaterThanEqual(pixel,scScreen.xy)))return;
    vec4 view=texelFetch(wgPositions,pixel,0),normal=texelFetch(wgNormals,pixel,0);
    if(view.w<.5||normal.w<.5||!WGFinite(view.xyz)||dot(normal.xyz,normal.xyz)<.5)return;
    vec3 p=(scInverseView*vec4(view.xyz,1)).xyz,n=normalize(normal.xyz);
    bool rasterCore=false;
    if(wgLaunchBias.z>.5&&wgLaunchBias.w<.5){int object=int(view.w)-1;
        rasterCore=object>=0&&object<scAtlas.w/6&&WGNearRiskWithin(p,n,object,.03);
        if(!rasterCore){wgNearMask=vec4(.25,1,0,0);return;}}
    if(wgReceiverOptions.y>.5&&wgLaunchBias.z<.5){int object=int(view.w)-1;
        rasterCore=object>=0&&object<scAtlas.w/6&&WGNearRiskWithin(p,n,object,.03);}
    int anchor,own;if(!WGAnchor(view,p,n,anchor,own))return;
    wgAnchor=anchor;
)GLSL";
    if(directOnly){const auto core=source.find("    bool rasterCore=false;"),coreEnd=source.find("    int anchor,own;",core);
        source.erase(core,coreEnd-core);
        const auto at=source.find("outColor=vec4(0);");
        source.replace(at,std::string("outColor=vec4(0);").size(),"outColor=vec4(texelFetch(wgScene,pixel,0).rgb,1);");
        auto helper=GeometryGLSL();const auto first=helper.find("vec3 SCDirect(");auto last=helper.find('{',first);unsigned depth=1;
        while(++last<helper.size()&&depth){if(helper[last]=='{')++depth;else if(helper[last]=='}')--depth;}
        helper=helper.substr(first,last-first);helper.replace(helper.find("SCDirect"),std::string("SCDirect").size(),"WGUnoccluded");
        const std::string call="SC_DIRECT_VISIBLE(";std::size_t cursor=0;
        while((cursor=helper.find(call,cursor))!=std::string::npos){auto end=cursor+call.size()-1;unsigned nesting=1;
            while(++end<helper.size()&&nesting){if(helper[end]=='(')++nesting;else if(helper[end]==')')--nesting;}
            helper.replace(cursor,end-cursor,"true");cursor+=4;}
        const std::string grid="int grid=abs(light.u.w)>6?8:4;";const auto gridAt=helper.find(grid);
        if(gridAt==std::string::npos)throw std::logic_error("Unoccluded rectangle sampling anchor changed");helper.replace(gridAt,grid.size(),"int grid=4;");
        source.insert(source.find("void main()"),helper+"\n");
        auto directSource=R"GLSL(
    vec3 direct;float coverage;bool valid=WGSampleObject(p,n,own,direct,coverage);
    vec3 rho=max(texelFetch(wgAlbedo,pixel,0).rgb,vec3(0)),luma=vec3(.2126,.7152,.0722);
    float denominator=dot(rho*WGUnoccluded(p,n)/3.14159265359,luma);
    float visibility=dot(rho,luma)<1e-6?1.0:valid?clamp(dot(direct,luma)/max(denominator,1e-6),0.0,1.0):0.0;
    // The primary GGX lobe remains view-dependent. Cached mean visibility
    // suppresses fully occluded highlights without rasterizing hundreds of
    // shadow faces; partial-visibility specular is an explicit approximation.
    outColor=vec4(texelFetch(wgScene,pixel,0).rgb*visibility+(valid?.96*direct:vec3(0)),1);
}
)GLSL";
        if(ueDirectCardBlend) {
            // Use the producer's pure material-normal irradiance convention
            // only for the encoded reconstruction. Raw fallback retains the
            // old denominator, while either branch evaluates one quadrature.
            auto pixelHelper=helper;
            const std::string pixelSignature="vec3 WGUnoccluded(vec3 p,vec3 n)";
            const auto pixelAt=pixelHelper.find(pixelSignature);
            if(pixelAt==std::string::npos)throw std::logic_error("Direct pixel irradiance source anchor missing");
            pixelHelper.replace(pixelAt,pixelSignature.size(),"vec3 UECardUnoccluded(vec3 p,vec3 n,bool materialIrradiance)");
            const auto pixelBody=pixelHelper.find('{',pixelAt);
            pixelHelper.insert(pixelBody+1,R"GLSL(
    if(materialIrradiance){
        for(int i=0;i<4;++i){SCAreaLight light=scAreaLights[i];
            if(light.center.w<.5||all(lessThanEqual(light.radiance.rgb,vec3(0))))continue;
            vec3 analytic=UECardRectAnalytic(p,n,light);
            if(!any(isnan(analytic))&&!any(isinf(analytic)))return analytic;
            break;
        }
    }
)GLSL");
            const std::string horizon="float geometricHorizon=smoothstep(0.0,.05,dot(n,d));";
            const auto horizonAt=pixelHelper.find(horizon);
            if(horizonAt==std::string::npos)throw std::logic_error("Direct pixel horizon source anchor missing");
            pixelHelper.replace(horizonAt,horizon.size(),"float geometricHorizon=materialIrradiance?1.0:smoothstep(0.0,.05,dot(n,d));");
            source.insert(source.find("void main()"),UeLumenRectIrradianceGLSL()+pixelHelper+"\n");
            source.insert(source.find("void main()"),UeLumenCardDirectSampleGLSL()+"\n");
            std::string direct=directSource;
            const std::string previous="bool valid=WGSampleObject(p,n,own,direct,coverage);";
            const auto sample=direct.find(previous);
            if(sample==std::string::npos)throw std::logic_error("Direct card blend source anchor missing");
            direct.replace(sample,previous.size(),"bool valid=UESampleDirectCards(p,n,own,direct,coverage);");
            const std::string denominator="    float denominator=dot(rho*WGUnoccluded(p,n)/3.14159265359,luma);";
            const auto reconstruct=direct.find(denominator);
            if(reconstruct==std::string::npos)throw std::logic_error("Direct visibility reconstruction source anchor missing");
            direct.replace(reconstruct,denominator.size(),R"GLSL(
    bool singleRectangle=scSunDirectionIntensity.w<=0.0&&all(lessThanEqual(scSpotIntensityInner.rgb,vec3(0)));
    int rectangleCount=0;
    for(int i=0;i<4;++i){
        if(any(greaterThan(scPointColors[i].rgb,vec3(0))))singleRectangle=false;
        SCAreaLight light=scAreaLights[i];
        if(light.center.w>=.5&&any(greaterThan(light.radiance.rgb,vec3(0))))++rectangleCount;
    }
    bool reconstruct=valid&&ueCardVisibilityEncoded&&singleRectangle&&rectangleCount==1;
    vec3 unoccludedNow=rho*UECardUnoccluded(p,n,reconstruct)/3.14159265359;
    if(reconstruct){
        // Preserve the old cached-emissive term; the existing primary scene
        // contribution and its visibility treatment below remain compatible.
        int card=int(SCAttribute(scAtlas.y+wgAnchor).w+.5)-1;
        direct=unoccludedNow*ueCardVisibility+SCMeta(card,8).rgb;
    }
    float denominator=dot(unoccludedNow,luma);
)GLSL");
            auto result=source+direct;
            return useWorldAttributes?UeLumenCardWorldAttributeLookupGLSL(std::move(result)):result;
        }
        return source+directSource;
    }
    const std::string output="layout(location=0) out vec4 outColor;";
    const std::string anchorGuard="if(!WGAnchor(view,p,n,anchor,own))return;";
    source.replace(source.find(anchorGuard),anchorGuard.size(),"if(!WGAnchor(view,p,n,anchor,own)){wgNearMask=vec4(-1,0,0,0);return;}");
    source.replace(source.find(output),output.size(),output+"\nlayout(location=1) out vec4 wgNearMask;layout(location=2) out vec4 wgBase;");
    const std::string initialize="outColor=vec4(0);";
    source.replace(source.find(initialize),initialize.size(),initialize+"wgNearMask=vec4(0);wgBase=vec4(0);");
    source+=R"GLSL(
    vec3 farValue=vec3(0);float coverage=0;bool farValid=!rasterCore&&WGSampleObject(p,n,own,farValue,coverage);
    float risk=WGReceiverGeometryRisk(pixel,p,n,own,rasterCore,farValid);
    // Conservative world-space bounds include off-screen contacts. Exclude
    // only the receiver's own object; uncertain bounds request local work.
    vec3 rho=max(texelFetch(wgAlbedo,pixel,0).rgb,vec3(0));
    if(farValid)wgBase=vec4(rho*farValue*scParameters.z,coverage);
    if(farValid&&risk<=.000001){outColor=vec4(rho*farValue*scParameters.z,coverage);return;}
    if(!WGReceiverPassScheduled(pixel,p,n,own,farValid)){outColor=wgBase;wgNearMask=vec4(.25,risk,0,0);return;}
    vec3 origin;if(!CTReceiverOrigin(p,n,origin)||!WGCornerClearance(origin,n,own)){wgNearMask=vec4(-1,0,0,0);return;}
    vec3 sum=vec3(0);float inverseHitDistance=0;int readableCount=0,executed=0,count=int(wgParameters.z+.5);
    uint seed=WGHash(WGHash(uint(pixel.x)+uint(pixel.y)*uint(scScreen.x))^WGHash(uint(spTracing.w)+0x9e3779b9u));
    for(int ray=0;ray<32;++ray){if(ray>=count)break;
        vec3 d=WGDirection(n,ray,count,seed,uint(spScreen.z));
        float distance=scParameters.x;vec3 hitNormal;int triangle;bool exhausted;
        ++executed;bool found=SCNativeIntersect(origin,d,distance,hitNormal,triangle,exhausted,false);
        if(exhausted)continue;
        if(!found){++readableCount;continue;}
        inverseHitDistance+=1.0/max(distance,.00001);
        bool readable;sum+=WGHitLighting(origin,d,distance,hitNormal,triangle,readable);readableCount+=readable?1:0;
    }
    vec4 current=vec4(rho*sum*(scParameters.z/float(count)),float(readableCount)/float(count));
    outColor=farValid?mix(vec4(rho*farValue*scParameters.z,coverage),current,clamp(risk,0.0,1.0)):current;
    float harmonic=inverseHitDistance>0?min(scParameters.x,float(count)/inverseHitDistance):scParameters.x;
    wgNearMask=vec4(1,farValid?risk:1.0,harmonic,float(executed));
}
)GLSL";
    if(receiverPass==1||receiverPass==2){
        const auto replace=[&](const std::string& from,const std::string& to){auto at=source.find(from);
            if(at==std::string::npos)throw std::logic_error("Coherent receiver source changed");source.replace(at,from.size(),to);};
        const auto first=source.find("    bool rasterCore=false;"),last=source.find("    int anchor,own;",first);
        if(first==std::string::npos||last==std::string::npos)throw std::logic_error("Coherent receiver classification changed");
        source.replace(first,last-first,receiverPass==1?R"GLSL(
    const bool rasterCore=true;int object=int(view.w)-1;
    if(texelFetch(wgScene,pixel,0).x<.5){wgNearMask=vec4(.25,1,0,0);return;}
)GLSL":R"GLSL(
    const bool rasterCore=false;int object=int(view.w)-1;
    // A full-resolution core measurement, including a rejected launch, owns
    // this receiver. The lattice must not repeat its lookup or source work.
    if(texelFetch(wgScene,pixel,0).x>.5)return;
)GLSL");
        if(receiverPass==1)replace("    if(wgLaunchBias.w>.5){int grid=WGReceiverGrid(),phase=wgSchedule.x%(grid*grid);pixel=pixel*grid+ivec2(phase%grid,phase/grid);}","");
        replace("    if(!WGReceiverPassScheduled(pixel,p,n,own,farValid)){outColor=wgBase;wgNearMask=vec4(.25,risk,0,0);return;}","");
    }
    return source;
}
inline std::string TransportReceiverClassificationShader(){
    const auto common=TransportCommonGLSL();
    const auto begin=common.find("layout(std140,binding=14) uniform TransportData"),end=common.find("};",begin)+2;
    const auto function=[&](const std::string& name){const auto first=common.find(name+"(");auto at=common.rfind('\n',first)+1;
        auto last=common.find('{',first);unsigned depth=1;while(++last<common.size()&&depth){if(common[last]=='{')++depth;else if(common[last]=='}')--depth;}return common.substr(at,last-at);};
    return std::string("#version 450 core\n")+LookupGLSL()+common.substr(begin,end-begin)+R"GLSL(
layout(binding=2) uniform sampler2D wgPositions;layout(binding=3) uniform sampler2D wgNormals;
)GLSL"+function("WGFinite")+function("WGNearRiskWithin")+function("WGReceiverGrid")+function("WGReceiverDetail")+R"GLSL(
in vec2 vUV;layout(location=0) out vec4 outColor;
void main(){ivec2 pixel=ivec2(gl_FragCoord.xy);outColor=vec4(0);
    vec4 v=texelFetch(wgPositions,pixel,0),normal=texelFetch(wgNormals,pixel,0);
    if(v.w<.5||normal.w<.5||!WGFinite(v.xyz)||dot(normal.xyz,normal.xyz)<.5)return;
    int own=v.w>=1&&v.w==floor(v.w)?int(v.w)-1:-1;
    if(own<0||own>=scAtlas.w/6){outColor=vec4(1,0,0,0);return;}
    vec3 p=(scInverseView*vec4(v.xyz,1)).xyz,n=normalize(normal.xyz);
    outColor=vec4(WGReceiverDetail(pixel,p,n,own)?1:0,0,0,0);
}
)GLSL";
}
inline std::string TransportReconstructionShader(){
    const auto common=TransportCommonGLSL();
    const auto begin=common.find("layout(std140,binding=14) uniform TransportData"),end=common.find("};",begin)+2;
    const auto function=[&](const std::string& name){const auto first=common.find(name+"(");auto at=common.rfind('\n',first)+1;
        auto last=common.find('{',first);unsigned depth=1;while(++last<common.size()&&depth){if(common[last]=='{')++depth;else if(common[last]=='}')--depth;}return common.substr(at,last-at);};
    auto sample=TransportSampleGLSL();
    const auto replace=[&](const std::string& a,const std::string& b){auto at=sample.find(a);if(at==std::string::npos)throw std::logic_error("Sparse world sampler source changed");sample.replace(at,a.size(),b);};
    replace("layout(binding=0) uniform sampler2D wgTransport;","layout(binding=11) uniform sampler2D wgTransport;");
    replace("layout(binding=6) uniform sampler2D wgBy;","layout(binding=7) uniform sampler2D wgBy;");
    replace("layout(binding=7) uniform sampler2D wgBz;","layout(binding=8) uniform sampler2D wgBz;");
    return std::string("#version 450 core\n")+LookupGLSL()+common.substr(begin,end-begin)+"\n"+
        function("WGFinite")+"\n"+function("WGAnchor")+"\n"+function("WGReceiverGrid")+"\n"+sample+R"GLSL(
layout(binding=2) uniform sampler2D wgPositions;layout(binding=3) uniform sampler2D wgNormals;
in vec2 vUV;layout(location=0) out vec4 outColor;layout(location=1) out vec4 outMask;
layout(binding=0) uniform sampler2D sgCurrent;
layout(binding=4) uniform sampler2D sgAlbedo;
layout(binding=5) uniform sampler2D sgLowCurrent;
layout(binding=6) uniform sampler2D sgLowMask;
layout(binding=10) uniform sampler2D sgMask;
void main(){ivec2 pixel=ivec2(gl_FragCoord.xy);vec4 mask=texelFetch(sgMask,pixel,0),current=texelFetch(sgCurrent,pixel,0);
    outColor=current;outMask=mask;if(mask.w>0||mask.x<0)return;
    vec4 view=texelFetch(wgPositions,pixel,0),normal=texelFetch(wgNormals,pixel,0);
    if(view.w<.5||normal.w<.5){outColor=vec4(0);outMask=vec4(0);return;}
    int grid=WGReceiverGrid(),phase=wgSchedule.x%(grid*grid);ivec2 shift=ivec2(phase%grid,phase/grid);
    if(all(equal(pixel%grid,shift))){vec4 low=texelFetch(sgLowCurrent,pixel/grid,0),lowMask=texelFetch(sgLowMask,pixel/grid,0);
        if(low.a>0||lowMask.w>0||lowMask.x<0){outColor=low;outMask=lowMask;return;}}
    vec3 p=(scInverseView*vec4(view.xyz,1)).xyz,n=normalize(normal.xyz),albedo=texelFetch(sgAlbedo,pixel,0).rgb;
    vec2 coord=vec2(pixel-shift)/float(grid),fraction=fract(coord);ivec2 first=ivec2(floor(coord))*grid+shift;
    vec3 sum=vec3(0);float mass=0,coverage=0,hitDistance=scParameters.x;int fresh=0;
    for(int y=0;y<2;++y)for(int x=0;x<2;++x){ivec2 at=first+ivec2(x,y)*grid;
        if(any(lessThan(at,ivec2(0)))||any(greaterThanEqual(at,scScreen.xy)))continue;
        float weight=(x==0?1-fraction.x:fraction.x)*(y==0?1-fraction.y:fraction.y);if(weight<=.000001)continue;
        vec4 sampleMask=texelFetch(sgMask,at,0),source=texelFetch(wgPositions,at,0),nd=texelFetch(wgNormals,at,0);
        vec4 value=texelFetch(sgCurrent,at,0);
        if(sampleMask.w<=0){sampleMask=texelFetch(sgLowMask,at/grid,0);value=texelFetch(sgLowCurrent,at/grid,0);}
        if(source.w!=view.w||nd.w<.5||value.a<=0||!WGFinite(value.rgb))continue;
        vec3 q=(scInverseView*vec4(source.xyz,1)).xyz,nn=normalize(nd.xyz),delta=q-p,sourceAlbedo=texelFetch(sgAlbedo,at,0).rgb;
        float alignment=clamp(dot(n,nn),-1.0,1.0),curve=.75*length(delta)*sqrt(max(0.0,2.0*(1.0-alignment)));
        if(alignment<.95||max(abs(dot(delta,n)),abs(dot(delta,nn)))>max(.0003,curve)||
            any(greaterThan(abs(sourceAlbedo-albedo),vec3(.08)))||any(bvec3(albedo.x>.0001&&sourceAlbedo.x<.0001,albedo.y>.0001&&sourceAlbedo.y<.0001,albedo.z>.0001&&sourceAlbedo.z<.0001)))continue;
        bool clear=true;for(int step=1;step<4;++step){if(step>=grid)break;
            ivec2 middle=ivec2(floor(mix(vec2(pixel),vec2(at),float(step)/float(grid))+.5));
            vec4 v=texelFetch(wgPositions,middle,0),m=texelFetch(wgNormals,middle,0);
            if(v.w!=view.w||m.w<.5||dot(n,normalize(m.xyz))<.95){clear=false;break;}}
        if(!clear)continue;weight*=pow(alignment,16.0)*value.a;
        sum+=value.rgb/max(sourceAlbedo,vec3(.0001))*weight;mass+=weight;coverage+=value.a*weight;
        if(sampleMask.w>0){hitDistance=min(hitDistance,sampleMask.z);++fresh;}
    }
    if(mass>0){outColor=vec4(max(albedo*sum/mass,vec3(0)),clamp(coverage/mass,0.0,1.0));
        outMask=fresh>0?vec4(.25,float(fresh),hitDistance,0):vec4(0);
    }else {int anchor,own;vec3 lighting;float readable;outColor=vec4(0);outMask=vec4(0);
        if(WGAnchor(view,p,n,anchor,own)){wgAnchor=anchor;if(WGSampleObject(p,n,own,lighting,readable))outColor=vec4(albedo*lighting*scParameters.z,readable);}}
}
)GLSL";}
// Untimed replay of precisely the direction/origin/query policy used above.
// Four texels per logical direction: origin/status, hit/distance,
// query counters/triangle, direction/readability. Origin work is counted only
// on direction zero, since production computes it once per receiver.
inline std::string TransportRayDiagnosticShader(bool local,bool coherent=false) {
    auto source=std::string("#version 450 core\n#define WG_LOCAL ")+(local?"1\n":"0\n")+
        TransportCommonGLSL(true,local&&coherent,coherent)+(local?TransportSampleGLSL()+"\n#define wgPrevious wgTransport\n":"\nlayout(binding=0) uniform sampler2D wgPrevious;\n")+ScreenProbeTraceDetail::ConstantsGLSL()+
        "\n#define CT_ORIGIN_INTERSECT SCNativeIntersect\n"+ContactReceiverOriginGLSL()+TransportReceiverRiskGLSL()+
        (local?TransportGeometryRiskGLSL():std::string{})+R"GLSL(
in vec2 vUV;layout(location=0) out vec4 outColor;
void WGRejectedOrigin(int part,int ray,vec3 origin){
    if(part==0)outColor=vec4(origin,7);
    if(part==2&&ray==0)outColor=vec4(0,0,float(scTraceDiag[0].x+256u*scTraceDiag[1].y),0);
}
void main(){outColor=vec4(0);int texel=int(gl_FragCoord.y)*4096+int(gl_FragCoord.x);
    int part=texel%4,id=texel/4,count=int(WG_LOCAL!=0?wgParameters.z:wgParameters.y),index=id/count,ray=id%count;
    vec3 origin,n;uint seed;uvec2 originQueries=uvec2(0);SCTraceDiagReset();
#if WG_LOCAL
    if(index>=scScreen.x*scScreen.y)return;ivec2 pixel=ivec2(index%scScreen.x,index/scScreen.x);
    vec4 view=texelFetch(wgPositions,pixel,0),normal=texelFetch(wgNormals,pixel,0);
    if(view.w<.5||normal.w<.5||dot(normal.xyz,normal.xyz)<.5)return;
    vec3 p=(scInverseView*vec4(view.xyz,1)).xyz;n=normalize(normal.xyz);
    int anchor,own;if(!WGAnchor(view,p,n,anchor,own))return;
    bool rasterCore=wgReceiverOptions.y>.5&&WGReceiverDetail(pixel,p,n,own);
    wgAnchor=anchor;vec3 farValue;float coverage;bool farValid=!rasterCore&&WGSampleObject(p,n,own,farValue,coverage);
    float risk=WGReceiverGeometryRisk(pixel,p,n,own,rasterCore,farValid);
    if((farValid&&risk<=.000001)||!WGReceiverScheduled(pixel,p,n,own,farValid))return;
    if(!CTReceiverOrigin(p,n,origin)||!WGCornerClearance(origin,n,own)){WGRejectedOrigin(part,ray,origin);return;}
    seed=WGHash(WGHash(uint(index))^WGHash(uint(spTracing.w)+0x9e3779b9u));
#else
    if(index>=scAtlas.z)return;vec4 p=SCAttribute(index),normal=SCAttribute(scAtlas.y+index);
    if(p.w<.5||dot(normal.xyz,normal.xyz)<.5)return;n=normalize(normal.xyz);
    if(!WGRequested(p.xyz,n)||!WGVisibleSource(p.xyz,n,int(normal.w+.5)-1))return;
    int card=int(normal.w+.5)-1;bool dirty=card>=wgSchedule.z&&card<=wgSchedule.w;
    int resolution=int(SCMeta(card,9).w),cell=index-int(SCMeta(card,5).w),stride=int(wgParameters.w+.5);
    if((cell%resolution)%stride!=0||(cell/resolution)%stride!=0)return;
    vec4 previous=wgSchedule.y==0||dirty?vec4(0):texelFetch(wgPrevious,ivec2(index%scAtlas.x,index/scAtlas.x),0);
    if(wgSchedule.y!=0&&!dirty&&previous.w>0&&WGSchedulingIndex(card,cell,resolution,stride)%wgSchedule.y!=wgSchedule.x%wgSchedule.y)return;
    if(!WGWorldOrigin(p.xyz,n,card,origin)){WGRejectedOrigin(part,ray,origin);return;}
    seed=WGHash(uint(index));
#endif
    if(ray==0)originQueries=uvec2(scTraceDiag[0].x,scTraceDiag[1].y);SCTraceDiagReset();
#if WG_LOCAL
    uint phase=uint(spScreen.z);
#else
    uint phase=uint(wgSchedule.x);
#endif
    vec3 d=WGDirection(n,ray,count,seed,phase),hitNormal;int triangle;bool exhausted;
#if WG_LOCAL
    float distance=scParameters.x;
#else
    float distance=scParameters.x;
#endif
    bool found=SCNativeIntersect(origin,d,distance,hitNormal,triangle,exhausted,false);
    float state=exhausted?3.0:found?2.0:1.0;bool readable=true;
#if WG_LOCAL
    if(!farValid)state+=3.0;
#endif
    if(found&&!exhausted){vec3 unused=WGHitLighting(origin,d,distance,hitNormal,triangle,readable);}
    if(part==0)outColor=vec4(origin,state);
    if(part==1)outColor=vec4(found?origin+d*distance:vec3(0),distance);
    if(part==2)outColor=vec4(float(scTraceDiag[0].x),float(scTraceDiag[1].y),float(originQueries.x+256u*originQueries.y),found?float(triangle+1):0);
    if(part==3)outColor=vec4(d,readable?1.0:0.0);
}
)GLSL";
    return source;
}
}
