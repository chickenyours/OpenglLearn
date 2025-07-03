#pragma once

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <glad/glad.h>
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "code/ECS/Core/Resource/resource_interface.h"

#define MAX_BONE_INFLUENCE 4


struct Vertex {
    glm::vec3 Position;
    glm::vec2 TexCoords;
    glm::vec3 Normal;
    glm::vec3 Tangent;
    glm::vec3 Bitangent;
    // int m_BoneIDs[MAX_BONE_INFLUENCE];
    // float m_Weights[MAX_BONE_INFLUENCE];
};

namespace Resource {
    class Mesh { 
        public:
            Mesh() = default;
            Mesh(aiMesh& mesh);
            Mesh(std::vector<Vertex>&& movedVertices, std::vector<unsigned int>&& movedIndices);
            bool SetUpMesh(std::vector<Vertex>&& movedVertices, std::vector<unsigned int>&& movedIndices);
            ~Mesh();
            GLuint GetVAO() const {return VAO;}
            GLuint GetVBO() const {return VBO;}
            GLuint GetEBO() const {return EBO;}
            void SetMaterialIndex(size_t materialIndex){
                materialIndex_ = materialIndex;
            }
            size_t GetMaterialIndex(){return materialIndex_;}
        private:
            std::string fromNodeName_;
            GLuint VAO = 0u;
            GLuint VBO = 0u;
            GLuint EBO = 0u;
            size_t materialIndex_ = 0llu;
            std::vector<Vertex> vertices_;
            std::vector<unsigned int> indices_;


            friend class Model;
    };
}