#pragma once
#include <string>

namespace Render::ScreenProbeTraceDetail {
inline std::string ConstantsGLSL(){return R"GLSL(
layout(std140,binding=9) uniform PlacementData {
    mat4 spInverseView; mat4 spInverseProjection; mat4 spViewProjection; mat4 spView;
    ivec4 spGrid; ivec4 spScreen; vec4 spOptions;
    ivec4 spTracing; vec4 spTraceParams; ivec4 spTraceAtlas; ivec4 spTraceDisplay;
    vec4 spRefinement;vec4 spCurvature;
};
)GLSL";}

// This helper is also appended to the placement marker shader, whose constants
// already declare the shared 400-byte layout. No fallback color source exists.
inline std::string MarkerTraceGLSL(){return R"GLSL(
layout(binding=7) uniform sampler2D stRays;
vec3 STRayDirection(int ray) {
    int resolution=max(spTracing.x,1);
    vec2 square=2.0*(vec2(ray%resolution,ray/resolution)+.5)/float(resolution)-1.0;
    float diamond=1.0-abs(square.x)-abs(square.y),radius=1.0-abs(diamond);
    float angle=radius==0.0?0.0:.7853981633974483*((abs(square.y)-abs(square.x))/radius+1.0);
    float ring=radius*sqrt(max(0.0,2.0-radius*radius));
    return vec3(ring*sign(square.x)*abs(cos(angle)),ring*sign(square.y)*abs(sin(angle)),sign(diamond)*(1.0-radius*radius));
}
uint STSamplingHash(uint value) {
    value=(value^(value>>16))*0x7feb352du;
    value=(value^(value>>15))*0x846ca68bu;return value^(value>>16);
}
vec3 STCosineRayDirection(int probe,int ray,vec3 normal) {
    uint seed=uint(spTracing.w)>>2;
    uint key=STSamplingHash(uint(probe)^STSamplingHash(seed+0x9e3779b9u));
    uint stratum=uint(ray);
    // Both angular dimensions vary within every two-dimensional stratum.
    // Merely rotating a fixed polar angle creates a cone, not the hemisphere
    // required by diffuse integration, even if the azimuth is fully random.
    // Nineteen bits leave enough float precision when adding a cell index up
    // to fifteen: neither coordinate can round onto its next stratum or pole.
    vec2 jitter=(vec2(STSamplingHash(key^(stratum*2u+0x68bc21ebu))&0x0007ffffu,
        STSamplingHash(key^(stratum*2u+0x02e5be93u))&0x0007ffffu)+.5)/524288.0;
    int resolution=max(spTracing.x,1);
    vec2 xi=(vec2(ray%resolution,ray/resolution)+jitter)/float(resolution);
    float radius=sqrt(xi.x),angle=6.283185307179586*xi.y;
    vec3 axis=abs(normal.z)<.99?vec3(0,0,1):vec3(0,1,0);
    vec3 tangent=normalize(cross(axis,normal)),bitangent=cross(normal,tangent);
    return tangent*(radius*cos(angle))+bitangent*(radius*sin(angle))+normal*sqrt(max(0.0,1.0-xi.x));
}
vec4 STReadRay(int probe,int ray,int part) {
    int texel=(probe*spTracing.x*spTracing.x+ray)*4+part;
    return texelFetch(stRays,ivec2(texel%spTraceAtlas.x,texel/spTraceAtlas.x),0);
}
vec3 STDiagnosticColor(vec3 radiance,float status) {
    int state=int(status+.5);
    if(state==1)return spTraceDisplay.x==0?radiance:vec3(.04,.85,.12);
    if(state==2)return vec3(.85,.02,.70);
    if(state==3)return vec3(.03,.70,.80);
    if(state==4)return vec3(.90,.70,.03);
    if(state==5)return vec3(.12,.12,.14);
    if(state==6)return vec3(.90,.03,.02);
    if(state==7)return vec3(1,.22,.50);
    if(state==8)return vec3(.35,.03,1);
    if(state==9)return spTraceDisplay.x==0?radiance:vec3(.08,.35,1);
    if(state==10)return spTraceDisplay.x==0?radiance:vec3(.02,.8,.65);
    if(state==11)return vec3(1,.38,.03);
    if(state==12)return vec3(.75,.6,.03);
    if(state==13)return vec3(.65,.02,.55);
    if(state==14)return vec3(.8,.05,.1);
    if(state==15)return vec3(.45,.06,.9);
    if(state==16)return vec3(1,.02,.25);
    if(state==17)return vec3(1,.03,.65);
    return vec3(.025);
}
)GLSL";}

inline std::string HzbBaseShader(){return R"GLSL(#version 450 core
in vec2 vUV;layout(location=0) out vec4 outColor;
layout(binding=0) uniform sampler2D stPositions;
void main(){ivec2 pixel=ivec2(gl_FragCoord.xy);vec4 p=texelFetch(stPositions,pixel,0);
    bool valid=p.w>.5&&p.z<0.0&&!any(isnan(p.xyz))&&!any(isinf(p.xyz));
    outColor=vec4(valid?-p.z:1e20,0,0,1);}
)GLSL";}
inline std::string HzbReduceShader(){return R"GLSL(#version 450 core
in vec2 vUV;layout(location=0) out vec4 outColor;
layout(binding=0) uniform sampler2D stPreviousHzb;
void main(){ivec2 source=ivec2(gl_FragCoord.xy)*2,size=textureSize(stPreviousHzb,0);
    float closest=1e20;
    for(int y=0;y<2;++y)for(int x=0;x<2;++x)
        closest=min(closest,texelFetch(stPreviousHzb,min(source+ivec2(x,y),size-1),0).r);
    outColor=vec4(closest,0,0,1);}
)GLSL";}

inline std::string TraceCommonGLSL(bool safeOrigins=false){return std::string("#define ST_SAFE_ORIGINS ")+
    (safeOrigins?"1\n":"0\n")+ConstantsGLSL()+MarkerTraceGLSL()+R"GLSL(
#if ST_SAFE_ORIGINS
// The GI variant needs screen visibility only. Its hit radiance is read from
// the Surface Cache in the fallback stage, leaving slot zero for this atlas.
layout(binding=0) uniform sampler2D stSafeOrigins;
#else
layout(binding=0) uniform sampler2D stSceneColor;
#endif
layout(binding=1) uniform sampler2D stWorldNormals;
layout(binding=2) uniform sampler2D stViewPositions;
layout(binding=3) uniform sampler2D stUniformAtlas;
layout(binding=4) uniform sampler2D stSpawnAtlas;
layout(binding=5) uniform sampler2D stHzb0;
layout(binding=6) uniform sampler2D stHzb1;
layout(binding=8) uniform sampler2D stHzb3;
layout(binding=9) uniform sampler2D stHzb4;
layout(binding=10) uniform sampler2D stHzb5;
layout(binding=11) uniform sampler2D stHzb6;
layout(binding=12) uniform sampler2D stHzb7;
layout(binding=13) uniform sampler2D stHzb8;
layout(binding=14) uniform sampler2D stHzb9;
layout(binding=15) uniform sampler2D stHzb10;
// Binding seven is stRays in display/marker stages, and HZB level two in this
// trace stage. STReadRay is unused here; the optimizer drops its source access.
bool STFinite(vec3 v){return !any(isnan(v))&&!any(isinf(v));}
float STDepth(int level,ivec2 cell) {
    // Constant sampler operands are legal on GL4.5 and Vulkan without requiring
    // nonuniform descriptor indexing. Every call is one depth texture fetch.
    switch(level){
    case 0:return texelFetch(stHzb0,cell,0).r;
    case 1:return texelFetch(stHzb1,cell,0).r;
    case 2:return texelFetch(stRays,cell,0).r;
    case 3:return texelFetch(stHzb3,cell,0).r;
    case 4:return texelFetch(stHzb4,cell,0).r;
    case 5:return texelFetch(stHzb5,cell,0).r;
    case 6:return texelFetch(stHzb6,cell,0).r;
    case 7:return texelFetch(stHzb7,cell,0).r;
    case 8:return texelFetch(stHzb8,cell,0).r;
    case 9:return texelFetch(stHzb9,cell,0).r;
    default:return texelFetch(stHzb10,cell,0).r;}
}
vec4 STProbe(int probe,int part) {
    bool uniformProbe=probe<spTraceAtlas.z;
    int slot=uniformProbe?probe:probe-spTraceAtlas.z;
    int entriesWide=uniformProbe?spGrid.x:spGrid.x*spGrid.z;
    ivec2 texel=ivec2((slot%entriesWide)*2+part,slot/entriesWide);
    return uniformProbe?texelFetch(stUniformAtlas,texel,0):texelFetch(stSpawnAtlas,texel,0);
}
float STLinearToNdc(float depth,mat4 projection) {
    return (-depth*projection[2][2]+projection[3][2])/(-depth*projection[2][3]+projection[3][3]);
}
vec3 STWorldFromNdc(vec3 ndc) {
    vec4 view=spInverseProjection*vec4(ndc,1);view/=view.w;
    return (spInverseView*vec4(view.xyz,1)).xyz;
}
float STCellExit(vec2 start,vec2 direction,float time,int level) {
    float cellSize=float(1<<level);vec2 pixel=start+direction*time;
    vec2 boundary=(floor(pixel/cellSize)+vec2(direction.x<0?0:1,direction.y<0?0:1))*cellSize;
    // Nudge across the boundary in its travel direction to avoid re-entering
    // the same texel due to projection roundoff. Cell sizes remain 2^level for
    // odd extents; normalized UV * rounded mip extent would be incorrect.
    // Keep this epsilon in full-resolution pixels: scaling it with a coarse
    // cell could jump several pixels and miss a thin feature at the next edge.
    boundary+=vec2(direction.x<0?-.005:.005,direction.y<0?-.005:.005);
    vec2 crossing=vec2(abs(direction.x)>1e-8?(boundary.x-start.x)/direction.x:1e30,
        abs(direction.y)>1e-8?(boundary.y-start.y)/direction.y:1e30);
    return min(crossing.x,crossing.y);
}
void STTrace(int probe,int ray,out vec4 color,out vec4 hit,out vec4 metadata,out vec4 directionAndFetches) {
    color=vec4(0);hit=vec4(0);metadata=vec4(-1,-1,0,0);directionAndFetches=vec4(0);
    if(probe>=spTraceAtlas.w)return;
    vec4 position=STProbe(probe,0),normalDepth=STProbe(probe,1);
    if(position.w<.5)return;
    vec3 direction=normalize(STRayDirection(ray));directionAndFetches.xyz=direction;
    vec3 normal=normalDepth.xyz;hit.xyz=position.xyz;
    if(!STFinite(position.xyz)||!STFinite(normal)||dot(normal,normal)<.5){color.w=6;return;}
    normal=normalize(normal);
    bool cosineHemisphere=(spTracing.w&2)!=0;
    if(cosineHemisphere){direction=normalize(STCosineRayDirection(probe,ray,normal));directionAndFetches.xyz=direction;}
    if(dot(direction,normal)<=(cosineHemisphere?0.0:.02)){color.w=5;return;}
    // A validated native-only gather consumes the same cosine directions and
    // safe origins, then queries the full interval once. It needs no screen
    // depth hint that would be immediately re-certified against triangles.
    if(cosineHemisphere&&spCurvature.z>1.5){color.w=2;return;}
    mat4 projection=spViewProjection*spInverseView;
    vec4 probeClip=spViewProjection*vec4(position.xyz,1);
    if(probeClip.w<=0){color.w=6;return;}
    vec3 probeNdc=probeClip.xyz/probeClip.w;
    if(any(greaterThanEqual(abs(probeNdc),vec3(1)))){color.w=6;return;}
    ivec2 probePixel=ivec2(floor((probeNdc.xy*.5+.5)*vec2(spScreen.xy)));
    vec4 sourceGeometry=texelFetch(stViewPositions,probePixel,0);directionAndFetches.w+=1;
    float probeViewDepth=-(spView*vec4(position.xyz,1)).z;
    float sourceTolerance=spTraceParams.y+spTraceParams.w*max(probeViewDepth,.0001);
    if(sourceGeometry.w<.5||!STFinite(sourceGeometry.xyz)||abs(-sourceGeometry.z-probeViewDepth)>sourceTolerance){color.w=6;return;}
#if ST_SAFE_ORIGINS
    vec4 safeOrigin=texelFetch(stSafeOrigins,ivec2(probe%spGrid.x,probe/spGrid.x),0);
    if(!(safeOrigin.w>0)||!STFinite(safeOrigin.xyz)){color.w=16;return;}
    vec3 origin=safeOrigin.xyz;float bias=length(origin-position.xyz);
#else
    vec4 cornerView=spInverseProjection*vec4(probeNdc.xy+1.0/vec2(spScreen.xy),probeNdc.z,1);
    cornerView/=cornerView.w;
    vec3 cornerWorld=(spInverseView*vec4(cornerView.xyz,1)).xyz;
    float bias=max(spTraceParams.z,2.0*abs(dot(cornerWorld-position.xyz,normal)));
    vec3 origin=position.xyz+normal*bias;
#endif
    vec4 startClip=spViewProjection*vec4(origin,1);vec3 startNdc=startClip.xyz/startClip.w;
    if(startClip.w<=0||any(greaterThanEqual(abs(startNdc),vec3(1)))){color.w=2;return;}
    vec3 originView=(spView*vec4(origin,1)).xyz,viewDirection=mat3(spView)*direction;
    vec4 nearView=spInverseProjection*vec4(0,0,-1,1);float nearDepth=max(.00001,-nearView.z/nearView.w);
    float distanceEnd=spTraceParams.x;bool clipped=false;
    if(viewDirection.z>0){float nearDistance=(-nearDepth*1.001-originView.z)/viewDirection.z;
        if(nearDistance<distanceEnd){distanceEnd=max(nearDistance,0.0);clipped=true;}}
    vec4 endClip=spViewProjection*vec4(origin+direction*distanceEnd,1);
    if(endClip.w<=0||distanceEnd<=0){color.w=2;return;}
    vec3 endNdc=endClip.xyz/endClip.w,deltaNdc=endNdc-startNdc;
    float endFraction=1.0;
    for(int axis=0;axis<3;++axis){float d=deltaNdc[axis];if(abs(d)<1e-8)continue;
        float edge=d>0?1.0:-1.0;float crossing=(edge-startNdc[axis])/d;
        if(crossing>=0&&crossing<endFraction){endFraction=crossing;clipped=true;}}
    endNdc=startNdc+deltaNdc*endFraction;deltaNdc=endNdc-startNdc;
    vec2 startPixel=(startNdc.xy*.5+.5)*vec2(spScreen.xy),deltaPixel=deltaNdc.xy*.5*vec2(spScreen.xy);
    int maxMip=clamp(min(spTracing.z,spTraceDisplay.w-1),0,10),level=0;
    float time=STCellExit(startPixel,deltaPixel,0.0,0),lastVisibleTime=0;
    bool found=false;
    // Stackless hierarchical intersection, rather than fixed-length ray steps.
    // Front cells advance to their XY/depth boundary and can coarsen; potential
    // crossings descend without advancing until the exact full-size texel.
    for(int iteration=0;iteration<512;++iteration) {
        if(time>=1.0){color.w=clipped?2:3;time=1;break;}
        if(iteration>=spTracing.y){color.w=8;break;}
        vec3 ndc=startNdc+deltaNdc*time;
        vec2 pixel=startPixel+deltaPixel*time;
        if(any(lessThan(pixel,vec2(0)))||any(greaterThanEqual(pixel,vec2(spScreen.xy)))){color.w=2;time=min(time,1.0);break;}
        ivec2 cell=ivec2(floor(pixel/float(1<<level)));
        float depth=STDepth(level,cell);directionAndFetches.w+=1;metadata.z+=1;metadata.w=max(metadata.w,float(level));
        float depthNdc=depth<1e19?STLinearToNdc(depth,projection):2.0;
        bool inFront=ndc.z<depthNdc;
        float exitTime=STCellExit(startPixel,deltaPixel,time,level),nextTime=exitTime;
        bool skipped=inFront;
        if(deltaNdc.z>1e-10&&depth<1e19) {
            float depthTime=(depthNdc-startNdc.z)/deltaNdc.z;
            if(depthTime>=time&&depthTime<nextTime){nextTime=depthTime;skipped=false;}
        }
        if(inFront)time=max(time,nextTime);
        if(skipped){lastVisibleTime=time;level=min(level+1,maxMip);}
        else if(level>0)--level;
        else{found=true;break;}
    }
    if(color.w==0&&!found)color.w=time>=1.0?(clipped?2:3):8;
    vec3 rayWorld=STWorldFromNdc(startNdc+deltaNdc*clamp(time,0.0,1.0));
    hit=vec4(rayWorld,clamp(dot(rayWorld-origin,direction),0.0,spTraceParams.x));
    if(!found)return;
    ivec2 hitPixel=clamp(ivec2(floor(startPixel+deltaPixel*time)),ivec2(0),spScreen.xy-1);
    metadata.xy=vec2(hitPixel);
    vec4 geometry=texelFetch(stViewPositions,hitPixel,0);directionAndFetches.w+=1;
    if(geometry.w<.5||!STFinite(geometry.xyz)){color.w=4;return;}
    vec3 world=(spInverseView*vec4(geometry.xyz,1)).xyz;
    float rayDepth=-(spView*vec4(rayWorld,1)).z,sceneDepth=-geometry.z;
    float tolerance=spTraceParams.y+spTraceParams.w*max(sceneDepth,.0001);
    if(rayDepth<sceneDepth-tolerance||rayDepth-sceneDepth>tolerance){
        vec3 visibleWorld=STWorldFromNdc(startNdc+deltaNdc*clamp(lastVisibleTime,0.0,1.0));
        hit=vec4(visibleWorld,clamp(dot(visibleWorld-origin,direction),0.0,spTraceParams.x));color.w=4;return;}
    vec4 hitNormal=texelFetch(stWorldNormals,hitPixel,0);
    if(hitNormal.w<.5||!STFinite(hitNormal.xyz)||dot(hitNormal.xyz,hitNormal.xyz)<.5){color.w=4;return;}
    float distance=dot(world-origin,direction);
    hit=vec4(world,max(distance,0.0));
    if(distance<=max(2.0*bias,spTraceParams.y)||
        (dot(normalize(hitNormal.xyz),normal)>.95&&abs(dot(world-position.xyz,normal))<bias*.5)){color.w=4;return;}
    if(dot(normalize(hitNormal.xyz),-direction)<=.02){color.w=5;return;}
#if ST_SAFE_ORIGINS
    color=vec4(0,0,0,1);
#else
    ivec2 colorSize=textureSize(stSceneColor,0);
    if(any(greaterThanEqual(hitPixel,colorSize))){color.w=7;return;}
    vec3 radiance=texelFetch(stSceneColor,hitPixel,0).rgb;
    if(!STFinite(radiance)){color.w=7;return;}
    color=vec4(radiance,1);
#endif
}
)GLSL";}

inline std::string TraceShader(bool safeOrigins=false){return std::string("#version 450 core\n")+TraceCommonGLSL(safeOrigins)+R"GLSL(
layout(location=0) out vec4 outColor;
void main(){ivec2 pixel=ivec2(gl_FragCoord.xy);int linear=pixel.y*spTraceAtlas.x+pixel.x;
    int rayGlobal=linear/4,part=linear%4,raysPerProbe=spTracing.x*spTracing.x;
    vec4 color,hit,metadata,direction;
    STTrace(rayGlobal/raysPerProbe,rayGlobal%raysPerProbe,color,hit,metadata,direction);
    outColor=part==0?color:(part==1?hit:(part==2?metadata:direction));}
)GLSL";}

// Keep tracing in the fragment stage: the earlier geometry-stage experiment
// reduced invocation count but serialized the expensive marcher on the tested
// GPU. Four color outputs store one ray's complete tuple without retracing it.
// The public interleaved atlas is rebuilt by a separate fetch-only pass below.
inline std::string TraceOutputsShader(bool safeOrigins=false){return std::string("#version 450 core\n")+TraceCommonGLSL(safeOrigins)+R"GLSL(
layout(location=0) out vec4 outColor;
layout(location=1) out vec4 outHit;
layout(location=2) out vec4 outMetadata;
layout(location=3) out vec4 outDirection;
void main(){ivec2 pixel=ivec2(gl_FragCoord.xy);
    int rayGlobal=pixel.y*(spTraceAtlas.x/4)+pixel.x,raysPerProbe=spTracing.x*spTracing.x;
    STTrace(rayGlobal/raysPerProbe,rayGlobal%raysPerProbe,outColor,outHit,outMetadata,outDirection);}
)GLSL";}

inline std::string PackTraceOutputsShader(){return R"GLSL(#version 450 core
layout(binding=0) uniform sampler2D rayColor;
layout(binding=1) uniform sampler2D rayHit;
layout(binding=2) uniform sampler2D rayMetadata;
layout(binding=3) uniform sampler2D rayDirection;
layout(location=0) out vec4 outColor;
void main(){ivec2 pixel=ivec2(gl_FragCoord.xy),rayPixel=ivec2(pixel.x/4,pixel.y);
    int part=pixel.x%4;
    if(part==0)outColor=texelFetch(rayColor,rayPixel,0);
    else if(part==1)outColor=texelFetch(rayHit,rayPixel,0);
    else if(part==2)outColor=texelFetch(rayMetadata,rayPixel,0);
    else outColor=texelFetch(rayDirection,rayPixel,0);}
)GLSL";}

inline std::string RayQuadVertexShader(){return R"GLSL(#version 450 core
layout(location=0) out int rqRay;
void main(){rqRay=gl_InstanceID;gl_Position=vec4(0,0,0,1);}
)GLSL";}
// The original tracing routines use explicit texelFetch only. Keeping their
// arithmetic in one shared body avoids a second transport implementation.
inline std::string RayQuadGeometryMain(const std::string& traceCall){return R"GLSL(
layout(points) in;layout(triangle_strip,max_vertices=4) out;
layout(location=0) in int rqRay[];
layout(location=0) flat out vec4 rqColor;
layout(location=1) flat out vec4 rqHit;
layout(location=2) flat out vec4 rqMetadata;
layout(location=3) flat out vec4 rqDirection;
void main(){
    int rayGlobal=rqRay[0];vec4 color,hit,metadata,direction;
)GLSL"+traceCall+R"GLSL(
    int linear=rayGlobal*4;
    ivec2 base=ivec2(linear%spTraceAtlas.x,linear/spTraceAtlas.x);
    // A four-pixel row, not point-size-dependent rasterization. All vertices
    // carry the same flat tuple, so provoking-vertex conventions cannot matter.
    for(int vertex=0;vertex<4;++vertex){
        ivec2 pixel=base+ivec2((vertex&1)*4,vertex/2);
        gl_Position=vec4(vec2(pixel)*2.0/vec2(spTraceAtlas.xy)-1.0,0,1);
        rqColor=color;rqHit=hit;rqMetadata=metadata;rqDirection=direction;
        EmitVertex();
    }
    EndPrimitive();
}
)GLSL";}
inline std::string RayQuadFragmentShader(){return R"GLSL(#version 450 core
layout(location=0) flat in vec4 rqColor;
layout(location=1) flat in vec4 rqHit;
layout(location=2) flat in vec4 rqMetadata;
layout(location=3) flat in vec4 rqDirection;
layout(location=0) out vec4 outColor;
void main(){int part=int(gl_FragCoord.x)%4;
    outColor=part==0?rqColor:(part==1?rqHit:(part==2?rqMetadata:rqDirection));}
)GLSL";}
inline std::string TraceGeometryShader(bool safeOrigins=false){
    return std::string("#version 450 core\n")+TraceCommonGLSL(safeOrigins)+
        RayQuadGeometryMain("    int raysPerProbe=spTracing.x*spTracing.x;\n"
            "    STTrace(rayGlobal/raysPerProbe,rayGlobal%raysPerProbe,color,hit,metadata,direction);\n");
}

inline std::string DisplayShader(){return std::string("#version 450 core\n")+ConstantsGLSL()+MarkerTraceGLSL()+R"GLSL(
in vec2 vUV;layout(location=0) out vec4 outColor;
layout(binding=0) uniform sampler2D stScene;
void main(){ivec2 pixel=ivec2(gl_FragCoord.xy);vec4 scene=texelFetch(stScene,pixel,0);outColor=scene;
    if(spTraceDisplay.z==0||spTracing.w==0)return;
    float side=clamp(float(spScreen.y)*.42,96.0,256.0);
    vec2 low=vec2(float(spScreen.x)-side-16.0,16.0),local=vec2(pixel)+.5-low;
    if(any(lessThan(local,vec2(-3)))||any(greaterThanEqual(local,vec2(side+3))))return;
    if(any(lessThan(local,vec2(0)))||any(greaterThanEqual(local,vec2(side)))){outColor=vec4(.12,.14,.18,1);return;}
    int probe=spTraceDisplay.y;
    if(probe<0)probe=min((spGrid.y/2)*spGrid.x+spGrid.x/2,spTraceAtlas.z-1);
    probe=clamp(probe,0,max(spTraceAtlas.w-1,0));
    vec2 cell=local*float(spTracing.x)/side;ivec2 rayCell=clamp(ivec2(floor(cell)),ivec2(0),ivec2(spTracing.x-1));
    vec4 ray=STReadRay(probe,rayCell.x+rayCell.y*spTracing.x,0);
    vec3 color=STDiagnosticColor(ray.rgb,ray.w);vec2 border=min(fract(cell),1.0-fract(cell));
    if(min(border.x,border.y)<float(spTracing.x)/side)color*=.55;
    outColor=vec4(color,1);}
)GLSL";}
} // namespace Render::ScreenProbeTraceDetail
