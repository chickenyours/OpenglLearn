#include <iostream>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

void PrintMaterialTextures(aiMaterial* material) {
    aiString texturePath;
    
    // 检查 Diffuse 纹理
    for (unsigned int t = 0; t < material->GetTextureCount(aiTextureType_DIFFUSE); ++t) {
        if (material->GetTexture(aiTextureType_DIFFUSE, t, &texturePath) == AI_SUCCESS) {
            std::cout << "  Diffuse texture found: " << texturePath.C_Str() << std::endl;
        }
    }

    // 检查 Specular 纹理
    for (unsigned int t = 0; t < material->GetTextureCount(aiTextureType_SPECULAR); ++t) {
        if (material->GetTexture(aiTextureType_SPECULAR, t, &texturePath) == AI_SUCCESS) {
            std::cout << "  Specular texture found: " << texturePath.C_Str() << std::endl;
        }
    }

    // 检查 Normal 纹理
    for (unsigned int t = 0; t < material->GetTextureCount(aiTextureType_NORMALS); ++t) {
        if (material->GetTexture(aiTextureType_NORMALS, t, &texturePath) == AI_SUCCESS) {
            std::cout << "  Normal texture found: " << texturePath.C_Str() << std::endl;
        }
    }

    // 检查 Ambient Occlusion (AO) 纹理
    for (unsigned int t = 0; t < material->GetTextureCount(aiTextureType_AMBIENT_OCCLUSION); ++t) {
        if (material->GetTexture(aiTextureType_AMBIENT_OCCLUSION, t, &texturePath) == AI_SUCCESS) {
            std::cout << "  AO texture found: " << texturePath.C_Str() << std::endl;
        }
    }

    // 检查 Specular Power (Shininess) 纹理
    for (unsigned int t = 0; t < material->GetTextureCount(aiTextureType_SHININESS); ++t) {
        if (material->GetTexture(aiTextureType_SHININESS, t, &texturePath) == AI_SUCCESS) {
            std::cout << "  Shininess texture found: " << texturePath.C_Str() << std::endl;
        }
    }

    // 检查其他纹理
    for (unsigned int t = 0; t < material->GetTextureCount(aiTextureType_UNKNOWN); ++t) {
        if (material->GetTexture(aiTextureType_UNKNOWN, t, &texturePath) == AI_SUCCESS) {
            std::cout << "  Unknown texture found: " << texturePath.C_Str() << std::endl;
        }
    }
}

void PrintMeshInfo(aiMesh* mesh, aiMaterial* material) {
    std::cout << "  Mesh: " << mesh->mName.C_Str() << std::endl;
    std::cout << "    Number of vertices: " << mesh->mNumVertices << std::endl;
    std::cout << "    Number of faces: " << mesh->mNumFaces << std::endl;

    // 打印材质的纹理信息
    PrintMaterialTextures(material);
}

void ProcessNode(const aiNode* node, const aiScene* scene) {
    // 处理当前节点的网格
    for (unsigned int i = 0; i < node->mNumMeshes; ++i) {
        aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
        aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];
        PrintMeshInfo(mesh, material);
    }

    // 递归处理子节点
    for (unsigned int i = 0; i < node->mNumChildren; ++i) {
        ProcessNode(node->mChildren[i], scene);
    }
}

int main() {
    // 文件路径
    std::string filePath = "models/hintze-hall-vr-tour/source/hintze-hall_UV_pack01.fbx"; 

    // Assimp 导入器
    Assimp::Importer importer;

    // 读取 FBX 文件
    const aiScene* scene = importer.ReadFile(filePath, aiProcess_Triangulate | aiProcess_FlipUVs);

    if (!scene) {
        std::cerr << "Error loading model: " << importer.GetErrorString() << std::endl;
        return -1;
    }

    std::cout << "Successfully loaded model: " << filePath << std::endl;

    // 打印根节点的模型信息
    ProcessNode(scene->mRootNode, scene);

    return 0;
}
