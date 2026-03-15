#pragma once

#include <cstdint>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

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
        std::unordered_map<ArchType*, ObjectPtr<ArchType>> registeredArchTypeArray_;
        ArchTypeDescription description_;
        uint32_t sortKey_ = 0;
        bool destroying_ = false;

        ArchTypeManager(uint32_t sortKey)
            : description_(this), sortKey_(sortKey) {}

        ~ArchTypeManager(){
            destroying_ = true;
            description_.OnManagerDestroying();

            std::vector<ArchType*> victims;
            victims.reserve(registeredArchTypeArray_.size());
            for(auto& [ptr, holder] : registeredArchTypeArray_){
                (void)holder;
                victims.push_back(ptr);
            }

            for(ArchType* archtype : victims){
                PrepareArchTypeForDestruction(archtype);
            }
            registeredArchTypeArray_.clear();
        }

        ArchTypeDescription& GetDescription(){ return description_; }

        ObjectWeakPtr<ArchType> CreateArchType(size_t sizePerChuck){
            ObjectPtr<ArchType> archtype(&description_, this, sizePerChuck);
            if(!InitArchType(archtype.Get(), sizePerChuck)){
                LOG_ERROR("ArchTypeManager::CreateArchType", "init archtype failed");
                return {};
            }

            auto weak = archtype.GenWeakPtr();
            registeredArchTypeArray_.emplace(archtype.Get(), std::move(archtype));
            return weak;
        }

        void DestroyArchType(ArchType* archType){
            auto it = registeredArchTypeArray_.find(archType);
            if(it == registeredArchTypeArray_.end()){
                return;
            }

            PrepareArchTypeForDestruction(archType);
            registeredArchTypeArray_.erase(it);
        }

        bool InitArchType(ArchType* archtype, size_t chunkScale){
            if(archtype == nullptr){
                return false;
            }

            archtype->activeAddr2ComponentDenseArray_.clear();
            archtype->preloadAddr2ComponentDenseArray_.clear();
            archtype->activeAddr2ComponentDenseArray_.reserve(description_.index2ComponentArrayType.size());
            archtype->preloadAddr2ComponentDenseArray_.reserve(description_.index2ComponentArrayType.size());

            for(const auto& typeIndex : description_.index2ComponentArrayType){
                void* activeStorage = nullptr;
                void* preloadStorage = nullptr;
                if(!ConstructComponentStorage(typeIndex, chunkScale, activeStorage) ||
                   !ConstructComponentStorage(typeIndex, chunkScale, preloadStorage)){
                    if(activeStorage != nullptr){
                        DestroyComponentStorage(typeIndex, activeStorage);
                    }
                    if(preloadStorage != nullptr){
                        DestroyComponentStorage(typeIndex, preloadStorage);
                    }
                    ReleaseArchTypeStorage(archtype);
                    return false;
                }
                archtype->activeAddr2ComponentDenseArray_.push_back(activeStorage);
                archtype->preloadAddr2ComponentDenseArray_.push_back(preloadStorage);
            }
            return true;
        }

        bool ResponseAdd(std::type_index typeIndex){
            std::vector<std::pair<ArchType*, void*>> appendedActive;
            std::vector<std::pair<ArchType*, void*>> appendedPreload;
            appendedActive.reserve(registeredArchTypeArray_.size());
            appendedPreload.reserve(registeredArchTypeArray_.size());

            auto appendDefaultN = [&](void* storage, size_t n)->bool{
                auto addIt = description_.addFunctions_.back();
                if(addIt == nullptr){
                    return false;
                }
                for(size_t i = 0; i < n; ++i){
                    addIt(storage);
                }
                return true;
            };

            for(auto& [ptr, holder] : registeredArchTypeArray_){
                (void)holder;

                void* activeStorage = nullptr;
                void* preloadStorage = nullptr;

                if(!ConstructComponentStorage(typeIndex, ptr->sizePerChuck_, activeStorage) ||
                !ConstructComponentStorage(typeIndex, ptr->sizePerChuck_, preloadStorage)){
                    if(activeStorage) DestroyComponentStorage(typeIndex, activeStorage);
                    if(preloadStorage) DestroyComponentStorage(typeIndex, preloadStorage);

                    for(auto& [arch, storage] : appendedActive){
                        if(!arch->activeAddr2ComponentDenseArray_.empty() &&
                        arch->activeAddr2ComponentDenseArray_.back() == storage){
                            arch->activeAddr2ComponentDenseArray_.pop_back();
                        }
                        DestroyComponentStorage(typeIndex, storage);
                    }
                    for(auto& [arch, storage] : appendedPreload){
                        if(!arch->preloadAddr2ComponentDenseArray_.empty() &&
                        arch->preloadAddr2ComponentDenseArray_.back() == storage){
                            arch->preloadAddr2ComponentDenseArray_.pop_back();
                        }
                        DestroyComponentStorage(typeIndex, storage);
                    }
                    return false;
                }

                if(!appendDefaultN(activeStorage, ptr->activeCount_) ||
                !appendDefaultN(preloadStorage, ptr->preloadCount_)){
                    DestroyComponentStorage(typeIndex, activeStorage);
                    DestroyComponentStorage(typeIndex, preloadStorage);
                    return false;
                }

                ptr->activeAddr2ComponentDenseArray_.push_back(activeStorage);
                ptr->preloadAddr2ComponentDenseArray_.push_back(preloadStorage);

                appendedActive.emplace_back(ptr, activeStorage);
                appendedPreload.emplace_back(ptr, preloadStorage);
            }

            return true;
        }

        void ReleaseArchTypeStorage(ArchType* archtype){
            if(archtype == nullptr || archtype->description_ != &description_){
                return;
            }
            if(archtype->storageReleased_){
                return;
            }

            const size_t kinds = description_.index2ComponentArrayType.size();
            const size_t activeCount = archtype->activeAddr2ComponentDenseArray_.size();
            const size_t preloadCount = archtype->preloadAddr2ComponentDenseArray_.size();
            const size_t maxCount = activeCount > preloadCount ? activeCount : preloadCount;

            for(size_t index = 0; index < maxCount; ++index){
                const std::type_index* typeIndex = (index < kinds) ? &description_.index2ComponentArrayType[index] : nullptr;
                if(typeIndex == nullptr){
                    LOG_ERROR("ArchTypeManager::ReleaseArchTypeStorage", "component type metadata missing during release");
                    continue;
                }
                if(index < activeCount && archtype->activeAddr2ComponentDenseArray_[index] != nullptr){
                    DestroyComponentStorage(*typeIndex, archtype->activeAddr2ComponentDenseArray_[index]);
                    archtype->activeAddr2ComponentDenseArray_[index] = nullptr;
                }
                if(index < preloadCount && archtype->preloadAddr2ComponentDenseArray_[index] != nullptr){
                    DestroyComponentStorage(*typeIndex, archtype->preloadAddr2ComponentDenseArray_[index]);
                    archtype->preloadAddr2ComponentDenseArray_[index] = nullptr;
                }
            }

            archtype->activeAddr2ComponentDenseArray_.clear();
            archtype->preloadAddr2ComponentDenseArray_.clear();
            archtype->MarkStorageReleased();
        }

        bool ConstructComponentStorage(std::type_index typeIndex, size_t chunkScale, void*& outStorage){
            outStorage = nullptr;
            auto& constructorMap = ECS::Component::GetComponentChuckArrayConstructorMap();
            auto it = constructorMap.find(typeIndex);
            if(it == constructorMap.end()){
                LOG_ERROR("ArchTypeManager::ConstructComponentStorage", "no registered component constructor for " + std::string(typeIndex.name()));
                return false;
            }

            outStorage = it->second(chunkScale);
            if(outStorage == nullptr){
                LOG_ERROR("ArchTypeManager::ConstructComponentStorage", "component constructor returned nullptr for " + std::string(typeIndex.name()));
                return false;
            }
            return true;
        }

        bool DestroyComponentStorage(std::type_index typeIndex, void* storage){
            if(storage == nullptr){
                return true;
            }

            auto& destructorMap = ECS::Component::GetComponentChuckArrayDestructorMap();
            auto it = destructorMap.find(typeIndex);
            if(it == destructorMap.end()){
                LOG_ERROR("ArchTypeManager::DestroyComponentStorage", "can't find destructor of " + std::string(typeIndex.name()));
                return false;
            }

            it->second(storage);
            return true;
        }

        void PrepareArchTypeForDestruction(ArchType* archtype){
            if(archtype == nullptr){
                return;
            }

            ReleaseArchTypeStorage(archtype);
            archtype->ResetMetadataOnly();
            archtype->DetachFromManager();
            archtype->description_ = nullptr;
            archtype->isDestroyed_ = true;
        }
    };
}
