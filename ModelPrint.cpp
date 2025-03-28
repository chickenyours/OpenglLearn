#include <iostream>
#include <string>
#include <vector>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/version.h>


using namespace std;

void PrintAssimpVersion(){
    cout<<"==================AssimpVersion=================="<<endl;
    cout<<"Assimp Version: "<<aiGetVersionMajor()<<"."<<aiGetVersionMinor()<<"."<<aiGetVersionPatch()<<endl;
    cout<<"Assimp Revision: "<<aiGetVersionRevision()<<endl;
    cout<<"Assimp Branch: "<<aiGetBranchName()<<endl;
    cout<<"Assimp Legal: "<<aiGetLegalString()<<endl;
    cout<<endl;
}

void _PrintNodeDistribution(aiNode* node,string path){
    string name = node->mName.C_Str();
    path = path + "/" + name;
    for(int i = 0;i<node->mNumChildren;i++){
        _PrintNodeDistribution(node->mChildren[i],path);
    }
    cout<<path<<endl;
}

void PrintNodeDistribution(const aiScene* scene){
    cout<<"==================NodeDistribution=================="<<endl;
    _PrintNodeDistribution(scene->mRootNode,"");
    cout<<endl;
}

void PrintSceneInfo(const aiScene* scene){
    cout<<"==================SceneInfo=================="<<endl;
    cout<<"Scene Name"<<scene->mRootNode->mName.C_Str()<<endl;
    cout<<"Scene NumAnimations: "<<scene->mNumAnimations<<endl;
    cout<<"Scene NumCameras: "<<scene->mNumCameras<<endl;
    cout<<"Scene NumLights: "<<scene->mNumLights<<endl;
    cout<<"Scene NumMaterials: "<<scene->mNumMaterials<<endl;
    cout<<"Scene NumMeshes: "<<scene->mNumMeshes<<endl;
    cout<<"Scene NumTextures: "<<scene->mNumTextures<<endl;
    cout<<"Scene Metadata: "<<scene->mMetaData<<endl;
    cout<<"Scene Skeletons"<<scene->mNumSkeletons << endl;
    cout<<endl;
}

void PrintNodeInfo(const aiScene* scene,string nodePath){
    aiNode* node = scene->mRootNode->FindNode(nodePath.c_str());
    if(!node){
        cout<<"Node Not Found"<<endl;
        return;
    }
    cout<<"==================NodeInfo=================="<<endl;
    cout<<"Node Name: "<<node->mName.C_Str()<<endl;
    cout<<"Node Transformation: "<<endl;
    cout<<node->mTransformation.a1<<" "<<node->mTransformation.a2<<" "<<node->mTransformation.a3<<" "<<node->mTransformation.a4<<endl;
    cout<<node->mTransformation.b1<<" "<<node->mTransformation.b2<<" "<<node->mTransformation.b3<<" "<<node->mTransformation.b4<<endl;
    cout<<node->mTransformation.c1<<" "<<node->mTransformation.c2<<" "<<node->mTransformation.c3<<" "<<node->mTransformation.c4<<endl;
    cout<<node->mTransformation.d1<<" "<<node->mTransformation.d2<<" "<<node->mTransformation.d3<<" "<<node->mTransformation.d4<<endl;
    cout<<"Node Parent: "<<node->mParent->mName.C_Str()<<endl;
    cout<<"Node NumChildren: "<<node->mNumChildren<<endl;
    cout<<"Node NumMeshes: "<<node->mNumMeshes<<endl;
    cout<<"Node Meshes: ";
    for(int i = 0;i<node->mNumMeshes;i++){
        cout<<node->mMeshes[i]<<" ";
    }
    cout<<endl;
    cout<<"Node Metadata: "<<node->mMetaData<<endl;
    cout<<endl;
}

