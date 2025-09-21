#pragma once

#include <string>

#include "code/ECS/data_type.h"

#include "code/ECS/Component/component_register.h"

namespace ECS{
    class Scene;
}

namespace ECS::System{
    class System{
        public:
            void Init(Scene* scene){
                if(scene){
                    scene_ = scene;
                    if (!InitDerive()) {
                        LOG_ERROR("System:" + systemName_, "InitDerive() failed");
                    }
                }
                else{
                    LOG_ERROR("System:"+systemName_,"scene is nullptr");
                }
            }
            void AddEntities(const std::vector<EntityID>& entities){
                if(scene_){
                    for(EntityID entity : entities){
                        AddEntity(entity);
                    }
                }
                else{
                    LOG_ERROR("System:"+systemName_,"reg is nullptr");
                }
            }
            virtual void Update() = 0;
            virtual ~System() = default;
        protected:
            virtual bool AddEntity(EntityID AddEntity) = 0;
            virtual bool InitDerive() = 0;
            inline System(std::string systemName) : systemName_(systemName){}
            std::string systemName_;
            // ECS::Core::ComponentRegister* reg_;
            Scene* scene_ = nullptr;
    };
}

#include "code/Scene/scene.h"

// template<typename... Components>
// class SystemFilter : public ISystemFilter {
// public:
//     using ComponentTuple = std::tuple<Components*...>;

//     void SetComponentRegister(ComponentRegister* reg) {
//         reg_ = reg;
//     }

//     void Prepare(const std::vector<EntityID>& allEntities) override {
//         matched_.clear();
//         for (ECS::EntityID id : allEntities) {
//             if (HasAllComponents(id)) {
//                 matched_.emplace_back(GetAllComponents(id));
//             }
//         }
//     }

//     const std::vector<ComponentTuple>& GetMatched() const { return matched_; }

// private:
//     bool HasAllComponents(EntityID id) const {
//         return (reg_->HasComponent<Components>(id) && ...);
//     }

//     ComponentTuple GetAllComponents(EntityID id) const {
//         return std::make_tuple(reg_->GetComponent<Components>(id)...);
//     }

//     ComponentRegister* reg_;
//     std::vector<ComponentTuple> matched_;
// };

// namespace ECS::System {

// template<typename... Comps>
// class SystemWithFilter : public System {
// public:
//     using ComponentTuple = std::tuple<Comps*...>;

//     SystemWithFilter(const std::string& name) : System(name) {}

//     // 外部调用一次，传入 Entity 列表
//     void SetAllEntities(const std::vector<EntityID>& all) {
//         allEntities_ = all;
//     }

//     // 外部调用一次，传入组件注册器
//     void InitFilter(ECS::Core::ComponentRegister* reg) {
//         this->reg_ = reg;
//         filter_.SetComponentRegister(reg_);
//     }

//     // 每帧 Update 前调用，执行筛选
//     void PrepareFilter() {
//         filter_.Prepare(allEntities_);
//     }

//     virtual void Update() = 0;

// protected:
//     // 子类调用获取筛选后的组件组合
//     const std::vector<ComponentTuple>& GetFiltered() const {
//         return filter_.GetMatched();
//     }

// private:
//     std::vector<EntityID> allEntities_;
//     SystemFilter<Comps...> filter_;
// };




