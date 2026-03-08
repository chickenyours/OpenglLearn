#pragma once

#include "engine/ECS/Entity/entity.h"

namespace ECS{
    class IScript{
        public:
            IScript() = default;
            virtual ~IScript() = default;
            virtual void OnInit(EntityHandle entityHandle){ entityHandle_ = entityHandle; Init(); }
            // 可见时调用一次
            virtual void Awake(){}
            // 进入调度时调用一次
            virtual void BeginPlay(){}
            // 进入调度时
            virtual void Tick(float dt){}
        protected:
            // 实体组件完全创建完成时调用一次初始化
            virtual void Init(){}
            const EntityHandle& GetEntityHandle() const { return entityHandle_; }
        private:
            EntityHandle entityHandle_;
    };
    
}