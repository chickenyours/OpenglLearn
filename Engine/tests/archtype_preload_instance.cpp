#include "engine/ECS/ArchType/archtype_preload_instance.h"
#include "engine/ECS/ArchType/archtype_instance.h"
#include "engine/ECS/ArchType/archtype_manager.h"

namespace ECS::Core{

ArchTypePreloadInstance::ArchTypePreloadInstance(ArchTypeDescription* description,
                                                 ArchTypeManager* manager,
                                                 size_t sizePerChunk)
    : sizePerChunk_(sizePerChunk),
      description_(description),
      manager_(manager) {}

ArchTypePreloadInstance::~ArchTypePreloadInstance(){
    ReleaseOwnedResources();
}

bool ArchTypePreloadInstance::Check() const{
    if(isDestroyed_ || description_ == nullptr){
        return false;
    }

    const size_t kinds = description_->componentKinds_;
    return kinds == description_->addFunctions_.size()
        && kinds == description_->deleteFunctions_.size()
        && kinds == description_->swapFunctions_.size()
        && kinds == description_->copyAppendBetweenFunctions_.size()
        && kinds == description_->defaultAppendNFunctions_.size()
        && kinds == addr2ComponentDenseArray_.size();
}

size_t ArchTypePreloadInstance::CreateUnit(size_t num){
    if(num == 0){
        return count_;
    }

    if(!Check()){
        LOG_ERROR("ArchTypePreloadInstance::CreateUnit", "incomplete");
        return ARCHTYPE_PRELOAD_INVALID_INDEX;
    }

    const size_t old = count_;
    AppendUnits(num);
    count_ += num;
    return old;
}

void ArchTypePreloadInstance::DeleteUnit(size_t index){
    if(!Check()){
        LOG_ERROR("ArchTypePreloadInstance::DeleteUnit", "incomplete");
        return;
    }
    if(index >= count_){
        LOG_ERROR("ArchTypePreloadInstance::DeleteUnit", "index oversize");
        return;
    }

    const size_t last = count_ - 1;
    if(index != last){
        SwapUnitInArrays(index, last);
    }

    DeleteUnitInArrays(last);
    --count_;
}

void ArchTypePreloadInstance::Clear(){
    while(count_ > 0){
        DeleteUnit(count_ - 1);
    }
}

size_t ArchTypePreloadInstance::CopyUnitFrom(const ArchTypePreloadInstance& other, size_t otherIndex){
    if(!Check() || !other.Check()){
        LOG_ERROR("ArchTypePreloadInstance::CopyUnitFrom", "incomplete");
        return ARCHTYPE_PRELOAD_INVALID_INDEX;
    }
    if(description_ != other.description_){
        LOG_ERROR("ArchTypePreloadInstance::CopyUnitFrom", "description mismatch");
        return ARCHTYPE_PRELOAD_INVALID_INDEX;
    }
    if(otherIndex >= other.count_){
        LOG_ERROR("ArchTypePreloadInstance::CopyUnitFrom", "source index oversize");
        return ARCHTYPE_PRELOAD_INVALID_INDEX;
    }

    const size_t newIndex = count_;
    CopyAppendUnitFrom(other, otherIndex);
    ++count_;
    return newIndex;
}

size_t ArchTypePreloadInstance::CopyRangeFrom(const ArchTypePreloadInstance& other,
                                              size_t beginIndex,
                                              size_t num){
    if(num == 0){
        return count_;
    }

    if(!Check() || !other.Check()){
        LOG_ERROR("ArchTypePreloadInstance::CopyRangeFrom", "incomplete");
        return ARCHTYPE_PRELOAD_INVALID_INDEX;
    }
    if(description_ != other.description_){
        LOG_ERROR("ArchTypePreloadInstance::CopyRangeFrom", "description mismatch");
        return ARCHTYPE_PRELOAD_INVALID_INDEX;
    }
    if(beginIndex + num > other.count_){
        LOG_ERROR("ArchTypePreloadInstance::CopyRangeFrom", "range oversize");
        return ARCHTYPE_PRELOAD_INVALID_INDEX;
    }

    const size_t old = count_;
    for(size_t i = 0; i < num; ++i){
        if(CopyUnitFrom(other, beginIndex + i) == ARCHTYPE_PRELOAD_INVALID_INDEX){
            return ARCHTYPE_PRELOAD_INVALID_INDEX;
        }
    }
    return old;
}

size_t ArchTypePreloadInstance::MergeFrom(const ArchTypePreloadInstance& other){
    if(!Check() || !other.Check()){
        LOG_ERROR("ArchTypePreloadInstance::MergeFrom", "incomplete");
        return 0;
    }
    if(description_ != other.description_){
        LOG_ERROR("ArchTypePreloadInstance::MergeFrom", "description mismatch");
        return 0;
    }

    size_t mergedCount = 0;
    for(size_t i = 0; i < other.count_; ++i){
        if(CopyUnitFrom(other, i) != ARCHTYPE_PRELOAD_INVALID_INDEX){
            ++mergedCount;
        }
    }
    return mergedCount;
}

void ArchTypePreloadInstance::ReleaseOwnedResources(){
    if(storageReleased_){
        return;
    }

    storageReleased_ = true;

    if(manager_ != nullptr && description_ != nullptr){
        manager_->ReleaseArchTypePreloadStorage(this);
    }

    ResetMetadataOnly();
    addr2ComponentDenseArray_.clear();
    description_ = nullptr;
    manager_ = nullptr;
    isDestroyed_ = true;
}

void ArchTypePreloadInstance::ResetMetadataOnly(){
    count_ = 0;
}

void ArchTypePreloadInstance::AppendUnits(size_t num){
    for(size_t i = 0; i < description_->defaultAppendNFunctions_.size(); ++i){
        description_->defaultAppendNFunctions_[i](addr2ComponentDenseArray_[i], num);
    }
}

void ArchTypePreloadInstance::DeleteUnitInArrays(size_t index){
    for(size_t i = 0; i < description_->deleteFunctions_.size(); ++i){
        description_->deleteFunctions_[i](addr2ComponentDenseArray_[i], index);
    }
}

void ArchTypePreloadInstance::SwapUnitInArrays(size_t indexA, size_t indexB){
    if(indexA == indexB){
        return;
    }

    for(size_t i = 0; i < description_->swapFunctions_.size(); ++i){
        description_->swapFunctions_[i](addr2ComponentDenseArray_[i], indexA, indexB);
    }
}

void ArchTypePreloadInstance::CopyAppendUnitFrom(const ArchTypePreloadInstance& other, size_t otherIndex){
    for(size_t i = 0; i < description_->copyAppendBetweenFunctions_.size(); ++i){
        description_->copyAppendBetweenFunctions_[i](
            other.addr2ComponentDenseArray_[i],
            otherIndex,
            addr2ComponentDenseArray_[i]
        );
    }
}

size_t ArchTypePreloadInstance::RegisterRangeToArchType(
    ArchType& target,
    const EntityID* entityIDs,
    size_t preloadBegin,
    size_t count)
{
    return RegisterRangeToArchTypeByMask(
        target,
        entityIDs,
        nullptr,
        preloadBegin,
        count
    );
}

size_t ArchTypePreloadInstance::RegisterRangeToArchTypeByMask(
    ArchType& target,
    const EntityID* entityIDs,
    const uint8_t* passMask,
    size_t preloadBegin,
    size_t count)
{
    if(count == 0){
        return 0;
    }

    if(entityIDs == nullptr){
        LOG_ERROR("ArchTypePreloadInstance::RegisterRangeToArchTypeByMask",
                  "entityIDs is null");
        return 0;
    }

    if(!Check() || !target.Check()){
        LOG_ERROR("ArchTypePreloadInstance::RegisterRangeToArchTypeByMask",
                  "incomplete");
        return 0;
    }

    if(description_ != target.description_){
        LOG_ERROR("ArchTypePreloadInstance::RegisterRangeToArchTypeByMask",
                  "description mismatch");
        return 0;
    }

    if(preloadBegin + count > count_){
        LOG_ERROR("ArchTypePreloadInstance::RegisterRangeToArchTypeByMask",
                  "range oversize");
        return 0;
    }

    size_t passedCount = 0;
    for(size_t i = 0; i < count; ++i){
        if(passMask == nullptr || passMask[i] != 0){
            ++passedCount;
        }
    }

    if(passedCount == 0){
        return 0;
    }

    for(size_t i = 0; i < count; ++i){
        if(passMask != nullptr && passMask[i] == 0){
            continue;
        }

        if(target.entityID2Unit_.count(entityIDs[i]) != 0){
            LOG_ERROR("ArchTypePreloadInstance::RegisterRangeToArchTypeByMask",
                      "entity already exists");
            return 0;
        }
    }

    const size_t newBegin = target.activeCount_;
    target.AppendUnits(passedCount);

    size_t writeOffset = 0;
    for(size_t i = 0; i < count; ++i){
        if(passMask != nullptr && passMask[i] == 0){
            continue;
        }

        const size_t srcIndex = preloadBegin + i;
        const size_t dstIndex = newBegin + writeOffset;

        for(size_t k = 0; k < description_->copyAppendBetweenFunctions_.size(); ++k){
            description_->copyAppendBetweenFunctions_[k](
                addr2ComponentDenseArray_[k],
                srcIndex,
                target.activeAddr2ComponentDenseArray_[k]
            );
        }

        target.index2EntityID_.push_back(entityIDs[i]);
        target.entityID2Unit_[entityIDs[i]] = dstIndex;
        target.activeGenerationPerUnit_.push_back(1u);
        ++writeOffset;
    }

    target.activeCount_ += passedCount;
    return passedCount;
}

size_t ArchTypePreloadInstance::RegisterAllToArchType(
    ArchType& target,
    const EntityID* entityIDs,
    size_t entityCount)
{
    if(entityCount != count_){
        LOG_ERROR("ArchTypePreloadInstance::RegisterAllToArchType",
                  "entityCount mismatch");
        return 0;
    }

    return RegisterRangeToArchType(target, entityIDs, 0, count_);
}

size_t ArchTypePreloadInstance::RegisterAllToArchTypeByMask(
    ArchType& target,
    const EntityID* entityIDs,
    const uint8_t* passMask,
    size_t entityCount)
{
    if(entityCount != count_){
        LOG_ERROR("ArchTypePreloadInstance::RegisterAllToArchTypeByMask",
                  "entityCount mismatch");
        return 0;
    }

    return RegisterRangeToArchTypeByMask(
        target,
        entityIDs,
        passMask,
        0,
        count_
    );
}

} // namespace ECS::Core
