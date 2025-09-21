#include "code/Script/script_interface.h"

#include "code/ECS/Component/Render/mesh_renderer.h"

class XianShi : public IScript{
    public:

        virtual void OnStart(ECS::Scene* scene, ECS::EntityID entity) override {
            ren = scene->registry_->GetComponent<ECS::Component::MeshRenderer>(entity);
        }
        virtual void OnUpdate(ECS::Scene* scene, ECS::EntityID entity) override {
            if(ren){
                glm::vec3 color = glm::vec3(1.0 - 0.4 * (float)count,1.0,1.0);
                ren->uboData.values[0] = glm::vec4(color,1.0);
            }
        }
        virtual void OnCollisionStart(ECS::Scene* scene, ECS::EntityID me, ECS::EntityID you) override {
            count++;
        }
        virtual void OnCollisionExit(ECS::Scene* scene, ECS::EntityID me, ECS::EntityID you) override {
            count--;
        }
        ECS::Component::MeshRenderer* ren = nullptr;
        int count = 0;
};

REGISTER_CLASS_PLUGIN(IScript,XianShi);

