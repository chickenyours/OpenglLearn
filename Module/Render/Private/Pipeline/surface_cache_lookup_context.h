#pragma once
#include "surface_cache_shaders.h"
#include <stdexcept>

namespace Render::SurfaceCacheDetail {
// Transport-only execution variant. SurfaceCache::Update writes the same
// inverse transform, local bounds and resolution to all six cards of an
// object; the selected side's first texel and facing are already known by the
// nearest lookup. Carry that compact projection to the lighting sampler rather
// than loading eight metadata texels and transforming the hit a second time.
// Keep the original nearest search and lighting-tap loops verbatim. In
// particular, valid black, an unavailable tap, and a different depth layer
// retain their original meanings. No four-tap arrays extend register lifetimes.
inline std::string TransportLookupContextGLSL() {
    auto source=LookupGLSL(true);
    const auto replace=[&](const std::string& from,const std::string& to) {
        const auto at=source.find(from);
        if(at==std::string::npos||source.find(from,at+from.size())!=std::string::npos)
            throw std::logic_error("Transport projection context source changed");
        source.replace(at,from.size(),to);
    };
    replace(
        "bool SCLookupObjectContext(vec3 p,vec3 n,int object,out int result,out vec4 resultPosition,out vec4 resultNormal) {",
        "bool SCLookupObjectContext(vec3 p,vec3 n,int object,out int result,out vec4 resultPosition,out vec4 resultNormal,out vec4 resultProjection,out vec4 resultFacing,out int resultFirst) {\n"
        "    resultProjection=vec4(0);resultFacing=vec4(0);resultFirst=-1;");
    replace(
        "if(selected>=0){result=selected;resultPosition=selectedPosition;resultNormal=selectedNormal;return true;}",
        "if(selected>=0){result=selected;resultPosition=selectedPosition;resultNormal=selectedNormal;\n"
        "                resultProjection=vec4(at,float(resolution),max(facing.w,.00001));\n"
        "                resultFacing=facing;resultFirst=int(high.w)+side*pageSize;return true;}");
    replace(
        "vec4 position,normal;return SCLookupObjectContext(p,n,object,result,position,normal);",
        "vec4 position,normal,projection,facing;int first;\n"
        "    return SCLookupObjectContext(p,n,object,result,position,normal,projection,facing,first);");
    replace(
        "radiance=vec3(0);coverage=0;int anchor;vec4 anchorPosition,anchorNormal;\n"
        "    if(!SCLookupObjectContext(p,n,object,anchor,anchorPosition,anchorNormal))return false;",
        "radiance=vec3(0);coverage=0;int anchor,projectionFirst;vec4 anchorPosition,anchorNormal,projection,facing;\n"
        "    if(!SCLookupObjectContext(p,n,object,anchor,anchorPosition,anchorNormal,projection,facing,projectionFirst))return false;");
    replace(
        "    vec4 low=SCMeta(card,4),high=SCMeta(card,5),facing=SCMeta(card,6);\n"
        "    mat4 inverseTransform=mat4(SCMeta(card,0),SCMeta(card,1),SCMeta(card,2),SCMeta(card,3));\n"
        "    vec3 local=(inverseTransform*vec4(p,1)).xyz;\n"
        "    int axis=card%6/2,u=(axis+1)%3,v=(axis+2)%3;\n"
        "    int resolution=int(SCMeta(card,9).w);\n"
        "    vec2 extent=max(vec2(high[u]-low[u],high[v]-low[v]),vec2(1e-8));\n"
        "    vec2 at=(vec2(local[u],local[v])-vec2(low[u],low[v]))/extent*float(resolution)-.5;\n"
        "    ivec2 base=ivec2(floor(at));vec2 fraction=fract(at);\n"
        "    float tolerance=max(facing.w,.00001),weightSum=0;vec3 sum=vec3(0);",
        "    int resolution=int(projection.z);vec2 at=projection.xy;\n"
        "    ivec2 base=ivec2(floor(at));vec2 fraction=fract(at);\n"
        "    float tolerance=projection.w,weightSum=0;vec3 sum=vec3(0);");
    replace(
        "int index=int(high.w)+cell.y*resolution+cell.x;",
        "int index=projectionFirst+cell.y*resolution+cell.x;");
    return source;
}
}
