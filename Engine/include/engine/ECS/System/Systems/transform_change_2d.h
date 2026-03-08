#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "engine/ECS/System/system.h"

#include "engine/ToolAndAlgorithm/object.h"
#include "engine/ECS/Component/component_shortage.h"
#include "engine/ECS/Component/Transform/transform.h"

namespace ECS::System{

    struct LocalTransformCalculator2D_Data{
        ComponentViewArray<ECS::Component::Transform> transforms;
        
    };

    class LocalTransformCalculator2D : public System{
        public:
            LocalTransformCalculator2D():System("TransformChanger2D"){}

            virtual void Update() override {
                for(auto item : input.transforms){
                    for(auto transform : *item.transforms){
                        transform.localMatrix = glm::translate(
                            glm::scale(
                                glm::rotate(
                                    glm::rotate(
                                        glm::rotate(
                                            glm::mat4(1.0f),
                                            glm::radians(transform.rotation.x),
                                            glm::vec3(1.0f, 0.0f, 0.0f)
                                        ),
                                        glm::radians(transform.rotation.y),
                                        glm::vec3(0.0f, 1.0f, 0.0f)
                                    ),
                                    glm::radians(transform.rotation.z),
                                    glm::vec3(0.0f, 0.0f, 1.0f)
                                ),
                                transform.scale
                            ),
                            transform.position
                        );
                    }
                }
                seq_.clear();
            }

            void Add(const LocalTransformCalculator2D_Data& object){
                seq_.push_back(object);
            }
            
            LocalTransformCalculator2D_Data input;

            
    };
}

//ECS架构中 TransformChanger system 可以线性处理transform中的localMatrix 但是没法处理层级关系, 你可以改进如何处理层级关系吗,已知