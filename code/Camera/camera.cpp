#include "camera.h"
#include "code/Config/config.h"

using namespace Render;

Camera::Camera():camPos_(0.0,0.0,3.0),camUp_(0.0f,1.0f,0.0f),
    pitch_(0.0f),yaw_(-90.0f),roll_(0.0f),zoom_(glm::radians(45.0f)),aspectRatio((float)SCR_WIDTH/(float)SCR_HEIGHT),
    near_(0.1f),far_(500.0f),projection_(glm::perspective(zoom_,aspectRatio,near_,far_)){
        Update();
}

void Camera::Update(){
    //更新view_
    glm::vec3 direction;
    direction.x = glm::cos(glm::radians(pitch_)) * glm::cos(glm::radians(yaw_));
    direction.y = glm::sin(glm::radians(pitch_));
    direction.z = glm::sin(glm::radians(yaw_)) * glm::cos(glm::radians(pitch_));
    camFront_ = glm::normalize(direction); 
    view_ = glm::lookAt(camPos_,camFront_+camPos_,camUp_);
}




