#include "animation.h"
#include <cassert>

#include "bone.h"
#include "animdata.h"
#include "code/Model/model_animation.h"


using namespace Render;

Animation::Animation(const aiAnimation* animation, Model* model){

    _name = animation->mName.C_Str();

    if(_name.empty()){
        std::cout<<"Animation在初始化时所给的aiAnimation没有名字,和它传入的参数Model为:"<<model->GetName()<<"\n";
    }

    m_Duration = animation->mDuration;              // Duration of the animation in ticks
    m_TicksPerSecond = animation->mTicksPerSecond;  // Ticks per second
    
    //根据模型节点分布创建骨骼节点分布
    ReadHierarchyData(m_RootNode, model->GetRootNode());
    //读取缺失的骨骼,构造m_BoneInfoMap,并将骨骼数据存入m_Bones
    ReadMissingBones(animation, *model);
}


Animation::Animation(const std::string sceneFilePath, Model* model){}

Animation::Animation(Animation&& other){
    std::cout<<"调用Animation移动构造函数: Name: "<< other._name <<std::endl;
    _name = other._name;
    m_Duration = other.m_Duration;
    m_TicksPerSecond = other.m_TicksPerSecond;
    m_Bones = std::move(m_Bones);

    m_RootNode.name = std::move(other.m_RootNode.name);
    m_RootNode.childrenCount = other.m_RootNode.childrenCount;
    m_RootNode.children = std::move(other.m_RootNode.children);
    m_RootNode.transformation = other.m_RootNode.transformation;

    other._name = "";
    
}

Animation::~Animation(){
    if(!_name.empty()){
        std::cout<<"调用Animation析构函数: "<<_name<<std::endl;
    }
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

const AnimationNodeData& Animation::GetRootNode() const {
    return m_RootNode;
}

const std::map<std::string, BoneInfo>& Animation::GetBoneIDMap() const {
    return m_BoneInfoMap;
}


void Animation::ReadMissingBones(const aiAnimation* animation, Model& model) {

    int size = animation->mNumChannels;

    // 获取model会使用到的bone
    auto& boneInfoMap = model.GetBoneInfoMap(); // Get model's BoneInfo map
    int& boneCount = model.GetBoneCount();      // Get model's bone count

    // 调试模式登记表代码
    std::map<std::string, bool> registerMap;
    for (const auto& [key, _] : boneInfoMap) {
        registerMap[key] = true;
    }

    // 遍历aiAnimation::mChannels来找到model会用到的bone,并关联起来
    for (int i = 0; i < size; i++) {
        auto channel = animation->mChannels[i];
        std::string boneName = channel->mNodeName.data;
        
       
        auto it = boneInfoMap.find(boneName);
        if (it == boneInfoMap.end()) {
            // 如果找不到暂时添加(这个代码会在Model::boneInfoMap之后添加)
            //boneInfoMap[boneName].id = boneCount;
            //boneCount++;
            // 调试模式下打印警告
            std::cout << "Animation: "<< _name << " 在为Model: " << model.GetName() << " 加载骨骼时发现一个没有被记入的名字: " << boneName << "\n";
        }
        else{
            registerMap[boneName] = false;
            m_Bones.emplace_back(Bone(boneName, it->second.id , channel));
        }

    }

    //调试模式下检测Animation是否加载Model所需要的全部Bone
    for (const auto& [key, value] : registerMap) {
        if(value){
            std::cout << "Animation: "<< _name << " 在为Model: " << model.GetName() << " 加载骨骼时发现一个没有被加载的model所需的Bone: " << key << "\n";
        }
    }
    
    // 深拷贝
    m_BoneInfoMap = boneInfoMap;
}

//递归函数,用于读取模型的节点信息
void Animation::ReadHierarchyData(AnimationNodeData& dest, const ModelNode* src) {
    assert(src);
    //将节点的名称和变换矩阵赋值给目标节点
    dest.name = src->name;
    dest.transformation = src->Transformation;
    dest.childrenCount = src->childrenCount;
    //递归读取子节点
    for (int i = 0; i < src->childrenCount; i++) {
        AnimationNodeData newData;
        ReadHierarchyData(newData, src->children[i].get());
        dest.children.push_back(newData);
    }
}

void Animation::Print(int tabs) const{
    std::string tab(tabs, '\t');
    std::ostringstream oss;
    oss << tab << "======AnimationInfo======\n"
        << tab << "Name: " << this->_name << '\n'
        << tab << "Duration: " << this->m_Duration << '\n'
        << tab << "TicksPerSecond: " << this->m_TicksPerSecond << '\n';
        // << tab << "Bones: " << '\n';
        // for (const auto& bone : m_Bones) {
        //     oss << tab << '\t' << "BoneName: " << bone.GetBoneName() << '\n'
        //         << tab << '\t' <<  "BoneID: " << bone.GetBoneID() << '\n';
        // }
    oss << tab << "======EndAnimationInfo======\n";
    std::cout << oss.str();
}
