#pragma once

#include "engine/ECS/System/system.h"
#include "engine/ECS/Component/2D/Transform/transform_2D.h"
#include "Render2D/Public/Component/sprite.h"
#include "Render/Public/RHI/RHI_texture.h"

namespace Render2D::System{
    class Paper2DRenderSystem : public ECS::System::System {
        public:
            ECS::Core::ChunkQuery<
                ECS::Core::Require<
                    ECS::Component::Transform2D,
                    Render2D::Component::Sprite
                >,
                ECS::Core::Optional<>,
                ECS::Core::Exclude<>
            >* query = nullptr;
            virtual void Execute(){
                
            }
    };
}