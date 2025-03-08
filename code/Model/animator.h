#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <assimp/scene.h>
#include <assimp/Importer.hpp>
#include "Animation.h"
#include "Bone.h"

namespace Render{
    class Animator {
        public:
            explicit Animator(Animation* animation = nullptr);
            
            void UpdateAnimation(float dt);
            void PlayAnimation(Animation* pAnimation);
            std::vector<glm::mat4> GetFinalBoneMatrices() const;
        
        private:
            void CalculateBoneTransform(const AssimpNodeData* node, glm::mat4 parentTransform);
        
            std::vector<glm::mat4> m_FinalBoneMatrices;
            Animation* m_CurrentAnimation;
            float m_CurrentTime;
            float m_DeltaTime;
        };       
}

