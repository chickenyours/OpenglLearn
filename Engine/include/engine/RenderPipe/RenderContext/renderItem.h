#pragma once
#include <cstdint>

namespace Render{
    class Mesh;
    class Material;
    struct RenderItem{
        Mesh* mesh;
        Material* material;
        glm::mat4 model;
        uint64_t passMask;
    }; 
}