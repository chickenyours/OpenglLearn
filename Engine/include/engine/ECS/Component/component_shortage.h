#pragma once

#include <vector>
#include <unordered_map>
#include <type_traits>
#include <utility>
#include <cstddef>
#include <iterator>
#include <functional>
#include "engine/ECS/data_type.h"
#include "engine/ECS/Component/component.h"
#include "engine/ToolAndAlgorithm/Iterator/seq_iterator.h"

namespace ECS::Core {

class ComponentObjectWeakPtr{
    private:
        unsigned int version = 0;
        bool isGone_ = false;
        
    
};

struct ComponentHandle{
    public:
        
        uint32_t index = 0;
        uint32_t currentVersion = 0;
        EntityID entityID = 0;
        bool Has(){
            return currentVersion != 0;
        }
};

struct ComponentInfo{
    public:
        size_t version = 0;       // generation
};

class IComponentStorageBase {
public:
    virtual ~IComponentStorageBase() = default;
};

template<typename ComponentT>
class ComponentStorageSeqCursor : public ISeqCursor<ComponentT> {
    
};

template<typename ComponentT>
class ComponentStorage final : public IComponentStorageBase {
    static_assert(
        std::is_base_of<ECS::Component::Component<ComponentT>, ComponentT>::value,
        "ComponentT must derive from ECS::Component::Component<ComponentT>"
    );

public:
    using value_type = ComponentT;
    using ComponentInitFunction = std::function<void(ComponentT&)>;

    ComponentStorage() = default;
    ~ComponentStorage() override = default;

    ComponentHandle Add(EntityID id, const ComponentT* comp = nullptr,ComponentInitFunction function = nullptr) {
        if (Has(id)) return nullptr;
        if(comp){
            denseComponents.push_back(*comp);
        }
        else{
            denseComponents.emplace();
        }
        denseEntities.push_back(id);
        size_t backindex = denseComponents.size() - 1;
        sparseMap[id] = backindex;
        if(denseComponents.size() > denseVersion.size()){
            denseVersion.push_back({1});
        }
        else{
            denseVersion[backindex].version++;
        }
        if(function){
            function(denseComponents[backindex]);
        }
        return GetHandle(id);
    }

    template<typename... Args>
    ComponentT* Emplace(EntityID id, Args&&... args) {
        if (Has(id)) return nullptr;
        denseComponents.emplace_back(std::forward<Args>(args)...);
        denseEntities.push_back(id);
        size_t backindex = denseComponents.size() - 1;
        sparseMap[id] = backindex;
        if(denseComponents.size() > denseVersion.size()){
            denseVersion.push_back({1});
        }
        else{
            denseVersion[backindex].version++;
        }
        return &denseComponents[backindex];
    }

    ComponentHandle GetHandle(EntityID id){
        auto it = sparseMap.find(id);
        if (it == sparseMap.end()) return EntityComponentHandle();
        size_t idx = it->second;
        return EntityComponentHandle{
            denseVersion[idx].version,
            idx,
            id
        };
    }

    void Remove(EntityID id) {
        if (!Has(id)) return;
        size_t idx  = sparseMap[id];
        size_t last = denseComponents.size() - 1;

        std::swap(denseComponents[idx], denseComponents[last]);
        std::swap(denseEntities[idx],  denseEntities[last]);

        // swap 后 idx 位置现在是“原 last 的 entity”
        sparseMap[denseEntities[idx]] = idx;

        denseComponents.pop_back();
        denseEntities.pop_back();
        sparseMap.erase(id);
    }

    ComponentT* Get(EntityID entity, EntityComponentHandle* outHandle = nullptr) const{
        auto it = sparseMap.find(entity);
        if (it != sparseMap.end()) {
            if(outHandle){
                outHandle->index = it->second;
            }
            return &denseComponents[it->second];
        }
        return nullptr;
    }

    ComponentT* GetComponent(const EntityComponentHandle& handle) const{
        // 句柄合法性检测
        if(handle.index >= denseComponents.size() || denseVersion[handle.index].version != handle.currentVersion){
            // 不合法,执行更新措施
            EntityID& handleEntity = handle.entityID;
            return Get(handleEntity);
        }
        // 合法,直接返回指针
        return &denseComponents[handle.index];
    }

    EntityID GetEntityID(const EntityComponentHandle& handle) const{

        return denseEntities[handle.index];
    }

    const ComponentT* Get(EntityID entity) const {
        auto it = sparseMap.find(entity);
        if (it != sparseMap.end()) {
            return &denseComponents[it->second];
        }
        return nullptr;
    }

    bool Has(EntityID id) const {
        return sparseMap.count(id) != 0;
    }

    const std::vector<EntityID>& GetEntities() const { return denseEntities; }
    const std::vector<ComponentT>& GetComponents() const { return denseComponents; }

