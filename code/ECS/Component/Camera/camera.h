#pragma once

#include "code/ECS/Component/component.h"

#include "code/ECS/Component/Transform/transform.h"

#include "code/Config/config.h"

namespace ECS::Component{

struct Camera : public Component<Camera>
{
    // 常用缓存
    glm::vec3 camPos;
    glm::vec3 camFront;
    glm::mat4 view;
    glm::mat4 projection;
    // 状态属性
    float near_ = 0.1f;
    float far_ = 500.0f;
    float zoom_ = glm::radians(45.0f);
    float aspectRatio_ = (float)SCR_WIDTH/(float)SCR_HEIGHT;
    glm::vec3 camUp = glm::vec3(0.0,1.0,0.0);

    void Update(const Transform& transform){
        camPos = transform.position;

        projection = glm::perspective(zoom_,aspectRatio_,near_,far_);

        glm::vec3 front;
        front.x = glm::cos(glm::radians(transform.rotation.x)) * glm::cos(glm::radians(transform.rotation.y));
        front.y = glm::sin(glm::radians(transform.rotation.x));
        front.z = glm::cos(glm::radians(transform.rotation.x)) * glm::sin(glm::radians(transform.rotation.y));
        camFront = glm::normalize(front);

        // 基础 WorldUp
        glm::vec3 worldUp = glm::vec3(0.0f, 1.0f, 0.0f);

        // 构造基础 Right 和 Up
        glm::vec3 right = glm::normalize(glm::cross(camFront, worldUp));
        glm::vec3 up = glm::normalize(glm::cross(right, camFront));

        // 加入 roll_（绕前向量的旋转）
        float rollRad = glm::radians(transform.rotation.z);
        glm::mat4 rollMatrix = glm::rotate(glm::mat4(1.0f), rollRad, camFront);
        glm::vec3 rolledUp = glm::mat3(rollMatrix) * up;

        camUp = rolledUp;

        // 最终 LookAt
        view = glm::lookAt(camPos, camPos + camFront, camUp);
    }
    
    bool LoadFromMetaDataImpl(const Json::Value& data, Log::StackLogErrorHandle errHandle = nullptr) {
        std::string errReport = "Some properties fail to be loaded: ";
    
        bool flag = true;
    
        if(!Tool::JsonHelper::TryGetFloat(data, "near", near_)){
            flag = false;
            errReport += "near ";
        }

        if(!Tool::JsonHelper::TryGetFloat(data, "far", far_)){
            flag = false;
            errReport += "far ";
        }

        if(!Tool::JsonHelper::TryGetFloat(data, "zoom", zoom_)){
            flag = false;
            errReport += "zoom ";
        }

        if(!Tool::JsonHelper::TryGetFloat(data, "aspectRatio", aspectRatio_)){
            flag = false;
            errReport += "aspectRatio ";
        }

        if(!flag){
                REPORT_STACK_ERROR(errHandle, "Component:Camera->Load", errReport);
        }

        return flag;
    }
};

    
}