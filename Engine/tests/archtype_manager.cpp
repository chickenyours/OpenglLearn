#include "engine/ECS/ArchType/archtype_manager.h"

#include "engine/ECS/data_type.h"
#include "engine/ToolAndAlgorithm/object.h"
#include "engine/ECS/Component/component_loader_registry.h"
#include "engine/DebugTool/ConsoleHelp/color_log.h"
#include "engine/ECS/ArchType/archtype_description.h"
#include "engine/ECS/ArchType/archtype_instance.h"
#include "engine/ECS/ArchType/archtype_preload_instance.h"

namespace ECS::Core{
    ArchTypeManager::ArchTypeManager(uint32_t sortKey)
        : description_(this), sortKey_(sortKey){
    }

    ArchTypeManager::~ArchTypeManager(){
        destroying_ = true;
        description_->OnManagerDestroying();

        std::vector<ArchType*> archVictims;
        archVictims.reserve(registeredArchTypeArray_.size());
        for(auto& [ptr, holder] : registeredArchTypeArray_){
            (void)holder;
            archVictims.push_back(ptr);
        }
        for(ArchType* archtype : archVictims){
            PrepareArchTypeForDestruction(archtype);
        }
        registeredArchTypeArray_.clear();

        std::vector<ArchTypePreloadInstance*> preloadVictims;
        preloadVictims.reserve(registeredPreloadArray_.size());
        for(auto& [ptr, holder] : registeredPreloadArray_){
            (void)holder;
            preloadVictims.push_back(ptr);
        }
        for(ArchTypePreloadInstance* preload : preloadVictims){
            PreparePreloadForDestruction(preload);
        }
        registeredPreloadArray_.clear();
    }

    ObjectWeakPtr<ArchTypeDescription> ArchTypeManager::GetDescription(){
        return description_.GenWeakPtr();
    }

    ObjectWeakPtr<ArchType> ArchTypeManager::CreateArchType(size_t sizePerChunk){
        ObjectPtr<ArchType> archtype(description_.Get(), this, sizePerChunk);
        if(!InitArchType(archtype.Get(), sizePerChunk)){
            LOG_ERROR("ArchTypeManager::CreateArchType", "init archtype failed");
            return {};
        }

        auto weak = archtype.GenWeakPtr();
        registeredArchTypeArray_.emplace(archtype.Get(), std::move(archtype));
        return weak;
    }

    void ArchTypeManager::DestroyArchType(ArchType* archType){
        auto it = registeredArchTypeArray_.find(archType);
        if(it == registeredArchTypeArray_.end()){
            return;
        }

        PrepareArchTypeForDestruction(archType);
        registeredArchTypeArray_.erase(it);
    }


    ObjectWeakPtr<ArchTypePreloadInstance> ArchTypeManager::CreatePreload(size_t sizePerChunk){
        ObjectPtr<ArchTypePreloadInstance> preload(description_.Get(), this, sizePerChunk);
        if(!InitPreloadInstance(preload.Get(), sizePerChunk)){
            LOG_ERROR("ArchTypeManager::CreatePreload", "init preload failed");
            return {};
        }

        auto weak = preload.GenWeakPtr();
        registeredPreloadArray_.emplace(preload.Get(), std::move(preload));
        return weak;
    }

    void ArchTypeManager::DestroyPreloadInstance(ArchTypePreloadInstance* preload){
        auto it = registeredPreloadArray_.find(preload);
        if(it == registeredPreloadArray_.end()){
            return;
        }

        PreparePreloadForDestruction(preload);
        registeredPreloadArray_.erase(it);
    }

    bool ArchTypeManager::InitArchType(ArchType* archtype, size_t chunkScale){
        if(archtype == nullptr){
            return false;
        }

        archtype->activeAddr2ComponentDenseArray_.clear();
        archtype->activeAddr2ComponentDenseArray_.reserve(description_->index2ComponentArrayType.size());

        for(const auto& typeIndex : description_->index2ComponentArrayType){
            void* activeStorage = nullptr;
            if(!ConstructComponentStorage(typeIndex, chunkScale, activeStorage)){
                if(activeStorage != nullptr){
                    DestroyComponentStorage(typeIndex, activeStorage);
                }
                ReleaseArchTypeStorage(archtype);
                return false;
            }
            archtype->activeAddr2ComponentDenseArray_.push_back(activeStorage);
        }
        return true;
    }

    bool ArchTypeManager::InitPreloadInstance(ArchTypePreloadInstance* preload, size_t chunkScale){
        if(preload == nullptr){
            return false;
        }

        preload->addr2ComponentDenseArray_.clear();
        preload->addr2ComponentDenseArray_.reserve(description_->index2ComponentArrayType.size());

        for(const auto& typeIndex : description_->index2ComponentArrayType){
            void* storage = nullptr;
            if(!ConstructComponentStorage(typeIndex, chunkScale, storage)){
                if(storage != nullptr){
                    DestroyComponentStorage(typeIndex, storage);
                }
                ReleaseArchTypePreloadStorage(preload);
                return false;
            }
            preload->addr2ComponentDenseArray_.push_back(storage);
        }
        return true;
    }

