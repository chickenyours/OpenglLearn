#pragma once
#include <iostream>
#include <string>
#include <vector>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>


#define MAX_BONE_INFLUENCE 4
#define MAX_BONE_AMOUNT 100

namespace Render{
    class Material;
    class RenderPipe;
    class ShaderProgram;

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
            Mesh(std::vector<Vertex> vertices, std::vector<unsigned int> indices, unsigned int materialIndex);
            Mesh(Mesh&& other);
            ~Mesh();
            void Draw();
            void SetMaterial(Material* material);
            int GetMaterialIndex();
            void setupMesh(std::vector<Vertex> vertices, std::vector<unsigned int> indices);
            void Print(int tabs);
            inline unsigned int GetVAO() const { return VAO; }
            inline unsigned int GetVBO() const { return VBO; }
            inline unsigned int GetEBO() const { return EBO; }
            inline unsigned int GetVerticesSize() const { return verticesSize; }
            inline unsigned int GetIndicesSize() const { return indicesSize; }
            friend RenderPipe;
        private:
            unsigned int materialIndex;
            Material* _material;
            unsigned int VAO;
            unsigned int VBO, EBO;
            unsigned int verticesSize;
            unsigned int indicesSize;
    };
}
