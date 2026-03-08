#pragma once

#include "engine/ECS/Entity/entity_manager.h"

#include "engine/ECS/Component/component_register.h"

#include "engine/ECS/data_type.h"

namespace ECS{
    // 十分轻量级对象
    class EntityHandle {
        public:
            // C++ 17 在类中定义方法会被自动标记为内联函数 inline, 对于短小函数推荐采用这个方式
            
            inline EntityID GetID() const { return id_; }
    
            bool operator==(const EntityHandle& other) const { return id_ == other.id_; }
        
            // template <typename ComponentT>
            // void AddComponent(){
                
            // }
        private:
            explicit EntityHandle(EntityID id) : id_(id) {}
            EntityID id_;

            friend ECS::Core::EntityManager;
        };

}

