#pragma once

#include <vector>
#include "code/ECS/data_type.h"
#include "code/ECS/System/system.h"

namespace Render{
    class RenderPipe : public ECS::System::System{
        public:
            RenderPipe():System("RenderPipe"){}
            virtual void LoadEntity(const std::vector<ECS::EntityID>& seq) = 0;
            virtual void Draw() = 0;
            virtual ~RenderPipe() = default;
    };
} // namespace Render