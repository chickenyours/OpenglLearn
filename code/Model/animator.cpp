#include "animator.h"
#include <cmath>

using namespace Render;

Animator::Animator(Animation* animation)
    : m_CurrentAnimation(animation), m_CurrentTime(0.0f), m_DeltaTime(0.0f),_finalBoneMatricesCount(0) {
    m_FinalBoneMatrices.reserve(100);
    for (int i = 0; i < 100; i++) {
        m_FinalBoneMatrices.push_back(glm::mat4(1.0f));
    }
}

void Animator::UpdateAnimation(float dt) {
    m_DeltaTime = dt;
    if (m_CurrentAnimation) {
        m_CurrentTime += m_CurrentAnimation->GetTicksPerSecond() * dt;
        m_CurrentTime = std::fmod(m_CurrentTime, m_CurrentAnimation->GetDuration());
        CalculateBoneTransform(&m_CurrentAnimation->GetRootNode(), glm::mat4(1.0f));
    }
}

void Animator::PlayAnimation(Animation* pAnimation) {
    m_CurrentAnimation = pAnimation;
    m_CurrentTime = 0.0f;
}

void Animator::CalculateBoneTransform(const AnimationNodeData* node, glm::mat4 parentTransform) {
    std::string nodeName = node->name;
    glm::mat4 nodeTransform = node->transformation;             //默认先使用节点的变换矩阵

    Bone* bone = m_CurrentAnimation->FindBone(nodeName);
    if (bone) {
        bone->Update(m_CurrentTime);
        nodeTransform = bone->GetLocalTransform();              //如果有bone则使用bone的变换矩阵
    }

    glm::mat4 globalTransformation = parentTransform * nodeTransform;
    //glm::mat4 globalTransformation = nodeTransform;

    auto& boneInfoMap = m_CurrentAnimation->GetBoneIDMap();
    if (boneInfoMap.find(nodeName) != boneInfoMap.end()) {
        int index = boneInfoMap.at(nodeName).id;
        glm::mat4 offset = boneInfoMap.at(nodeName).offset;
        m_FinalBoneMatrices[index] = globalTransformation * offset;
    }
    

    for (int i = 0; i < node->childrenCount; i++) {
        CalculateBoneTransform(&node->children[i], globalTransformation);
    }
}

void Animator::Print(int tabs) const{
    std::string tab(tabs, '\t');
    std::ostringstream oss;
    oss << tab << "======AnimatorInfo======\n"
        << tab << "Current animation: " << m_CurrentAnimation->GetName() << '\n'
        << tab << "======EndAnimationInfo======\n";
    std::cout << oss.str();
}

Animator::~Animator(){
    std::cout << "Animator被销毁,持有Animation: " << m_CurrentAnimation->GetName();
}

Animator::Animator(Animator&& other){
    std::cout << "Animator发送移动,持有Animation: " << m_CurrentAnimation->GetName();
    this->PlayAnimation(other.m_CurrentAnimation);
    other.m_CurrentAnimation = nullptr;
}