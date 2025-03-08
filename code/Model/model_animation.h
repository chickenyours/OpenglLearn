#ifndef MODEL_H
#define MODEL_H

#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include <map>
#include <vector>

#include <glad/glad.h> 
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <stb_image.h>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include "mesh.h"
#include "code/shader.h"
#include "assimp_glm_helper.h"
#include "animdata.h"
#include "code/Material/material.h"
#include "code/Texture/texture.h"




using namespace std;

namespace Render{
    class Model{
    public:
        // 构造函数，读取模型配置文件并加载模型所有属性
        Model(string const& modelConfigPath);
        // 绘制模型
        void Draw();
        // 获取登记过的骨骼信息
        std::map<std::string,BoneInfo>& GetBoneInfoMap();
        // 获取登记过的骨骼数量
        int& GetBoneCount();
    private:
        // 模型的目录
        string directory;
        // 模型持有的网格
        vector<Mesh> meshes;
        // 模型持有的材质
        vector<Material> materials;
        
        // 模型登记的骨骼
        int m_BoneCounter = 0;
        std::map<string, BoneInfo> m_BoneInfoMap;
        // 模型配置Json参数对象,根据这个配置加载模型的所有属性
        Json::Value _modelConfigJson;
        // 使用路径加载Json配置文件
        bool LoadModelJson(const string& path);

        // 模型加载函数，加载模型需要的网格
        void LoadModel(const aiScene* scene);
        // 递归处理节点函数
        void ProcessNode(aiNode* node, const aiScene* scene);
        // 处理网格函数(由ProcessNode调用,加载该节点涉及到的网格),它会为网格加载顶点(包括在内的骨骼权重数据数据)数据,分配材质,索引数据
        Mesh ProcessMesh(aiMesh* mesh, const aiScene* scene);
        // 骨骼权重初始化函数
        void SetVertexBoneDataToDefault(Vertex& vertex);
        // 设置顶点的骨骼数据(由ExtractBoneWeightForVertices调用)
        void SetVertexBoneData(Vertex& vertex, int boneID, float weight);
        // 提取网格的骨骼权重,并设置到顶点上
        void ExtractBoneWeightForVertices(std::vector<Vertex>& vertices, aiMesh* mesh, const aiScene* scene);
        
        // 配置材质(从模型文件和配置文件中加载)
        void ConfigureMaterials(const aiScene* scene,Json::Value const& materialJson);
        // 加载材质(这个过程包含加载纹理(前提是材质信息已经配置好的),当然是调用Material::LoadTextureFromMaterial)
        void LoadAllMaterials();
        // 为mesh设置材质
        void SetMeshesMaterial();

    };
    
}

#endif // MODEL_H
