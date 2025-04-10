#include "Bone.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cassert>
#include "assimp_glm_helper.h"

using namespace Render;

Bone::Bone(const std::string& name, int ID, const aiNodeAnim* channel)
    : m_Name(name), m_ID(ID), m_LocalTransform(1.0f) {
    m_NumPositions = channel->mNumPositionKeys;
    for (int i = 0; i < m_NumPositions; ++i) {
        KeyPosition data;
        data.position = AssimpGLMHelpers::GetGLMVec(channel->mPositionKeys[i].mValue);
        data.timeStamp = channel->mPositionKeys[i].mTime;
        m_Positions.push_back(data);
    }

    m_NumRotations = channel->mNumRotationKeys;
    for (int i = 0; i < m_NumRotations; ++i) {
        KeyRotation data;
        data.orientation = AssimpGLMHelpers::GetGLMQuat(channel->mRotationKeys[i].mValue);
        data.timeStamp = channel->mRotationKeys[i].mTime;
        m_Rotations.push_back(data);
    }

    m_NumScalings = channel->mNumScalingKeys;
    for (int i = 0; i < m_NumScalings; ++i) {
        KeyScale data;
        data.scale = AssimpGLMHelpers::GetGLMVec(channel->mScalingKeys[i].mValue);
        data.timeStamp = channel->mScalingKeys[i].mTime;
        m_Scales.push_back(data);
    }
}

void Bone::Update(float animationTime) {
    glm::mat4 translation = InterpolatePosition(animationTime);
    glm::mat4 rotation = InterpolateRotation(animationTime);
    glm::mat4 scale = InterpolateScaling(animationTime);
    m_LocalTransform = translation * rotation * scale;
}

glm::mat4 Bone::GetLocalTransform() const {
    return m_LocalTransform;
}

std::string Bone::GetBoneName() const {
    return m_Name;
}

int Bone::GetBoneID() const {
    return m_ID;
}

int Bone::GetPositionIndex(float animationTime) const {
    for (int i = 0; i < m_NumPositions - 1; ++i) {
        if (animationTime < m_Positions[i + 1].timeStamp)
            return i;
    }
    assert(false);
    return 0;
}

int Bone::GetRotationIndex(float animationTime) const {
    for (int i = 0; i < m_NumRotations - 1; ++i) {
        if (animationTime < m_Rotations[i + 1].timeStamp)
            return i;
    }
    assert(false);
    return 0;
}

int Bone::GetScaleIndex(float animationTime) const {
    for (int i = 0; i < m_NumScalings - 1; ++i) {
        if (animationTime < m_Scales[i + 1].timeStamp)
            return i;
    }
    assert(false);
    return 0;
}

float Bone::GetScaleFactor(float lastTimeStamp, float nextTimeStamp, float animationTime) const {
    return (animationTime - lastTimeStamp) / (nextTimeStamp - lastTimeStamp);
}


//这里还能扩展，比如使用样条插值，而不是线性插值
glm::mat4 Bone::InterpolatePosition(float animationTime) const {
    if (m_NumPositions == 1)
        return glm::translate(glm::mat4(1.0f), m_Positions[0].position);

    int p0Index = GetPositionIndex(animationTime);
    int p1Index = p0Index + 1;
    float scaleFactor = GetScaleFactor(m_Positions[p0Index].timeStamp,
                                       m_Positions[p1Index].timeStamp, animationTime);
    glm::vec3 finalPosition = glm::mix(m_Positions[p0Index].position, m_Positions[p1Index].position, scaleFactor);
    return glm::translate(glm::mat4(1.0f), finalPosition);
}

glm::mat4 Bone::InterpolateRotation(float animationTime) const {
    if (m_NumRotations == 1)
        return glm::toMat4(glm::normalize(m_Rotations[0].orientation));

    int p0Index = GetRotationIndex(animationTime);
    int p1Index = p0Index + 1;
    float scaleFactor = GetScaleFactor(m_Rotations[p0Index].timeStamp,
                                       m_Rotations[p1Index].timeStamp, animationTime);
    glm::quat finalRotation = glm::slerp(m_Rotations[p0Index].orientation,
                                         m_Rotations[p1Index].orientation, scaleFactor);
    return glm::toMat4(glm::normalize(finalRotation));
}

glm::mat4 Bone::InterpolateScaling(float animationTime) const {
    if (m_NumScalings == 1)
        return glm::scale(glm::mat4(1.0f), m_Scales[0].scale);

    int p0Index = GetScaleIndex(animationTime);
    int p1Index = p0Index + 1;
    float scaleFactor = GetScaleFactor(m_Scales[p0Index].timeStamp,
                                       m_Scales[p1Index].timeStamp, animationTime);
    glm::vec3 finalScale = glm::mix(m_Scales[p0Index].scale, m_Scales[p1Index].scale, scaleFactor);
    return glm::scale(glm::mat4(1.0f), finalScale);
}
