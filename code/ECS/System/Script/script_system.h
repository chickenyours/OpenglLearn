#pragma once

#include "code/ECS/System/system.h"

#include "code/ECS/Component/Script/scirpt.h"

namespace ECS::System{
    class ScriptSystem : public System{
        public:
            ScriptSystem();
            virtual bool InitDerive() override;
            virtual bool AddEntity(EntityID entity) override;
            virtual void Update() override;
        private:
            std::vector<ECS::EntityID> entities;
            std::vector<ECS::Component::Script*> scripts;
    };
} // namespace ECS::System