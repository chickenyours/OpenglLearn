#pragma once
#include <cstdint>
#include <memory>
#include <vector>
#include <glm/glm.hpp>
namespace Render {
// Immutable encoded color cube: x=R, y=G, z=B, x varies fastest. Values
// remain in their authored ACEScct domain; no sRGB decoding during upload.
struct DisplayColorCube {std::uint32_t edge=0;std::vector<glm::vec4> texels;};
struct SourceDisplaySettings {
    bool enabled=false;
    // ThreeSection pixel-shader coefficient groups. Study callers explicitly
    // supply the reconstructed coefficients and their own scene exposure.
    glm::vec4 curve0{1,.22f,.4f,1},curve1{1,.532f,.468f,-3.0826817113012f},curve2{1.63998667041224f,4.5454545f,0,0};
    std::shared_ptr<const DisplayColorCube> cube;
    float displayGamma=1,convertToLimit=1,lowerLimit=0;
};
}
