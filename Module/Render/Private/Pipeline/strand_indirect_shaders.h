#pragma once
#include <string>
namespace Render::PipelineDetail {
// StrandIndirectLightCS evaluates a six-coefficient ambient cube without a
// cosine/pi convolution. Coefficients arrive after source probe decompression;
// position-dependent probe selection is outside this constant-input adapter.
inline std::string StrandIndirectGLSL() { return R"GLSL(
layout(std140,binding=7) uniform StrandIndirect {
    vec4 strandIndirectCoefficients[6]; // +X,-X,+Y,-Y,+Z,-Z
    vec4 strandIndirectOptions; // enabled,producer gain * host EV,cos Y,sin Y
};
vec3 StrandIndirectDirection(vec3 direction) {
    float norm=dot(direction,direction);
    vec3 d=norm>1e-12?direction*inversesqrt(norm):vec3(0.0,0.0,1.0);
    vec3 n=vec3(strandIndirectOptions.z*d.x+strandIndirectOptions.w*d.z,d.y,
        -strandIndirectOptions.w*d.x+strandIndirectOptions.z*d.z);
    vec3 value=vec3(0.0);
    for(int axis=0;axis<3;++axis)
        value+=strandIndirectCoefficients[axis*2+(n[axis]>0.0?0:1)].rgb*n[axis]*n[axis];
    return value*strandIndirectOptions.y;
}
vec3 StrandIndirectRing(vec3 tangent) {
    float norm=dot(tangent,tangent);
    vec3 t=norm>1e-12?tangent*inversesqrt(norm):vec3(0.0,1.0,0.0);
    t=vec3(strandIndirectOptions.z*t.x+strandIndirectOptions.w*t.z,t.y,
        -strandIndirectOptions.w*t.x+strandIndirectOptions.z*t.z);
    // For normals uniformly distributed around the cylinder, E[n_i^2] is
    // (1-T_i^2)/2. Each sign occupies half that ring. This host legacy adapter
    // uses the same cube input without replacing it by SH or a view normal.
    vec3 value=vec3(0.0);
    for(int axis=0;axis<3;++axis)
        value+=(strandIndirectCoefficients[axis*2].rgb+strandIndirectCoefficients[axis*2+1].rgb)*
            (.25*(1.0-t[axis]*t[axis]));
    return value*strandIndirectOptions.y;
}
)GLSL"; }
} // namespace Render::PipelineDetail
