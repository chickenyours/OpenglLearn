#pragma once

#include "engine/ECS/data_type.h"
#include "engine/Scene/scene.h"

#include "engine/ECS/Context/context.h"



namespace ECS{
    class EntityHandle {
        friend class ECS::Core::Scene;
        public:
            explicit EntityHandle(EntityID id) : id_(id){}
            // explicit EntityHandle(EntityID id, Scene* scene) : id_(id), scene_(scene) {}
            // EntityHandle(const EntityHandle& other){ id_ = other.id_; scene_ = other.scene_; }
            inline EntityID GetID() const { return id_; }
            ECS::Core::Scene* GetScene() const { return ECS::Core::globalECSCoreContext.scene; }
            // Scene* GetScene() const { return scene_; }
            explicit operator EntityID() const{ return GetID(); }
            // explicit operator bool() const { return scene_ != nullptr && id_ != 0; }
            // bool operator==(const EntityHandle& rhs) const { return id_ == rhs.id_ && scene_ == rhs.scene_; }
        private:
            EntityHandle() = default;
            EntityID id_ = 0;
        };

}