    size_t size() const { return denseComponents.size(); }

    // =========================
    // 1) 直接迭代组件：for(auto& c : storage)
    // =========================
    class iterator {
    public:
        using iterator_category = std::random_access_iterator_tag;
        using value_type        = ComponentT;
        using difference_type   = std::ptrdiff_t;
        using pointer           = ComponentT*;
        using reference         = ComponentT&;

        iterator() = default;
        iterator(ComponentT* p) : p_(p) {}

        reference operator*()  const { return *p_; }
        pointer   operator->() const { return  p_; }

        iterator& operator++() { ++p_; return *this; }
        iterator  operator++(int) { iterator t=*this; ++(*this); return t; }
        iterator& operator--() { --p_; return *this; }
        iterator  operator--(int) { iterator t=*this; --(*this); return t; }

        iterator& operator+=(difference_type n) { p_ += n; return *this; }
        iterator& operator-=(difference_type n) { p_ -= n; return *this; }
        friend iterator operator+(iterator it, difference_type n) { it += n; return it; }
        friend iterator operator-(iterator it, difference_type n) { it -= n; return it; }
        friend difference_type operator-(const iterator& a, const iterator& b) { return a.p_ - b.p_; }

        friend bool operator==(const iterator& a, const iterator& b) { return a.p_ == b.p_; }
        friend bool operator!=(const iterator& a, const iterator& b) { return a.p_ != b.p_; }
        friend bool operator< (const iterator& a, const iterator& b) { return a.p_ <  b.p_; }
        friend bool operator<=(const iterator& a, const iterator& b) { return a.p_ <= b.p_; }
        friend bool operator> (const iterator& a, const iterator& b) { return a.p_ >  b.p_; }
        friend bool operator>=(const iterator& a, const iterator& b) { return a.p_ >= b.p_; }

    private:
        ComponentT* p_ = nullptr;
    };

    class const_iterator {
    public:
        using iterator_category = std::random_access_iterator_tag;
        using value_type        = const ComponentT;
        using difference_type   = std::ptrdiff_t;
        using pointer           = const ComponentT*;
        using reference         = const ComponentT&;

        const_iterator() = default;
        const_iterator(const ComponentT* p) : p_(p) {}

        reference operator*()  const { return *p_; }
        pointer   operator->() const { return  p_; }

        const_iterator& operator++() { ++p_; return *this; }
        const_iterator  operator++(int) { const_iterator t=*this; ++(*this); return t; }
        const_iterator& operator--() { --p_; return *this; }
        const_iterator  operator--(int) { const_iterator t=*this; --(*this); return t; }

        friend bool operator==(const const_iterator& a, const const_iterator& b) { return a.p_ == b.p_; }
        friend bool operator!=(const const_iterator& a, const const_iterator& b) { return a.p_ != b.p_; }

    private:
        const ComponentT* p_ = nullptr;
    };

    iterator begin() { return iterator(denseComponents.data()); }
    iterator end()   { return iterator(denseComponents.data() + denseComponents.size()); }

    const_iterator begin() const { return const_iterator(denseComponents.data()); }
    const_iterator end()   const { return const_iterator(denseComponents.data() + denseComponents.size()); }

    const_iterator cbegin() const { return begin(); }
    const_iterator cend()   const { return end(); }

    // =========================
    // 2) 迭代 (EntityID, ComponentT&) ：for (auto [e, c] : storage.view())
    // =========================
    class View {
    public:
        explicit View(ComponentStorage& s) : s_(s) {}

        class iterator {
        public:
            using iterator_category = std::forward_iterator_tag;
            using difference_type   = std::ptrdiff_t;

            iterator(ComponentStorage* s, size_t i) : s_(s), i_(i) {}

            // 返回 pair<EntityID, ComponentT&>（注意第二个是引用）
            std::pair<EntityID, ComponentT&> operator*() const {
                return { s_->denseEntities[i_], s_->denseComponents[i_] };
            }

            iterator& operator++() { ++i_; return *this; }
            friend bool operator==(const iterator& a, const iterator& b) { return a.i_ == b.i_; }
            friend bool operator!=(const iterator& a, const iterator& b) { return a.i_ != b.i_; }

        private:
            ComponentStorage* s_ = nullptr;
            size_t i_ = 0;
        };

        iterator begin() { return iterator(&s_, 0); }
        iterator end()   { return iterator(&s_, s_.denseComponents.size()); }

    private:
        ComponentStorage& s_;
    };

    View view() { return View(*this); }

private:
    std::vector<ComponentT> denseComponents;
    std::vector<EntityID> denseEntities;
    std::vector<ComponentInfo> denseVersion;
    std::unordered_map<EntityID, size_t> sparseMap;
};

} // namespace ECS::Core
