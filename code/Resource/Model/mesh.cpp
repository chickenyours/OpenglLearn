#include "mesh.h"
#include "code\ToolAndAlgorithm\ModelLoad\assimp_glm_helper.h"
using namespace Resource;

Mesh::Mesh(Mesh&& other) noexcept {
    VAO = other.VAO; other.VAO = 0;
    VBO = other.VBO; other.VBO = 0;
    EBO = other.EBO; other.EBO = 0;
    vertices_ = std::move(other.vertices_);
    indices_ = std::move(other.indices_);
    fromNodeName_ = std::move(other.fromNodeName_);
    materialIndex_ = other.materialIndex_;
}

Mesh& Mesh::operator=(Mesh&& other) noexcept {
    if (this != &other) {
        // 清理旧资源
        glDeleteVertexArrays(1, &VAO);
        glDeleteBuffers(1, &VBO);
        glDeleteBuffers(1, &EBO);

        VAO = other.VAO; other.VAO = 0;
        VBO = other.VBO; other.VBO = 0;
        EBO = other.EBO; other.EBO = 0;
        vertices_ = std::move(other.vertices_);
        indices_ = std::move(other.indices_);
        fromNodeName_ = std::move(other.fromNodeName_);
        materialIndex_ = other.materialIndex_;
    }
    return *this;
}

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
        return false;
    }
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices_.size() * sizeof(unsigned int), &indices_[0], GL_STATIC_DRAW);
    if (GLenum err = glGetError(); err != GL_NO_ERROR) {
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
        LOG_ERROR("Mesh","Failed to generate VAO");
        return false;
    }
    if (!glIsBuffer(VBO)) {
        LOG_ERROR("Mesh","Failed to generate VBO");
        return false;
    }
    if (!glIsBuffer(EBO)) {
        LOG_ERROR("Mesh","Failed to generate EBO");
        return false;
    }

    // LOG_INFO("Mesh", "Successfully set up mesh. Vertices = " + std::to_string(vertices_.size()) + ", Indices = " + std::to_string(indices_.size()));
    return true;
}

Mesh::Mesh(std::vector<Vertex>&& movedVertices, std::vector<unsigned int>&& movedIndices){
    SetUpMesh(std::move(movedVertices), std::move(movedIndices));
}

Mesh::Mesh(const aiMesh& mesh){
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;
    // 只处理3角面
    vertices.reserve(mesh.mNumVertices);
    indices.reserve(mesh.mNumFaces * 3);
    
    if(mesh.mMaterialIndex == 0){
        LOG_ERROR("Mesh", "mMaterialIndex == 0");
    }
    materialIndex_ = mesh.mMaterialIndex - 1;

    const bool hasTexCoords = mesh.HasTextureCoords(0);
    if(!hasTexCoords){
        LOG_WARNING("Mesh", "Mesh does not have texture coordinates.");
    }
    const bool hasNormals = mesh.HasNormals();
    if(!hasNormals){
        LOG_WARNING("Mesh", "Mesh does not have normals.");
    }
    const bool hasTangents = mesh.HasTangentsAndBitangents();
    if(!hasNormals){
        LOG_WARNING("Mesh", "Mesh does not have tangents and bitangents.");
    }
    for(unsigned int i = 0; i < mesh.mNumVertices; i++){
        glm::vec3 pos = AssimpGLMHelpers::GetGLMVec(mesh.mVertices[i]);
        glm::vec3 normal = hasNormals ? AssimpGLMHelpers::GetGLMVec(mesh.mNormals[i]) : glm::vec3(0.0f);
        glm::vec3 tangent = hasTangents ? AssimpGLMHelpers::GetGLMVec(mesh.mTangents[i]) : glm::vec3(0.0f);
        glm::vec2 uv = hasTexCoords ? glm::vec2(mesh.mTextureCoords[0][i].x, mesh.mTextureCoords[0][i].y) : glm::vec2(0.0f);
        vertices.emplace_back(pos, uv, normal, tangent);
    }

    for (unsigned int i = 0; i < mesh.mNumFaces; i++)
    {
        const aiFace& face = mesh.mFaces[i];
        indices.insert(indices.end(), face.mIndices, face.mIndices + face.mNumIndices);
    }

    SetUpMesh(std::move(vertices),std::move(indices));
}

Mesh::~Mesh(){
    if (VAO) glDeleteVertexArrays(1, &VAO);
    if (VBO) glDeleteBuffers(1, &VBO);
    if (EBO) glDeleteBuffers(1, &EBO);
}