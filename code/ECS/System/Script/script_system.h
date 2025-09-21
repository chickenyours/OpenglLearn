#pragma once

#include <memory>
#include <unordered_map>
#include <unordered_set>

#include "code/ECS/System/system.h"
#include "code/ECS/Component/Script/scirpt.h"

#include "code/ECS/DataType/script_collision_event.h"
#include "code/ToolAndAlgorithm/DateType/resizable_ring_queue.h"

namespace ECS::System{
    class ScriptSystem : public System{
        public:
            ScriptSystem();
            virtual bool InitDerive() override;
            virtual bool AddEntity(EntityID entity) override;
            virtual void Update() override;
            ResizableRingQueue<DataType::ScriptCollsionEvent> queue;
        private:
            std::vector<ECS::EntityID> entities_;
            std::vector<ECS::Component::Script*> scripts_;
            std::unordered_map<ECS::EntityID,ECS::Component::Script*> map_;
            std::unordered_set<uint64_t> cacheA_;
            std::unordered_set<uint64_t> cacheB_;
            std::unordered_set<uint64_t> seen_; // 本帧去重
            bool flip_ = true;

           
            
    };
} // namespace ECS::System