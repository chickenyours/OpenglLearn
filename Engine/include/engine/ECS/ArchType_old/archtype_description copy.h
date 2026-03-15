#pragma once

#include <unordered_map>
#include <vector>
#include <string>
#include <typeindex>

namespace ECS::Core{
    class ArchTypeManager;
    class Scene;
}

#include "engine/ECS/ArchType/archtype_instance.h"
#include "engine/ECS/ArchType/archtype_manager.h"
#include "engine/ToolAndAlgorithm/DateType/chunk_array.h"

namespace ECS::Core{

    class ArchTypeDescription{
        friend class ArchType;
        friend class ArchTypeManager;
        friend class Scene;

    private:
        ArchTypeDescription(ArchTypeManager* manager)
            : responseManager_(manager) {}

        // 自检查
        bool Check(ArchType* archType){
            return archType
                && archType->description_ == this
                && addFunctions_.size() == archType->activeAddr2ComponentDenseArray_.size()
                && addFunctions_.size() == archType->preloadAddr2ComponentDenseArray_.size();
        }

        // --------------------------------------------------------------------
        // 单元处理功能
        // --------------------------------------------------------------------
        template <typename ComponentT>
        static void Append(void* chunkAddr){
            FixedChunkArray<ComponentT>* chunkptr =
                reinterpret_cast<FixedChunkArray<ComponentT>*>(chunkAddr);
            chunkptr->push_back();
        }

        template <typename ComponentT>
        static void Delete(void* chunkAddr, size_t index){
            FixedChunkArray<ComponentT>* chunkptr =
                reinterpret_cast<FixedChunkArray<ComponentT>*>(chunkAddr);
            chunkptr->remove(index);
        }

        template <typename ComponentT>
        static void Swap(void* chunkAddr, size_t indexA, size_t indexB){
            FixedChunkArray<ComponentT>* chunkptr =
                reinterpret_cast<FixedChunkArray<ComponentT>*>(chunkAddr);
            chunkptr->swap(indexA, indexB);
        }

        template <typename ComponentT>
        static void SwapBetween(void* chunkAddrA, size_t indexA, void* chunkAddrB, size_t indexB){
            FixedChunkArray<ComponentT>* a =
                reinterpret_cast<FixedChunkArray<ComponentT>*>(chunkAddrA);
            FixedChunkArray<ComponentT>* b =
                reinterpret_cast<FixedChunkArray<ComponentT>*>(chunkAddrB);
            a->swap_with_other(*b, indexA, indexB);
        }

        // --------------------------------------------------------------------
        // 组件数组初始化和扩展功能
        // --------------------------------------------------------------------
        template <typename ComponentT>
        void AddComponentArray(){
            auto index = std::type_index(typeid(ComponentT));
            if(!componentArrayDescription_.count(index)){
                index2ComponentArrayType.push_back(index);
                componentArrayDescription_[index] = componentKinds_++;

                if(responseManager_){
                    addFunctions_.push_back(&Append<ComponentT>);
                    deleteFunctions_.push_back(&Delete<ComponentT>);
                    swapFunctions_.push_back(&Swap<ComponentT>);
                    swapBetweenFunctions_.push_back(&SwapBetween<ComponentT>);
                    responseManager_->ResponseAdd(index);
                }
            }
        }

        template <typename ComponentT>
        bool TryGetComponentArray(size_t& out) const {
            auto index = std::type_index(typeid(ComponentT));
            auto comp = componentArrayDescription_.find(index);
            if(comp != componentArrayDescription_.end()){
                out = comp->second;
                return true;
            }
            return false;
        }

    private:
        // 组件类型参数检索表
        std::unordered_map<std::type_index, size_t> componentArrayDescription_;
        std::vector<std::type_index> index2ComponentArrayType;
        size_t componentKinds_ = 0;

        // 索引映射函数指针
        std::vector<void(*)(void*)> addFunctions_;
        std::vector<void(*)(void*, size_t)> deleteFunctions_;
        std::vector<void(*)(void*, size_t, size_t)> swapFunctions_;
        std::vector<void(*)(void*, size_t, void*, size_t)> swapBetweenFunctions_;

        // 管理器指针
        ArchTypeManager* responseManager_ = nullptr;
    };

} // namespace ECS::Core