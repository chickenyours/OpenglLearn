#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtx/norm.hpp>
#include <cmath>
#include <string>

#include "engine/ECS/Component/component.h"
#include "engine/ToolAndAlgorithm/Json/json_helper.h"

namespace ECS::Component{

    struct Transform2D : Component<Transform2D>
    {

        bool LoadFromMetaDataImpl(const Json::Value& data, Log::StackLogErrorHandle errHandle = nullptr) {
            return true;
        }
        // Local space information
        glm::vec2 position = {0.0f, 0.0f};
        // 单位：度
        float rotation = 0.0f;
        glm::vec2 scale = {1.0f, 1.0f};

        // Global / concatenated matrices
        glm::mat4 localMatrix{1.0f};
        glm::mat4 worldMatrix{1.0f};

        // 计算 2D localMatrix
        // 约定 2D 在 XY 平面，绕 Z 轴旋转
        inline void CalMatrix()
        {
            glm::mat4 T = glm::translate(glm::mat4(1.0f), glm::vec3(position, 0.0f));
            glm::mat4 R = glm::rotate(glm::mat4(1.0f), glm::radians(rotation), glm::vec3(0.0f, 0.0f, 1.0f));
            glm::mat4 S = glm::scale(glm::mat4(1.0f), glm::vec3(scale, 1.0f));

            localMatrix = T * R * S;
        }

        // 获取 2D forward 方向（局部 +Y 旋转后的方向）
        inline glm::vec2 GetForward() const
        {
            float rad = glm::radians(rotation);
            float s = std::sin(rad);
            float c = std::cos(rad);

            // 局部 +Y = (0,1)，绕 Z 旋转后：
            // x = -sin(a), y = cos(a)
            return glm::normalize(glm::vec2(-s, c));
        }

        // 获取 2D right 方向（局部 +X 旋转后的方向）
        inline glm::vec2 GetRight() const
        {
            float rad = glm::radians(rotation);
            float s = std::sin(rad);
            float c = std::cos(rad);

            // 局部 +X = (1,0)，绕 Z 旋转后：
            // x = cos(a), y = sin(a)
            return glm::normalize(glm::vec2(c, s));
        }

        // 输出 forward 和 rotation
        inline void CalForwardAndRotation(glm::vec2& forward, float& rotationDeg) const
        {
            forward = GetForward();
            rotationDeg = rotation;
        }

        // 由 forward 反推出 rotation（以局部 +Y 作为默认朝向）
        inline void FromForward(glm::vec2 forward)
        {
            const float eps = 1e-6f;
            if(glm::length2(forward) < eps){
                return;
            }

            forward = glm::normalize(forward);

            // 由于 forward = (-sin(a), cos(a))
            // 所以 a = atan2(-x, y)
            rotation = glm::degrees(std::atan2(-forward.x, forward.y));
        }

        // 直接设置朝向角（度）
        inline void SetRotationDeg(float deg)
        {
            rotation = deg;
        }

        // 直接设置朝向角（弧度）
        inline void SetRotationRad(float rad)
        {
            rotation = glm::degrees(rad);
        }

        // 获取弧度制角度
        inline float GetRotationRad() const
        {
            return glm::radians(rotation);
        }

        // 让物体朝向目标点（局部 +Y 指向 target）
        inline void LookAt(const glm::vec2& target)
        {
            glm::vec2 dir = target - position;
            const float eps = 1e-6f;
            if(glm::length2(dir) < eps){
                return;
            }

            FromForward(glm::normalize(dir));
        }
    };

}