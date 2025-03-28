#pragma once

#include <vector>
#include <map>
#include <glm/glm.hpp>
#include <assimp/scene.h>
#include <functional>
#include "bone.h"
#include "animdata.h"
#include "model_animation.h"

namespace Render{

    //前向声明
    class Model;
    class ModelNode;

    struct AnimationNodeData {
        glm::mat4 transformation;
        std::string name;
        int childrenCount;
        std::vector<AnimationNodeData> children;
    };
    
    class Animation {
    public:
        Animation() = default;
        // 为这个模型model初始化动画,其中scene提取骨骼,如果配对scene和model配对,对应的骨骼都能提取出来
        Animation(const aiAnimation* animation, Model* model);
        Animation(const std::string sceneFilePath, Model* model);
        Animation(Animation&& other);
        ~Animation();
    
        Bone* FindBone(const std::string& name);
        
        float GetTicksPerSecond() const;
        float GetDuration() const;
        inline string GetName(){return _name;};
        const AnimationNodeData& GetRootNode() const;
        const std::map<std::string, BoneInfo>& GetBoneIDMap() const;

        void Print(int tabs) const;
    
    private:
        void ReadMissingBones(const aiAnimation* animation, Model& model);
        void ReadHierarchyData(AnimationNodeData& dest, const ModelNode* srv);
    
        std::string _name;
        float m_Duration;
        int m_TicksPerSecond;
        std::vector<Bone> m_Bones;
        AnimationNodeData m_RootNode;
        std::map<std::string, BoneInfo> m_BoneInfoMap;

    };
}


