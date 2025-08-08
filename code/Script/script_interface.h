#pragma once

#include "code/ECS/data_type.h"

#include "code/Scene/scene.h"

#include "code/Plugin/host_api.h"

#include "code/Script/Bindable/json_bindable.h"


    class IScript : public JsonBindable
    {
        public:
            virtual void OnStart(ECS::Scene* scene, ECS::EntityID entity) = 0;
            virtual void OnUpdate(ECS::Scene* scene, ECS::EntityID entity) = 0;
            virtual ~IScript() = default;
    };
    
