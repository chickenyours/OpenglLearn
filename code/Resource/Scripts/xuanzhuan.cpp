#include "code/Script/script_interface.h"

#include "code/Environment/environment.h"

#include "code/ECS/Component/Transform/transform.h"
#include "code/ECS/Component/Render/mesh_renderer.h"

class XuanZhuang : public IScript{
    public:

        virtual void OnStart(ECS::Scene* scene, ECS::EntityID entity) override {
            auto ren = scene->registry_->GetComponent<ECS::Component::MeshRenderer>(entity);
            if(ren){
                ren->uboData.values[0] = glm::vec4(hhh,1.0);
            }
            std::cout << hhhh << std::endl;
        }
        virtual void OnUpdate(ECS::Scene* scene, ECS::EntityID entity) override {
            auto transform = scene->registry_->GetComponent<ECS::Component::Transform>(entity); // 开销很高,尽量使用缓存
            if(transform){
                transform->rotation.y += 50.0 * Environment::Environment::Instance().GetUpdateIntervalTime();
                transform->rotation.z += 33.0 * Environment::Environment::Instance().GetUpdateIntervalTime();
            }
           
        }
        /// @meta clamp(0.0,1.0) invisible
        float y = 0.0;
        glm::vec3 hhh = glm::vec3(0.0);
        std::string hhhh;
};

REGISTER_CLASS_PLUGIN(IScript,XuanZhuang);

