#include "code/Script/script_interface.h"

#include "code/Environment/environment.h"

#include "code/ECS/Component/Transform/transform.h"

class XuanZhuang : public IScript{
    public:

        virtual void OnStart(ECS::Scene* scene, ECS::EntityID entity) override {
            auto transform = scene->registry_->GetComponent<ECS::Component::Transform>(entity);
            if(transform){
                trans = transform;
            }
        }
        virtual void OnUpdate(ECS::Scene* scene, ECS::EntityID entity) override {
            // auto transform = scene->registry_->GetComponent<ECS::Component::Transform>(entity);
            if(trans){
                trans->rotation.y += 50.0 * Environment::Environment::Instance().GetUpdateIntervalTime();
                trans->rotation.z += 33.0 * Environment::Environment::Instance().GetUpdateIntervalTime();
            }
            // if(transform)
        }
        float x = 0.0;
        ECS::Component::Transform* trans;
};

REGISTER_CLASS_PLUGIN(IScript,XuanZhuang);

