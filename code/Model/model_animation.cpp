#include "model_animation.h"

using namespace Render;


Model::Model(string const &materialConfigPath) 
{
    // 加载配置文件
    if (!LoadModelJson(materialConfigPath))
    {
        cout << "Failed to load model config file: " << materialConfigPath << endl;
        return;
    }
    // 获取模型文件地址
    string path = _modelConfigJson["path"].asString();
    string modelFile = _modelConfigJson["model"].asString();

    // 加载模型文件
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(path + modelFile, aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_CalcTangentSpace);
    if(!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
    {
        cout << "ERROR::ASSIMP:: " << importer.GetErrorString() << endl;
        return;
    }
    directory = path.substr(0, path.find_last_of('/'));

    // 读取模型文件并加载模型
    LoadModel(scene);

    // 获取材质Json对象
    Json::Value materialJson = _modelConfigJson["materials"];
    // 为模型加载材质
    ConfigureMaterials(scene,materialJson);
    // 加载所有的材质
    LoadAllMaterials();
}

void Model::Draw()
{
    for(unsigned int i = 0; i < meshes.size(); i++)
        meshes[i].Draw();
}

std::map<std::string,BoneInfo>& Model::GetBoneInfoMap() { return m_BoneInfoMap; }
int& Model::GetBoneCount() { return m_BoneCounter; }

void Model::LoadModel(const aiScene* scene)
{
    ProcessNode(scene->mRootNode, scene);
}

void Model::ProcessNode(aiNode *node, const aiScene *scene)
{
    for(unsigned int i = 0; i < node->mNumMeshes; i++)
    {
        aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
        meshes.push_back(ProcessMesh(mesh, scene));
    }

    for(unsigned int i = 0; i < node->mNumChildren; i++)
    {
        ProcessNode(node->mChildren[i], scene);
    }
}

void Model::SetVertexBoneDataToDefault(Vertex& vertex)
{
    for (int i = 0; i < MAX_BONE_INFLUENCE; i++)
    {
        vertex.m_BoneIDs[i] = -1;
        vertex.m_Weights[i] = 0.0f;
    }
}

Mesh Model::ProcessMesh(aiMesh* mesh, const aiScene* scene)
{
    vector<Vertex> vertices;
    vector<unsigned int> indices;
    vector<Texture> textures;

    for (unsigned int i = 0; i < mesh->mNumVertices; i++)
    {
        Vertex vertex;
        SetVertexBoneDataToDefault(vertex);
        vertex.Position = AssimpGLMHelpers::GetGLMVec(mesh->mVertices[i]);
        vertex.Normal = AssimpGLMHelpers::GetGLMVec(mesh->mNormals[i]);

        if (mesh->mTextureCoords[0])
        {
            vertex.TexCoords = { mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y };
        }
        else
        {
            vertex.TexCoords = glm::vec2(0.0f, 0.0f);
        }

        vertices.push_back(vertex);
    }

    for (unsigned int i = 0; i < mesh->mNumFaces; i++)
    {
        aiFace face = mesh->mFaces[i];
        for (unsigned int j = 0; j < face.mNumIndices; j++)
            indices.push_back(face.mIndices[j]);
    }
    
    ExtractBoneWeightForVertices(vertices, mesh, scene);

    return Mesh(vertices, indices, mesh->mMaterialIndex);
}

void Model::SetVertexBoneData(Vertex& vertex, int boneID, float weight)
{
    for (int i = 0; i < MAX_BONE_INFLUENCE; ++i)
    {
        if (vertex.m_BoneIDs[i] < 0)
        {
            vertex.m_Weights[i] = weight;
            vertex.m_BoneIDs[i] = boneID;
            break;
        }
    }
}

void Model::ExtractBoneWeightForVertices(std::vector<Vertex>& vertices, aiMesh* mesh, const aiScene* scene)
{
    for (int boneIndex = 0; boneIndex < mesh->mNumBones; ++boneIndex)
    {
        int boneID = -1;
        std::string boneName = mesh->mBones[boneIndex]->mName.C_Str();
        if (m_BoneInfoMap.find(boneName) == m_BoneInfoMap.end())
        {
            BoneInfo newBoneInfo;
            newBoneInfo.id = m_BoneCounter;
            newBoneInfo.offset = AssimpGLMHelpers::ConvertMatrixToGLMFormat(mesh->mBones[boneIndex]->mOffsetMatrix);
            m_BoneInfoMap[boneName] = newBoneInfo;
            boneID = m_BoneCounter++;
        }
        else
        {
            boneID = m_BoneInfoMap[boneName].id;
        }

        assert(boneID != -1);
        auto weights = mesh->mBones[boneIndex]->mWeights;
        int numWeights = mesh->mBones[boneIndex]->mNumWeights;

        for (int weightIndex = 0; weightIndex < numWeights; ++weightIndex)
        {
            int vertexId = weights[weightIndex].mVertexId;
            float weight = weights[weightIndex].mWeight;
            assert(vertexId <= vertices.size());
            SetVertexBoneData(vertices[vertexId], boneID, weight);
        }
    }
}

bool Model::LoadModelJson(const string& path){
    Json::Value root;
    Json::CharReaderBuilder builder;
    std::ifstream in(path, std::ios::binary);
    
    if (!in.is_open()) {
        // 文件未成功打开
        return false;
    }

    std::string errs;
    if (!Json::parseFromStream(builder, in, &root, &errs)) {
        // 解析失败
        return false;
    }

    _modelConfigJson = std::move(root);
    return true;
}

void Model::ConfigureMaterials(const aiScene* scene,const Json::Value& materialJson)
{

    for (unsigned int i = 0; i < scene->mNumMaterials; i++)
    {
        aiMaterial* material = scene->mMaterials[i];
        Material newMaterial;
        newMaterial.LoadParameterFromModelAiMaterial(*material);
        // 在配置文件中找到同名材质
        unsigned int j;
        for (j = 0; j < materialJson.size(); j++)
        {
            if (materialJson[j]["name"].asString() == newMaterial.name)
            {
                newMaterial.LoadParameterFromConfigFile(materialJson[j]);
                break;
            }
        }
        if(j == materialJson.size())
        {
            cout << "Material " << newMaterial.name << " not found in config file" << endl;
        }

        materials.push_back(newMaterial);
    }
}

void Model::LoadAllMaterials(){
    for(int i = 0;i < materials.size();i++){
        materials[i].LoadAllTexture();
    }
}

void Model::SetMeshesMaterial(){
    for(int i = 0;i < meshes.size();i++){
        meshes[i].SetMaterial(&materials[meshes[i].GetMaterialIndex()]);
    }
}
