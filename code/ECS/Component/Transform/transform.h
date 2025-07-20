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
