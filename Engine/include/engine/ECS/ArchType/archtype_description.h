#pragma once

#include <unordered_map>
#include <vector>
#include <string>
#include <typeindex>

namespace ECS::Core{
    class ArchTypeManager;
    class Scene;
}

#include "engine/ECS/ArchType/archtype_manager.h"
#include "engine/ToolAndAlgorithm/DateType/chunk_array.h"

namespace ECS::Core{ 
    class ArchTypeDescription{
        friend class ArchType;
        friend class ArchTypeManager;
        friend class Scene;
        private:
            std::unordered_map<std::type_index,size_t> componentArrayDescription_;
            std::vector<std::type_index> index2ComponentArrayType;
            std::vector<void(*)(void*)> functions;
            ArchTypeManager* responseManager_ = nullptr;
            size_t componentKinds_ = 0;
            ArchTypeDescription(ArchTypeManager* manager):responseManager_(manager){}

            // template <typename ComponentT>
            // void AddUnit(ArchType* archtype){
            //     if(archtype->description_ != this){
            //         return;
            //     }
            //     for()
            // }

            template <typename ComponentT>
            static void Append(void* chunkAddr){
                FixedChunkArray<ComponentT>* chunkptr = reinterpret_cast<FixedChunkArray<ComponentT>*>(chunkAddr);
                chunkptr->push_back();
            }

        public:
            template <typename ComponentT>
            void AddComponentArray(){
                auto index = std::type_index(typeid(ComponentT));
                if(!componentArrayDescription_.count(index)){
                    index2ComponentArrayType.push_back(index);
                    componentArrayDescription_[index] = componentKinds_++;
                    if(responseManager_){
                        functions.push_back(&Append<ComponentT>);
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

            void AppendUnit(ArchType* archType){
                if(!archType){
                    return;
                }
                if(archType->description_ != this){
                    return;
                }
                if(functions.size() != archType->addr2ComponentDenseArray.size()){
                    return;
                }

                for(size_t i = 0; i < functions.size(); ++i){
                    functions[i](archType->addr2ComponentDenseArray[i]);
                }
            }

            

    };
}