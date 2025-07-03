#pragma once

#include "code/ECS/Entity/entity_manager.h"

#include "code/ECS/Component/component_register.h"

#include "code/ECS/data_type.h"

namespace ECS{
    // 十分轻量级对象
    class Entity {
        public:
            // C++ 17 在类中定义方法会被自动标记为内联函数 inline, 对于短小函数推荐采用这个方式
            
            inline EntityID GetID() const { return id_; }
    
            bool operator==(const Entity& other) const { return id_ == other.id_; }
        
            // template <typename ComponentT>
            // void AddComponent(){
                
            // }
        private:
            explicit Entity(EntityID id) : id_(id) {}
            EntityID id_;

            friend ECS::Core::EntityManager;
        };

}
