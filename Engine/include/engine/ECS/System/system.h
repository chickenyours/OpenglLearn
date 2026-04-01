#pragma once

#include "engine/ECS/Query/query.h"


namespace ECS::System{
    class System{
        public:
            virtual void Execute() = 0;
            virtual ~System() = default;
    };
};