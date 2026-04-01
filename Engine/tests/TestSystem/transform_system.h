#pragma once

#include "engine/ECS/System/system.h"
#include "engine/ECS/Component/Transform/transform.h"

class TransformDoSystem : public ECS::System::System {
    public:
        ECS::Core::ChunkQuery<
         ECS::Core::Require<ECS::Component::Transform>,
         ECS::Core::Optional<>,
         ECS::Core::Exclude<>>* query;

        virtual void Execute() {

        }
};