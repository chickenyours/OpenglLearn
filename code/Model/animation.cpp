#include "Animation.h"
#include <cassert>

using namespace Render;

Animation::Animation(const std::string& animationPath, Model* model) {
    //初始化导入器
    Assimp::Importer importer;
    //用导入器加载模型
    const aiScene* scene = importer.ReadFile(animationPath, aiProcess_Triangulate);
    //如果加载失败,则输出错误信息
    assert(scene && scene->mRootNode);
    //使用API获取动画数据
    auto animation = scene->mAnimations[0];         // Assume only one animation per model
    m_Duration = animation->mDuration;              // Duration of the animation in ticks
    m_TicksPerSecond = animation->mTicksPerSecond;  // Ticks per second
    //获取全局变换矩阵
    aiMatrix4x4 globalTransformation = scene->mRootNode->mTransformation;
    //将全局变换矩阵转换为
    globalTransformation = globalTransformation.Inverse();
    //根据模型节点分布创建骨骼节点分布
    ReadHierarchyData(m_RootNode, scene->mRootNode);
    //读取缺失的骨骼,构造m_BoneInfoMap,并将骨骼数据存入m_Bones
    ReadMissingBones(animation, *model);
}

Bone* Animation::FindBone(const std::string& name) {
    auto iter = std::find_if(m_Bones.begin(), m_Bones.end(),
        [&](const Bone& bone) { return bone.GetBoneName() == name; });

    return (iter != m_Bones.end()) ? &(*iter) : nullptr;
}

float Animation::GetTicksPerSecond() const {
    return m_TicksPerSecond;
}

float Animation::GetDuration() const {
    return m_Duration;
}

const AssimpNodeData& Animation::GetRootNode() const {
    return m_RootNode;
}

const std::map<std::string, BoneInfo>& Animation::GetBoneIDMap() const {
    return m_BoneInfoMap;
}


void Animation::ReadMissingBones(const aiAnimation* animation, Model& model) {
    int size = animation->mNumChannels;

    auto& boneInfoMap = model.GetBoneInfoMap(); // Get model's BoneInfo map
    int& boneCount = model.GetBoneCount();      // Get model's bone count

    for (int i = 0; i < size; i++) {
        auto channel = animation->mChannels[i];
        std::string boneName = channel->mNodeName.data;

        if (boneInfoMap.find(boneName) == boneInfoMap.end()) {
            boneInfoMap[boneName].id = boneCount;
            boneCount++;
        }

        m_Bones.emplace_back(Bone(boneName, boneInfoMap[boneName].id, channel));
    }

    m_BoneInfoMap = boneInfoMap;
}

//递归函数,用于读取模型的骨骼节点分布
void Animation::ReadHierarchyData(AssimpNodeData& dest, const aiNode* src) {
    assert(src);
    //将节点的名称和变换矩阵赋值给目标节点
    dest.name = src->mName.data;
    dest.transformation = AssimpGLMHelpers::ConvertMatrixToGLMFormat(src->mTransformation);
    dest.childrenCount = src->mNumChildren;
    //递归读取子节点
    for (int i = 0; i < src->mNumChildren; i++) {
        AssimpNodeData newData;
        ReadHierarchyData(newData, src->mChildren[i]);
        dest.children.push_back(newData);
    }
}
