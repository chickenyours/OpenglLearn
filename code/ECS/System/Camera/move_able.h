#pragma once

#include "code/ECS/System/system.h"

#include "code/ECS/Component/Camera/camera.h"

#include "code/Input/key_get.h"
#include "code/Input/mouse_input.h"

#include "code/Environment/setting.h"
#include "code/Environment/environment.h"

#include "code/ToolAndAlgorithm/transformation.h"

#include "code/Scene/scene.h"

namespace ECS::System{
    class CameraMovable : public System{
        public:
            CameraMovable():System("CameraMovable"){}
            virtual bool InitDerive() override {
                return true;
            }
            virtual bool AddEntity(EntityID entity) override {
                auto camera = scene_->registry_->GetComponent<ECS::Component::Camera>(entity); 
                auto transform = scene_->registry_->GetComponent<ECS::Component::Transform>(entity); 
                if(camera && transform){
                    camera_ = camera;
                    transform_ = transform;
                    return true;
                }
                LOG_ERROR("System:CameraMovable","Haven't camera or transform");
                return false;
            }
            virtual void Update() override {
                float newTime = Environment::Environment::Instance().GetTime();
                float deltaTime = newTime - lastTime;
                lastTime = newTime;

                // 更新角度
                glm::vec2 offset = Input::MouseInput::Instance().GetMouseOffset();
                transform_->rotation.y += offset.x * Setting::Setting::Instance().sensitivity;
                transform_->rotation.z -= offset.y * Setting::Setting::Instance().sensitivity;
                if(transform_->rotation.z>89.0f) transform_->rotation.z = 89.0f;
                if(transform_->rotation.z<-89.0f) transform_->rotation.z = -89.0f;

                glm::vec3 direction;
                direction.x = glm::cos(glm::radians(transform_->rotation.z)) * glm::cos(glm::radians(transform_->rotation.y));
                direction.y = glm::sin(glm::radians(transform_->rotation.z));
                direction.z = glm::sin(glm::radians(transform_->rotation.y)) * glm::cos(glm::radians(transform_->rotation.z));
                camera_->camFront = glm::normalize(direction); 

                float camSpeed = 1.0;

                if(Input::KeyboardInput::Instance().GetKeyState(VK_LSHIFT) == Input::KeyboardInput::KeyState::HELD){
                    camSpeed *= 3.0f;
                }

                if(Input::KeyboardInput::Instance().GetKeyState('W') == Input::KeyboardInput::KeyState::HELD){
                    transform_->position += camera_->camFront * camSpeed * deltaTime;
                }
                if(Input::KeyboardInput::Instance().GetKeyState('S') == Input::KeyboardInput::KeyState::HELD){
                    transform_->position -= camera_->camFront * camSpeed * deltaTime;
                }
                if(Input::KeyboardInput::Instance().GetKeyState('D') == Input::KeyboardInput::KeyState::HELD){
                    transform_->position += glm::normalize(glm::cross(camera_->camFront,camera_->camUp)) * camSpeed * deltaTime;
                }
                if(Input::KeyboardInput::Instance().GetKeyState('A') == Input::KeyboardInput::KeyState::HELD){
                    transform_->position += glm::normalize(glm::cross(camera_->camUp,camera_->camFront)) * camSpeed * deltaTime;
                }
                
                camera_->camPos = transform_->position;
                camera_->projection = glm::perspective(camera_->zoom_, camera_->aspectRatio_, camera_->near_, camera_->far_);
                camera_->view = Algorithm::GetViewMat4(camera_->camPos,camera_->camFront);
            }
        private:
            float lastTime = Environment::Environment::Instance().GetTime();
            ECS::Component::Camera* camera_;
            ECS::Component::Transform* transform_;
    };
}