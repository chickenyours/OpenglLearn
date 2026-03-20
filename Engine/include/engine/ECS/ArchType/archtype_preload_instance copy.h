#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <vector>
#include <queue>

#include "engine/ECS/data_type.h"
#include "engine/ToolAndAlgorithm/DateType/chunk_array.h"
#include "engine/DebugTool/ConsoleHelp/color_log.h"
#include "engine/ECS/ArchType/archtype_description.h"

namespace ECS::Core{
    class ArchType;
    class ArchTypeManager;
    class ArchTypeDescription;

    constexpr size_t ARCHTYPE_PRELOAD_INVALID_INDEX = static_cast<size_t>(-1);

    class ArchTypePreloadInstance{
        friend class ArchTypeManager;
        friend class ArchType;

    public:
        ArchTypePreloadInstance(ArchTypeDescription* description,
                                ArchTypeManager* manager,
                                size_t sizePerChuck)
            : sizePerChuck_(sizePerChuck),
              description_(description),
              manager_(manager) {}

        ArchTypePreloadInstance(const ArchTypePreloadInstance&) = delete;
        ArchTypePreloadInstance& operator=(const ArchTypePreloadInstance&) = delete;
        ArchTypePreloadInstance(ArchTypePreloadInstance&&) = delete;
        ArchTypePreloadInstance& operator=(ArchTypePreloadInstance&&) = delete;

        ~ArchTypePreloadInstance(){
            ReleaseOwnedResources();
        }

        bool Check() const{
            if(isDestroyed_ || description_ == nullptr){
                return false;
            }

            const size_t kinds = description_->addFunctions_.size();
            return kinds == description_->deleteFunctions_.size()
                && kinds == description_->swapFunctions_.size()
                && kinds == description_->copyAppendBetweenFunctions_.size()
                && kinds == addr2ComponentDenseArray_.size();
        }

        bool IsAlive() const { return !isDestroyed_; }

        template <typename ComponentT>
        const FixedChunkArray<ComponentT>* TryCastComponentArray() const{
            if(!Check() || description_ == nullptr){
                return nullptr;
            }

            auto it = description_->componentType2Index_.find(std::type_index(typeid(ComponentT)));
            if(it == description_->componentType2Index_.end()){
                return nullptr;
            }

            const size_t idx = it->second;
            if(idx >= addr2ComponentDenseArray_.size()){
                return nullptr;
            }

            return reinterpret_cast<const FixedChunkArray<ComponentT>*>(addr2ComponentDenseArray_[idx]);
        }

        size_t Count() const { return count_; }

        bool HasReservation(ReservationID reservationID) const{
            return reservationID2Index_.find(reservationID) != reservationID2Index_.end();
        }

        size_t FindReservationIndex(ReservationID reservationID) const{
            auto it = reservationID2Index_.find(reservationID);
            if(it == reservationID2Index_.end()){
                return ARCHTYPE_PRELOAD_INVALID_INDEX;
            }
            return it->second;
        }

        uint32_t GetGeneration(size_t index) const{
            if(index >= generationPerUnit_.size()){
                return 0;
            }
            return generationPerUnit_[index];
        }

        ReservationID GetReservationID(size_t index) const{
            if(index >= index2ReservationID_.size()){
                return ReservationID{};
            }
            return index2ReservationID_[index];
        }

        size_t CreateUnit(size_t num = 1){
            if(num == 0){
                return count_;
            }
            if(!Check()){
                LOG_ERROR("ArchTypePreloadInstance::CreateUnit","incomplete");
                return ARCHTYPE_PRELOAD_INVALID_INDEX;
            }

            const size_t old = count_;
            AppendUnits(num);

            count_ += num;
            generationPerUnit_.insert(generationPerUnit_.end(), num, 1u);
            index2ReservationID_.insert(index2ReservationID_.end(), num, ReservationID{});
            return old;
        }

        size_t CreateReservation(ReservationID reservationID){
            if(!Check()){
                LOG_ERROR("ArchTypePreloadInstance::CreateReservation","incomplete");
                return ARCHTYPE_PRELOAD_INVALID_INDEX;
            }
            if(reservationID2Index_.count(reservationID) != 0){
                LOG_ERROR("ArchTypePreloadInstance::CreateReservation","reservation already exists");
                return ARCHTYPE_PRELOAD_INVALID_INDEX;
            }

            AppendUnits(1);

            const size_t index = count_;
            ++count_;

            index2ReservationID_.push_back(reservationID);
            generationPerUnit_.push_back(1u);
            reservationID2Index_[reservationID] = index;
            return index;
        }

