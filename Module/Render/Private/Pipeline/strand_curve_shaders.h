#pragma once
#include <stdexcept>
#include <string>

namespace Render::PipelineDetail {
// Native fillTriangle consumer math. CPU only selects/compacts the original
// producer's child records; curve positions and ribbon directions remain GPU.
inline std::string StrandCurveGLSL(){return R"GLSL(
layout(binding=8) uniform sampler2D strandCurveData;
layout(std140,binding=13) uniform StrandCurveLayout {
    uvec4 strandCurveAtlas; // width,height,pointCount,segmentCount
    uvec4 strandCurveOffsets; // previousPoint,attributes,topology,children
    mat4 strandCurveInverseRasterProjection;
};
vec4 StrandCurveRead(uint index){
    return texelFetch(strandCurveData,ivec2(index%strandCurveAtlas.x,index/strandCurveAtlas.x),0);
}
struct StrandCurveVertex {
    vec4 positionRadius,tangentArc,previousPositionRadius,previousTangentArc;
    vec2 colorCoordinates;
    uint shadeStart,shadeEnd,segment;
    float t;
};
void StrandCurvePose(vec3 p0,vec3 p1,vec3 p2,vec3 p3,float t,out vec3 p,out vec3 d){
    float a=sqrt(length(p1-p0)),b=sqrt(length(p2-p1)),c=sqrt(length(p3-p2));
    // Native knot divisions have no guard. A collapsed control span is a
    // labeled finite host fallback, never a claim of source bit equivalence.
    if(min(a,min(b,c))<1e-6){p=mix(p1,p2,t);d=p2-p1;return;}
    float u=b*t,v=a+u,w=b-u,z=b+c-u;
    vec3 a1=(-u*p0+v*p1)/a,a2=(w*p1+u*p2)/b,a3=(z*p2-w*p3)/c;
    vec3 b1=(w*a1+v*a2)/(a+b),b2=(z*a2+u*a3)/(b+c);
    p=(w*b1+u*b2)/b;
    vec3 da1=(p1-p0)/a,da2=(p2-p1)/b,da3=(p3-p2)/c;
    // Literal native SSA264 divides the first term by b, not a+b. This
    // interior ribbon direction differs from the textbook CR derivative.
    vec3 db1=(a2-a1)/b+(w*da1+v*da2)/(a+b);
    vec3 db2=(-a2+a3+z*da2+u*da3)/(b+c);
    d=(b2-b1+w*db1+u*db2)/b;
}
void StrandCurveControls(uvec4 indices,uint base,out vec4 p0,out vec4 p1,out vec4 p2,out vec4 p3){
    p1=StrandCurveRead(base+indices.y);p2=StrandCurveRead(base+indices.z);
    p0=indices.x==indices.y?vec4(2.0*p1.xyz-p2.xyz,p1.w):StrandCurveRead(base+indices.x);
    p3=indices.w==indices.z?vec4(2.0*p2.xyz-p1.xyz,p2.w):StrandCurveRead(base+indices.w);
}
StrandCurveVertex StrandCurveInput(){
    vec4 piece=StrandCurveRead(strandCurveOffsets.w+uint(gl_InstanceID));
    uint segment=uint(piece.x),count=uint(piece.y),child=uint(piece.z);
    uvec4 indices=uvec4(StrandCurveRead(strandCurveOffsets.z+segment));
    float t=(float(child)+float(gl_VertexID&1))/float(max(count,1u));
    float side=(gl_VertexID&2)==0?-1.0:1.0;
    vec4 p0,p1,p2,p3,q0,q1,q2,q3;vec3 p,d,q,e;
    StrandCurveControls(indices,0u,p0,p1,p2,p3);
    StrandCurveControls(indices,strandCurveOffsets.x,q0,q1,q2,q3);
    StrandCurvePose(p0.xyz,p1.xyz,p2.xyz,p3.xyz,t,p,d);
    StrandCurvePose(q0.xyz,q1.xyz,q2.xyz,q3.xyz,t,q,e);
    vec4 attributes=mix(StrandCurveRead(strandCurveOffsets.y+indices.y),
        StrandCurveRead(strandCurveOffsets.y+indices.z),t);
    StrandCurveVertex result;
    result.positionRadius=vec4(p,side*max(mix(p1.w,p2.w,t),1e-10));
    result.previousPositionRadius=vec4(q,side*max(mix(q1.w,q2.w,t),1e-10));
    result.tangentArc=vec4(d,attributes.x);result.previousTangentArc=vec4(e,attributes.x);
    result.colorCoordinates=attributes.yz;
    result.shadeStart=indices.y;result.shadeEnd=indices.z;result.segment=segment;result.t=t;
    return result;
}
)GLSL";}

inline void StrandCurveReplace(std::string& source,const std::string& from,const std::string& to){
    std::size_t p=0;while((p=source.find(from,p))!=std::string::npos){source.replace(p,from.size(),to);p+=to.size();}
}
// Apply only to the main body: keep the original 72B attributes/layout usable
// by legacy pipelines and by the unit-quad mesh consumed by the curve variant.
inline std::string StrandCurveVertexMain(std::string source){
    const auto begin=source.find("void main()");
    if(begin==std::string::npos)throw std::runtime_error("Curve raster requires a vertex main");
    source.insert(begin,StrandCurveGLSL());
    const auto body=source.find("void main()",begin+StrandCurveGLSL().size());
    if(body==std::string::npos)throw std::runtime_error("Curve raster lost its vertex main");
    auto main=source.substr(body);
    StrandCurveReplace(main,"previousPositionRadius","curve.previousPositionRadius");
    StrandCurveReplace(main,"previousTangentArc","curve.previousTangentArc");
    StrandCurveReplace(main,"positionRadius","curve.positionRadius");
    StrandCurveReplace(main,"tangentArc","curve.tangentArc");
    StrandCurveReplace(main,"colorCoordinates","curve.colorCoordinates");
    const auto opening=main.find('{');
    main.insert(opening+1,R"GLSL(
    StrandCurveVertex curve;
    if(strandCurveAtlas.z>0u)curve=StrandCurveInput();
    else {
        curve.positionRadius=positionRadius;curve.tangentArc=tangentArc;
        curve.previousPositionRadius=previousPositionRadius;curve.previousTangentArc=previousTangentArc;
        curve.colorCoordinates=colorCoordinates;curve.shadeStart=uint(gl_VertexID/2);
        curve.shadeEnd=curve.shadeStart;curve.segment=0u;curve.t=0.0;
    }
)GLSL");
    source.replace(body,std::string::npos,main);
    return source;
}
} // namespace Render::PipelineDetail
