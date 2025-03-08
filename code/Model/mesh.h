#ifndef MESH_H
#define MESH_H

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "code/shader.h"
#include "code/Material/material.h"

#include <string>
#include <vector>

using namespace std;

#define MAX_BONE_INFLUENCE 4

namespace Render{
    struct Vertex {
        glm::vec3 Position;
        glm::vec3 Normal;
        glm::vec2 TexCoords;
        glm::vec3 Tangent;
        glm::vec3 Bitangent;
        int m_BoneIDs[MAX_BONE_INFLUENCE];
        float m_Weights[MAX_BONE_INFLUENCE];
    };
    
    class Mesh {
    public:
        Mesh(vector<Vertex> vertices, vector<unsigned int> indices, unsigned int materialIndex);
        void Draw();
        void SetMaterial(Material* material);
        int GetMaterialIndex();
    private:
        unsigned int materialIndex;
        Material* _material;
        unsigned int VAO;
        unsigned int indicesSize;
        void setupMesh(vector<Vertex> vertices, vector<unsigned int> indices);
    };
}


#endif // MESH_H