        void BindReservation(size_t index, ReservationID reservationID){
            if(index >= count_){
                LOG_ERROR("ArchTypePreloadInstance::BindReservation","index oversize");
                return;
            }

            if(reservationID2Index_.count(reservationID) != 0){
                LOG_ERROR("ArchTypePreloadInstance::BindReservation","reservation already exists");
                return;
            }

            ReservationID oldID = index2ReservationID_[index];
            if(reservationID2Index_.count(oldID) != 0){
                reservationID2Index_.erase(oldID);
            }

            index2ReservationID_[index] = reservationID;
            reservationID2Index_[reservationID] = index;
            ++generationPerUnit_[index];
        }

        void UnbindReservation(size_t index){
            if(index >= count_){
                LOG_ERROR("ArchTypePreloadInstance::UnbindReservation","index oversize");
                return;
            }

            ReservationID oldID = index2ReservationID_[index];
            if(reservationID2Index_.count(oldID) != 0){
                reservationID2Index_.erase(oldID);
            }
            index2ReservationID_[index] = ReservationID{};
            ++generationPerUnit_[index];
        }

        void DeleteUnit(size_t index){
            if(!Check()){
                LOG_ERROR("ArchTypePreloadInstance::DeleteUnit","incomplete");
                return;
            }
            if(index >= count_){
                LOG_ERROR("ArchTypePreloadInstance::DeleteUnit","oversize");
                return;
            }

            const size_t last = count_ - 1;
            const ReservationID deletedReservationID = index2ReservationID_[index];
            const bool hadReservation = reservationID2Index_.count(deletedReservationID) != 0;

            if(index != last){
                SwapUnitInArrays(index, last);

                const ReservationID movedReservationID = index2ReservationID_[last];
                index2ReservationID_[index] = movedReservationID;
                if(reservationID2Index_.count(movedReservationID) != 0){
                    reservationID2Index_[movedReservationID] = index;
                }
                ++generationPerUnit_[index];
            }

            DeleteUnitInArrays(last);

            if(hadReservation){
                reservationID2Index_.erase(deletedReservationID);
            }

            index2ReservationID_.pop_back();
            generationPerUnit_.pop_back();
            --count_;
        }

        void DeleteReservation(ReservationID reservationID){
            auto it = reservationID2Index_.find(reservationID);
            if(it == reservationID2Index_.end()){
                LOG_ERROR("ArchTypePreloadInstance::DeleteReservation","reservation not found");
                return;
            }
            DeleteUnit(it->second);
        }

        void Clear(){
            while(count_ > 0){
                DeleteUnit(count_ - 1);
            }
        }

        size_t CopyUnitFrom(const ArchTypePreloadInstance& other, size_t otherIndex){
            if(!Check() || !other.Check()){
                LOG_ERROR("ArchTypePreloadInstance::CopyUnitFrom","incomplete");
                return ARCHTYPE_PRELOAD_INVALID_INDEX;
            }
            if(description_ != other.description_){
                LOG_ERROR("ArchTypePreloadInstance::CopyUnitFrom","description mismatch");
                return ARCHTYPE_PRELOAD_INVALID_INDEX;
            }
            if(otherIndex >= other.count_){
                LOG_ERROR("ArchTypePreloadInstance::CopyUnitFrom","source index oversize");
                return ARCHTYPE_PRELOAD_INVALID_INDEX;
            }

            const size_t newIndex = count_;
            CopyAppendUnitFrom(other, otherIndex);

            ++count_;
            generationPerUnit_.push_back(1u);

            ReservationID copiedReservationID = other.index2ReservationID_[otherIndex];
            if(copiedReservationID != ReservationID{} && reservationID2Index_.count(copiedReservationID) == 0){
                index2ReservationID_.push_back(copiedReservationID);
                reservationID2Index_[copiedReservationID] = newIndex;
            }else{
                index2ReservationID_.push_back(ReservationID{});
            }

            return newIndex;
        }

