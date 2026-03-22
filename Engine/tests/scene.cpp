#include "engine/ECS/Scene/scene.h"

#include <utility>

#include "engine/ECS/ArchType/archtype_instance.h"
#include "engine/ECS/ArchType/archtype_preload_instance.h"

namespace ECS::Core{

    Scene::Scene(){
        entity2entityInfo_.push_back(EntitySceneInfo{});
    }

    bool Scene::Check(ArchType* archtype) const{
        if(archtype == nullptr || archtype->manager_ == nullptr){
            return false;
        }
        const size_t key = archtype->manager_->sortKey_;
        if(key >= archtypeManagers_.size()){
            return false;
        }
        return archtypeManagers_[key].Get() == archtype->manager_;
    }

    bool Scene::Check(ArchTypePreloadInstance* preload) const{
        if(preload == nullptr || preload->manager_ == nullptr){
            return false;
        }
        const size_t key = preload->manager_->sortKey_;
        if(key >= archtypeManagers_.size()){
            return false;
        }
        return archtypeManagers_[key].Get() == preload->manager_;
    }

    ObjectWeakPtr<ArchTypeDescription> Scene::CreateArchTypeDescription(){
        const uint32_t nowIndex = static_cast<uint32_t>(archtypeManagers_.size());
        archtypeManagers_.push_back(ObjectPtr<ArchTypeManager>(nowIndex));
        return archtypeManagers_[nowIndex]->description_.GenWeakPtr();
    }

    ObjectWeakPtr<ArchType> Scene::CreateArchType(ObjectWeakPtr<ArchTypeDescription> description, size_t sizePerChuck){
        if(!description.lock() || description->responseManager_ == nullptr){
            LOG_ERROR("Scene::CreateArchType", "description or response manager is null");
            return {};
        }

        const size_t key = description->responseManager_->sortKey_;
        if(key >= archtypeManagers_.size()){
            LOG_ERROR("Scene::CreateArchType", "manager key oversize");
            return {};
        }

        ArchTypeManager* manager = archtypeManagers_[key].Get();
        if(manager == nullptr || manager->description_ != description){
            LOG_ERROR("Scene::CreateArchType", "description and manager not match");
            return {};
        }

        return manager->CreateArchType(sizePerChuck);
    }

    void Scene::DeleteArchType(ObjectWeakPtr<ArchType>& archtype){
        if(!Check(archtype.Get())){
            LOG_ERROR("Scene::DeleteArchType", "archtype manager not match");
            return;
        }
        archtypeManagers_[archtype->manager_->sortKey_]->DestroyArchType(archtype.Get());
        archtype.SetNull();
    }

    ObjectWeakPtr<ArchTypePreloadInstance> Scene::CreateArchTypePreloadInstance(
        ObjectWeakPtr<ArchTypeDescription> description,
        size_t sizePerChuck)
    {
        if(!description.lock() || description->responseManager_ == nullptr){
            LOG_ERROR("Scene::CreateArchTypePreloadInstance", "description or response manager is null");
            return {};
        }

        const size_t key = description->responseManager_->sortKey_;
        if(key >= archtypeManagers_.size()){
            LOG_ERROR("Scene::CreateArchTypePreloadInstance", "manager key oversize");
            return {};
        }

        ArchTypeManager* manager = archtypeManagers_[key].Get();
        if(manager == nullptr || manager->description_ != description){
            LOG_ERROR("Scene::CreateArchTypePreloadInstance", "description and manager not match");
            return {};
        }

        return manager->CreatePreload(sizePerChuck);
    }

    void Scene::DeleteArchTypePreloadInstance(ObjectWeakPtr<ArchTypePreloadInstance>& preload){
        if(!Check(preload.Get())){
            LOG_ERROR("Scene::DeleteArchTypePreloadInstance", "preload manager not match");
            return;
        }
        archtypeManagers_[preload->manager_->sortKey_]->DestroyPreloadInstance(preload.Get());
        preload.SetNull();
    }

    EntityHandle Scene::CreateEntity(ObjectWeakPtr<ArchType>& archtype){
        auto entities = CreateEntities(archtype, 1);
        if(entities.empty()){
            return {};
        }
        return entities.front();
    }

    std::vector<EntityHandle> Scene::CreateEntities(ObjectWeakPtr<ArchType>& archtype, size_t count){
        std::vector<EntityHandle> results;
        if(count == 0){
            return results;
        }

        if(!Check(archtype.Get())){
            LOG_ERROR("Scene::CreateEntities", "invalid archtype");
            return results;
        }

        results.reserve(count);
        std::vector<EntityID> entityIDs;
        entityIDs.reserve(count);

        for(size_t i = 0; i < count; ++i){
            EntityID newID = 0;
            uint32_t generation = 1;

            if(!recycleEntityID_.empty()){
                newID = recycleEntityID_.front();
                recycleEntityID_.pop();

                entity2entityInfo_[newID].ownArchtype = archtype;
                entity2entityInfo_[newID].alive = true;
                generation = entity2entityInfo_[newID].generation;
            }else{
                newID = ++entityCount_;
                entity2entityInfo_.push_back(EntitySceneInfo{});
                entity2entityInfo_[newID].ownArchtype = archtype;
                entity2entityInfo_[newID].alive = true;
                entity2entityInfo_[newID].generation = 1;
                generation = 1;
            }

            EntityHandle handle;
            handle.id_ = newID;
            handle.generation_ = generation;
            results.push_back(handle);
            entityIDs.push_back(newID);
        }

        const size_t beginIndex = archtype->CreateEntities(entityIDs.data(), count);
        if(beginIndex == ARCHTYPE_INVALID_INDEX){
            for(const auto& handle : results){
                EntitySceneInfo& info = entity2entityInfo_[handle.id_];
                info.ownArchtype.SetNull();
                info.alive = false;

                if(handle.id_ == entityCount_){
                    entity2entityInfo_.pop_back();
                    --entityCount_;
                }else{
                    recycleEntityID_.push(handle.id_);
                }
            }
            LOG_ERROR("Scene::CreateEntities", "archtype create entities failed");
            return {};
        }

        return results;
    }

