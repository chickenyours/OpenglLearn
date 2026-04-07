#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "engine/ECS/System/system.h"
#include "engine/ECS/Component/2D/Transform/transform_2D.h"

class LocalTransform2DSystem : public ECS::System::System {
public:
    ECS::Core::ChunkQuery<
        ECS::Core::Require<ECS::Component::Transform2D>,
        ECS::Core::Optional<>,
        ECS::Core::Exclude<>
    >* query = nullptr;

public:
    virtual void Execute() override {
        if(query == nullptr){
            return;
        }

        for(auto chunk : (*query)){
            auto transformChunk = chunk.Get<ECS::Component::Transform2D>();

            for(size_t i = 0; i < chunk.count; ++i){
                auto& transform = transformChunk[i];
                // 2D 纸片图元：
                // XY 平移，绕 Z 旋转，XY 缩放
                transform.localMatrix = glm::translate(
                    glm::scale(
                        glm::rotate(
                            glm::mat4(1.0f),
                            glm::radians(transform.rotation),
                            glm::vec3(0.0f, 0.0f, 1.0f)
                        ),
                        glm::vec3(transform.scale, 1.0f)
                    ),
                    glm::vec3(transform.position, 0.0f)
                );
            }
        }
    }
};