#pragma once
#include <cstdint>
#include <stdexcept>
#include <string>

namespace Render {
// The 208-byte block matches SurfaceCacheGiConstants. These pure GLSL helpers
// are shared by card lighting and the opt-in opaque PBR area-shadow variant.
inline std::string SurfaceCacheDataGLSL() {return R"GLSL(
layout(std140,binding=12) uniform SurfaceCacheData {
    mat4 scInverseView;
    ivec4 scAtlas,scGeometry,scScreen;
    vec4 scParameters,scSunDirectionIntensity,scSunColor;
    vec4 scSpotPositionRadius,scSpotDirectionOuter,scSpotIntensityInner;
};
)GLSL";}
inline std::string SoftwareBvhVisibilityGLSL(std::uint32_t textureSlot=9,bool reuseClosestBounds=false,bool fastGeometryAddressing=false) {
    auto source="\nlayout(binding="+std::to_string(textureSlot)+") uniform sampler2D scTriangles;\n"+R"GLSL(
vec4 SCGeometry(int index){return texelFetch(scTriangles,ivec2(index%scGeometry.y,index/scGeometry.y),0);}
bool SCBox(vec3 p,vec3 d,vec3 low,vec3 high,float maximum,out float near) {
    near=0;float far=maximum;
    for(int axis=0;axis<3;++axis) {
        if(abs(d[axis])<1e-9){if(p[axis]<low[axis]||p[axis]>high[axis])return false;continue;}
        float a=(low[axis]-p[axis])/d[axis],b=(high[axis]-p[axis])/d[axis];
        near=max(near,min(a,b));far=min(far,max(a,b));if(near>far)return false;
    }return true;
}
bool SCIntersect(vec3 p,vec3 d,inout float maximum,out vec3 n,out int triangle,out bool exhausted,bool anyHit) {
    n=vec3(0);triangle=-1;exhausted=false;if(scGeometry.z==0)return false;
    // Exact-median BVH construction has at most 18 root-to-leaf entries for
    // the validated <= 2^20 triangles. Retain overflow/exhaustion guards.
    int stack[32];stack[0]=0;int size=1;bool found=false;
    for(int visit=0;visit<16384&&size>0;++visit) {
        int node=stack[--size];vec4 low=SCGeometry(node*2),high=SCGeometry(node*2+1);float near;
        if(!SCBox(p,d,low.xyz,high.xyz,maximum,near))continue;
        if(high.w>=0) {
            if(size>29){exhausted=true;return found;}
            int left=int(low.w),right=int(high.w);float ln,rn;
            bool l=SCBox(p,d,SCGeometry(left*2).xyz,SCGeometry(left*2+1).xyz,maximum,ln);
            bool r=SCBox(p,d,SCGeometry(right*2).xyz,SCGeometry(right*2+1).xyz,maximum,rn);
            if(l&&r){stack[size++]=ln<rn?right:left;stack[size++]=ln<rn?left:right;}
            else if(l)stack[size++]=left;else if(r)stack[size++]=right;
            continue;
        }
        for(int i=0;i<int(-high.w);++i) {
            int offset=scGeometry.x+(int(low.w)+i)*5;
            vec3 a=SCGeometry(offset).xyz,ab=SCGeometry(offset+1).xyz-a,ac=SCGeometry(offset+2).xyz-a;
            vec3 q=cross(d,ac);float determinant=dot(ab,q);if(abs(determinant)<1e-9)continue;
            vec3 relative=p-a;float u=dot(relative,q)/determinant;if(u< -1e-6||u>1.000001)continue;
            vec3 r=cross(relative,ab);float v=dot(d,r)/determinant;if(v< -1e-6||u+v>1.000001)continue;
            float distance=dot(ac,r)/determinant;if(distance<1e-5||distance>=maximum)continue;
            maximum=distance;n=normalize(cross(ab,ac));triangle=int(low.w)+i;found=true;if(anyHit)return true;
        }
    }
    exhausted=size>0;return found;
}
// Visibility-only traversal: the segment bound is immutable and the first
// triangle hit returns immediately. Every non-root entry already passed the
// identical child bounds test. Keep the legacy closest-hit function untouched.
bool SCAnyHitReusingBounds(vec3 p,vec3 d,float maximum,out bool exhausted) {
    exhausted=false;if(scGeometry.z==0)return false;
    int stack[32];stack[0]=0;int size=1;
    for(int visit=0;visit<16384&&size>0;++visit) {
        int node=stack[--size];vec4 low=SCGeometry(node*2),high=SCGeometry(node*2+1);float near;
        if(node==0&&!SCBox(p,d,low.xyz,high.xyz,maximum,near))continue;
        if(high.w>=0) {
            if(size>29){exhausted=true;return false;}
            int left=int(low.w),right=int(high.w);float ln,rn;
            bool l=SCBox(p,d,SCGeometry(left*2).xyz,SCGeometry(left*2+1).xyz,maximum,ln);
            bool r=SCBox(p,d,SCGeometry(right*2).xyz,SCGeometry(right*2+1).xyz,maximum,rn);
            if(l&&r){stack[size++]=ln<rn?right:left;stack[size++]=ln<rn?left:right;}
            else if(l)stack[size++]=left;else if(r)stack[size++]=right;
            continue;
        }
        for(int i=0;i<int(-high.w);++i) {
            int offset=scGeometry.x+(int(low.w)+i)*5;
            vec3 a=SCGeometry(offset).xyz,ab=SCGeometry(offset+1).xyz-a,ac=SCGeometry(offset+2).xyz-a;
            vec3 q=cross(d,ac);float determinant=dot(ab,q);if(abs(determinant)<1e-9)continue;
            vec3 relative=p-a;float u=dot(relative,q)/determinant;if(u< -1e-6||u>1.000001)continue;
            vec3 r=cross(relative,ab);float v=dot(d,r)/determinant;if(v< -1e-6||u+v>1.000001)continue;
            float distance=dot(ac,r)/determinant;if(distance<1e-5||distance>=maximum)continue;
            return true;
        }
    }
    exhausted=size>0;return false;
}
bool SCVisible(vec3 p,vec3 n,vec3 d,float distance) {
    vec3 unused;int triangle;bool exhausted;distance=max(0.0,distance-2*scParameters.y);
    bool found;
    if((int(scParameters.w+.5)&4)!=0)found=SCAnyHitReusingBounds(p+n*scParameters.y,d,distance,exhausted);
    else found=SCIntersect(p+n*scParameters.y,d,distance,unused,triangle,exhausted,true);
    return !found&&!exhausted;
}
)GLSL";
    if(fastGeometryAddressing) {
        const std::string original="vec4 SCGeometry(int index){return texelFetch(scTriangles,ivec2(index%scGeometry.y,index/scGeometry.y),0);}";
        const auto at=source.find(original);
        source.replace(at,original.size(),R"GLSL(ivec2 SCGeometryAddress(int index) {
    // Preserve signed division/remainder for negative diagnostic indices and
    // resident layouts with another width. Production geometry indices are
    // nonnegative; the shift/mask is only valid for this exact width.
    if(scGeometry.y==1024&&index>=0)return ivec2(index&1023,index>>10);
    return ivec2(index%scGeometry.y,index/scGeometry.y);
}
vec4 SCGeometry(int index){return texelFetch(scTriangles,SCGeometryAddress(index),0);})GLSL");
    }
    if(reuseClosestBounds)source+=R"GLSL(
