#pragma once

#include <tuple>
#include <type_traits>
#include <vector>
#include <cstddef>
#include <utility>

#include "engine/ECS/ArchType/archtype_description.h"
#include "engine/ECS/ArchType/archtype_instance.h"

namespace ECS::Core{

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

        template<typename TList>
        struct ChunkPointerTuple;

        template<typename... Ts>
        struct ChunkPointerTuple<TypeList<Ts...>>{
            using Type = std::tuple<Ts*...>;
        };

    } // namespace Detail


    template<typename... Args>
    class Require{
    public:
        using Types = Detail::TypeList<Args...>;

        static bool Check(ArchType* archType){
            return Detail::HasAll<Args...>(archType);
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
            return Detail::HasNone<Args...>(archType);
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
            T* Get() const{
                return std::get<T*>(components);
            }

            template<typename T>
            T* Data() const{
                return std::get<T*>(components);
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
        static std::tuple<Ts*...> BuildPointerTupleImpl(
            ArchType* archType,
            size_t beginIndex,
            Detail::TypeList<Ts...>
        ){
            return std::tuple<Ts*...>{
                Detail::GetComponentPtrAt<Ts>(archType, beginIndex)...
            };
        }
    };

} // namespace ECS::Core