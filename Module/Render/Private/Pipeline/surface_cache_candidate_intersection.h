#pragma once
#include <stdexcept>
#include <string>

namespace Render::SurfaceCacheDetail {
// Experimental transport-only replacement for SCNativeIntersect. The caller
// supplies scAcceleration, SCGeometry, WGPreciseVertex and optional scTraceDiag.
// A normal native interior hit keeps the build22 packed-float predicate. An
// ambiguous hit first validates just its original residual-restored triangle.
// Only an invalid original candidate repeats traversal with NoOpaque and
// selects the nearest precise t without confirmation or software BVH storage.
//
// Both ray-query and float64 features are required. Candidate coverage remains
// that of the existing expanded AS; retain the independent original oracle.
inline std::string TransportCandidateIntersectionGLSL(bool diagnostics=false) {
    auto source=std::string(R"GLSL(
#ifdef RENDER_NATIVE_FLOAT64
bool WGCandidatePrecise(vec3 p,vec3 d,int candidate,double upper,out double distance,out vec3 normal) {
    distance=0;normal=vec3(0);
    int offset=scGeometry.x+candidate*5,base=candidate*3;
    dvec3 a=WGPreciseVertex(base,SCGeometry(offset).xyz),
        ab=WGPreciseVertex(base+1,SCGeometry(offset+1).xyz)-a,
        ac=WGPreciseVertex(base+2,SCGeometry(offset+2).xyz)-a,
        direction=dvec3(d),q=cross(direction,ac);
    double determinant=dot(ab,q);
    if(!(abs(determinant)>=1e-9))return false;
    dvec3 relative=dvec3(p)-a,r=cross(relative,ab);
    double u=dot(relative,q)/determinant,v=dot(direction,r)/determinant;
    if(!(u>=0&&v>=0&&u+v<=1))return false;
    distance=dot(ac,r)/determinant;
    if(!(distance>=double(1)/double(100000)&&distance<upper))return false;
    normal=vec3(normalize(cross(ab,ac)));return true;
}
#endif
bool SCNativeIntersect(vec3 p,vec3 d,inout float maximum,out vec3 n,out int triangle,out bool exhausted,bool anyHit) {
    n=vec3(0);triangle=-1;exhausted=false;
    if(scGeometry.z==0||maximum<=1e-5)return false;
#if defined(RENDER_NATIVE_RAY_QUERY) && defined(RENDER_NATIVE_FLOAT64)
    float initialMaximum=maximum;
    float rayMaximum=uintBitsToFloat(floatBitsToUint(initialMaximum)-1u);
    rayQueryEXT query;
    uint flags=gl_RayFlagsOpaqueEXT|(anyHit?gl_RayFlagsTerminateOnFirstHitEXT:0u);
    rayQueryInitializeEXT(query,scAcceleration,flags,255,p,1e-5,d,rayMaximum);
    while(rayQueryProceedEXT(query)) {}
    if(rayQueryGetIntersectionTypeEXT(query,true)==gl_RayQueryCommittedIntersectionNoneEXT)return false;
    int candidate=int(rayQueryGetIntersectionPrimitiveIndexEXT(query,true));
    if(candidate<0||candidate>=scGeometry.w){exhausted=true;return false;}
    {
        int offset=scGeometry.x+candidate*5;
        vec3 a=SCGeometry(offset).xyz,ab=SCGeometry(offset+1).xyz-a,ac=SCGeometry(offset+2).xyz-a;
        vec3 q=cross(d,ac);float determinant=dot(ab,q);
        if(abs(determinant)>=1e-9) {
            vec3 relative=p-a;float u=dot(relative,q)/determinant;
            vec3 r=cross(relative,ab);float v=dot(d,r)/determinant;
            float distance=dot(ac,r)/determinant;
            if(min(u,min(v,1.0-u-v))>8e-6&&distance>=.0001&&distance<initialMaximum&&
                abs(rayQueryGetIntersectionTEXT(query,true)-distance)<=max(1e-7,abs(distance)*2e-6)) {
                maximum=distance;n=normalize(cross(ab,ac));triangle=candidate;return true;
            }
        }
    }
    double preciseDistance;vec3 preciseNormal;
    if(WGCandidatePrecise(p,d,candidate,double(initialMaximum),preciseDistance,preciseNormal)) {
        maximum=float(preciseDistance);n=preciseNormal;triangle=candidate;return true;
    }
    // Expanded nearest candidate was outside the original triangle. Keep the
    // original interval while enumerating alternatives: confirming an expanded
    // candidate would prune by native t rather than original precise t.
    rayQueryEXT retry;
    rayQueryInitializeEXT(retry,scAcceleration,gl_RayFlagsNoOpaqueEXT,255,p,1e-5,d,rayMaximum);
    double closest=double(initialMaximum);bool found=false;
    while(rayQueryProceedEXT(retry)) {
        if(rayQueryGetIntersectionTypeEXT(retry,false)!=gl_RayQueryCandidateIntersectionTriangleEXT) {
            exhausted=true;triangle=-1;n=vec3(0);rayQueryTerminateEXT(retry);return false;
        }
        candidate=int(rayQueryGetIntersectionPrimitiveIndexEXT(retry,false));
        if(candidate<0||candidate>=scGeometry.w) {
            exhausted=true;triangle=-1;n=vec3(0);rayQueryTerminateEXT(retry);return false;
        }
        double distance;vec3 normal;
        if(!WGCandidatePrecise(p,d,candidate,closest,distance,normal))continue;
        closest=distance;n=normal;triangle=candidate;found=true;
        if(anyHit) {
            maximum=float(closest);rayQueryTerminateEXT(retry);return true;
        }
    }
    if(found)maximum=float(closest);
    return found;
#else
    exhausted=true;return false;
#endif
}
)GLSL");
    if(diagnostics) {
        const auto replace=[&](const std::string& from,const std::string& to) {
            const auto at=source.find(from);
            if(at==std::string::npos||source.find(from,at+from.size())!=std::string::npos)
                throw std::logic_error("Candidate trace diagnostic source changed");
            source.replace(at,from.size(),to);
        };
        replace("\n    rayQueryInitializeEXT(query,", "\n    ++scTraceDiag[0].x;\n    rayQueryInitializeEXT(query,");
        replace("\n    rayQueryInitializeEXT(retry,", "\n    ++scTraceDiag[0].x;\n    rayQueryInitializeEXT(retry,");
        replace("\n                maximum=distance;", "\n                ++scTraceDiag[0].y;\n                maximum=distance;");
        replace("\n        maximum=float(preciseDistance);", "\n        ++scTraceDiag[0].y;\n        maximum=float(preciseDistance);");
        replace("\n            maximum=float(closest);", "\n            ++scTraceDiag[0].y;\n            maximum=float(closest);");
        replace("\n    if(found)maximum=float(closest);", "\n    if(found){++scTraceDiag[0].y;maximum=float(closest);}");
    }
    return source;
}
}
