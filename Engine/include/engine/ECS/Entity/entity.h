#pragma once

#include "engine/ECS/data_type.h"
#include "engine/Scene/scene.h"

namespace ECS{
    class EntityHandle {
        public:
            EntityHandle() = default;
            explicit EntityHandle(EntityID id, Scene* scene) : id_(id), scene_(scene) {}
            EntityHandle(const EntityHandle& other){ id_ = other.id_; scene_ = other.scene_; }
            inline EntityID GetID() const { return id_; }
            Scene* GetScene() const { return scene_; }
            explicit operator EntityID() const{ return GetID(); }
            explicit operator bool() const { return scene_ != nullptr && id_ != 0; }
            bool operator==(const EntityHandle& rhs) const { return id_ == rhs.id_ && scene_ == rhs.scene_; }
        private:
            EntityID id_ = 0;
            Scene* scene_ = nullptr;
        };

}
