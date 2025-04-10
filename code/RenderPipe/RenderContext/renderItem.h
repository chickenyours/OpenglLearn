#pragma once



namespace Render{
    class Mesh;
    class Material;
    struct RenderItem{
        Mesh* mesh;
        Material* material;
        glm::mat4 model;
        uint8_t passMask;
    }; 
}