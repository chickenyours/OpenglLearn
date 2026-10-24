#pragma once
#include <stdexcept>
#include <string>
#include "surface_cache_spatial_shaders.h"
#include "surface_cache_native_screen_probe_shaders.h"

namespace Render::SurfaceCacheDetail {
// Keep the original probe-pair geometry, safe endpoint and stencil decisions.
// Only the visibility segment's traversal changes. The same original-triangle
// predicate used by native probe and receiver rays validates expanded AS hits.
// An invalid/ambiguous result cannot certify visibility; no software BVH walk
// or screen-space substitute is present in this variant.
inline std::string NativeProbeSpatialNeighborsShader(bool diagnostics=false) {
    auto source=SpatialNeighborsShader();
    const auto replace=[&](const std::string& from,const std::string& to) {
        const auto at=source.find(from);
        if(at==std::string::npos||source.find(from,at+from.size())!=std::string::npos)
            throw std::logic_error("Native probe neighbor visibility source changed");
        source.replace(at,from.size(),to);
    };
    replace(GeometryGLSL(),NativeScreenProbeIntersectionGLSL(diagnostics));
    replace("bool blocked=SCIntersect(a,delta/distance,maximum,unused,triangle,exhausted,true);",
            "bool blocked=SCNativeIntersect(a,delta/distance,maximum,unused,triangle,exhausted,true);");
    if(diagnostics) {
        // xy and the local-candidate count in z retain the original table ABI.
        // w is otherwise unused. Count actual rayQueryInitializeEXT calls even
        // when a segment is rejected or requires a second native traversal.
        replace("void main() {","void NSNativeNeighborMain() {");
        source+=R"GLSL(
void main() {
    scTraceDiag[0]=uvec4(0);scTraceDiag[1]=uvec4(0);
    NSNativeNeighborMain();outColor.w=float(scTraceDiag[0].x);
}
)GLSL";
    }
    return source;
}
} // namespace Render::SurfaceCacheDetail
