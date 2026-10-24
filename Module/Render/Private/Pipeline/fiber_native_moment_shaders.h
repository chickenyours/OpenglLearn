#pragma once
#include <string>

namespace Render::PipelineDetail {
// Atlas representation of the native 64(theta) x64(width) x16(absorption)
// Texture3D. Each stored value has already passed through RGBA16F storage.
// Fetch all eight neighbors explicitly: ordinary 2D filtering would bleed
// across absorption-slice boundaries and cannot filter the width axis in 3D.
inline std::string FiberNativeMomentGLSL() { return R"GLSL(
#ifdef FIBER_NATIVE_MOMENT_LUT
layout(binding=9) uniform sampler2D strandNativeMomentSampler;
vec4 FiberNativeMomentTexel(ivec3 index) {
    return texelFetch(strandNativeMomentSampler,ivec2(index.x,index.z*64+index.y),0);
}
vec4 FiberNativeMoments(float theta,float roughness,float absorption) {
    vec3 coordinate=vec3(theta,clamp(roughness,.18,.6),absorption);
    vec3 p=clamp(coordinate*vec3(64.0,64.0,16.0)-.5,vec3(0.0),vec3(63.0,63.0,15.0));
    ivec3 lower=ivec3(floor(p)),upper=min(lower+ivec3(1),ivec3(63,63,15));
    vec3 f=fract(p);
    vec4 z0=mix(mix(FiberNativeMomentTexel(lower),
                        FiberNativeMomentTexel(ivec3(upper.x,lower.y,lower.z)),f.x),
                    mix(FiberNativeMomentTexel(ivec3(lower.x,upper.y,lower.z)),
                        FiberNativeMomentTexel(ivec3(upper.x,upper.y,lower.z)),f.x),f.y);
    vec4 z1=mix(mix(FiberNativeMomentTexel(ivec3(lower.x,lower.y,upper.z)),
                        FiberNativeMomentTexel(ivec3(upper.x,lower.y,upper.z)),f.x),
                    mix(FiberNativeMomentTexel(ivec3(lower.x,upper.y,upper.z)),
                        FiberNativeMomentTexel(upper),f.x),f.y);
    return mix(z0,z1,f.z);
}
#endif
)GLSL"; }
} // namespace Render::PipelineDetail
