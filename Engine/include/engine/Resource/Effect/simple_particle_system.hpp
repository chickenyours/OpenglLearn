#pragma once
#include "engine/Resource/Effect/particle_system.h"
#include "engine/Resource/RenderPipe/RenderItems/effect_render_item.h"
#include "engine/Resource/RenderPipe/renderPipe.h"
#include "engine/Resource/Effect/particle_type.h"
#include "engine/Environment/environment.h"


class SimpleParticleSystem : public IParticleSystem{
    private:
        float simulationTime = 0.0;
        float simulationDeltaTime = 0.0;
        float totalTime = 500.0;
        Graphic::SSBO particleSSBO;
        ResourceHandle<Resource::ShaderProgram> computeShaderProgram;
        ResourceHandle<Resource::Material> material;
        BaseParticleDrawItem drawItem;
    private: // 粒子属性
        int num;

    public:
        SimpleParticleSystem(): num(100){

        }
        virtual void Init() override{

            // 创建着色器
            auto factoryptr = ECS::Core::ResourceModule::ResourceManager::GetInctance().Get<Resource::ShaderProgramFactory>(
                        FromConfig<Resource::ShaderProgramFactory>("./RenderShader/particle_compute.json"));
            if(!factoryptr){
                LOG_ERROR("SimpleParticleSystem::Init", "factoryptr nullptr");
                return;
            }
            computeShaderProgram = factoryptr->GetShaderProgramInstance();
            if(!computeShaderProgram){
                LOG_ERROR("SimpleParticleSystem::Init", "computeShaderProgram nullptr");
                return;
            }

            // 生成粒子数组
            std::vector<SimpleParticle> particleArray;
            particleArray.reserve(num);
            for(int i = 0;i<num;i++){
                SimpleParticle temp;
                temp.position = glm::vec4(0.0,(float)i,0.0,1.0);
                particleArray.push_back(SimpleParticle{});
            }

            // 获取粒子材质
            material = ECS::Core::ResourceModule::ResourceManager::GetInctance().Get<Material>(
                    FromConfig<Material>("./materials/particle/sijiaoxin.json")
                );
            // material = ECS::Core::ResourceModule::ResourceManager::GetInctance().Get<Material>(
            //         FromConfig<Material>("./materials/particle/hh.json")
            //     );

            particleSSBO.Init(num, sizeof(SimpleParticle), static_cast<void*>(particleArray.data()));

            drawItem.material = this->material.get();
            drawItem.particleBuffer = &this->particleSSBO;
        }
        virtual void UpdateLogic() override {
            simulationDeltaTime = Environment::Environment::Instance().GetUpdateIntervalTime();
            simulationTime += simulationDeltaTime;
            while(simulationTime > totalTime){
                simulationTime -= totalTime;
            }
        }

        virtual void CollectComputeTasks(std::vector<ParticleComputeTask>& out) override {
            if (num == 0) return;
            if (simulationDeltaTime <= 0.0f) return;
            ParticleComputeTask item;
            item.params.deltaTime = simulationDeltaTime;
            item.params.simulationTime = simulationTime;
            item.computeShaderProgram = this->computeShaderProgram.get();
            item.particleBuffer = &particleSSBO;
            out.push_back(item);
        }

        const BaseParticleDrawItem& GetDrawItem(){
            return drawItem;
        }

        
};