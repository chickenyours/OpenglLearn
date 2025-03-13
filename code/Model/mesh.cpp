#include "mesh.h"
#include <iostream>

using namespace std;
using namespace Render;

Mesh::Mesh(vector<Vertex> vertices, vector<unsigned int> indices, unsigned int materialIndex):VAO(0)
{
    setupMesh(vertices,indices);
    this->materialIndex = materialIndex;
}

/*
    * @brief 绘制网格
    * @warning 只会对纹理通道和着色器配置，并不会改变其他上下文比如混合模式，viewport等
    * @param shaderProgram 着色器程序
*/
void Mesh::Draw()
{
    // 模型绑定
    glBindVertexArray(VAO);
    // 材质设置
    _material->SetShaderParams();
    // 索引绘制
    glDrawElements(GL_TRIANGLES, static_cast<unsigned int>(indicesSize), GL_UNSIGNED_INT, 0);
}

/*
    * @brief 初始化网格
    * @param vertices 顶点数据
*/
void Mesh::setupMesh(vector<Vertex> vertices, vector<unsigned int> indices)
{
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    glGenBuffers(1, &EBO);
    glBindVertexArray(VAO);
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), &vertices[0], GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), &indices[0], GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, TexCoords));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Normal));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Tangent));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Bitangent));
    glEnableVertexAttribArray(5);
    glVertexAttribIPointer(5, 4, GL_INT, sizeof(Vertex), (void*)offsetof(Vertex, m_BoneIDs));
    glEnableVertexAttribArray(6);
    glVertexAttribPointer(6, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, m_Weights));
    glBindVertexArray(0);
    verticesSize = vertices.size();
    indicesSize = indices.size();
}

void Mesh::SetMaterial(Material* material)
{
    this->_material = material;
}

int Mesh::GetMaterialIndex()
{
    return materialIndex;
}

void Mesh::Print(){
    cout << "Mesh material index: " << materialIndex << endl;
    cout << "Mesh VAO: " << VAO << endl;
    cout << "Mesh indices size: " << indicesSize << endl;
}

Mesh::~Mesh()
{
    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    glDeleteBuffers(1, &EBO);
}