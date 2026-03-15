#pragma once

#include <vector>
#include <iterator>
#include <utility>
#include <type_traits>

#include "engine/ECS/Component/component_shortage.h"
#include "engine/ToolAndAlgorithm/object.h"

namespace ECS::Core {



    
template<typename ComponentT>
class ComponentStorageView {
    static_assert(
        std::is_base_of<ECS::Component::Component<ComponentT>, ComponentT>::value,
        "ComponentT must derive from ECS::Component::Component<ComponentT>"
    );

public:
    using StorageT = ComponentStorage<ComponentT>;

    ComponentStorageView() = default;

    explicit ComponentStorageView(ObjectPtr<StorageT>& componentStorage) {
        SetOwner(componentStorage);
    }

    void SetOwner(ObjectPtr<StorageT>& componentStorage) {
        owner_ = componentStorage.GenWeakPtr();
    }

    // 你可以按需提供填充接口
    void Clear() { index_.clear(); }
    void Reserve(size_t n) { index_.reserve(n); }
    void PushHandle(const EntityComponentHandle& h) { index_.push_back(h); }

    size_t size() const { return index_.size(); }
    bool empty() const { return index_.empty(); }

    // =========================
    // 1) 迭代 ComponentT& ：for(auto& c : view)
    // =========================
    class iterator {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type        = ComponentT;
        using difference_type   = std::ptrdiff_t;
        using pointer           = ComponentT*;
        using reference         = ComponentT&;

        iterator() = default;

        iterator(ComponentStorageView* v, size_t i)
            : v_(v), i_(i) {
            // 缓存 owner（避免每次解引用都 Lock）
            if (v_) {
                ownerStrong_ = v_->owner_.Lock();
                ownerRaw_ = ownerStrong_ ? ownerStrong_.Get() : nullptr; // 你的 ObjectPtr 若不是 Get()，换成对应方法
            }
            SkipIfInvalid();
        }

        reference operator*() const {
            // 如果你希望“owner 可能中途销毁”也安全，那这里每次 Lock；否则用缓存 ownerRaw_
            auto* s = ownerRaw_;
            // 这里假设 handle.index 是有效的；如果你需要更强健，就判断 GetComponent 返回 nullptr
            return *s->GetComponent(v_->index_[i_]);
        }

        pointer operator->() const {
            auto* s = ownerRaw_;
            return s->GetComponent(v_->index_[i_]);
        }

        iterator& operator++() {
            ++i_;
            SkipIfInvalid();
            return *this;
        }

        iterator operator++(int) {
            iterator t = *this;
            ++(*this);
            return t;
        }

        friend bool operator==(const iterator& a, const iterator& b) {
            return a.v_ == b.v_ && a.i_ == b.i_;
        }
        friend bool operator!=(const iterator& a, const iterator& b) {
            return !(a == b);
        }

    private:
        void SkipIfInvalid() {
            // 如果 owner 不存在：直接变成 end
            if (!v_ || !ownerRaw_) {
                if (v_) i_ = v_->index_.size();
                return;
            }

            // 如果你希望“跳过无效 handle”，可以在这里循环判断 GetComponent 是否为 nullptr
            while (i_ < v_->index_.size()) {
                if (ownerRaw_->GetComponent(v_->index_[i_]) != nullptr) break;
                ++i_;
            }
        }

    private:
        ComponentStorageView* v_ = nullptr;
        size_t i_ = 0;

        ObjectPtr<StorageT> ownerStrong_{};
        StorageT* ownerRaw_ = nullptr;
    };

    class const_iterator {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type        = const ComponentT;
        using difference_type   = std::ptrdiff_t;
        using pointer           = const ComponentT*;
        using reference         = const ComponentT&;

        const_iterator() = default;

