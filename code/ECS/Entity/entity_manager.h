#pragma once

#include <unordered_map>
#include <string>

#include "code/ECS/data_type.h"
#include "code/ECS/Entity/entity.h"

namespace ECS::Core{
    class EntityManager{
        public:
            EntityManager() = default;
            Entity GenNewEntity(EntityID id, std::string lable = ""){
                entities_.push_back(id);
                if(!lable.empty()){
                    entityMap_[lable] = id;
                }
                return Entity(id);
            }
        private:
            std::vector<EntityID> entities_;
            std::unordered_map<std::string,EntityID> entityMap_;
    };
}