        size_t CopyReservationFrom(const ArchTypePreloadInstance& other, ReservationID reservationID){
            auto it = other.reservationID2Index_.find(reservationID);
            if(it == other.reservationID2Index_.end()){
                LOG_ERROR("ArchTypePreloadInstance::CopyReservationFrom","reservation not found");
                return ARCHTYPE_PRELOAD_INVALID_INDEX;
            }
            return CopyUnitFrom(other, it->second);
        }

        size_t CopyRangeFrom(const ArchTypePreloadInstance& other, size_t beginIndex, size_t num){
            if(num == 0){
                return count_;
            }
            if(!Check() || !other.Check()){
                LOG_ERROR("ArchTypePreloadInstance::CopyRangeFrom","incomplete");
                return ARCHTYPE_PRELOAD_INVALID_INDEX;
            }
            if(description_ != other.description_){
                LOG_ERROR("ArchTypePreloadInstance::CopyRangeFrom","description mismatch");
                return ARCHTYPE_PRELOAD_INVALID_INDEX;
            }
            if(beginIndex + num > other.count_){
                LOG_ERROR("ArchTypePreloadInstance::CopyRangeFrom","range oversize");
                return ARCHTYPE_PRELOAD_INVALID_INDEX;
            }

            const size_t old = count_;
            for(size_t i = 0; i < num; ++i){
                CopyUnitFrom(other, beginIndex + i);
            }
            return old;
        }

        size_t MergeFrom(const ArchTypePreloadInstance& other, bool skipDuplicateReservation = true){
            if(!Check() || !other.Check()){
                LOG_ERROR("ArchTypePreloadInstance::MergeFrom","incomplete");
                return 0;
            }
            if(description_ != other.description_){
                LOG_ERROR("ArchTypePreloadInstance::MergeFrom","description mismatch");
                return 0;
            }

            size_t mergedCount = 0;
            for(size_t i = 0; i < other.count_; ++i){
                ReservationID rid = other.index2ReservationID_[i];
                if(skipDuplicateReservation && rid != ReservationID{} && reservationID2Index_.count(rid) != 0){
                    continue;
                }

                if(CopyUnitFrom(other, i) != ARCHTYPE_PRELOAD_INVALID_INDEX){
                    ++mergedCount;
                }
            }
            return mergedCount;
        }

    private:
        size_t count_ = 0;
        size_t sizePerChuck_ = 0;

        std::vector<void*> addr2ComponentDenseArray_;
        
        ArchTypeDescription* description_ = nullptr;
        ArchTypeManager* manager_ = nullptr;

        bool isDestroyed_ = false;
        bool storageReleased_ = false;

    private:
        void ReleaseOwnedResources(){
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

        void ResetMetadataOnly(){
            count_ = 0;
            reservationID2Index_.clear();
            index2ReservationID_.clear();
            generationPerUnit_.clear();
        }

        void AppendUnits(size_t num){
            for(size_t i = 0; i < description_->addFunctions_.size(); ++i){
                for(size_t j = 0; j < num; ++j){
                    description_->addFunctions_[i](addr2ComponentDenseArray_[i]);
                }
            }
        }

        void DeleteUnitInArrays(size_t index){
            for(size_t i = 0; i < description_->deleteFunctions_.size(); ++i){
                description_->deleteFunctions_[i](addr2ComponentDenseArray_[i], index);
            }
        }

        void SwapUnitInArrays(size_t indexA, size_t indexB){
            if(indexA == indexB){
                return;
            }
            for(size_t i = 0; i < description_->swapFunctions_.size(); ++i){
                description_->swapFunctions_[i](addr2ComponentDenseArray_[i], indexA, indexB);
            }
        }

        void CopyAppendUnitFrom(const ArchTypePreloadInstance& other, size_t otherIndex){
            for(size_t i = 0; i < description_->copyAppendBetweenFunctions_.size(); ++i){
                description_->copyAppendBetweenFunctions_[i](
                    other.addr2ComponentDenseArray_[i],
                    otherIndex,
                    addr2ComponentDenseArray_[i]
                );
            }
        }

        size_t RegisterRangeToArchType(
            ArchType& target,
            const EntityID* entityIDs,
            size_t preloadBegin,
            size_t count
        );

        size_t RegisterAllToArchType(
            ArchType& target,
            const EntityID* entityIDs,
            size_t entityCount
        );

        size_t RegisterReservationListToArchType(
            ArchType& target,
            const ReservationID* reservationIDs,
            const EntityID* entityIDs,
            size_t count
        );
    };
}