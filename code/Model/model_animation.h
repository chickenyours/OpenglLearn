#pragma once

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
#include "code/Model/animation.h"
#include "code/Model/animator.h"
#include "code/VisualTool/visual.h"
#include "code/RenderPipe/renderPipe.h"




using namespace std;

namespace Render{

//前向声明
class Animation;
class Animator;

struct ModelNode {
    std::string name;              // 使用 std::move 传递，减少拷贝
    glm::mat4 Transformation;
    ModelNode* parent = nullptr;  // 直接存储原始指针，避免智能指针管理父节点

    int childrenCount;
    std::vector<std::unique_ptr<ModelNode>> children;  // 使用 unique_ptr 避免 push_back 触发拷贝
    std::vector<int> modelMeshes; // 直接存储 mesh 索引

    // 禁止拷贝，允许移动
    ModelNode() = default;
    ModelNode(const ModelNode&) = delete;
    ModelNode& operator=(const ModelNode&) = delete;

    ModelNode(ModelNode&& other) noexcept;

    ~ModelNode();
        
};

    class Model{
    public:
        // 构造函数，读取模型配置文件并加载模型所有属性
        Model(string const& modelConfigPath);
        // 绘制模型
        void Draw();
        // 获取登记过的骨骼信息
        const std::map<std::string,BoneInfo>& GetBoneInfoMap();
        // 获取登记过的骨骼数量
        int& GetBoneCount();

        inline bool HasAnimation(){ return _isHasAnimation; }
        float currentAnimationTime = 0.0;
        // 如果模型不支持动画(也就是_isHasAnimation为false)返回nullptr
        inline Animator* GetAnimator(){ 
            return HasAnimation()? animator.get() : nullptr; 
        }
        
        inline ModelNode* GetRootNode(){ return nodeRoot.get(); }

        inline std::string GetName(){
            if(name.empty()){
                std::cout<<"尝试获取一个没有名字的Model\n";
            }
            return name;
        }

        void Print(int tabs = 0);

        ~Model();

        // 使用marker可视化组件展示节点分布
        void VisualAddNodeAttribution(Marker* marker);

        void CommitMeshToRenderPipe(SimpleRenderPipe *renderPipe);

        // 模型持有的材质
        vector<Material> materials;

        glm::mat4 model = glm::mat4(1.0);
    private:
        // 模型的名称
        string name;
        // 模型的目录
        string directory;
        // 模型的节点信息
        std::unique_ptr<ModelNode> nodeRoot;
        // 模型持有的网格
        vector<Mesh> meshes;

        // 是否支持动画
        bool _isHasAnimation = false;
        // 模型持有的动画
        vector<Animation> animations;
        // 模型持有的动画机
        std::unique_ptr<Animator> animator;
        
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
        void ProcessNode(aiNode* node, const aiScene* scene, ModelNode* modelNode);
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

        // 加载动画
        void LoadAnimations(const aiScene* scene,Json::Value const& animationJson);

    };
    
}