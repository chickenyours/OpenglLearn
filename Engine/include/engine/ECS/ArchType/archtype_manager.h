#pragma once

#include <typeindex>

#include "engine/ECS/ArchType/archtype_description.h"
#include "engine/ECS/ArchType/archtype_instance.h"
#include "engine/ToolAndAlgorithm/object.h"
#include "engine/ECS/Component/component_loader_registry.h"

namespace ECS::Core{
    class Scene;
}



namespace ECS::Core{
    class ArchTypeManager{
        friend class Scene;
        friend class ArchType;
        private:
            std::vector<ObjectPtr<ArchType>> registeredArchTypeArray;
            ArchTypeDescription description_;
            ArchTypeManager(uint32_t sortKey):description_(this,sortKey){}

            ArchTypeDescription& GetDescription(){ return description_; }

            ObjectWeakPtr<ArchType> CreateArchType(size_t sizePerChuck){
                ObjectPtr<ArchType> archtype(&description_, this, sizePerChuck);
                InitArchType(archtype.Get(), sizePerChuck);
                registeredArchTypeArray.push_back(std::move(archtype));
                return archtype.GenWeakPtr();
            }

            void InitArchType(ArchType* archtype, size_t chuckScale){
                archtype->addr2ComponentDenseArray.clear();
                archtype->addr2ComponentDenseArray.reserve(description_.index2ComponentArrayType.size());
                for(auto typeIndex : description_.index2ComponentArrayType){
                    ArchTypeAddComponentArray(archtype, typeIndex, chuckScale);
                }
            }

            void ArchTypeAddComponentArray(ArchType* archtype, std::type_index typeIndex, size_t chuckScale){
                auto& map = ECS::Component::GetComponentChuckArrayConstructorMap();
                auto componentArrayChuckConstructorItor = map.find(typeIndex);
                if(componentArrayChuckConstructorItor == map.end()){
                    LOG_ERROR("ArchTypeManager::InitArchType", "no registered component");
                    return;
                }
                archtype->addr2ComponentDenseArray.push_back(componentArrayChuckConstructorItor->second(chuckScale));
            }

            void ResponseAdd(std::type_index typeIndex){
                for(auto& it : registeredArchTypeArray){
                    ArchTypeAddComponentArray(it.Get(), typeIndex, it->sizePerChuck_);
                }
            }

            void ReleaseArchType(ArchType* archtype){
                if(archtype->description_ != &description_){
                    LOG_ERROR("ArchTypeManager::InitArchType","the type of archtype is not same");
                }
                for(size_t index = 0; index < description_.componentKinds_; index++){
                    void*& compontShortageAddr = archtype->addr2ComponentDenseArray[index];
                    std::type_index& typeIndex = description_.index2ComponentArrayType[index];
                    auto& map = ECS::Component::GetComponentChuckArrayDestructorMap();
                    auto componentArrayChuckDestructorItor = map.find(typeIndex);
                    if(componentArrayChuckDestructorItor == map.end()){
                        LOG_ERROR("ArchTypeManager::InitArchType","can't find the Destructor of " + std::string(typeIndex.name()));
                        return;
                    }
                    componentArrayChuckDestructorItor->second(compontShortageAddr);
                }
                archtype->addr2ComponentDenseArray.clear();
            }
    };
}