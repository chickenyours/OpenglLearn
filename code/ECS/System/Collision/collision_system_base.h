#pragma once

#include "code/ECS/System/system.h"

#include "code/ECS/Component/Transform/transform.h"

#include "code/ECS/DataType/script_collision_event.h"
#include "code/ToolAndAlgorithm/DateType/resizable_ring_queue.h"

namespace ECS::System{
    template <typename T>
    class CollisionSystemBase : public System{
        public:
            CollisionSystemBase() : System("CollisionSystemBase") {

            }
            virtual void Update() override {
                if(!sub_) return;
                for(int i = 0; i < entities_.size(); i++){
                    for(int j = i + 1; j < entities_.size(); j++){
                        if(Compare(*transforms_[i],*colComs_[i],*transforms_[j],*colComs_[j])){
                            sub_->push({entities_[i],entities_[j],colComs_[i],colComs_[j]});
                        }
                    }
                }
            }
            void SetEventSub(ResizableRingQueue<ECS::DataType::ScriptCollsionEvent>* sub){
                sub_ = sub;
            }
        protected:
            virtual bool Compare(ECS::Component::Transform&,
                                T&,
                                ECS::Component::Transform&,
                                T&) = 0;
            virtual bool AddEntity(EntityID entity) override {
                if(set_.count(entity)) return true;
                auto transform = scene_->registry_->GetComponent<ECS::Component::Transform>(entity); 
                T* colCom = scene_->registry_->GetComponent<T>(entity); 
                if(transform && colCom){
                    entities_.push_back(entity);
                    colComs_.push_back(colCom);
                    transforms_.push_back(transform);
                    set_.insert(entity);
                }
                return true;
            }
            virtual bool InitDerive() override {
                return true;
            }
        private:
            std::vector<EntityID> entities_;
            std::vector<T*> colComs_;
            std::vector<ECS::Component::Transform*> transforms_;
            std::unordered_set<EntityID> set_;
            ResizableRingQueue<ECS::DataType::ScriptCollsionEvent>* sub_ = nullptr;
    };
}