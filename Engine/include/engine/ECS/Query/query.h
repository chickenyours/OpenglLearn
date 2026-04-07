#pragma once

#include <tuple>
#include <type_traits>
#include <vector>
#include <cstddef>
#include <utility>

#include "engine/ECS/ArchType/archtype_description.h"
#include "engine/ECS/ArchType/archtype_instance.h"

namespace ECS::Core{

    template<typename... Ts>
    struct AnyOf{};

    namespace Detail{

        template<typename T>
        bool HasComponent(ArchType* archType){
            if(archType == nullptr || !archType->Check() || archType->GetDescription() == nullptr){
                return false;
            }

            size_t index = 0;
            return archType->GetDescription()->TryGetComponentArray<T>(index);
        }

        template<typename... Args>
        bool HasAll(ArchType* archType){
            return (... && HasComponent<Args>(archType));
        }

        template<typename... Args>
        bool HasAny(ArchType* archType){
            if constexpr(sizeof...(Args) == 0){
                return false;
            }else{
                return (... || HasComponent<Args>(archType));
            }
        }

        template<typename... Args>
        bool HasNone(ArchType* archType){
            return (... && (!HasComponent<Args>(archType)));
        }

        template<typename T>
        T* GetComponentPtrAt(ArchType* archType, size_t baseIndex){
            if(archType == nullptr){
                return nullptr;
            }

            auto* array = archType->TryCastActiveComponentArray<T>();
            if(array == nullptr){
                return nullptr;
            }

            return &((*array)[baseIndex]);
        }

        template<typename... Ts>
        struct TypeList{};

        template<typename TListA, typename TListB>
        struct TypeListCat;

        template<typename... A, typename... B>
        struct TypeListCat<TypeList<A...>, TypeList<B...>>{
            using Type = TypeList<A..., B...>;
        };

        template<typename T, typename... Ts>
        struct IndexOfType;

        template<typename T, typename First, typename... Rest>
        struct IndexOfType<T, First, Rest...>{
            static constexpr size_t value =
                std::is_same_v<T, First> ? 0 : 1 + IndexOfType<T, Rest...>::value;
        };

        template<typename T>
        struct IndexOfType<T>{
            static_assert(!std::is_same_v<T, T>, "Type not found in AnyOf.");
        };

        template<size_t I, typename... Ts>
        struct TypeAtIndex;

        template<typename First, typename... Rest>
        struct TypeAtIndex<0, First, Rest...>{
            using Type = First;
        };

        template<size_t I, typename First, typename... Rest>
        struct TypeAtIndex<I, First, Rest...>{
            using Type = typename TypeAtIndex<I - 1, Rest...>::Type;
        };

        template<typename T>
        struct IsAnyOf : std::false_type{};

        template<typename... Ts>
        struct IsAnyOf<AnyOf<Ts...>> : std::true_type{};

        template<typename T>
        inline constexpr bool IsAnyOfV = IsAnyOf<T>::value;

    } // namespace Detail


    template<typename... Ts>
    class AnyOfView{
    public:
        static constexpr size_t npos = static_cast<size_t>(-1);

        enum class Match : size_t{
            None = npos
        };

        AnyOfView() = default;

        template<typename T>
        void Set(T* ptr){
            ptr_ = static_cast<void*>(ptr);
            selectedIndex_ = TypeIndex<T>();
        }

        void Reset(){
            ptr_ = nullptr;
            selectedIndex_ = npos;
        }

        bool Valid() const{
            return ptr_ != nullptr && selectedIndex_ != npos;
        }

        explicit operator bool() const{
            return Valid();
        }

        size_t Index() const{
            return selectedIndex_;
        }

        Match Selected() const{
            return Valid() ? static_cast<Match>(selectedIndex_) : Match::None;
        }

        template<typename T>
        bool Is() const{
            return selectedIndex_ == TypeIndex<T>();
        }

        template<typename T>
        T* Get() const{
            if(!Is<T>()){
                return nullptr;
            }
            return static_cast<T*>(ptr_);
        }

        template<typename T>
        static constexpr size_t TypeIndex(){
            return Detail::IndexOfType<T, Ts...>::value;
        }

        template<size_t I>
        using TypeAt = typename Detail::TypeAtIndex<I, Ts...>::Type;

