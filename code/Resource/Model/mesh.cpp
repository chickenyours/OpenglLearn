#include "mesh.h"
#include "code\ToolAndAlgorithm\ModelLoad\assimp_glm_helper.h"
using namespace Resource;

bool Mesh::SetUpMesh(std::vector<Vertex>&& movedVertices, std::vector<unsigned int>&& movedIndices){

    vertices_ = std::move(movedVertices);
    indices_ = std::move(movedIndices);
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);
    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, vertices_.size() * sizeof(Vertex), &vertices_[0], GL_STATIC_DRAW);
    if (GLenum err = glGetError(); err != GL_NO_ERROR) {
        // REPORT_STACK_ERROR(errHandle, "Mesh", "glBufferData VBO failed: GL error code " + std::to_string(err));
        return false;
    }
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices_.size() * sizeof(unsigned int), &indices_[0], GL_STATIC_DRAW);
    if (GLenum err = glGetError(); err != GL_NO_ERROR) {
        // REPORT_STACK_ERROR(errHandle, "Mesh", "glBufferData EBO failed: GL error code " + std::to_string(err));
        return false;
    }

     // 配置顶点属性
    auto safeAttrib = [&](GLuint index, GLint size, GLenum type, GLboolean normalized, size_t offset, bool integer = false) {
        if (integer)
            glVertexAttribIPointer(index, size, type, sizeof(Vertex), (void*)offset);
        else
            glVertexAttribPointer(index, size, type, normalized, sizeof(Vertex), (void*)offset);
        glEnableVertexAttribArray(index);
    };

    safeAttrib(0, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, Position));
    safeAttrib(1, 2, GL_FLOAT, GL_FALSE, offsetof(Vertex, TexCoords));
    safeAttrib(2, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, Normal));
    safeAttrib(3, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, Tangent));
    safeAttrib(4, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, Bitangent));
    // safeAttrib(5, 4, GL_INT,   GL_FALSE, offsetof(Vertex, m_BoneIDs), true);   // 整型
    // safeAttrib(6, 4, GL_FLOAT, GL_FALSE, offsetof(Vertex, m_Weights));

    glBindVertexArray(0);

    
     // ✅ 检查创建是否成功
    if (!glIsVertexArray(VAO)) {
        // REPORT_STACK_ERROR(errHandle, "Mesh", "Failed to generate VAO");
        LOG_ERROR("Mesh","Failed to generate VAO");
        return false;
    }
    if (!glIsBuffer(VBO)) {
        LOG_ERROR("Mesh","Failed to generate VBO");
        // REPORT_STACK_ERROR(errHandle, "Mesh", "Failed to generate VBO");
        return false;
    }
    if (!glIsBuffer(EBO)) {
        LOG_ERROR("Mesh","Failed to generate EBO");
        // REPORT_STACK_ERROR(errHandle, "Mesh", "Failed to generate EBO");
        return false;
    }

    // LOG_INFO("Mesh", "Successfully set up mesh. Vertices = " + std::to_string(vertices_.size()) + ", Indices = " + std::to_string(indices_.size()));
    return true;
}

Mesh::Mesh(std::vector<Vertex>&& movedVertices, std::vector<unsigned int>&& movedIndices){
    SetUpMesh(std::move(movedVertices), std::move(movedIndices));
}

Mesh::Mesh(aiMesh& mesh){
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
    // 只处理3角面
    vertices.reserve(mesh.mNumVertices);
    indices.reserve(mesh.mNumFaces * 3);
    const bool hasTexCoords = mesh.HasTextureCoords(0);
    
    for(unsigned int i = 0; i < mesh.mNumVertices; i++){
    vertices.emplace_back(
        AssimpGLMHelpers::GetGLMVec(mesh.mVertices[i]),
            hasTexCoords 
                ? glm::vec2(mesh.mTextureCoords[0][i].x, mesh.mTextureCoords[0][i].y)
                : glm::vec2(0.0f, 0.0f),
                AssimpGLMHelpers::GetGLMVec(mesh.mNormals[i]),
                AssimpGLMHelpers::GetGLMVec(mesh.mTangents[i])
    );
    }

    for (unsigned int i = 0; i < mesh.mNumFaces; i++)
    {
        const aiFace& face = mesh.mFaces[i];
        indices.insert(indices.end(), face.mIndices, face.mIndices + face.mNumIndices);
    }

    SetUpMesh(std::move(vertices),std::move(indices));
}

Mesh::~Mesh(){
    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteBuffers(1, &EBO);
}