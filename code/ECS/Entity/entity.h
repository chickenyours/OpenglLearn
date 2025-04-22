#pragma once
#include <cstdint>

typedef uint32_t EntityID;

namespace ECS{

    class Entity{
        public:
            inline EntityID GetEntityID(){return id_;}
        private:
            EntityID id_;
    };

}