void PrintMeshInfo(const aiMesh* mesh){
    cout<<"==================MeshInfo=================="<<endl;
    cout<<"Mesh Name: "<<mesh->mName.C_Str()<<endl;
    cout<<"Mesh HasPositions: "<<mesh->HasPositions()<<endl;
    cout<<"Mesh HasFaces: "<<mesh->HasFaces()<<endl;
    cout<<"Mesh HasNormals: "<<mesh->HasNormals()<<endl;
    cout<<"Mesh HasBones: "<<mesh->HasBones()<<endl;
    cout<<"Mesh NumVertices: "<<mesh->mNumVertices<<endl;
    cout<<"Mesh NumFaces: "<<mesh->mNumFaces<<endl;
    int numBone = mesh->mNumBones;
    cout<<"Mesh NumBones: "<<numBone<<endl;
    cout<<endl;
    for(int i = 0;i<numBone;i++){
        aiBone* bone = mesh->mBones[i];
        cout<<"    Bone Name: "<<bone->mName.C_Str()<<endl;
        cout<<"    Bone NumWeights: "<<bone->mNumWeights<<endl;
        cout<<"    Bone OffsetMatrix: "<<endl;
        cout<<"    "<<bone->mOffsetMatrix.a1<<" "<<bone->mOffsetMatrix.a2<<" "<<bone->mOffsetMatrix.a3<<" "<<bone->mOffsetMatrix.a4<<endl;
        cout<<"    "<<bone->mOffsetMatrix.b1<<" "<<bone->mOffsetMatrix.b2<<" "<<bone->mOffsetMatrix.b3<<" "<<bone->mOffsetMatrix.b4<<endl;
        cout<<"    "<<bone->mOffsetMatrix.c1<<" "<<bone->mOffsetMatrix.c2<<" "<<bone->mOffsetMatrix.c3<<" "<<bone->mOffsetMatrix.c4<<endl;
        cout<<"    "<<bone->mOffsetMatrix.d1<<" "<<bone->mOffsetMatrix.d2<<" "<<bone->mOffsetMatrix.d3<<" "<<bone->mOffsetMatrix.d4<<endl;
        cout<<endl;
    }
    cout<<"Mesh MaterialIndex: "<<mesh->mMaterialIndex<<endl;
    cout<<"Mesh HasTangentsAndBitangents: "<<mesh->HasTangentsAndBitangents()<<endl;
    cout<<"Mesh HasTextureCoords: "<<mesh->HasTextureCoords(0)<<endl;
    cout<<"Mesh HasVertexColors: "<<mesh->HasVertexColors(0)<<endl;
    cout<<endl;
}

void PrintTextureInfo(const aiTexture* texture){
    cout<<"==================TextureInfo=================="<<endl;
    cout<<"Texture File Name: "<<texture->mFilename.C_Str()<<endl;
    cout<<"Texture Width: "<<texture->mWidth<<endl;
    cout<<"Texture Height: "<<texture->mHeight<<endl;
    cout<<"Texture FormatHint: "<<texture->achFormatHint<<endl;
    cout<<"Texture Data: "<<texture->pcData<<endl;
    cout<<endl;
}

void PrintAnimationInfo(const aiAnimation* animation){
    cout<<"==================AnimationInfo=================="<<endl;
    cout<<"Animation Name: "<<animation->mName.C_Str()<<endl;
    cout<<"Animation Duration: "<<animation->mDuration<<endl;
    cout<<"Animation TicksPerSecond: "<<animation->mTicksPerSecond<<endl;
    cout<<"Animation NumChannels: "<<animation->mNumChannels<<endl;
    cout<<"Animation NumMeshChannels: "<<animation->mNumMeshChannels<<endl;
    cout<<"Animation NumMorphMeshChannels: "<<animation->mNumMorphMeshChannels<<endl;
    cout<<endl;
}

