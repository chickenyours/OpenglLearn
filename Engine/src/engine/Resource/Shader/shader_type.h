#pragma once

namespace Resource{

enum class ShaderProgramType{
        VERTEX_FRAGMENT,
        VERTEX_GEOMETRY_FRAGMENT,
        COMPUTE,
        UNKNOWN,
    };

enum class ShaderStage {
    Vertex, Fragment, Geometry, Compute // 甚至可以扩展
};

} // namespace Resource