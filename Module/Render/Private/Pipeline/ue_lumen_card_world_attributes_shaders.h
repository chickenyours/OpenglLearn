#pragma once
#include <stdexcept>
#include <string>
#include "surface_cache_shaders.h"

namespace Render::SurfaceCacheDetail {
// The project keeps captures in mesh-local space to avoid CPU recapture and
// upload on movement. Amortize their unchanged SCAttribute conversion over the
// cache, before primary direct's many receiver/card/tap lookups. UE's card
// lighting and SurfaceCache sampling likewise consume decoded world geometry;
// this pass adapts the project's local attribute carrier, not its ray geometry.
inline std::string UeLumenCardWorldAttributesShader() {
    return std::string("#version 450 core\n")+LookupGLSL()+R"GLSL(
in vec2 vUV;layout(location=0) out vec4 outColor;
void main(){
    int index=int(gl_FragCoord.y)*scAtlas.x+int(gl_FragCoord.x);
    // The target is exactly width x (2*PlaneTexels/width). Padding intentionally
    // follows the original accessor too: preserve its raw value and alpha.
    outColor=SCAttribute(index);
}
)GLSL";
}

inline std::string UeLumenCardWorldAttributeLookupGLSL(std::string source) {
    const std::string signature="vec4 SCAttribute(int index) {";
    const auto first=source.find(signature);
    if(first==std::string::npos)throw std::logic_error("Direct world attribute source anchor missing");
    const std::string renamed="vec4 UECardOriginalAttribute(int index) {";
    source.replace(first,signature.size(),renamed);
    auto end=source.find('{',first);unsigned depth=1;
    while(++end<source.size()&&depth){if(source[end]=='{')++depth;else if(source[end]=='}')--depth;}
    if(depth)throw std::logic_error("Direct world attribute source body incomplete");
    source.insert(end,R"GLSL(
vec4 SCAttribute(int index) {
    if((int(scParameters.w+.5)&256)!=0&&scAtlas.x>0&&scAtlas.y>0&&
        index>=0&&index<2*scAtlas.y&&scAtlas.y%scAtlas.x==0) {
        ivec2 size=textureSize(ueCardWorldAttributes,0);
        if(size.x==scAtlas.x&&size.y==2*(scAtlas.y/scAtlas.x))
            return texelFetch(ueCardWorldAttributes,ivec2(index%scAtlas.x,index/scAtlas.x),0);
    }
    return UECardOriginalAttribute(index);
}
)GLSL");
    source.insert(first,"layout(binding=7) uniform sampler2D ueCardWorldAttributes;\n");
    return source;
}
} // namespace Render::SurfaceCacheDetail