    bool ArchTypeManager::ResponseAdd(std::type_index typeIndex){
        std::vector<std::pair<ArchType*, void*>> appendedActive;
        appendedActive.reserve(registeredArchTypeArray_.size());

        auto appendDefaultN = [&](void* storage, size_t n)->bool{
            auto addIt = description_->defaultAppendNFunctions_.empty()
                       ? nullptr
                       : description_->defaultAppendNFunctions_.back();
            if(addIt == nullptr){
                return false;
            }
            addIt(storage, n);
            return true;
        };

        for(auto& [ptr, holder] : registeredArchTypeArray_){
            (void)holder;

            void* activeStorage = nullptr;
            if(!ConstructComponentStorage(typeIndex, ptr->sizePerChunk_, activeStorage)){
                if(activeStorage) DestroyComponentStorage(typeIndex, activeStorage);

                for(auto& [arch, storage] : appendedActive){
                    if(!arch->activeAddr2ComponentDenseArray_.empty() &&
                       arch->activeAddr2ComponentDenseArray_.back() == storage){
                        arch->activeAddr2ComponentDenseArray_.pop_back();
                    }
                    DestroyComponentStorage(typeIndex, storage);
                }
                LOG_ERROR("ArchTypeManager::ResponseAdd", "construct active storage failed");
                return false;
            }

            if(!appendDefaultN(activeStorage, ptr->activeCount_)){
                DestroyComponentStorage(typeIndex, activeStorage);
                LOG_ERROR("ArchTypeManager::ResponseAdd", "append default active units failed");
                return false;
            }

            ptr->activeAddr2ComponentDenseArray_.push_back(activeStorage);
            appendedActive.emplace_back(ptr, activeStorage);
        }

        return true;
    }

    void ArchTypeManager::ReleaseArchTypeStorage(ArchType* archtype){
        if(archtype == nullptr || archtype->description_ != description_.Get()){
            return;
        }
        if(archtype->storageReleased_){
            return;
        }

        const size_t kinds = description_->index2ComponentArrayType.size();
        const size_t activeStorageCount = archtype->activeAddr2ComponentDenseArray_.size();
        const size_t count = (activeStorageCount < kinds) ? activeStorageCount : kinds;

        for(size_t index = 0; index < count; ++index){
            void*& storage = archtype->activeAddr2ComponentDenseArray_[index];
            if(storage == nullptr){
                continue;
            }
            DestroyComponentStorage(description_->index2ComponentArrayType[index], storage);
            storage = nullptr;
        }

        archtype->activeAddr2ComponentDenseArray_.clear();
        archtype->MarkStorageReleased();
    }

    void ArchTypeManager::ReleaseArchTypePreloadStorage(ArchTypePreloadInstance* preload){
        if(preload == nullptr || preload->description_ != description_.Get()){
            return;
        }
        if(preload->storageReleased_){
            return;
        }

        const size_t kinds = description_->index2ComponentArrayType.size();
        const size_t storageCount = preload->addr2ComponentDenseArray_.size();
        const size_t count = (storageCount < kinds) ? storageCount : kinds;

        for(size_t index = 0; index < count; ++index){
            void*& storage = preload->addr2ComponentDenseArray_[index];
            if(storage == nullptr){
                continue;
            }
            DestroyComponentStorage(description_->index2ComponentArrayType[index], storage);
            storage = nullptr;
        }

        preload->addr2ComponentDenseArray_.clear();
        preload->storageReleased_ = true;
    }

    bool ArchTypeManager::ConstructComponentStorage(std::type_index typeIndex, size_t chunkScale, void*& outStorage){
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

    bool ArchTypeManager::DestroyComponentStorage(std::type_index typeIndex, void* storage){
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

    void ArchTypeManager::PrepareArchTypeForDestruction(ArchType* archtype){
        if(archtype == nullptr){
            return;
        }

        ReleaseArchTypeStorage(archtype);
        archtype->ResetMetadataOnly();
        archtype->DetachFromManager();
        archtype->description_ = nullptr;
        archtype->isDestroyed_ = true;
    }


    void ArchTypeManager::PreparePreloadForDestruction(ArchTypePreloadInstance* preload){
        if(preload == nullptr){
            return;
        }

        ReleaseArchTypePreloadStorage(preload);
        preload->ResetMetadataOnly();
        preload->manager_ = nullptr;
        preload->description_ = nullptr;
        preload->isDestroyed_ = true;
    }
}
