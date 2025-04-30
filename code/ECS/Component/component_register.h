#pragma once

#include <vector>
#include "code/ECS/data_type.h"

#include "code/ECS/Component/component_shortage.h"

#include "code/DebugTool/ConsoleHelp/color_log.h"

namespace ECS::Core{


class ComponentRegister {
    public:
        template<typename ComponentT>
        ComponentT* AddComponent(EntityID entity, const ComponentT& comp = ComponentT{}) {
            Check(entity);
            return GetStorage<ComponentT>().Add(entity, comp);
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
        template<typename ComponentT>
        ComponentStorage<ComponentT>& GetStorage() {
            static ComponentStorage<ComponentT> storage;
            return storage;
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

