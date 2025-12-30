#pragma once
#include "engine/Resource/Effect/particle_system.h"
#include "engine/Resource/RenderPipe/RenderItems/effect_render_item.h"
#include "engine/Resource/RenderPipe/renderPipe.h"
#include "engine/Resource/Effect/particle_type.h"
#include "engine/Environment/environment.h"


class SimpleParticleSystem : public IParticleSystem{
    private:
        float simulationTime = 0.0;
        Graphic::SSBO particleSSBO;
        ResourceHandle<Resource::ShaderProgram> computeShaderProgram;
        Render::RenderPipe* proxy = nullptr;
    private: // 粒子属性
        int num;

    public:
        SimpleParticleSystem(): num(30){

        }
        virtual void Init() override{

            // 创建着色器
            computeShaderProgram = ECS::Core::ResourceModule::ResourceManager::GetInctance().Get<Resource::ShaderProgramFactory>(
                        FromConfig<Resource::ShaderProgramFactory>("./RenderShader/particle.json"))->GetShaderProgramInstance();
            
            // 生成粒子数组
            std::vector<SimpleParticle> particleArray;
            particleArray.resize(num);
            particleSSBO.Init(num, sizeof(SimpleParticle), static_cast<void*>(particleArray.data()));

        }
        virtual void Update() override {
            ParticleRenderProxy item;
            item.params.deltaTime = Environment::Environment::Instance().GetUpdateIntervalTime();
            simulationTime += item.params.deltaTime;
            item.params.simulationTime = simulationTime;
            item.computeShaderProgram = this->computeShaderProgram.get();
            item.particleBuffer = &particleSSBO;
            proxy->AddParticalProcessProxyItem(&item);
        }
        virtual void SetRenderPipeProxy(Render::RenderPipe* proxy){
            this->proxy = proxy;
        }
};