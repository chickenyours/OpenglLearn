#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "code/ECS/Component/component.h"
#include "code/ToolAndAlgorithm/Json/json_helper.h"

namespace ECS::Component{

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
    };
    
}