// Opt-in closest-hit execution variant. The immediately next child has just
// passed the identical bounds test and no intervening triangle can shrink
// maximum. Cache only that child. A later sibling always reloads and retests
// with the potentially reduced maximum. Stack order, guards, triangle math,
// strict upper endpoint, ties and partial exhausted results remain unchanged.
bool SCIntersectReusingChildBounds(vec3 p,vec3 d,inout float maximum,out vec3 n,out int triangle,out bool exhausted,bool anyHit) {
    n=vec3(0);triangle=-1;exhausted=false;if(scGeometry.z==0)return false;
    int stack[32];stack[0]=0;int size=1;bool found=false;
    int cachedNode=-1;vec4 cachedLow,cachedHigh;
    for(int visit=0;visit<16384&&size>0;++visit) {
        int node=stack[--size];vec4 low,high;bool cached=node==cachedNode;
        if(cached){low=cachedLow;high=cachedHigh;cachedNode=-1;}
        else {low=SCGeometry(node*2);high=SCGeometry(node*2+1);}
        float near;
        if(!cached&&!SCBox(p,d,low.xyz,high.xyz,maximum,near))continue;
        if(high.w>=0) {
            if(size>29){exhausted=true;return found;}
            int left=int(low.w),right=int(high.w);float ln,rn;
            vec4 leftLow=SCGeometry(left*2),leftHigh=SCGeometry(left*2+1);
            vec4 rightLow=SCGeometry(right*2),rightHigh=SCGeometry(right*2+1);
            bool l=SCBox(p,d,leftLow.xyz,leftHigh.xyz,maximum,ln);
            bool r=SCBox(p,d,rightLow.xyz,rightHigh.xyz,maximum,rn);
            if(l&&r) {
                stack[size++]=ln<rn?right:left;stack[size++]=ln<rn?left:right;
                cachedNode=stack[size-1];cachedLow=ln<rn?leftLow:rightLow;cachedHigh=ln<rn?leftHigh:rightHigh;
            } else if(l){stack[size++]=left;cachedNode=left;cachedLow=leftLow;cachedHigh=leftHigh;}
            else if(r){stack[size++]=right;cachedNode=right;cachedLow=rightLow;cachedHigh=rightHigh;}
            continue;
        }
        for(int i=0;i<int(-high.w);++i) {
            int offset=scGeometry.x+(int(low.w)+i)*5;
            vec3 a=SCGeometry(offset).xyz,ab=SCGeometry(offset+1).xyz-a,ac=SCGeometry(offset+2).xyz-a;
            vec3 q=cross(d,ac);float determinant=dot(ab,q);if(abs(determinant)<1e-9)continue;
            vec3 relative=p-a;float u=dot(relative,q)/determinant;if(u< -1e-6||u>1.000001)continue;
            vec3 r=cross(relative,ab);float v=dot(d,r)/determinant;if(v< -1e-6||u+v>1.000001)continue;
            float distance=dot(ac,r)/determinant;if(distance<1e-5||distance>=maximum)continue;
            maximum=distance;n=normalize(cross(ab,ac));triangle=int(low.w)+i;found=true;if(anyHit)return true;
        }
    }
    exhausted=size>0;return found;
}
// Definitions above remain available verbatim. Only later call sites and
// native fallback/certification helpers select the new traversal. SCVisible's
// existing body and independently selectable any-hit variant stay intact.
#define SCIntersect SCIntersectReusingChildBounds
)GLSL";
    return source;
}
// Opt-in only: the backend recognizes this marker and binds the acceleration
// structure built from the same packed triangle order as SCGeometry. Keep it
// separate from the shared software helper so probe/SDF/launch certification
// shaders never acquire a hardware ray-query dependency accidentally.
inline std::string NativeSurfaceCacheIntersectionGLSL() {return R"GLSL(
#define SC_NATIVE_GEOMETRY_SLOT 9
#ifdef RENDER_NATIVE_RAY_QUERY
layout(set=2,binding=0) uniform accelerationStructureEXT scAcceleration;
#endif
bool SCNativeIntersect(vec3 p,vec3 d,inout float maximum,out vec3 n,out int triangle,out bool exhausted,bool anyHit) {
#ifdef RENDER_NATIVE_RAY_QUERY
    n=vec3(0);triangle=-1;exhausted=false;
    if(scGeometry.z==0||maximum<=1e-5)return false;
    rayQueryEXT query;
    uint flags=gl_RayFlagsOpaqueEXT|(anyHit?gl_RayFlagsTerminateOnFirstHitEXT:0u);
    // Hardware any-hit can terminate at an arbitrary candidate. Exclude the
    // open segment endpoint before traversal, rather than discarding an
    // endpoint hit afterwards and accidentally overlooking another blocker.
    float rayMaximum=uintBitsToFloat(floatBitsToUint(maximum)-1u);
    rayQueryInitializeEXT(query,scAcceleration,flags,255,p,1e-5,d,rayMaximum);
    while(rayQueryProceedEXT(query)) {}
    if(rayQueryGetIntersectionTypeEXT(query,true)==gl_RayQueryCommittedIntersectionNoneEXT) {
        // The software closest-hit predicate deliberately accepts a tiny
        // barycentric edge expansion. Native watertight triangles do not;
        // retain its rare edge hits when receiver rays miss natively. Visible
        // light segments must not retrace all ordinary hardware misses.
        if(!anyHit)return SCIntersect(p,d,maximum,n,triangle,exhausted,false);
        return false;
    }
    triangle=int(rayQueryGetIntersectionPrimitiveIndexEXT(query,true));
    if(triangle<0||triangle>=scGeometry.w){exhausted=true;triangle=-1;return false;}
    // Use hardware only to find a candidate. Recover distance and normal with
    // the identical packed-float Moller predicate used by SCIntersect, rather
    // than feeding native intersection rounding into contact hit positions.
    int offset=scGeometry.x+triangle*5;
    vec3 a=SCGeometry(offset).xyz,ab=SCGeometry(offset+1).xyz-a,ac=SCGeometry(offset+2).xyz-a;
    vec3 q=cross(d,ac);float determinant=dot(ab,q);
    if(abs(determinant)<1e-9)return SCIntersect(p,d,maximum,n,triangle,exhausted,anyHit);
    vec3 relative=p-a;float u=dot(relative,q)/determinant;
    if(u< -1e-6||u>1.000001)return SCIntersect(p,d,maximum,n,triangle,exhausted,anyHit);
    vec3 r=cross(relative,ab);float v=dot(d,r)/determinant;
    if(v< -1e-6||u+v>1.000001)return SCIntersect(p,d,maximum,n,triangle,exhausted,anyHit);
    // The SC acceleration structure conservatively expands triangle edges.
    // It may therefore nominate a shared-edge/epsilon hit that the software
    // hierarchy's original bounds would reject, or break a coplanar tie in a
    // different order. Resolve only this tiny boundary band with the original
    // traversal; ordinary interior hits and visible-light misses stay native.
    if(min(u,min(v,1.0-u-v))<=8e-6)
        return SCIntersect(p,d,maximum,n,triangle,exhausted,anyHit);
    float distance=dot(ac,r)/determinant;
    if(distance<1e-5||distance>=maximum)return SCIntersect(p,d,maximum,n,triangle,exhausted,anyHit);
    n=normalize(cross(ab,ac));
    maximum=distance;return true;
#else
    return SCIntersect(p,d,maximum,n,triangle,exhausted,anyHit);
#endif
}
bool SCNativeVisible(vec3 p,vec3 n,vec3 d,float distance) {
    vec3 unused;int triangle;bool exhausted;distance=max(0.0,distance-2*scParameters.y);
    bool found=SCNativeIntersect(p+n*scParameters.y,d,distance,unused,triangle,exhausted,true);
    return !found&&!exhausted;
}
// Safe source anchoring requires the original nearest face and tie order.
// Expanded, independently rounded native triangles can miss a near grazing
// face while committing a farther face. Use native search to tighten the
// interval, then certify it once with the original packed-float BVH. A native
// candidate rejected by original hierarchy bounds must not hide a farther
// accepted face, so an empty bounded verification retries the original range.
bool SCNativeOriginIntersect(vec3 p,vec3 d,inout float maximum,out vec3 n,out int triangle,out bool exhausted,bool anyHit) {
    if(anyHit)return SCNativeIntersect(p,d,maximum,n,triangle,exhausted,true);
    float originalMaximum=maximum;
    bool found=SCNativeIntersect(p,d,maximum,n,triangle,exhausted,false);
    if(!found||exhausted)return found;
    maximum=min(originalMaximum,uintBitsToFloat(floatBitsToUint(maximum)+1u));
    found=SCIntersect(p,d,maximum,n,triangle,exhausted,false);
    if(found||exhausted)return found;
    maximum=originalMaximum;
    return SCIntersect(p,d,maximum,n,triangle,exhausted,false);
}
// Opt-in execution variant. A miss/boundary/invalid native candidate already
// requires a complete original traversal. Return that authoritative result
// directly instead of running the original traversal a second time merely
// because its fallback found a hit. Ordinary native candidates still require
// bounded nearest/tie certification and the original-range retry. Keep the
// Round7 wrapper above intact for controlled execution comparisons.
bool SCNativeOriginIntersectReusingCertification(vec3 p,vec3 d,inout float maximum,out vec3 n,out int triangle,out bool exhausted,bool anyHit) {
    if(anyHit)return SCNativeIntersect(p,d,maximum,n,triangle,exhausted,true);
#ifdef RENDER_NATIVE_RAY_QUERY
    n=vec3(0);triangle=-1;exhausted=false;
    if(scGeometry.z==0||maximum<=1e-5)return false;
    float originalMaximum=maximum;
    rayQueryEXT query;
    float rayMaximum=uintBitsToFloat(floatBitsToUint(maximum)-1u);
    rayQueryInitializeEXT(query,scAcceleration,gl_RayFlagsOpaqueEXT,255,p,1e-5,d,rayMaximum);
    while(rayQueryProceedEXT(query)) {}
    if(rayQueryGetIntersectionTypeEXT(query,true)==gl_RayQueryCommittedIntersectionNoneEXT)
        return SCIntersect(p,d,maximum,n,triangle,exhausted,false);
    triangle=int(rayQueryGetIntersectionPrimitiveIndexEXT(query,true));
    if(triangle<0||triangle>=scGeometry.w){exhausted=true;triangle=-1;return false;}
    int offset=scGeometry.x+triangle*5;
    vec3 a=SCGeometry(offset).xyz,ab=SCGeometry(offset+1).xyz-a,ac=SCGeometry(offset+2).xyz-a;
    vec3 q=cross(d,ac);float determinant=dot(ab,q);
    if(abs(determinant)<1e-9)return SCIntersect(p,d,maximum,n,triangle,exhausted,false);
    vec3 relative=p-a;float u=dot(relative,q)/determinant;
    if(u< -1e-6||u>1.000001)return SCIntersect(p,d,maximum,n,triangle,exhausted,false);
    vec3 r=cross(relative,ab);float v=dot(d,r)/determinant;
    if(v< -1e-6||u+v>1.000001)return SCIntersect(p,d,maximum,n,triangle,exhausted,false);
    if(min(u,min(v,1.0-u-v))<=8e-6)
        return SCIntersect(p,d,maximum,n,triangle,exhausted,false);
    float distance=dot(ac,r)/determinant;
    if(distance<1e-5||distance>=maximum)return SCIntersect(p,d,maximum,n,triangle,exhausted,false);
    maximum=min(originalMaximum,uintBitsToFloat(floatBitsToUint(distance)+1u));
    bool found=SCIntersect(p,d,maximum,n,triangle,exhausted,false);
    if(found||exhausted)return found;
    maximum=originalMaximum;
    return SCIntersect(p,d,maximum,n,triangle,exhausted,false);
#else
    return SCIntersect(p,d,maximum,n,triangle,exhausted,false);
#endif
}
)GLSL";}