void PrintMaterialInfo(aiMaterial* material){
    static const string textureTypeInfo[] = {
        "aiTextureType_NONE",
        "aiTextureType_DIFFUSE",
        "aiTextureType_SPECULAR",
        "aiTextureType_AMBIENT",
        "aiTextureType_EMISSIVE",
        "aiTextureType_HEIGHT",
        "aiTextureType_NORMALS",
        "aiTextureType_SHININESS",
        "aiTextureType_OPACITY",
        "aiTextureType_DISPLACEMENT",
        "aiTextureType_LIGHTMAP",
        "aiTextureType_REFLECTION",
        "aiTextureType_BASE_COLOR",
        "aiTextureType_NORMAL_CAMERA",
        "aiTextureType_EMISSION_COLOR",
        "aiTextureType_METALNESS",
        "aiTextureType_DIFFUSE_ROUGHNESS",
        "aiTextureType_AMBIENT_OCCLUSION",
        "aiTextureType_UNKNOWN",
        "aiTextureType_SHEEN",
        "aiTextureType_CLEARCOAT",
        "aiTextureType_TRANSMISSION",
        "aiTextureType_MAYA_BASE",
        "aiTextureType_MAYA_SPECULAR",
        "aiTextureType_MAYA_SPECULAR_COLOR",
        "aiTextureType_MAYA_SPECULAR_ROUGHNESS"
        };
    cout<<"==================MaterialInfo=================="<<endl;
    cout<<"Material Name: "<<material->GetName().C_Str()<<endl;
    int c = 0;
    for(int i = aiTextureType::aiTextureType_NONE;i<aiTextureType::aiTextureType_MAYA_SPECULAR_ROUGHNESS;i++){
        int num = material->GetTextureCount((aiTextureType)i);
        if(num!=0){
            for(int j = 0;j<num;j++){
                aiString path;
                aiTextureMapping mapping;
                unsigned int uvindex;
                ai_real blend;
                aiTextureOp op;
                aiTextureMapMode mapmode;
                if(material->GetTexture((aiTextureType)i,j,&path,&mapping,&uvindex,&blend,&op,&mapmode)==aiReturn::aiReturn_SUCCESS){
                    cout<<endl;
                    cout<<"Texture Type: "<<textureTypeInfo[i]<<endl;
                    cout<<"Texture Path: "<<path.C_Str()<<endl;
                    cout<<"Texture Mapping: "<<mapping<<endl;
                    cout<<"Texture UVIndex: "<<uvindex<<endl;
                    cout<<"Texture Blend: "<<blend<<endl;
                    cout<<"Texture Op: "<<op<<endl;
                    cout<<"Texture MapMode: "<<mapmode<<endl;
                    cout<<endl;
                    c++;
                }
            }
        }
    

    }

    cout<<"Material Texture Count "<<c<<endl;
    cout<<"Material NumProperties: "<<material->mNumProperties<<endl;
    cout<<"Material NumAllocated: "<<material->mNumAllocated<<endl;

    //材质属性
    // Ambient (环境光)
    aiColor4D ambientColor;
    if (material->Get(AI_MATKEY_COLOR_AMBIENT, ambientColor) == AI_SUCCESS) {
        cout<<"Ambient: "<<ambientColor.r<<" "<<ambientColor.g<<" "<<ambientColor.b<<" "<<ambientColor.a<<endl;
    }

    // Diffuse (漫反射)
    aiColor4D diffuseColor;
    if (material->Get(AI_MATKEY_COLOR_DIFFUSE, diffuseColor) == AI_SUCCESS) {
        cout<<"Diffuse: "<<diffuseColor.r<<" "<<diffuseColor.g<<" "<<diffuseColor.b<<" "<<diffuseColor.a<<endl;
    }

    // Specular (镜面反射)
    aiColor4D specularColor;
    if (material->Get(AI_MATKEY_COLOR_SPECULAR, specularColor) == AI_SUCCESS) {
        cout<<"Specular: "<<specularColor.r<<" "<<specularColor.g<<" "<<specularColor.b<<" "<<specularColor.a<<endl;
    }

    // Emissive (自发光)
    aiColor4D emissiveColor;
    if (material->Get(AI_MATKEY_COLOR_EMISSIVE, emissiveColor) == AI_SUCCESS) {
        cout<<"Emissive: "<<emissiveColor.r<<" "<<emissiveColor.g<<" "<<emissiveColor.b<<" "<<emissiveColor.a<<endl;
    }

    // Shininess (光泽度)
    float shininess = 0.0f;
    if (material->Get(AI_MATKEY_SHININESS, shininess) == AI_SUCCESS) {
        cout<<"Shininess: "<<shininess<<endl;
    }

    // ni(折射率)
    float ni = 0.0f;
    if (material->Get(AI_MATKEY_REFRACTI, ni) == AI_SUCCESS) {
        cout<<"ni: "<<ni<<endl;
    }

    // 透明度
    float opacity = 1.0f;
    if (material->Get(AI_MATKEY_OPACITY, opacity) == AI_SUCCESS) {
        cout<<"opacity: "<<opacity<<endl;
    }

    // Transparency
    float transparency = 1.0f;
    if (material->Get(AI_MATKEY_COLOR_TRANSPARENT, transparency) == AI_SUCCESS) {
        cout<<"transparency: "<<transparency<<endl;
    }

    // 光照模型
    int shadingModel = 0;
    if (material->Get(AI_MATKEY_SHADING_MODEL, shadingModel) == AI_SUCCESS) {
        cout<<"shadingModel: "<<shadingModel<<endl;
    }

    //PBR属性
    // Metalness (金属度)
    float metalness = 0.0f;
    if (material->Get(AI_MATKEY_METALLIC_FACTOR, metalness) == AI_SUCCESS) {
        cout<<"Metalness: "<<metalness<<endl;
    }


    // Roughness
    float roughness = 0.0f;
    if (material->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness) == AI_SUCCESS) {
        cout<<"Roughness: "<<roughness<<endl;
    }
    cout<<endl;
}

