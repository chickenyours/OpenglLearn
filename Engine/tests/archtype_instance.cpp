#include "engine/ECS/ArchType/archtype_instance.h"

#include "engine/ECS/ArchType/archtype_description.h"
#include "engine/ECS/ArchType/archtype_manager.h"

namespace ECS::Core{
    ArchType::ArchType(ArchTypeDescription* description, ArchTypeManager* manager, size_t sizePerChuck)
        : sizePerChuck_(sizePerChuck), description_(description), manager_(manager) {}

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
            && kinds == description_->swapBetweenFunctions_.size()
            && kinds == activeAddr2ComponentDenseArray_.size()
            && kinds == preloadAddr2ComponentDenseArray_.size();
    }

    size_t ArchType::CreateUnit(size_t num){
        if(num == 0){
            return preloadCount_;
        }
        if(!Check()){
            LOG_ERROR("ArchType::CreateUnit","incomplete");
            return ARCHTYPE_INVALID_INDEX;
        }

        const size_t old = preloadCount_;
        AppendUnits(preloadAddr2ComponentDenseArray_, num);
        preloadCount_ += num;
        preloadGenerationPerUnit_.insert(preloadGenerationPerUnit_.end(), num, 1u);
        index2ReservationID_.insert(index2ReservationID_.end(), num, ReservationID{});
        return old;
    }

    size_t ArchType::CreateEntity(EntityID entity){
        if(!Check()){
            LOG_ERROR("ArchType::CreateEntity","incomplete");
            return ARCHTYPE_INVALID_INDEX;
        }
        if(entityID2Unit_.count(entity)){
            LOG_ERROR("ArchType::CreateEntity","entity already registered");
            return ARCHTYPE_INVALID_INDEX;
        }

        AppendUnits(activeAddr2ComponentDenseArray_, 1);

        const size_t index = activeCount_;
        ++activeCount_;

        index2EntityID_.push_back(entity);
        entityID2Unit_[entity] = index;
        activeGenerationPerUnit_.push_back(1);
        return index;
    }

    size_t ArchType::CreateReservation(ReservationID reservationID){
        if(!Check()){
            LOG_ERROR("ArchType::CreateReservation","incomplete");
            return ARCHTYPE_INVALID_INDEX;
        }
        if(reservationID2Index_.count(reservationID)){
            LOG_ERROR("ArchType::CreateReservation","reservation already exists");
            return ARCHTYPE_INVALID_INDEX;
        }

        AppendUnits(preloadAddr2ComponentDenseArray_, 1);

        const size_t index = preloadCount_;
        ++preloadCount_;

        if(index >= index2ReservationID_.size()){
            index2ReservationID_.push_back(reservationID);
            preloadGenerationPerUnit_.push_back(1);
        }else{
            index2ReservationID_[index] = reservationID;
            preloadGenerationPerUnit_[index] = 1;
        }
        reservationID2Index_[reservationID] = index;
        return index;
    }

    size_t ArchType::RegisterPreloadToEntity(size_t preloadIndex, EntityID entity){
        if(!Check()){
            LOG_ERROR("ArchType::RegisterPreloadToEntity","incomplete");
            return ARCHTYPE_INVALID_INDEX;
        }
        if(preloadIndex >= preloadCount_){
            LOG_ERROR("ArchType::RegisterPreloadToEntity","preload index oversize");
            return ARCHTYPE_INVALID_INDEX;
        }
        if(entityID2Unit_.count(entity)){
            LOG_ERROR("ArchType::RegisterPreloadToEntity","entity already registered");
            return ARCHTYPE_INVALID_INDEX;
        }

        AppendUnits(activeAddr2ComponentDenseArray_, 1);

        const size_t newActiveIndex = activeCount_;
        SwapUnitBetween(preloadAddr2ComponentDenseArray_, preloadIndex,
                        activeAddr2ComponentDenseArray_, newActiveIndex);

        ++activeCount_;
        index2EntityID_.push_back(entity);
        entityID2Unit_[entity] = newActiveIndex;
        activeGenerationPerUnit_.push_back(1);

        DeletePreloadUnit(preloadIndex);
        return newActiveIndex;
    }

    size_t ArchType::RegisterPreloadToEntity(ReservationID reservationID, EntityID entity){
        auto it = reservationID2Index_.find(reservationID);
        if(it == reservationID2Index_.end()){
            LOG_ERROR("ArchType::RegisterPreloadToEntity","reservation not found");
            return ARCHTYPE_INVALID_INDEX;
        }
        return RegisterPreloadToEntity(it->second, entity);
    }

    void ArchType::DeleteUnit(size_t index){
        if(!Check()){
            LOG_ERROR("ArchType::DeleteUnit","incomplete");
            return;
        }

        if(index < activeCount_){
            DeleteActiveUnit(index);
            return;
        }

        const size_t preloadIndex = index - activeCount_;
        if(preloadIndex < preloadCount_){
            DeletePreloadUnit(preloadIndex);
            return;
        }

        LOG_ERROR("ArchType::DeleteUnit","oversize");
    }

    void ArchType::DeleteEntity(EntityID entity){
        auto it = entityID2Unit_.find(entity);
        if(it == entityID2Unit_.end()){
            LOG_ERROR("ArchType::DeleteEntity","not registered");
            return;
        }
        DeleteActiveUnit(it->second);
    }

    void ArchType::DeleteReservation(ReservationID reservationID){
        auto it = reservationID2Index_.find(reservationID);
        if(it == reservationID2Index_.end()){
            LOG_ERROR("ArchType::DeleteReservation","reservation not found");
            return;
        }
        DeletePreloadUnit(it->second);
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
        preloadAddr2ComponentDenseArray_.clear();
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
        preloadCount_ = 0;
        index2EntityID_.clear();
        entityID2Unit_.clear();
        activeGenerationPerUnit_.clear();
        reservationID2Index_.clear();
        index2ReservationID_.clear();
        preloadGenerationPerUnit_.clear();
    }

    void ArchType::AppendUnits(std::vector<void*>& arrays, size_t num){
        for(size_t i = 0; i < description_->addFunctions_.size(); ++i){
            for(size_t j = 0; j < num; ++j){
                description_->addFunctions_[i](arrays[i]);
            }
        }
    }

    void ArchType::DeleteUnitInArrays(std::vector<void*>& arrays, size_t index){
        for(size_t i = 0; i < description_->deleteFunctions_.size(); ++i){
            description_->deleteFunctions_[i](arrays[i], index);
        }
    }

    void ArchType::SwapUnitInArrays(std::vector<void*>& arrays, size_t indexA, size_t indexB){
        if(indexA == indexB){
            return;
        }
        for(size_t i = 0; i < description_->swapFunctions_.size(); ++i){
            description_->swapFunctions_[i](arrays[i], indexA, indexB);
        }
    }

    void ArchType::SwapUnitBetween(std::vector<void*>& arraysA, size_t indexA,
                                   std::vector<void*>& arraysB, size_t indexB){
        if(arraysA.size() != arraysB.size()){
            LOG_ERROR("ArchType::SwapUnitBetween","component array size mismatch");
            return;
        }
        for(size_t i = 0; i < description_->swapBetweenFunctions_.size(); ++i){
            description_->swapBetweenFunctions_[i](arraysA[i], indexA, arraysB[i], indexB);
        }
    }

    void ArchType::DeleteActiveUnit(size_t index){
        if(index >= activeCount_){
            LOG_ERROR("ArchType::DeleteActiveUnit","oversize");
            return;
        }

        const size_t last = activeCount_ - 1;
        const EntityID deletedEntityID = index2EntityID_[index];

        if(index != last){
            SwapUnitInArrays(activeAddr2ComponentDenseArray_, index, last);

            const EntityID movedEntity = index2EntityID_[last];
            index2EntityID_[index] = movedEntity;
            entityID2Unit_[movedEntity] = index;
            ++activeGenerationPerUnit_[index];
        }

        DeleteUnitInArrays(activeAddr2ComponentDenseArray_, last);

        entityID2Unit_.erase(deletedEntityID);
        index2EntityID_.pop_back();
        activeGenerationPerUnit_.pop_back();
        --activeCount_;
    }

    void ArchType::DeletePreloadUnit(size_t index){
        if(index >= preloadCount_){
            LOG_ERROR("ArchType::DeletePreloadUnit","oversize");
            return;
        }

        const size_t last = preloadCount_ - 1;
        const ReservationID deletedReservationID = index2ReservationID_[index];
        const bool hadReservation = reservationID2Index_.count(deletedReservationID) != 0;

        if(index != last){
            SwapUnitInArrays(preloadAddr2ComponentDenseArray_, index, last);

            const ReservationID movedReservationID = index2ReservationID_[last];
            index2ReservationID_[index] = movedReservationID;
            if(reservationID2Index_.count(movedReservationID)){
                reservationID2Index_[movedReservationID] = index;
            }
            ++preloadGenerationPerUnit_[index];
        }

        DeleteUnitInArrays(preloadAddr2ComponentDenseArray_, last);

        if(hadReservation){
            reservationID2Index_.erase(deletedReservationID);
        }
        index2ReservationID_.pop_back();
        preloadGenerationPerUnit_.pop_back();
        --preloadCount_;
    }
}
