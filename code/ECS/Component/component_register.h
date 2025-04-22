#pragma once

#include <vector>
#include "code/ECS/data_type.h"

#include "code/ECS/Component/component_shortage.h"


namespace ECS::Core{


class ComponentRegister {
    public:
        template<typename T>
        void AddComponent(EntityID id, const T& comp) {
            GetStorage<T>().Add(id, comp);
        }
    
        template<typename T>
        T& GetComponent(EntityID id) {
            return GetStorage<T>().Get(id);
        }
    
        template<typename T>
        bool HasComponent(EntityID id) {
            return GetStorage<T>().Has(id);
        }
    
        template<typename T>
        void RemoveComponent(EntityID id) {
            GetStorage<T>().Remove(id);
        }
    
        // 遍历组件实体列表（例如用于系统查询）
        template<typename T>
        const std::vector<EntityID>& View() {
            return GetStorage<T>().GetEntities();
        }
    
    private:
        template<typename T>
        ComponentStorage<T>& GetStorage() {
            static ComponentStorage<T> storage;
            return storage;
        }
};
        


}