    size_t Scene::RegisterPreloadToArchType(
        ObjectWeakPtr<ArchTypePreloadInstance>& preload,
        ObjectWeakPtr<ArchType>& archtype,
        std::vector<EntityHandle>& outEntities)
    {
        if(!preload.lock()){
            return 0;
        }
        return RegisterPreloadToArchTypeByMask(preload, archtype, nullptr, preload->Count(), outEntities);
    }

    size_t Scene::RegisterPreloadToArchTypeByMask(
        ObjectWeakPtr<ArchTypePreloadInstance>& preload,
        ObjectWeakPtr<ArchType>& archtype,
        const uint8_t* passMask,
        size_t maskCount,
        std::vector<EntityHandle>& outEntities)
    {
        outEntities.clear();

        ArchTypePreloadInstance* preloadPtr = preload.Get();
        ArchType* archPtr = archtype.Get();

        if(!Check(preloadPtr) || !Check(archPtr)){
            LOG_ERROR("Scene::RegisterPreloadToArchTypeByMask", "invalid preload or archtype");
            return 0;
        }

        if(preloadPtr->description_ != archPtr->description_){
            LOG_ERROR("Scene::RegisterPreloadToArchTypeByMask", "description mismatch");
            return 0;
        }

        if(maskCount != preloadPtr->Count()){
            LOG_ERROR("Scene::RegisterPreloadToArchTypeByMask", "maskCount mismatch");
            return 0;
        }

        if(maskCount == 0){
            return 0;
        }

        // 先写组件数据，真正成功写入多少，以 writtenCount 为准
        size_t targetBegin = ARCHTYPE_INVALID_INDEX;
        const size_t writtenCount = preloadPtr->RegisterAllToArchTypeByMask(
            *archPtr,
            passMask,
            maskCount,
            targetBegin
        );

        if(writtenCount == 0 || targetBegin == ARCHTYPE_INVALID_INDEX){
            LOG_ERROR("Scene::RegisterPreloadToArchTypeByMask", "register preload failed");
            return 0;
        }

        if(targetBegin + writtenCount > archPtr->index2EntityID_.size() ||
        targetBegin + writtenCount > archPtr->activeGenerationPerUnit_.size()){
            LOG_ERROR("Scene::RegisterPreloadToArchTypeByMask", "archtype metadata oversize");
            return 0;
        }

        outEntities.reserve(writtenCount);

        for(size_t i = 0; i < writtenCount; ++i){
            EntityID newID = 0;
            uint32_t generation = 1;

            if(!recycleEntityID_.empty()){
                newID = recycleEntityID_.front();
                recycleEntityID_.pop();
#ifdef ENGINE_DEVELOP
                if(newID >= entity2entityInfo_.size()){
                    LOG_ERROR("Scene::RegisterPreloadToArchTypeByMask", "recycled entity id oversize");
                    return 0;
                }
#endif

                generation = entity2entityInfo_[newID].generation;
            }else{
                newID = ++entityCount_;
                entity2entityInfo_.push_back(EntitySceneInfo{});
                entity2entityInfo_[newID].generation = 1;
                generation = 1;
            }

            const size_t dstIndex = targetBegin + i;

            archPtr->index2EntityID_[dstIndex] = newID;
            archPtr->entityID2Unit_[newID] = dstIndex;
            archPtr->activeGenerationPerUnit_[dstIndex] = generation;

            EntitySceneInfo& info = entity2entityInfo_[newID];
            info.ownArchtype = archtype;
            info.alive = true;
            info.generation = generation;

            EntityHandle handle;
            handle.id_ = newID;
            handle.generation_ = generation;
            outEntities.push_back(handle);
        }

        archPtr->activeCount_ += writtenCount;
        return writtenCount;
    }

    void Scene::DeleteEntity(EntityHandle entity){
        if(entity.id_ == 0 || entity.id_ >= entity2entityInfo_.size()){
            return;
        }

        EntitySceneInfo& info = entity2entityInfo_[entity.id_];
        if(!info.alive){
            return;
        }

        if(entity.generation_ != 0 && entity.generation_ != info.generation){
            LOG_WARNING("Scene::DeleteEntity", "generation mismatch");
            return;
        }

        ArchType* archtype = info.ownArchtype.Get();
        if(archtype == nullptr){
            info.ownArchtype.SetNull();
            info.alive = false;
            ++info.generation;
            recycleEntityID_.push(entity.id_);
            return;
        }

        archtype->DeleteEntity(entity.id_);
        info.ownArchtype.SetNull();
        info.alive = false;
        ++info.generation;
        recycleEntityID_.push(entity.id_);
    }

    void Scene::DeleteEntities(const std::vector<EntityHandle>& entities){
        for(const auto& entity : entities){
            DeleteEntity(entity);
        }
    }

    bool Scene::IsAlive(EntityHandle entity) const{
        if(entity.id_ == 0 || entity.id_ >= entity2entityInfo_.size()){
            return false;
        }
        const auto& info = entity2entityInfo_[entity.id_];
        return info.alive && info.generation == entity.generation_;
    }
}
