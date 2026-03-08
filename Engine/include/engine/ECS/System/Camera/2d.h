#pragma once

#include "engine/ECS/System/system.h"
#include "engine/ECS/Component/Camera/camera.h"
#include "engine/ECS/Component/Transform/transform.h"
#include "engine/Input/key_get.h"
#include "engine/Input/mouse_input.h"
#include "engine/Environment/setting.h"
#include "engine/Environment/environment.h"
#include "engine/ToolAndAlgorithm/object.h"
#include "engine/Environment/environment.h"

namespace ECS::System{

    // struct CameraMovable2D_Data{
    //     Component::Camera* camera;
    //     Component::Transform* transform;
    // };

    class CameraMovable2D : public System{
        public:
            CameraMovable2D():System("CameraMovable2D"){

            }
            virtual void Update() {
                float deltaTime = 1.0;
                if(mainCamera && mainCameraTransform){
                    if(Input::KeyboardInput::Instance().GetKeyState('W') == Input::KeyboardInput::KeyState::HELD){
                        mainCameraTransform->position.y += deltaTime;
                    }
                    if(Input::KeyboardInput::Instance().GetKeyState('S') == Input::KeyboardInput::KeyState::HELD){
                        mainCameraTransform->position.y -= deltaTime;
                    }
                    if(Input::KeyboardInput::Instance().GetKeyState('D') == Input::KeyboardInput::KeyState::HELD){
                        mainCameraTransform->position.x += deltaTime;
                    }
                    if(Input::KeyboardInput::Instance().GetKeyState('A') == Input::KeyboardInput::KeyState::HELD){
                        mainCameraTransform->position.x += deltaTime;
                    }
                    mainCamera->Update(*mainCameraTransform.get());
                }
            }
            ObjectWeakPtr<ECS::Component::Camera> mainCamera;
            ObjectWeakPtr<ECS::Component::Transform> mainCameraTransform;
    };
}