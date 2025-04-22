#pragma once


template<typename T>
class ComponentStorage {
public:
    void Add(EntityID id, const T& comp) {
        if (Has(id)) return;
        denseComponents.push_back(comp);
        denseEntities.push_back(id);
        sparseMap[id] = denseComponents.size() - 1;
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

    T& Get(EntityID id) {
        return denseComponents[sparseMap.at(id)];
    }

    bool Has(EntityID id) const {
        return sparseMap.count(id);
    }

    const std::vector<EntityID>& GetEntities() const {
        return denseEntities;
    }

private:
    std::vector<T> denseComponents;
    std::vector<EntityID> denseEntities;
    std::unordered_map<EntityID, size_t> sparseMap;
};
