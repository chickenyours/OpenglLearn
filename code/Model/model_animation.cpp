#include "model_animation.h"

#include "assimp_glm_helper.h"
#include "animdata.h"
#include "code/shader.h"
#include "code/Model/mesh.h"
#include "code/Material/material.h"
#include "code/Texture/texture.h"
#include "code/Model/animation.h"
#include "code/Model/animator.h"
#include "code/VisualTool/visual.h"
#include "code/RenderPipe/simpleRenderPipe.h"
#include "code/RenderPipe/RenderContext/renderItem.h"

using namespace Render;

ModelNode::~ModelNode(){
    if(name != "") std::cout<<"ModelNode: "<<name<<"销毁"<<std::endl;
}

ModelNode::ModelNode(ModelNode&& other) noexcept {
    std::cout<<"ModelNode: "<<other.name<<"发生移动"<<std::endl;
    this->name = std::move(other.name);
    this->Transformation = other.Transformation;
    this->parent = other.parent;
    this->childrenCount = other.childrenCount;
    this->children = std::move(other.children);
    this->modelMeshes = std::move(other.modelMeshes);
    other.name = "";
}

Model::Model(std::string const &materialConfigPath) 
{
    // 加载配置文件
    if (!LoadModelJson(materialConfigPath))
    {
        std::cout << "Failed to load model config file: " << materialConfigPath << std::endl;
        return;
    }
    // 获取模型文件地址
    std::string modelFile = _modelConfigJson["model"].asString();

    // 加载模型文件
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(modelFile,
        aiProcess_Triangulate |
        aiProcess_GenSmoothNormals |
        aiProcess_CalcTangentSpace |
        aiProcess_FlipUVs 
        // aiProcess_JoinIdenticalVertices |   // ✅ 避免骨骼重复数据
        // aiProcess_PopulateArmatureData |   // ✅ 解析骨骼层级
        // aiProcess_LimitBoneWeights |       // ✅ 限制骨骼影响数
        // aiProcess_GlobalScale              // ✅ 处理 FBX 缩放问题
    );
    if(!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
    {
        std::cout << "ERROR::ASSIMP:: " << importer.GetErrorString() << std::endl;
        return;
    }

    name = modelFile.substr(modelFile.find_last_of('/') + 1);
    // 获取模型目录
    directory = modelFile.substr(0, modelFile.find_last_of('/'));

    // 初始化节点
    nodeRoot = std::make_unique<ModelNode>();

    // 读取模型文件并加载模型
    LoadModel(scene);

    // 获取材质Json对象
    Json::Value materialJson = _modelConfigJson["materials"];
    // 为模型加载材质
    ConfigureMaterials(scene,materialJson);
    // 加载所有的材质
    LoadAllMaterials();
    // 分配材质到Mesh
    SetMeshesMaterial();

    // 模型是否有动画
    if(scene->HasAnimations()){
        //加载动画
        Json::Value animationJson = _modelConfigJson["animations"];
        LoadAnimations(scene,animationJson);
        //加载动画机
        animator = std::make_unique<Animator>(&animations[0]);
        _isHasAnimation = true;
    }
}

void Model::Draw()
{
    //处理动画,传递数据到uniform mat4 finalBonesMatrices[i]
    if(_isHasAnimation){
        auto&& animationMat = animator->GetFinalBoneMatrices();
        for(int i = 0;i<materials.size();i++){
            for(int j = 0;j<animationMat.size();j++){
                materials[i].mat4ParameterMap["finalBonesMatrices[" + std::to_string(j) + "]"] = animationMat[j];
            }
        }
    }

    //处理每个材质动画
   

    for(unsigned int i = 0; i < meshes.size(); i++)
        meshes[i].Draw();
}

const std::map<std::string,BoneInfo>& Model::GetBoneInfoMap() { return m_BoneInfoMap; }
int& Model::GetBoneCount() { return m_BoneCounter; }

void Model::LoadModel(const aiScene* scene)
{
    ProcessNode(scene->mRootNode, scene, nodeRoot.get());
}

void Model::ProcessNode(aiNode* node, const aiScene* scene, ModelNode* modelNode)
{
    // 获取节点信息
    modelNode->name = node->mName.C_Str();
    modelNode->Transformation = AssimpGLMHelpers::ConvertMatrixToGLMFormat(node->mTransformation);
    modelNode->childrenCount = node->mNumChildren;

    for(unsigned int i = 0; i < node->mNumMeshes; i++)
    {
        modelNode->modelMeshes.push_back(meshes.size());
        aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
        meshes.push_back(ProcessMesh(mesh, scene));
    }

    for(unsigned int i = 0; i < node->mNumChildren; i++)
    {
        aiNode* child = node->mChildren[i];
        auto modelChildNode = std::make_unique<ModelNode>();
        modelChildNode->parent = modelNode;
        ModelNode* rawPtr = modelChildNode.get(); // 获取裸指针,注意std::move(modelChildNode)后modelChildNode变空指针无法获取数据,如果让其递归传递则会崩溃,需要一个裸指针保存地址
        modelNode->children.push_back(std::move(modelChildNode));
        ProcessNode(child, scene, rawPtr);
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
    std::vector<Vertex> vertices;
    std::vector<unsigned int> indices;

    for (unsigned int i = 0; i < mesh->mNumVertices; i++)
    {
        Vertex vertex;
        SetVertexBoneDataToDefault(vertex);
        vertex.Position = AssimpGLMHelpers::GetGLMVec(mesh->mVertices[i]);
        vertex.Normal = AssimpGLMHelpers::GetGLMVec(mesh->mNormals[i]);
        vertex.Tangent = AssimpGLMHelpers::GetGLMVec(mesh->mTangents[i]);

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
    static int maxMAX_BONE_INFLUENCE = 0;
    int i;
    for (i = 0 ; i < MAX_BONE_INFLUENCE; ++i)
    {
        if (vertex.m_BoneIDs[i] < 0)
        {
            vertex.m_Weights[i] = weight;
            vertex.m_BoneIDs[i] = boneID;
            break;
        }
    }
    if(i + 1 > maxMAX_BONE_INFLUENCE){
        std::cout<<"Model: "<< name << "统计出某个顶点最大骨骼影响数" << i + 1 << std::endl;
        maxMAX_BONE_INFLUENCE = i + 1;
        if(maxMAX_BONE_INFLUENCE > MAX_BONE_INFLUENCE){
            std::cout<<"Model: "<< name << "某个顶点最大骨骼影响数超过了MAX_BONE_INFLUENCE" << std::endl;
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
            
            // glm::mat4 correction = glm::rotate(glm::mat4(1.0f), glm::radians(-90.0f), glm::vec3(1, 0, 0));
            // newBoneInfo.offset = correction * AssimpGLMHelpers::ConvertMatrixToGLMFormat(mesh->mBones[boneIndex]->mOffsetMatrix);

            // AssimpGLMHelpers::PrintGLMMatrix(newBoneInfo.offset);
            // float determinant = glm::determinant(newBoneInfo.offset);
            // std::cout << "Determinant: " << determinant << std::endl;

            m_BoneInfoMap[boneName] = newBoneInfo;
            boneID = m_BoneCounter;
            m_BoneCounter++;
            //std::cout<<"记入一个Bone: " << boneName << std::endl;
        }
        else
        {
            //如果有重复骨骼则选择第一个
            boneID = m_BoneInfoMap[boneName].id;
        }

        assert(boneID != -1);
        auto weights = mesh->mBones[boneIndex]->mWeights;
        int numWeights = mesh->mBones[boneIndex]->mNumWeights;

        for (int weightIndex = 0; weightIndex < numWeights; ++weightIndex)
        {
            int vertexId = weights[weightIndex].mVertexId;
            float weight = weights[weightIndex].mWeight;
            assert(vertexId < vertices.size());
            SetVertexBoneData(vertices[vertexId], boneID, weight);
        }
    }
}

bool Model::LoadModelJson(const std::string& path){
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
    in.close(); // 显式关闭文件

    _modelConfigJson = std::move(root);
    return true;
}

void Model::ConfigureMaterials(const aiScene* scene,const Json::Value& materialJson)
{
    //配对材质
    std::vector<int> match(scene->mNumMaterials,-1);
    std::unordered_map<std::string, int> sceneMaterialMap;
    for(int i = 0;i < scene->mNumMaterials; i++){
        std::string materialName = scene->mMaterials[i]->GetName().C_Str();
        sceneMaterialMap[materialName] = i;
    }
    try{
        for(int i = 0;i< materialJson.size(); i++){
            std::string materialName = materialJson[i]["name"].asString();
            auto it = sceneMaterialMap.find(materialName);
            if(it != sceneMaterialMap.end()){
                match[it->second] = i;
            }
        }
    }
    catch (const std::exception& e) {  // 捕获异常
        std::cout << "There are errors when ConfigureMaterials by JsonValue:"<<std::endl;
        std::cout << materialJson <<std::endl;
        std::cout << "Exception caught: " << e.what() << std::endl;
    }
    
    for(int i = 0;i<match.size();i++){
        const Json::Value& josnValue = match[i] != -1 ? materialJson[match[i]] : Json::Value();
        materials.emplace_back(*(scene->mMaterials[i]), josnValue);
    }
}

// animationJson layout:
//
// [
//     {
//         "name" : "Armature|Chilling on Ground 0-55",
//         "filePath" : "models/mouse/W_hlmaus.fbx"
//     },
//     {
//         "name" : "Armature|Chilling on Ground 0-55",
//         "filePath" : "models/mouse/W_hlmaus.fbx"
//     }, ...
// ]
//////////////////////////
void Model::LoadAnimations(const aiScene* scene,Json::Value const& animationJson){
    // 配对与分组算法
    // 
    // 1.scene.mAnimations 名字和 animationJson[i]["name"] 相等则配对,储存到
    // (这个算法暂时不涉及,目前不考虑一个模型使用不同模型文件中的动画,假设动画数据来自scene)

    // 扩充数组
    animations.reserve(scene->mNumAnimations);
    for(int i = 0; i< scene->mNumAnimations;i++){
        animations.emplace_back(scene->mAnimations[i],this);
    }
}

void Model::CommitMeshToRenderPipe(RenderPipe *renderPipe){
    // for(auto& mesh : meshes){
    //     renderPipe->Addmesh(&mesh);
    // }
    for(auto& mesh : meshes){
        RenderItem renderItem;
        renderItem.mesh = &mesh;
        renderItem.material = &(this->materials[mesh.GetMaterialIndex()]);
        renderItem.model = this->model;
        renderPipe->Push(renderItem);
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

void Model::Print(int tabs){
    std::string tab = "";
    for(int i = 0; i< tabs; i++){
        tab += "\t";
    }
    std::cout<<tab<<"======ModelInfo======"<<std::endl;
    std::cout<<tab<<"Name: "<<name<<std::endl;
    std::cout<<tab<<"Directory: "<<directory<<std::endl;
    std::cout<<tab<<"meshes Count: "<<meshes.size()<<std::endl;
    for(int i = 0; i< meshes.size();i++){
        meshes[i].Print(tabs + 1);
    }
    
    std::cout<<tab<<"materials Count: "<<materials.size()<<std::endl;
    for(int i = 0;i < materials.size();i++){
        materials[i].Print(tabs + 1);
    }
    
    std::cout<< tab <<"Mesh Bone Count: "<<m_BoneCounter<<std::endl;
    std::cout<< tab <<"MeshBones: "<<m_BoneCounter<<'\n';
    for(const auto& [key, _] : m_BoneInfoMap){
        std::cout << tab << '\t' << key << '\n';
    }

    std::cout<< tab << "Is Has Animation: " << (int)_isHasAnimation;

    if(_isHasAnimation){
        // 调用动画打印
        for(int i =0;i< animations.size();i++){
            animations[i].Print(tabs + 1);
        }
    
        // 调用动画机打印
        animator->Print(tabs + 1);
    }

    std::cout<<tab<<"======EndModelInfo======="<<std::endl;
}

Model::~Model(){
    std::cout<< "destroy Model: "<< name <<std::endl;
}

void Model::VisualAddNodeAttribution(Marker* marker){
    // 递归函数
    std::function<void(ModelNode*, glm::vec4, glm::vec3, bool)> myiterators =
    [&](ModelNode* node, glm::vec4 pos, glm::vec3 color, bool flag) {
        for (const auto& it : node->children) {
            glm::vec4 newPos4 = it->Transformation * pos;
            if(newPos4.w != 1.0){
                std::cout<< "非齐次变换:" << newPos4.w << std::endl;
            }
            glm::vec3 newPos = glm::vec3(newPos4);  // 确保正确转换
            glm::vec3 newColor = flag ? glm::vec3(0.0, 1.0, 0.0) : glm::vec3(0.0, 0.0, 1.0);
            if(glm::distance(newPos4,pos) < 0.02){
                std::cout<< "出现节点距离较小的情况:" << std::endl;
                newColor = glm::vec3(1.0);
                color = glm::vec3(1.0);
            }
            marker->AddPoint({glm::vec3(pos)}, {color});
            marker->AddLine({pos}, {color}, {newPos}, {newColor});
            myiterators(it.get(), newPos4, newColor, !flag);
        }
    };

    myiterators(nodeRoot.get(),glm::vec4(0.0,0.0,0.0,1.0),glm::vec3(1.0,0.0,0.0),true);

}