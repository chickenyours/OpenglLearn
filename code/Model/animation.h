#pragma once

#include <vector>
#include <map>
#include <glm/glm.hpp>
#include <assimp/scene.h>
#include "bone.h"
#include <functional>
#include "animdata.h"
#include "model_animation.h"

namespace Render{
    struct AssimpNodeData {
        glm::mat4 transformation;
        std::string name;
        int childrenCount;
        std::vector<AssimpNodeData> children;
    };
    
    class Animation {
    public:
        Animation() = default;
        Animation(const std::string& animationPath, Model* model);
        ~Animation() = default;
    
        Bone* FindBone(const std::string& name);
        
        float GetTicksPerSecond() const;
        float GetDuration() const;
        const AssimpNodeData& GetRootNode() const;
        const std::map<std::string, BoneInfo>& GetBoneIDMap() const;
    
    private:
        void ReadMissingBones(const aiAnimation* animation, Model& model);
        void ReadHierarchyData(AssimpNodeData& dest, const aiNode* src);
    
        float m_Duration;
        int m_TicksPerSecond;
        std::vector<Bone> m_Bones;
        AssimpNodeData m_RootNode;
        std::map<std::string, BoneInfo> m_BoneInfoMap;
    };
}