    private:
        void* ptr_ = nullptr;
        size_t selectedIndex_ = npos;
    };


    namespace Detail{

        template<typename T>
        struct QueryItemTraits{
            using StorageType = T*;

            static bool Check(ArchType* archType){
                return HasComponent<T>(archType);
            }

            static StorageType Build(ArchType* archType, size_t beginIndex){
                return GetComponentPtrAt<T>(archType, beginIndex);
            }
        };

        template<typename... Ts>
        struct QueryItemTraits<AnyOf<Ts...>>{
            using StorageType = AnyOfView<Ts...>;

            static bool Check(ArchType* archType){
                return HasAny<Ts...>(archType);
            }

            static StorageType Build(ArchType* archType, size_t beginIndex){
                StorageType view;
                BuildImpl<0, Ts...>(view, archType, beginIndex);
                return view;
            }

        private:
            template<size_t I, typename First, typename... Rest>
            static void BuildImpl(StorageType& view, ArchType* archType, size_t beginIndex){
                if(auto* ptr = GetComponentPtrAt<First>(archType, beginIndex); ptr != nullptr){
                    view.template Set<First>(ptr);
                    return;
                }

                if constexpr(sizeof...(Rest) > 0){
                    BuildImpl<I + 1, Rest...>(view, archType, beginIndex);
                }
            }
        };

        template<typename TList>
        struct ChunkPointerTuple;

        template<typename... Ts>
        struct ChunkPointerTuple<TypeList<Ts...>>{
            using Type = std::tuple<typename QueryItemTraits<Ts>::StorageType...>;
        };

    } // namespace Detail


    template<typename... Args>
    class Require{
    public:
        using Types = Detail::TypeList<Args...>;

        static bool Check(ArchType* archType){
            return (... && Detail::QueryItemTraits<Args>::Check(archType));
        }
    };

    template<typename... Args>
    class Optional{
    public:
        using Types = Detail::TypeList<Args...>;

        static bool Check(ArchType*){
            return true;
        }
    };

    template<typename... Args>
    class Exclude{
    public:
        using Types = Detail::TypeList<Args...>;

        static bool Check(ArchType* archType){
            return (... && (!Detail::QueryItemTraits<Args>::Check(archType)));
        }
    };


    template <typename RequireT, typename OptionalT, typename ExcludeT>
    class ChunkQuery{
    public:
        using RequireTypes = typename RequireT::Types;
        using OptionalTypes = typename OptionalT::Types;
        using QueryTypes =
            typename Detail::TypeListCat<RequireTypes, OptionalTypes>::Type;
        using PointerTuple =
            typename Detail::ChunkPointerTuple<QueryTypes>::Type;

        struct ChunkView{
            ArchType* archType = nullptr;

            // 当前是该 ArchType 的第几个 chunk
            size_t chunkIndex = 0;

            // 这个 chunk 在 ActiveComponentArray 中的起始下标
            size_t beginIndex = 0;

            // 当前 chunk 的有效组件数量，不是 chunk 固定容量
            size_t count = 0;

            PointerTuple components{};

            template<typename T>
            auto Get() const -> decltype(std::get<typename Detail::QueryItemTraits<T>::StorageType>(components)){
                return std::get<typename Detail::QueryItemTraits<T>::StorageType>(components);
            }

            template<typename T>
            auto Data() const -> decltype(std::get<typename Detail::QueryItemTraits<T>::StorageType>(components)){
                return std::get<typename Detail::QueryItemTraits<T>::StorageType>(components);
            }

            bool Empty() const{
                return count == 0;
            }
        };

        class Iterator{
        public:
            using value_type = ChunkView;
            using difference_type = std::ptrdiff_t;
            using pointer = void;
            using reference = ChunkView;

            Iterator() = default;

            Iterator(const ChunkQuery* owner, size_t archIndex, size_t chunkIndex)
                : owner_(owner), archIndex_(archIndex), chunkIndex_(chunkIndex){
                SkipInvalid();
            }

            value_type operator*() const{
                return owner_->BuildChunkView(archIndex_, chunkIndex_);
            }

            Iterator& operator++(){
                if(owner_ == nullptr){
                    return *this;
                }

                ++chunkIndex_;
                SkipInvalid();
                return *this;
            }

