#pragma once

// #include "engine/ECS/Query/query.h"


namespace ECS{
    class System{
        public:
            // 受主线程ECS框架调度
            virtual void OnStart() = 0;
            virtual void OnTick() = 0;
            virtual void OnEnd() = 0;
            virtual ~System() = default;
    };
};