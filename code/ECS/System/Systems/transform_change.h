#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "code/ECS/System/system.h"

#include "code/ECS/Component/Transform/transform.h"

namespace ECS::System{
    class LocalTransformCalculator : public System{
        public:
            LocalTransformCalculator():System("TransformChanger"){}
            virtual bool AddEntity(EntityID entity, ECS::Core::ComponentRegister& reg) override {
                auto transform = reg.GetComponent<ECS::Component::Transform>(entity);
                if(!transform){
                    return false;
                }
                seq_.push_back(transform);
                return true;
            }

            virtual void Update() override {
                for(auto transform : seq_){
                    transform->localMatrix = glm::translate(
                    glm::scale(
                        glm::rotate(
                            glm::rotate(
                                glm::rotate(
                                    glm::mat4(1.0f),
                                    glm::radians(transform->rotation.x),
                                    glm::vec3(1.0f, 0.0f, 0.0f)
                                ),
                                glm::radians(transform->rotation.y),
                                glm::vec3(0.0f, 1.0f, 0.0f)
                            ),
                            glm::radians(transform->rotation.z),
                            glm::vec3(0.0f, 0.0f, 1.0f)
                        ),
                        transform->scale
                    ),
                    transform->position
                );
                }
            }
        private:
            std::vector<ECS::Component::Transform*> seq_;
    };
}

//ECS架构中 TransformChanger system 可以线性处理transform中的localMatrix 但是没法处理层级关系, 你可以改进如何处理层级关系吗,已知