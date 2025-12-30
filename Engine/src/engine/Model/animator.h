#pragma once

#include <iostream>
#include <sstream>
#include <map>
#include <cmath>
#include <glm/glm.hpp>
#include <vector>
#include <assimp/scene.h>
#include <assimp/Importer.hpp>


namespace Render{
    //前向声明
    class Animation;
    class AnimationNodeData;
    class Bone;

    class Animator {
        public:
            explicit Animator(Animation* animation = nullptr);
            ~Animator();
            Animator(Animator&& other);
            void UpdateAnimation(float dt);
            void PlayAnimation(Animation* pAnimation);
            std::vector<glm::mat4> GetFinalBoneMatrices() const;
            inline std::vector<glm::mat4>& GetFinalBoneMatrices() { return m_FinalBoneMatrices; }
            inline int GetFinalBoneMatricesCount() { return _finalBoneMatricesCount; }

            void Print(int tabs) const;


        
        private:
            void CalculateBoneTransform(const AnimationNodeData* node, glm::mat4 parentTransform);

            int _finalBoneMatricesCount;
            std::vector<glm::mat4> m_FinalBoneMatrices;
            Animation* m_CurrentAnimation;
            float m_CurrentTime;
            float m_DeltaTime;
        };       
}

