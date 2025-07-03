#pragma once

#include <typeindex>
#include <type_traits>
#include <vector>
#include <unordered_map>
#include "code/ECS/data_type.h"

#include "code/ECS/Component/component_shortage.h"

#include "code/DebugTool/ConsoleHelp/color_log.h"


namespace ECS::Core{


// class ComponentRegister {
//     public:
//         static ComponentRegister& Instance() {
//             static ComponentRegister instance;
//             return instance;
//         }

//         template<typename ComponentT>
//         ComponentT* AddComponent(EntityID entity, const ComponentT& comp) {
//             Check(entity);
//             return GetStorage<ComponentT>().Add(entity, comp);
//         }

//         template<typename ComponentT>
//         ComponentT* AddComponent(EntityID entity) {
//             Check(entity);
//             return GetStorage<ComponentT>().Add(entity);
//         }
    
//         //如果此处没有收入entity的组件(没有添加过),就会返回nullptr
//         template<typename ComponentT>
//         ComponentT* GetComponent(EntityID entity) {
//             Check(entity);
//             return GetStorage<ComponentT>().Get(entity);
//         }
    
//         template<typename ComponentT>
//         bool HasComponent(EntityID entity) {
//             Check(entity);
//             return GetStorage<ComponentT>().Has(entity);
//         }
    
//         template<typename ComponentT>
//         void RemoveComponent(EntityID entity) {
//             Check(entity);
//             GetStorage<ComponentT>().Remove(entity);
//         }
    
//         // 遍历组件实体列表（例如用于系统查询）
//         template<typename ComponentT>
//         const std::vector<EntityID>& View() {
//             return GetStorage<ComponentT>().GetEntities();
//         }

        
    
//     private:
//         template<typename ComponentT>
//         ComponentStorage<ComponentT>& GetStorage() {
//             static ComponentStorage<ComponentT> storage;
//             return storage;
//         }
//         bool Check(EntityID id){
//             if(id == INVALID_ENTITY){
//                 Log::Error("ComponentRegister","Submit a INVALID_ENTITY");
//                 return true;
//             }
//             return false;
//         }
// };
        
class ComponentRegister {
    public:
       

        template<typename ComponentT>
        ComponentT* AddComponent(EntityID entity, const ComponentT& comp) {
            Check(entity);
            return GetStorage<ComponentT>().Add(entity, comp);
        }

        template<typename ComponentT>
        ComponentT* AddComponent(EntityID entity) {
            Check(entity);
            return GetStorage<ComponentT>().Add(entity);
        }
    
        //如果此处没有收入entity的组件(没有添加过),就会返回nullptr
        template<typename ComponentT>
        ComponentT* GetComponent(EntityID entity) {
            Check(entity);
            return GetStorage<ComponentT>().Get(entity);
        }
    
        template<typename ComponentT>
        bool HasComponent(EntityID entity) {
            Check(entity);
            return GetStorage<ComponentT>().Has(entity);
        }
    
        template<typename ComponentT>
        void RemoveComponent(EntityID entity) {
            Check(entity);
            GetStorage<ComponentT>().Remove(entity);
        }
    
        // 遍历组件实体列表（例如用于系统查询）
        template<typename ComponentT>
        const std::vector<EntityID>& View() {
            return GetStorage<ComponentT>().GetEntities();
        }

        
    
    private:
        std::unordered_map<std::type_index,std::unique_ptr<IComponentStorageBase>> map_;

        template<typename ComponentT>
        ComponentStorage<ComponentT>& GetStorage() {
            static_assert(std::is_base_of<IComponentStorageBase, ComponentStorage<ComponentT>>::value,
                  "ComponentStorage<ComponentT> must derive from IComponentStorageBase");

            std::type_index typeIdx(typeid(ComponentT));
            auto it = map_.find(typeIdx);
            if (it == map_.end()) {
                auto storage = new ComponentStorage<ComponentT>();
                map_[typeIdx] = std::unique_ptr<IComponentStorageBase>(storage);
                return *storage;
            }
            return *static_cast<ComponentStorage<ComponentT>*>(map_[typeIdx].get());
        }
        bool Check(EntityID id){
            if(id == INVALID_ENTITY){
                Log::Error("ComponentRegister","Submit a INVALID_ENTITY");
                return true;
            }
            return false;
        }
};


}

