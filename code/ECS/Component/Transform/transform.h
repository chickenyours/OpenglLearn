#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>      // glm::normalize(quat)
#include <glm/gtx/quaternion.hpp>      // glm::rotate(quat, vec3)
#include <glm/gtx/euler_angles.hpp>    // glm::yaw(quat)
#include <glm/gtc/constants.hpp>       // glm::pi(), optional
#include <cmath>        

#include "code/ECS/Component/component.h"
#include "code/ToolAndAlgorithm/Json/json_helper.h"

namespace ECS::Component{

    enum class RotationMode {
        Euler,
        Quaternion
    };

    struct Transform : Component<Transform>
    {
        //Local space information
        glm::vec3 position = {0.0f,0.0f,0.0f}; 
        // pitch yaw roll (x,y,z)
        glm::vec3 rotation = {0.0f,0.0f,0.0f};
        glm::vec3 scale = {1.0f,1.0f,1.0f};
        glm::quat quaternion = {0.0f,0.0f,0.0f,1.0f};
        //Global space information concatenate in matrix
        glm::mat4 localMatrix{1.0f};
        glm::mat4 worldMatrix{1.0f};

        RotationMode rotationMode = RotationMode::Euler;

        bool LoadFromMetaDataImpl(const Json::Value& data, Log::StackLogErrorHandle errHandle = nullptr) {
            std::string errReport = "Some properties fail to be loaded: ";

            bool flag = true;

            if(!Tool::JsonHelper::TryGetVec3(data, "position", position)){
                flag = false;
                errReport += "position ";
            }

            if(!Tool::JsonHelper::TryGetVec3(data, "rotation", rotation)){
                flag = false;
                errReport += "rotation ";
            }

            if(!Tool::JsonHelper::TryGetVec3(data, "scale", scale)){
                flag = false;
                errReport += "scale ";
            }

            if(!flag){
                REPORT_STACK_ERROR(errHandle, "Component:Transform->Load", errReport);
            }

            return flag;
        }

        
        // 计算model矩阵并赋值到localMatrix
        inline void CalMatrix()
        {
            glm::quat rotQ;

            if (rotationMode == RotationMode::Euler)
            {
                // 欧拉角 -> 四元数
                glm::quat qx = glm::angleAxis(glm::radians(rotation.x), glm::vec3(1,0,0));
                glm::quat qy = glm::angleAxis(glm::radians(rotation.y), glm::vec3(0,1,0));
                glm::quat qz = glm::angleAxis(glm::radians(rotation.z), glm::vec3(0,0,1));

                rotQ = glm::normalize(qy * qx * qz);

                // 存回成员变量
                quaternion = rotQ;
            }
            else // Quaternion 模式
            {
                rotQ = glm::normalize(quaternion);
            }

            glm::mat4 T = glm::translate(glm::mat4(1.0f), position);
            glm::mat4 R = glm::mat4_cast(rotQ);
            glm::mat4 S = glm::scale(glm::mat4(1.0f), scale);

            localMatrix = T * R * S;
        }

        // 返回朝向和横向转向
        inline void CalForwardAndYaw(glm::vec3& forward, float& yawDeg){
            {
                // 使用四元数计算朝向（局部 +Z 在世界空间中的方向）
                glm::quat q = glm::normalize(quaternion);
                forward = glm::normalize(glm::rotate(q, glm::vec3(0.0f, 0.0f, 1.0f)));

                // 将朝向投影到 XZ 平面以计算横向角（绕 worldUp = (0,1,0) 的偏航）
                glm::vec3 proj(forward.x, 0.0f, forward.z);
                const float eps = 1e-6f;
                if (glm::length2(proj) < eps) {
                    // 当朝向接近上下时，使用四元数的 yaw 作为回退
                    yawDeg = glm::degrees(glm::yaw(q));
                } else {
                    proj = glm::normalize(proj);
                    // atan2(x, z) 使得 0 度对应 +Z，正值朝 +X 方向（与 FromForwardAndYaw 的定义一致）
                    yawDeg = glm::degrees(atan2(proj.x, proj.z));
                }
            }
        }

        // 输入朝向向量和横向角度计算rotation(已自动归一化)
        inline void FromForwardAndYaw(glm::vec3 forward, float yawDeg)
        {
            glm::vec3 f = glm::normalize(forward);
            glm::vec3 worldUp(0, 1, 0);

            // 1. forward → quaternion
            glm::vec3 r = glm::normalize(glm::cross(worldUp, f));
            glm::vec3 u = glm::cross(f, r);
            glm::mat3 rot(r, u, f);
            glm::quat qForward = glm::quat_cast(rot);

            // 2. yaw → quaternion
            glm::quat qYaw = glm::angleAxis(glm::radians(yawDeg), worldUp);

            // 3. 合并旋转：yaw * forward
            quaternion = glm::normalize(qYaw * qForward);

            // 4. 同步欧拉角（pitch, yaw, roll）
            rotation.x = glm::degrees(glm::pitch(quaternion));  // pitch
            rotation.y = glm::degrees(glm::yaw(quaternion));    // yaw
            rotation.z = glm::degrees(glm::roll(quaternion));   // roll
        }






    };

    struct TransformView{
        float& posX;
        float& posY;
        float& posZ;
        float& rotX;
        float& rotY;
        float& rotZ;
        float& scaleX;
        float& scaleY;
        float& scaleZ;
        glm::quat& quaternion;
        glm::mat4& localMatrix;
        glm::mat4& worldMatrix;
    };

    // struct TransformStorage {
    //     std::vector<float> posX, posY, posZ;
    //     std::vector<float> rotX, rotY, rotZ;
    //     std::vector<float> scaleX, scaleY, scaleZ;

    //     std::vector<glm::quat> quaternion;
    //     std::vector<glm::mat4> localMatrix;
    //     std::vector<glm::mat4> worldMatrix;

    //     std::vector<>

    //     void Resize(size_t count) {
    //         posX.resize(count);
    //         posY.resize(count);
    //         posZ.resize(count);
    //         rotX.resize(count);
    //         rotY.resize(count);
    //         rotZ.resize(count);
    //         scaleX.resize(count, 1.0f);
    //         scaleY.resize(count, 1.0f);
    //         scaleZ.resize(count, 1.0f);
    //         quaternion.resize(count, glm::quat(0,0,0,1));
    //         localMatrix.resize(count, glm::mat4(1.0f));
    //         worldMatrix.resize(count, glm::mat4(1.0f));
    //     }

    //     size_t Size() const {
    //         return posX.size();
    //     }

    //     TransformView GetView(size_t i){
    //         return TransformView{
    //             posX[i], posY[i], posZ[i],
    //             rotX[i], rotY[i], rotZ[i],
    //             scaleX[i], scaleY[i], scaleZ[i],
    //             quaternion[i],
    //             localMatrix[i],
    //             worldMatrix[i]
    //         };
    //     }

    // };

    
}
