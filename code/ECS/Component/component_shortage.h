#pragma once

#include <vector>
#include <unordered_map>

#include "code/ECS/data_type.h"

namespace ECS::Core{

class IComponentStorageBase{
    public:
        virtual ~IComponentStorageBase() = default;
};

template<typename ComponentT>
class ComponentStorage final : public IComponentStorageBase {
    public:
        virtual ~ComponentStorage() = default;
        ComponentStorage() {
            denseComponents.reserve(4096);  // 足够大的默认容量,目前能足够避免扩容带来的外部悬空指针问题
            denseEntities.reserve(4096);    // 未来可以编写 Stable Vector
        }
        // 添加成功,则返回容器中的组件地址,如果已存在则会返回nullptr
        ComponentT* Add(EntityID id, const ComponentT& comp) {
            if (Has(id)) return nullptr;
            denseComponents.push_back(comp);
            denseEntities.push_back(id);
            sparseMap[id] = denseComponents.size() - 1;
            return &denseComponents.back();
        }

        ComponentT* Add(EntityID id) {
            if (Has(id)) return nullptr;
            denseComponents.emplace_back();
            denseEntities.push_back(id);
            sparseMap[id] = denseComponents.size() - 1;
            return &denseComponents.back();
        }

        // 添加成功,则返回容器中的组件地址,如果已存在则会返回nullptr
        template<typename... Args>
        ComponentT* Emplace(EntityID id, Args&&... args) {
            if (Has(id)) return nullptr;
            denseComponents.emplace_back(std::forward<Args>(args)...);
            denseEntities.push_back(id);
            sparseMap[id] = denseComponents.size() - 1;
            return &denseComponents.back();
        }

        void Remove(EntityID id) {
            if (!Has(id)) return;
            size_t idx = sparseMap[id];
            size_t last = denseComponents.size() - 1;

            std::swap(denseComponents[idx], denseComponents[last]);
            std::swap(denseEntities[idx], denseEntities[last]);

            sparseMap[denseEntities[idx]] = idx;

            denseComponents.pop_back();
            denseEntities.pop_back();
            sparseMap.erase(id);
        }

        // 如果entity上没有组件,则返回空指针
        ComponentT* Get(EntityID entity) {
            auto it = sparseMap.find(entity);
            if(it != sparseMap.end()){
                return &denseComponents[it->second];
            }
            return nullptr;
        }

        bool Has(EntityID id) const {
            return sparseMap.count(id);
        }

        const std::vector<EntityID>& GetEntities() const {
            return denseEntities;
        }

    private:
        std::vector<ComponentT> denseComponents;
        std::vector<EntityID> denseEntities;
        std::unordered_map<EntityID, size_t> sparseMap;
};

}