// Diagnostic replay only. Instrument a copied GeometryGLSL result so ordinary
// shaders retain their exact source and no counter, atomic or buffer accesses.
// Low vector: actual native queries, accepted interior candidates, native-miss
// software fallbacks, edge software fallbacks. High vector: invalid-candidate
// software fallbacks, all closest software calls, origin certification calls
// (bounded plus original-range retry), original-range retry calls.
inline std::string InstrumentSurfaceCacheTraceDiagnosticsGLSL(std::string source) {
    const auto replace=[](std::string& text,const std::string& from,const std::string& to) {
        const auto at=text.find(from);
        if(at==std::string::npos)throw std::logic_error("Surface cache trace diagnostic source anchor missing");
        text.replace(at,from.size(),to);
    };
    const auto instrumentFunction=[&](const std::string& name,const auto& edit,bool required) {
        const auto begin=source.find("bool "+name+"(");
        if(begin==std::string::npos) {
            if(required)throw std::logic_error("Surface cache trace diagnostic function missing");
            return;
        }
        auto end=source.find('{',begin);if(end==std::string::npos)throw std::logic_error("Diagnostic function has no body");
        unsigned depth=1;
        while(++end<source.size()&&depth) {
            if(source[end]=='{')++depth;else if(source[end]=='}')--depth;
        }
        if(depth)throw std::logic_error("Diagnostic function body is incomplete");
        std::string body=source.substr(begin,end-begin);edit(body);
        source.replace(begin,end-begin,body);
    };
    const auto countSoftware=[&](std::string& body) {
        replace(body,"{\n","{\n    ++scTraceDiag[1].y;\n");
    };
    instrumentFunction("SCIntersect",countSoftware,true);
    instrumentFunction("SCIntersectReusingChildBounds",countSoftware,false);
    const auto nativeFallbacks=[&](std::string& body,bool reusedOrigin) {
        replace(body,"    rayQueryInitializeEXT(query,scAcceleration,",
            "    ++scTraceDiag[0].x;\n    rayQueryInitializeEXT(query,scAcceleration,");
        const std::string arguments=reusedOrigin?"false":"anyHit";
        const auto invalid=[&](const std::string& condition) {
            replace(body,"if("+condition+")return SCIntersect(p,d,maximum,n,triangle,exhausted,"+arguments+");",
                "if("+condition+"){++scTraceDiag[1].x;return SCIntersect(p,d,maximum,n,triangle,exhausted,"+arguments+");}");
        };
        invalid("abs(determinant)<1e-9");invalid("u< -1e-6||u>1.000001");
        invalid("v< -1e-6||u+v>1.000001");invalid("distance<1e-5||distance>=maximum");
        replace(body,"if(min(u,min(v,1.0-u-v))<=8e-6)\n        return SCIntersect(p,d,maximum,n,triangle,exhausted,"+arguments+");",
            "if(min(u,min(v,1.0-u-v))<=8e-6){++scTraceDiag[0].w;\n        return SCIntersect(p,d,maximum,n,triangle,exhausted,"+arguments+");}");
        if(reusedOrigin) {
            replace(body,"if(rayQueryGetIntersectionTypeEXT(query,true)==gl_RayQueryCommittedIntersectionNoneEXT)\n        return SCIntersect(p,d,maximum,n,triangle,exhausted,false);",
                "if(rayQueryGetIntersectionTypeEXT(query,true)==gl_RayQueryCommittedIntersectionNoneEXT){++scTraceDiag[0].z;\n        return SCIntersect(p,d,maximum,n,triangle,exhausted,false);}");
            replace(body,"    maximum=min(originalMaximum,uintBitsToFloat(floatBitsToUint(distance)+1u));",
                "    ++scTraceDiag[0].y;\n    maximum=min(originalMaximum,uintBitsToFloat(floatBitsToUint(distance)+1u));");
        } else {
            replace(body,"if(!anyHit)return SCIntersect(p,d,maximum,n,triangle,exhausted,false);",
                "if(!anyHit){++scTraceDiag[0].z;return SCIntersect(p,d,maximum,n,triangle,exhausted,false);}");
            replace(body,"    maximum=distance;return true;","    ++scTraceDiag[0].y;\n    maximum=distance;return true;");
        }
    };
    instrumentFunction("SCNativeIntersect",[&](std::string& body){nativeFallbacks(body,false);},false);
    const auto countCertification=[&](std::string& body) {
        const std::string call="found=SCIntersect(p,d,maximum,n,triangle,exhausted,false);";
        replace(body,call,"++scTraceDiag[1].z;"+call);
        replace(body,"    maximum=originalMaximum;\n    return SCIntersect(p,d,maximum,n,triangle,exhausted,false);",
            "    maximum=originalMaximum;\n    ++scTraceDiag[1].z;++scTraceDiag[1].w;\n    return SCIntersect(p,d,maximum,n,triangle,exhausted,false);");
    };
    instrumentFunction("SCNativeOriginIntersect",countCertification,false);
    instrumentFunction("SCNativeOriginIntersectReusingCertification",[&](std::string& body) {
        nativeFallbacks(body,true);
        // Keep the declaration intact while inserting the certification count.
        replace(body,"    bool found=SCIntersect(p,d,maximum,n,triangle,exhausted,false);",
            "    ++scTraceDiag[1].z;\n    bool found=SCIntersect(p,d,maximum,n,triangle,exhausted,false);");
        replace(body,"    maximum=originalMaximum;\n    return SCIntersect(p,d,maximum,n,triangle,exhausted,false);",
            "    maximum=originalMaximum;\n    ++scTraceDiag[1].z;++scTraceDiag[1].w;\n    return SCIntersect(p,d,maximum,n,triangle,exhausted,false);");
    },false);
    return R"GLSL(
uvec4 scTraceDiag[2];
void SCTraceDiagReset(){scTraceDiag[0]=uvec4(0);scTraceDiag[1]=uvec4(0);}
)GLSL"+source;
}
} // namespace Render
