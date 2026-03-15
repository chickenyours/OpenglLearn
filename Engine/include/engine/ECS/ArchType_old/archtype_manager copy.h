#pragma once

#include <typeindex>
#include <unordered_map>
#include <cstdint>

#include "engine/ECS/ArchType/archtype_description.h"
#include "engine/ECS/ArchType/archtype_instance.h"
#include "engine/ToolAndAlgorithm/object.h"
#include "engine/ECS/Component/component_loader_registry.h"
#include "engine/DebugTool/ConsoleHelp/color_log.h"

namespace ECS::Core{
    class Scene;
}



namespace ECS::Core{
    class ArchTypeManager{
        friend class Scene;
        friend class ArchType;
        private:
            std::unordered_map<ArchType*,ObjectPtr<ArchType>> registeredArchTypeArray;
            ArchTypeDescription description_;
            uint32_t sortKey_;
            ArchTypeManager(uint32_t sortKey):description_(this),sortKey_(sortKey){}

            ArchTypeDescription& GetDescription(){ return description_; }

            ObjectWeakPtr<ArchType> CreateArchType(size_t sizePerChuck){
                ObjectPtr<ArchType> archtype(&description_, this, sizePerChuck);
                InitArchType(archtype.Get(), sizePerChuck);
                auto weak = archtype.GenWeakPtr();
                registeredArchTypeArray.emplace(archtype.Get(), std::move(archtype));
                return weak;
            }

            void DestroyArchType(ArchType* archType){
                if(registeredArchTypeArray.count(archType)){
                    registeredArchTypeArray.erase(archType);
                }
            }

            void InitArchType(ArchType* archtype, size_t chunkScale){
                archtype->addr2ComponentDenseArray.clear();
                archtype->addr2ComponentDenseArray.reserve(description_.index2ComponentArrayType.size());
                for(auto typeIndex : description_.index2ComponentArrayType){
                    ArchTypeAddComponentArray(archtype, typeIndex, chunkScale);
                }
            }

            void ArchTypeAddComponentArray(ArchType* archtype, std::type_index typeIndex, size_t chunkScale){
                auto& map = ECS::Component::GetComponentChuckArrayConstructorMap();
                auto componentArrayChuckConstructorItor = map.find(typeIndex);
                if(componentArrayChuckConstructorItor == map.end()){
                    LOG_ERROR("ArchTypeManager::InitArchType", "no registered component");
                    return;
                }
                archtype->addr2ComponentDenseArray.push_back(componentArrayChuckConstructorItor->second(chunkScale));
            }

            void ResponseAdd(std::type_index typeIndex){
                for(auto& [key,val] : registeredArchTypeArray){
                    ArchTypeAddComponentArray(val.Get(), typeIndex, val->sizePerChuck_);
                }
            }

            void ReleaseArchType(ArchType* archtype){
                if(archtype->description_ != &description_){
                    LOG_ERROR("ArchTypeManager::InitArchType","the type of archtype is not same");
                    return;
                }
                for(size_t index = 0; index < description_.componentKinds_; index++){
                    void*& compontStorageAddr = archtype->addr2ComponentDenseArray[index];
                    std::type_index& typeIndex = description_.index2ComponentArrayType[index];
                    auto& map = ECS::Component::GetComponentChuckArrayDestructorMap();
                    auto componentArrayChuckDestructorItor = map.find(typeIndex);
                    if(componentArrayChuckDestructorItor == map.end()){
                        LOG_ERROR("ArchTypeManager::InitArchType","can't find the Destructor of " + std::string(typeIndex.name()));
                        return;
                    }
                    componentArrayChuckDestructorItor->second(compontStorageAddr);
                }
                archtype->addr2ComponentDenseArray.clear();
            }
    };
}