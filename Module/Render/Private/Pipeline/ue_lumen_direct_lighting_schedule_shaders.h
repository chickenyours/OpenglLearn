#pragma once
#include <stdexcept>
#include <string>
#include "surface_cache_shaders.h"

namespace Render::SurfaceCacheDetail {
// Project atlas adapter for UE's selected-card/page raster consumer. A logical
// card is stored as a flattened contiguous interval, not as a square R x R page.
// The fragment wrapper clips the oversized fullscreen triangle to its instance
// rectangle before the unchanged lighting main can query or write anything.
inline std::string UeLumenDirectLightingTileVertexShader(){return R"GLSL(#version 450 core
layout(location=0) in vec2 position;layout(location=1) in vec2 uv;
layout(std140,binding=15) uniform UeDirectLightingRects {
    ivec4 udlAtlas;ivec4 udlRects[256];
};
out vec2 vUV;layout(location=2) flat out ivec4 udlRect;
void main(){
    udlRect=udlRects[gl_InstanceID];vUV=uv;
    vec2 pixel=vec2(udlRect.xy)+(position*.5+.5)*vec2(udlRect.zw);
    gl_Position=vec4(pixel/vec2(udlAtlas.xy)*2.0-1.0,0,1);
}
)GLSL";}

inline std::string UeLumenDirectLightingTileFragmentShader(bool nativeVisibility,bool diagnostics=false){
    auto source=LightingShader(nativeVisibility,diagnostics,true);
    const std::string anchor="void main(){";
    const auto at=source.find(anchor);
    if(at==std::string::npos)throw std::logic_error("Direct lighting main source changed");
    source.replace(at,anchor.size(),"void UdlLightingMain(){");
    source+=R"GLSL(
layout(location=2) flat in ivec4 udlRect;
void main(){
    ivec2 pixel=ivec2(gl_FragCoord.xy);
    if(any(lessThan(pixel,udlRect.xy))||any(greaterThanEqual(pixel,udlRect.xy+udlRect.zw)))discard;
    UdlLightingMain();
}
)GLSL";
    return source;
}
} // namespace Render::SurfaceCacheDetail
