#include "model.h"

#include <stack>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include "code/ToolAndAlgorithm/Json/json_config_identification.h"
#include "code/ToolAndAlgorithm/ModelLoad/assimp_glm_helper.h"

#include "code/Resource/Model/mesh.h"

#include "code/ToolAndAlgorithm/Performance/time.h"

using namespace Resource;

std::vector<Mesh> Model::LoadMeshes(const aiScene& scene){
    std::vector<Mesh> result;

    std::stack<aiNode*> nodes;
    nodes.push(scene.mRootNode);
    auto LoadMesh = [&](){
        aiNode* node = nodes.top();
        nodes.pop();
        for(unsigned int i = 0; i < node->mNumMeshes; i++){
            aiMesh* mesh = scene.mMeshes[node->mMeshes[i]];
            result.emplace_back(*mesh);
            auto& newMesh = result.back();
            newMesh.SetMaterialIndex(mesh->mMaterialIndex);
            newMesh.fromNodeName_ = node->mName.C_Str();
        }
        for(unsigned int i = 0; i < node->mNumChildren; i++){
            nodes.push(node->mChildren[i]);
        }
    };

    while(!nodes.empty()){
        LoadMesh();
    }
    
    return result;
}



bool Model::LoadFromConfigFile(const std::string& configFile, Log::StackLogErrorHandle errHandle){

    Json::Value config;
    if(!Tool::JsonHelper::LoadJsonValueFromFile(configFile, config, errHandle)){
        REPORT_STACK_ERROR(errHandle, "Mesh->LoadFromConfigFile", "failed to load config file: " + configFile);
        return false;
    }

    Json::Value* resource;
    if(!Tool::TryToExtractResourceObject(config, resource,errHandle)){
        REPORT_STACK_ERROR(errHandle,"Mesh->LoadFromConfigFile", "file to load configFile");
        return false;
    }

    std::string resourceType;
    if(!Tool::JsonHelper::TryGetString(*resource, "resourceType", resourceType)){
        REPORT_STACK_ERROR(errHandle, "Mesh->LoadFromConfigFile", "missing or invalid 'resourceType' in configFile");
        return false;
    }

    if(resourceType != "staticModel"){
        REPORT_STACK_ERROR(errHandle, "Mesh->LoadFromConfigFile", "'resourceType' is not 'mesh' in configFile");
        return false;
    }

    const Json::Value* model;
    if(!Tool::JsonHelper::TryGetObject(*resource, "model", model)){
        REPORT_STACK_ERROR(errHandle, "Mesh->LoadFromConfigFile", "missing or invalid 'mesh' object in configFile");
        return false;
    }

    std::string modelFilePath;
    if(!Tool::JsonHelper::TryGetString(*model, "modelFilePath", modelFilePath)){
        REPORT_STACK_ERROR(errHandle, "Mesh->LoadFromConfigFile", "missing or invalid 'modelFilePath' in mesh object");
        return false;
    }

    Assimp::Importer importer;
    const aiScene* scene = MEASURETIMEMS("Assimp loading", 
       importer.ReadFile(modelFilePath,
            aiProcess_Triangulate |
            aiProcess_GenSmoothNormals |
            aiProcess_CalcTangentSpace |
            aiProcess_FlipUVs
    ));
    

    if(!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
    {
        REPORT_STACK_ERROR(errHandle, "Mesh->LoadFromConfigFile", std::string("Assimp error: ") + importer.GetErrorString());
        return false;
    }

    meshes_ = MEASURETIMEMS("LoadMeshes",LoadMeshes(*scene));

    return true;


}

std::string Model::GetInfo(){
    std::stringstream result;
    for(const auto& mesh : meshes_){
        result << "Meshes Size: " << meshes_.size() << '\n';
            for(int i = 0; i < meshes_.size(); i++){
                result << '\t' << "MeshIndex " << i << ":" 
                    << "\t\t" << "FromNode: " << mesh.fromNodeName_ << '\n'
                    << "\t\t" << "VAO: " << mesh.GetVAO() << '\n'
                    << "\t\t" << "VertexSize:" << mesh.vertices_.size() << '\n'
                    << "\t\t" << "Indices" << mesh.indices_.size() << '\n';
            }
    }
    return result.str();
}

void Model::Release(){
    // 释放所有Mesh资源
    LOG_INFO("Model", "Released all meshes.");
} 