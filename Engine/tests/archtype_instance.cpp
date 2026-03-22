#include "engine/ECS/ArchType/archtype_instance.h"

#include "engine/ECS/ArchType/archtype_description.h"
#include "engine/ECS/ArchType/archtype_manager.h"

namespace ECS::Core{
    ArchType::ArchType(ArchTypeDescription* description, ArchTypeManager* manager, size_t sizePerChunk)
        : sizePerChunk_(sizePerChunk), description_(description), manager_(manager) {}

    ArchType::~ArchType(){
        ReleaseOwnedResources();
    }

    bool ArchType::Check() const {
        if(isDestroyed_ || manager_ == nullptr || description_ == nullptr){
            return false;
        }

        const size_t kinds = description_->addFunctions_.size();
        return kinds == description_->deleteFunctions_.size()
            && kinds == description_->swapFunctions_.size()
            && kinds == activeAddr2ComponentDenseArray_.size();
    }

    size_t ArchType::CreateEntity(EntityID entity){
        return CreateEntities(&entity, 1);
    }

    size_t ArchType::CreateEntities(const EntityID* entityIDs, size_t count){
        if(count == 0){
            return activeCount_;
        }
        if(entityIDs == nullptr){
            LOG_ERROR("ArchType::CreateEntities", "entityIDs is null");
            return ARCHTYPE_INVALID_INDEX;
        }
        if(!Check()){
            LOG_ERROR("ArchType::CreateEntities", "incomplete");
            return ARCHTYPE_INVALID_INDEX;
        }

        for(size_t i = 0; i < count; ++i){
            if(entityID2Unit_.count(entityIDs[i]) != 0){
                LOG_ERROR("ArchType::CreateEntities", "entity already registered");
                return ARCHTYPE_INVALID_INDEX;
            }
        }

        return AllocateEntitySlots(entityIDs, count);
    }

    void ArchType::DeleteEntity(EntityID entity){
        auto it = entityID2Unit_.find(entity);
        if(it == entityID2Unit_.end()){
            LOG_ERROR("ArchType::DeleteEntity", "not registered");
            return;
        }
        DeleteActiveUnit(it->second);
    }

    void ArchType::DeleteEntities(const EntityID* entityIDs, size_t count){
        if(count == 0){
            return;
        }
        if(entityIDs == nullptr){
            LOG_ERROR("ArchType::DeleteEntities", "entityIDs is null");
            return;
        }

        for(size_t i = 0; i < count; ++i){
            DeleteEntity(entityIDs[i]);
        }
    }

    void ArchType::ReleaseOwnedResources(){
        if(storageReleased_){
            return;
        }

        storageReleased_ = true;

        if(manager_ != nullptr && description_ != nullptr){
            manager_->ReleaseArchTypeStorage(this);
        }

        ResetMetadataOnly();
        activeAddr2ComponentDenseArray_.clear();
        description_ = nullptr;
        manager_ = nullptr;
        isDestroyed_ = true;
    }

    void ArchType::DetachFromManager(){
        manager_ = nullptr;
    }

    void ArchType::MarkStorageReleased(){
        storageReleased_ = true;
    }

    void ArchType::ResetMetadataOnly(){
        activeCount_ = 0;
        index2EntityID_.clear();
        entityID2Unit_.clear();
        activeGenerationPerUnit_.clear();
    }

    // void ArchType::AppendUnits(size_t num){
    //     if(num == 0){
    //         return;
    //     }

    //     for(size_t i = 0; i < description_->defaultAppendNFunctions_.size(); ++i){
    //         description_->defaultAppendNFunctions_[i](activeAddr2ComponentDenseArray_[i], num);
    //     }
    // }

    void ArchType::AppendUnits(size_t num){
        if(num == 0){
            return;
        }

        for(size_t i = 0; i < description_->defaultAppendNFunctions_.size(); ++i){
            description_->defaultAppendNFunctions_[i](
                activeAddr2ComponentDenseArray_[i],
                num
            );
        }

        const size_t newCount = activeCount_ + num;

        if(index2EntityID_.size() < newCount){
            index2EntityID_.resize(newCount, 0);
        }

        if(activeGenerationPerUnit_.size() < newCount){
            activeGenerationPerUnit_.resize(newCount, 0);
        }

    }

    void ArchType::DeleteUnitInArrays(size_t index){
        for(size_t i = 0; i < description_->deleteFunctions_.size(); ++i){
            description_->deleteFunctions_[i](activeAddr2ComponentDenseArray_[i], index);
        }
    }

    void ArchType::SwapUnitInArrays(size_t indexA, size_t indexB){
        if(indexA == indexB){
            return;
        }
        for(size_t i = 0; i < description_->swapFunctions_.size(); ++i){
            description_->swapFunctions_[i](activeAddr2ComponentDenseArray_[i], indexA, indexB);
        }
    }

    size_t ArchType::AllocateEntitySlots(const EntityID* entityIDs, size_t count){
        const size_t begin = activeCount_;
        AppendUnits(count);

        for(size_t i = 0; i < count; ++i){
            const size_t index = begin + i;
            const EntityID entity = entityIDs[i];
            index2EntityID_.push_back(entity);
            entityID2Unit_[entity] = index;
            activeGenerationPerUnit_.push_back(1u);
        }

        activeCount_ += count;
        return begin;
    }

    void ArchType::DeleteActiveUnit(size_t index){
        if(index >= activeCount_){
            LOG_ERROR("ArchType::DeleteActiveUnit", "oversize");
            return;
        }

        const size_t last = activeCount_ - 1;
        const EntityID deletedEntityID = index2EntityID_[index];

        if(index != last){
            SwapUnitInArrays(index, last);

            const EntityID movedEntity = index2EntityID_[last];
            index2EntityID_[index] = movedEntity;
            entityID2Unit_[movedEntity] = index;
            ++activeGenerationPerUnit_[index];
        }

        DeleteUnitInArrays(last);

        entityID2Unit_.erase(deletedEntityID);
        index2EntityID_.pop_back();
        activeGenerationPerUnit_.pop_back();
        --activeCount_;
    }
}