            Iterator operator++(int){
                Iterator temp = *this;
                ++(*this);
                return temp;
            }

            bool operator==(const Iterator& rhs) const{
                return owner_ == rhs.owner_
                    && archIndex_ == rhs.archIndex_
                    && chunkIndex_ == rhs.chunkIndex_;
            }

            bool operator!=(const Iterator& rhs) const{
                return !(*this == rhs);
            }

        private:
            const ChunkQuery* owner_ = nullptr;
            size_t archIndex_ = 0;
            size_t chunkIndex_ = 0;

            void SkipInvalid(){
                if(owner_ == nullptr){
                    return;
                }

                while(archIndex_ < owner_->archTypes_.size()){
                    ArchType* arch = owner_->archTypes_[archIndex_];
                    if(arch == nullptr || !arch->Check()){
                        ++archIndex_;
                        chunkIndex_ = 0;
                        continue;
                    }

                    const size_t totalChunks = owner_->GetChunkCount(arch);
                    if(chunkIndex_ < totalChunks){
                        return;
                    }

                    ++archIndex_;
                    chunkIndex_ = 0;
                }
            }
        };

    public:
        static bool CheckArchType(ArchType* archType){
            if(archType == nullptr || !archType->Check()){
                return false;
            }

            return RequireT::Check(archType)
                && OptionalT::Check(archType)
                && ExcludeT::Check(archType);
        }

        void Reserve(size_t n){
            archTypes_.reserve(n);
        }

        void Clear(){
            archTypes_.clear();
        }

        bool RegisterArchType(ArchType* archType){
            if(!CheckArchType(archType)){
                return false;
            }

            archTypes_.push_back(archType);
            return true;
        }

        template<typename ContainerT>
        void RegisterArchTypes(const ContainerT& archTypes){
            for(auto* archType : archTypes){
                RegisterArchType(archType);
            }
        }

        size_t ArchTypeCount() const{
            return archTypes_.size();
        }

        ArchType* GetArchType(size_t index) const{
            return index < archTypes_.size() ? archTypes_[index] : nullptr;
        }

        Iterator begin() const{
            return Iterator(this, 0, 0);
        }

        Iterator end() const{
            return Iterator(this, archTypes_.size(), 0);
        }

    private:
        std::vector<ArchType*> archTypes_;

        static size_t GetChunkCount(ArchType* archType){
            if(archType == nullptr){
                return 0;
            }

            const size_t activeCount = archType->ActiveCount();
            const size_t chunkCapacity = archType->SizePerChunk();

            if(chunkCapacity == 0 || activeCount == 0){
                return 0;
            }

            return (activeCount + chunkCapacity - 1) / chunkCapacity;
        }

        ChunkView BuildChunkView(size_t archIndex, size_t chunkIndex) const{
            ChunkView view;

            if(archIndex >= archTypes_.size()){
                return view;
            }

            ArchType* archType = archTypes_[archIndex];
            if(archType == nullptr || !archType->Check()){
                return view;
            }

            const size_t chunkCapacity = archType->SizePerChunk();
            const size_t activeCount = archType->ActiveCount();
            const size_t beginIndex = chunkIndex * chunkCapacity;

            if(chunkCapacity == 0 || beginIndex >= activeCount){
                return view;
            }

            const size_t remain = activeCount - beginIndex;
            const size_t validCount = (remain < chunkCapacity) ? remain : chunkCapacity;

            view.archType = archType;
            view.chunkIndex = chunkIndex;
            view.beginIndex = beginIndex;
            view.count = validCount;
            view.components = BuildPointerTuple(archType, beginIndex);
            return view;
        }

        static PointerTuple BuildPointerTuple(ArchType* archType, size_t beginIndex){
            return BuildPointerTupleImpl(archType, beginIndex, QueryTypes{});
        }

        template<typename... Ts>
        static std::tuple<typename Detail::QueryItemTraits<Ts>::StorageType...> BuildPointerTupleImpl(
            ArchType* archType,
            size_t beginIndex,
            Detail::TypeList<Ts...>
        ){
            return std::tuple<typename Detail::QueryItemTraits<Ts>::StorageType...>{
                Detail::QueryItemTraits<Ts>::Build(archType, beginIndex)...
            };
        }
    };

} // namespace ECS::Core