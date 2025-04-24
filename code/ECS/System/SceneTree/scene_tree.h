#pragma once

#include <vector>
#include <unordered_map>

#include "code/ECS/data_type.h"

#include "code/ECS/System/system.h"

#include "code/ECS/Component/Transform/transform.h"
#include "code/ECS/Component/Hierarchy/hierarchy.h"


namespace ECS::System{

    
    class SceneTreeSystem:public System {
        public:
            void SetParent(EntityID child, EntityID newParent);
            void RemoveEntity(EntityID entity);
        
            EntityID GetParent(EntityID child) const;
            const std::vector<EntityID>& GetChildren(EntityID parent) const;
        
            void SceneTreeSystem::UpdateTransforms(Core::ComponentStorage<Component::Transform>& transforms);
        
        private:
            // 子,父
            // std::unordered_map<EntityID, EntityID> parentMap_;
            // std::unordered_map<EntityID, std::vector<EntityID>> childrenMap_;
            ECS::Core::ComponentRegister* rgs;
            std::vector<EntityID> roots_;
        
            void RemoveFromOldParent(EntityID child);
            void SceneTreeSystem::UpdateRecursive(EntityID entity, const glm::mat4& parentMatrix,
                Core::ComponentStorage<Component::Transform>& transforms);
    };
        
}