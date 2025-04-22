#pragma once

#include "code/ECS/data_type.h"

namespace ECS{

    class Entity {
        public:
            // C++ 17 在类中定义方法会被自动标记为内联函数 inline, 对于短小函数推荐采用这个方式
            explicit Entity(EntityID id) : id_(id) {}
            inline EntityID GetID() const { return id_; }
    
            bool operator==(const Entity& other) const { return id_ == other.id_; }
    
        private:
            EntityID id_;
        };

}
