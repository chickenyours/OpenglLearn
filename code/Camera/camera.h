#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

namespace Render{
    class Camera{
        public:
            Camera();
            inline glm::mat4 GetViewMatrix(){return view_;}
            inline const glm::mat4& GetViewMatrixRef() const {return view_;} 
            inline glm::vec3 GetCameraFront(){return camFront_;}
            inline const glm::vec3& GetCameraFrontRef() const {return camFront_;}
            inline glm::vec3 GetCameraUp(){return camUp_;}
            inline const glm::vec3& GetCameraUpRef() const {return camUp_;}
            inline const glm::mat4& GetProjectionMatrixRef() const {return projection_;} 
            inline const glm::mat4 GetProjectionMatrix() const {return projection_;} 
            inline void SetPosition(glm::vec3 position){camPos_ = position;}
            inline void Move(glm::vec3 position){camPos_ += position;}
            // void LookAt(glm::vec3 direction, glm::vec3 Up = glm::vec3(1.0));
            inline void Rotate(float deltaYaw, float deltaPitch){
                yaw_ += deltaYaw; 
                pitch_ += deltaPitch;
                if(pitch_>89.0f)pitch_ = 89.0f;
                if(pitch_<-89.0f)pitch_ = -89.0f;
            }
            inline glm::vec3 GetPosition() const { return camPos_; }
            inline glm::vec3 GetUp() const { return camUp_; }
            inline float GetPitch() const { return pitch_; }
            inline float GetYaw() const { return yaw_; }
            inline float GetRoll() const { return roll_; }
            inline float GetNear() const { return near_; }
            inline float GetFar() const { return far_; }
            inline float GetAspectRatio() const { return aspectRatio; }
            inline float GetZoom() const{return zoom_;}
            
            void Update();
        private:
            float pitch_;
            float yaw_;
            glm::vec3 camPos_;
            glm::vec3 camUp_;
            glm::vec3 camFront_;
            glm::mat4 view_;
            //投影相关
            float near_;
            float far_;
            float roll_;
            float zoom_;
            float aspectRatio;
            glm::mat4 projection_;
    };
}