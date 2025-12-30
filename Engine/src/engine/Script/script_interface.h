#pragma once

#include "engine/ECS/data_type.h"

#include "engine/Scene/scene.h"

#include "engine/Plugin/host_api.h"

#include "engine/ECS/DataType/script_collision_event.h"

class IScript
{
    public:
        virtual void OnStart(ECS::Scene* scene, ECS::EntityID entity) = 0;
        virtual void OnUpdate(ECS::Scene* scene, ECS::EntityID entity) = 0;
        virtual void OnCollisionStart(ECS::Scene* scene, ECS::EntityID me, ECS::EntityID you){}
        virtual void OnCollisionStay(ECS::Scene* scene, ECS::EntityID me, ECS::EntityID you){}
        virtual void OnCollisionExit(ECS::Scene* scene, ECS::EntityID me, ECS::EntityID you){}
        virtual ~IScript() = default;
};
    