void PrintMeshListFromScene(const aiScene* scene){
    cout<<"==================MeshList=================="<<endl;
    for(int i = 0;i<scene->mNumMeshes;i++){
        aiMesh* mesh = scene->mMeshes[i];
        PrintMeshInfo(mesh);
    }
    cout<<endl;
}

void PrintTextureListFromScene(const aiScene* scene){
    cout<<"==================TextureList=================="<<endl;
    for(int i = 0;i<scene->mNumTextures;i++){
        aiTexture* texture = scene->mTextures[i];
        PrintTextureInfo(texture);
    }
    cout<<endl;
}

void PrintMaterialListFromScene(const aiScene* scene){
    cout<<"==================MaterialList=================="<<endl;
    for(int i = 0;i<scene->mNumMaterials;i++){
        aiMaterial* material = scene->mMaterials[i];
        cout<<"Material Index"<<i<<endl;
        PrintMaterialInfo(material);
    }
    cout<<endl;
}

void PrintAnimationListFromScene(const aiScene* scene){
    cout<<"==================AnimationList=================="<<endl;
    for(int i = 0;i<scene->mNumAnimations;i++){
        aiAnimation* animation = scene->mAnimations[i];
        PrintAnimationInfo(animation);
    }
    cout<<endl;
}

int main() {

    //输出版本
    PrintAssimpVersion();
    
    //导入器
    Assimp::Importer importer;
    //导入模型
    //bin/models/Oil_barrel/Oil_barrel.obj
    //bin/models/mouse/W_hlmaus.fbx
    string modelPath;
    cout<<"Please Input Model Path: ";
    cin>>modelPath;
    const aiScene* scene = importer.ReadFile(modelPath, aiProcess_Triangulate | aiProcess_FlipUVs);
    
    //检查是否导入成功
    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        std::cout << "ERROR::ASSIMP::" << importer.GetErrorString() << std::endl;
        return -1;
    }

    //输出Scene信息
    PrintSceneInfo(scene);

    //输出Scene中的Mesh信息
    PrintMeshListFromScene(scene);

    //输出Scene中的Material信息
    PrintMaterialListFromScene(scene);

    //输出Scene中的Animation信息
    PrintAnimationListFromScene(scene);

    //输出Scene中的Node分布
    PrintNodeDistribution(scene);

    //输出Node信息
    PrintNodeInfo(scene,"Body");

    system("pause");
    return 0;
}