        const_iterator(const ComponentStorageView* v, size_t i)
            : v_(v), i_(i) {
            if (v_) {
                ownerStrong_ = v_->owner_.Lock();
                ownerRaw_ = ownerStrong_ ? ownerStrong_.Get() : nullptr;
            }
            SkipIfInvalid();
        }

        reference operator*() const {
            auto* s = ownerRaw_;
            return *s->GetComponent(v_->index_[i_]);
        }

        pointer operator->() const {
            auto* s = ownerRaw_;
            return s->GetComponent(v_->index_[i_]);
        }

        const_iterator& operator++() {
            ++i_;
            SkipIfInvalid();
            return *this;
        }

        const_iterator operator++(int) {
            const_iterator t = *this;
            ++(*this);
            return t;
        }

        friend bool operator==(const const_iterator& a, const const_iterator& b) {
            return a.v_ == b.v_ && a.i_ == b.i_;
        }
        friend bool operator!=(const const_iterator& a, const const_iterator& b) {
            return !(a == b);
        }

    private:
        void SkipIfInvalid() {
            if (!v_ || !ownerRaw_) {
                if (v_) i_ = v_->index_.size();
                return;
            }
            while (i_ < v_->index_.size()) {
                if (ownerRaw_->GetComponent(v_->index_[i_]) != nullptr) break;
                ++i_;
            }
        }

    private:
        const ComponentStorageView* v_ = nullptr;
        size_t i_ = 0;

        ObjectPtr<StorageT> ownerStrong_{};
        const StorageT* ownerRaw_ = nullptr;
    };

    iterator begin() { return iterator(this, 0); }
    iterator end()   { return iterator(this, index_.size()); }

    const_iterator begin() const { return const_iterator(this, 0); }
    const_iterator end()   const { return const_iterator(this, index_.size()); }
    const_iterator cbegin() const { return begin(); }
    const_iterator cend()   const { return end(); }

    // =========================
    // 2) 迭代 (EntityID, ComponentT&) ：for (auto [e, c] : view.view())
    // =========================
    class EntityView {
    public:
        explicit EntityView(ComponentStorageView& v) : v_(v) {}

        class iterator {
        public:
            using iterator_category = std::forward_iterator_tag;
            using difference_type   = std::ptrdiff_t;

            iterator(ComponentStorageView* v, size_t i)
                : v_(v), i_(i) {
                if (v_) {
                    ownerStrong_ = v_->owner_.Lock();
                    ownerRaw_ = ownerStrong_ ? ownerStrong_.Get() : nullptr;
                }
                SkipIfInvalid();
            }

            std::pair<EntityID, ComponentT&> operator*() const {
                auto& h = v_->index_[i_];
                return { ownerRaw_->GetEntityID(h), *ownerRaw_->GetComponent(h) };
            }

            iterator& operator++() { ++i_; SkipIfInvalid(); return *this; }
            friend bool operator==(const iterator& a, const iterator& b) { return a.v_ == b.v_ && a.i_ == b.i_; }
            friend bool operator!=(const iterator& a, const iterator& b) { return !(a == b); }

        private:
            void SkipIfInvalid() {
                if (!v_ || !ownerRaw_) {
                    if (v_) i_ = v_->index_.size();
                    return;
                }
                while (i_ < v_->index_.size()) {
                    if (ownerRaw_->GetComponent(v_->index_[i_]) != nullptr) break;
                    ++i_;
                }
            }

        private:
            ComponentStorageView* v_ = nullptr;
            size_t i_ = 0;

            ObjectPtr<StorageT> ownerStrong_{};
            StorageT* ownerRaw_ = nullptr;
        };

        iterator begin() { return iterator(&v_, 0); }
        iterator end()   { return iterator(&v_, v_.index_.size()); }

    private:
        ComponentStorageView& v_;
    };

    EntityView view() { return EntityView(*this); }

private:
    std::vector<EntityComponentHandle> index_;
    ObjectWeakPtr<StorageT> owner_;
};

} // namespace ECS::Core
