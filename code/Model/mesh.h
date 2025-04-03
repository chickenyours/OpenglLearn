#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "code/shader.h"
#include "code/Material/material.h"

#include <string>
#include <vector>

#include "code/RenderPipe/renderPipe.h"

using namespace std;

#define MAX_BONE_INFLUENCE 4
#define MAX_BONE_AMOUNT 100

namespace Render{
    class SimpleRenderPipe;

    struct Vertex {
        glm::vec3 Position;
        glm::vec2 TexCoords;
        glm::vec3 Normal;
        glm::vec3 Tangent;
        glm::vec3 Bitangent;
        int m_BoneIDs[MAX_BONE_INFLUENCE];
        float m_Weights[MAX_BONE_INFLUENCE];
    };
    
    class Mesh {
        public:
            Mesh(vector<Vertex> vertices, vector<unsigned int> indices, unsigned int materialIndex);
            Mesh(Mesh&& other);
            ~Mesh();
            void Draw();
            void SetMaterial(Material* material);
            int GetMaterialIndex();
            void setupMesh(vector<Vertex> vertices, vector<unsigned int> indices);
            void Print(int tabs);
            friend SimpleRenderPipe;
        private:
            unsigned int materialIndex;
            Material* _material;
            unsigned int VAO;
            unsigned int VBO, EBO;
            unsigned int verticesSize;
            unsigned int indicesSize;
    };
}
