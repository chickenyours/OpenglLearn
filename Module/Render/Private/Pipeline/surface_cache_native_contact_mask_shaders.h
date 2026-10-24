#pragma once
#include <stdexcept>
#include <string>
#include "surface_cache_contact_shaders.h"

namespace Render::SurfaceCacheDetail {
// The original screen-neighbor risk test needs a neighbor normal only inside
// the finite world-space radius. Delay that fetch without changing arithmetic,
// rejection thresholds, the 48-tap stencil or any accepted sample's weight.
inline std::string NativeContactMaskShader() {
    auto source=ContactMaskShader();
    const std::string from=R"GLSL(        vec4 view=texelFetch(ctPositions,at,0),nd=texelFetch(ctNormals,at,0);
        if(view.w<.5||nd.w<.5||!CTFinite(view.xyz)||!CTFinite(nd.xyz)||dot(nd.xyz,nd.xyz)<.5)continue;
        vec3 q=(spInverseView*vec4(view.xyz,1)).xyz,nn=normalize(nd.xyz),delta=q-p;
        float distance=length(delta);if(!CTFinite(q)||distance>=ctParameters.x)continue;)GLSL";
    const std::string to=R"GLSL(        vec4 view=texelFetch(ctPositions,at,0);
        if(view.w<.5||!CTFinite(view.xyz))continue;
        vec3 q=(spInverseView*vec4(view.xyz,1)).xyz,delta=q-p;
        float distance=length(delta);if(!CTFinite(q)||distance>=ctParameters.x)continue;
        vec4 nd=texelFetch(ctNormals,at,0);
        if(nd.w<.5||!CTFinite(nd.xyz)||dot(nd.xyz,nd.xyz)<.5)continue;
        vec3 nn=normalize(nd.xyz);)GLSL";
    const auto at=source.find(from);
    if(at==std::string::npos||source.find(from,at+from.size())!=std::string::npos)
        throw std::logic_error("Native screen contact mask source changed");
    source.replace(at,from.size(),to);
    return source;
}
} // namespace Render::SurfaceCacheDetail
