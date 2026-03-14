#pragma once

#include <vector>
#include <typeindex>
#include <string>
#include <unordered_map>

#include "engine/ECS/ArchType/archtype_manager.h"

namespace ECS::Core{
    class ArchTypeManager;
    class ArchTypeDescription;
    class Scene;
}

namespace ECS::Core{

    constexpr size_t ARCHTYPE_SMALL  = 32;
    constexpr size_t ARCHTYPE_MEDIUM = 128;
    constexpr size_t ARCHTYPE_LARGE  = 512;

    class ArchType{
        friend class ArchTypeDescription;
        friend class ArchTypeManager;
        friend class Scene;

    public:
        bool Check() const {
            return manager_
                && description_
                && description_->addFunctions_.size() == activeAddr2ComponentDenseArray_.size()
                && description_->addFunctions_.size() == preloadAddr2ComponentDenseArray_.size();
        }

        ~ArchType(){
            if(!manager_ || !description_){
                LOG_ERROR("ArchType::~ArchType","no manager_ or description_, fail to release");
                return;
            }
            manager_->ReleaseArchType(this);
        }

        // 默认返回正式实体区组件数组（保持 query 语义更自然）
        template <typename ComponentT>
        const FixedChunkArray<ComponentT>* TryCastComponentArray() const {
            return TryCastActiveComponentArray<ComponentT>();
        }

        template <typename ComponentT>
        const FixedChunkArray<ComponentT>* TryCastActiveComponentArray() const {
            if(description_){
                auto it = description_->componentArrayDescription_.find(std::type_index(typeid(ComponentT)));
                if(it == description_->componentArrayDescription_.end()){
                    LOG_ERROR("ArchType::TryCastActiveComponentArray", "no component type in this archtype");
                    return nullptr;
                }
                size_t index = it->second;
                if(index >= activeAddr2ComponentDenseArray_.size()){
                    LOG_ERROR("ArchType::TryCastActiveComponentArray", "index is over size " + std::to_string(index));
                    return nullptr;
                }
                return reinterpret_cast<const FixedChunkArray<ComponentT>*>(activeAddr2ComponentDenseArray_[index]);
            }
            return nullptr;
        }

        template <typename ComponentT>
        const FixedChunkArray<ComponentT>* TryCastPreloadComponentArray() const {
            if(description_){
                auto it = description_->componentArrayDescription_.find(std::type_index(typeid(ComponentT)));
                if(it == description_->componentArrayDescription_.end()){
                    LOG_ERROR("ArchType::TryCastPreloadComponentArray", "no component type in this archtype");
                    return nullptr;
                }
                size_t index = it->second;
                if(index >= preloadAddr2ComponentDenseArray_.size()){
                    LOG_ERROR("ArchType::TryCastPreloadComponentArray", "index is over size " + std::to_string(index));
                    return nullptr;
                }
                return reinterpret_cast<const FixedChunkArray<ComponentT>*>(preloadAddr2ComponentDenseArray_[index]);
            }
            return nullptr;
        }

        // --------------------------------------------------------------------
        // 数量
        // --------------------------------------------------------------------
        size_t ActiveCount() const { return activeCount_; }
        size_t PreloadCount() const { return preloadCount_; }
        size_t TotalUsed() const { return activeCount_ + preloadCount_; }

        // --------------------------------------------------------------------
        // 创建
        // --------------------------------------------------------------------
        // 保持原函数名：默认创建到预载区
        size_t CreateUnit(size_t num = 1){
            if(!Check()){
                LOG_ERROR("ArchType::CreateUnit","incomplete");
                return 0;
            }
            AppendUnits(preloadAddr2ComponentDenseArray_, num);
            size_t old = preloadCount_;
            preloadCount_ += num;
            return old; // 返回预载区起始 index
        }

        // 直接创建正式实体
        size_t CreateEntity(EntityID entity){
            if(!Check()){
                LOG_ERROR("ArchType::CreateEntity","incomplete");
                return static_cast<size_t>(-1);
            }
            if(entityID2Unit_.count(entity)){
                LOG_ERROR("ArchType::CreateEntity","entity already registered");
                return static_cast<size_t>(-1);
            }

            AppendUnits(activeAddr2ComponentDenseArray_, 1);

            size_t index = activeCount_;
            activeCount_++;

            index2EntityID_.push_back(entity);
            entityID2Unit_[entity] = index;
            activeGenerationPerUnit_.push_back(1);

            return index;
        }

        // 创建预载并登记 ReservationID
        size_t CreateReservation(ReservationID reservationID){
            if(!Check()){
                LOG_ERROR("ArchType::CreateReservation","incomplete");
                return static_cast<size_t>(-1);
            }
            if(reservationID2Index_.count(reservationID)){
                LOG_ERROR("ArchType::CreateReservation","reservation already exists");
                return static_cast<size_t>(-1);
            }

            AppendUnits(preloadAddr2ComponentDenseArray_, 1);

            size_t index = preloadCount_;
            preloadCount_++;

            index2ReservationID_.push_back(reservationID);
            reservationID2Index_[reservationID] = index;
            preloadGenerationPerUnit_.push_back(1);

            return index;
        }

        // 把预载区 index 登记为实体
        size_t RegisterPreloadToEntity(size_t preloadIndex, EntityID entity){
            if(!Check()){
                LOG_ERROR("ArchType::RegisterPreloadToEntity","incomplete");
                return static_cast<size_t>(-1);
            }
            if(preloadIndex >= preloadCount_){
                LOG_ERROR("ArchType::RegisterPreloadToEntity","preload index oversize");
                return static_cast<size_t>(-1);
            }
            if(entityID2Unit_.count(entity)){
                LOG_ERROR("ArchType::RegisterPreloadToEntity","entity already registered");
                return static_cast<size_t>(-1);
            }

            // 1. active 尾插一个默认构造槽
            AppendUnits(activeAddr2ComponentDenseArray_, 1);

            size_t newActiveIndex = activeCount_;

            // 2. 把 preload[preloadIndex] 和 active[newActiveIndex] 交换
            //    这样可复用现有 swap 机制，无需新增 move API
            SwapUnitBetween(preloadAddr2ComponentDenseArray_, preloadIndex,
                            activeAddr2ComponentDenseArray_, newActiveIndex);

            // 3. active 建立实体映射
            activeCount_++;
            index2EntityID_.push_back(entity);
            entityID2Unit_[entity] = newActiveIndex;
            activeGenerationPerUnit_.push_back(1);

            // 4. 删除 preload 中那个“被换过去之后”的尾部默认对象
            //    此时 preload[preloadIndex] 是从 active 新建尾槽换回来的默认对象
            DeletePreloadUnit(preloadIndex);

            return newActiveIndex;
        }

        // 按 ReservationID 登记
        size_t RegisterPreloadToEntity(ReservationID reservationID, EntityID entity){
            auto it = reservationID2Index_.find(reservationID);
            if(it == reservationID2Index_.end()){
                LOG_ERROR("ArchType::RegisterPreloadToEntity","reservation not found");
                return static_cast<size_t>(-1);
            }
            return RegisterPreloadToEntity(it->second, entity);
        }

        // --------------------------------------------------------------------
        // 删除
        // --------------------------------------------------------------------
        // 保持兼容：全局逻辑 index
        // [0, activeCount_) => active
        // [activeCount_, activeCount_ + preloadCount_) => preload
        void DeleteUnit(size_t index){
            if(!Check()){
                LOG_ERROR("ArchType::DeleteUnit","incomplete");
                return;
            }

            if(index < activeCount_){
                DeleteActiveUnit(index);
                return;
            }

            size_t preloadIndex = index - activeCount_;
            if(preloadIndex < preloadCount_){
                DeletePreloadUnit(preloadIndex);
                return;
            }

            LOG_ERROR("ArchType::DeleteUnit","oversize");
        }

        void DeleteEntity(EntityID entity){
            auto it = entityID2Unit_.find(entity);
            if(it == entityID2Unit_.end()){
                LOG_ERROR("ArchType::DeleteEntity","not registered");
                return;
            }

            size_t index = it->second;
#ifdef DEVELOP
            if(index >= activeCount_){
                LOG_ERROR("ArchType::DeleteEntity","system error");
            }
#endif
            DeleteActiveUnit(index);
        }

        void DeleteReservation(ReservationID reservationID){
            auto it = reservationID2Index_.find(reservationID);
            if(it == reservationID2Index_.end()){
                LOG_ERROR("ArchType::DeleteReservation","reservation not found");
                return;
            }
            DeletePreloadUnit(it->second);
        }

    private:
        // --------------------------------------------------------------------
        // 数据：拆分为两个容器
        // --------------------------------------------------------------------
        size_t activeCount_ = 0;
        size_t preloadCount_ = 0;
        size_t sizePerChuck_;

        // 正式实体区
        std::vector<void*> activeAddr2ComponentDenseArray_;
        std::vector<EntityID> index2EntityID_;
        std::unordered_map<EntityID, size_t> entityID2Unit_;
        std::vector<uint32_t> activeGenerationPerUnit_;

        // 预载区
        std::vector<void*> preloadAddr2ComponentDenseArray_;
        std::unordered_map<ReservationID, size_t> reservationID2Index_;
        std::vector<ReservationID> index2ReservationID_;
        std::vector<uint32_t> preloadGenerationPerUnit_;

        ArchTypeDescription* description_ = nullptr;
        ArchTypeManager* manager_ = nullptr;

        ArchType(ArchTypeDescription* description, ArchTypeManager* manager, size_t sizePerChuck)
            : description_(description), sizePerChuck_(sizePerChuck), manager_(manager) {}

        // --------------------------------------------------------------------
        // 组件数组统一操作
        // --------------------------------------------------------------------
        void AppendUnits(std::vector<void*>& arrays, size_t num){
            for(size_t i = 0; i < description_->addFunctions_.size(); ++i){
                for(size_t j = 0; j < num; ++j){
                    description_->addFunctions_[i](arrays[i]);
                }
            }
        }

        void DeleteUnitInArrays(std::vector<void*>& arrays, size_t index){
            for(size_t i = 0; i < description_->deleteFunctions_.size(); ++i){
                description_->deleteFunctions_[i](arrays[i], index);
            }
        }

        void SwapUnitInArrays(std::vector<void*>& arrays, size_t indexA, size_t indexB){
            if(indexA == indexB) return;
            for(size_t i = 0; i < description_->swapFunctions_.size(); ++i){
                description_->swapFunctions_[i](arrays[i], indexA, indexB);
            }
        }

        void SwapUnitBetween(std::vector<void*>& arraysA, size_t indexA,
                             std::vector<void*>& arraysB, size_t indexB){
            if(arraysA.size() != arraysB.size()){
                LOG_ERROR("ArchType::SwapUnitBetween","component array size mismatch");
                return;
            }
            for(size_t i = 0; i < description_->swapBetweenFunctions_.size(); ++i){
                description_->swapBetweenFunctions_[i](arraysA[i], indexA, arraysB[i], indexB);
            }
        }

        // --------------------------------------------------------------------
        // active 删除
        // --------------------------------------------------------------------
        void DeleteActiveUnit(size_t index){
            if(index >= activeCount_){
                LOG_ERROR("ArchType::DeleteActiveUnit","oversize");
                return;
            }
            if(activeCount_ == 0){
                return;
            }

            const size_t last = activeCount_ - 1;
            EntityID deletedEntityID = index2EntityID_[index];

            // 1. 若不是最后一个 active，把最后一个 active 换到当前位置
            if(index != last){
                SwapUnitInArrays(activeAddr2ComponentDenseArray_, index, last);

                EntityID movedEntity = index2EntityID_[last];
                index2EntityID_[index] = movedEntity;
                entityID2Unit_[movedEntity] = index;

                if(index < activeGenerationPerUnit_.size()){
                    activeGenerationPerUnit_[index]++;
                }
            }

            // 2. 删掉 active 尾部组件对象
            DeleteUnitInArrays(activeAddr2ComponentDenseArray_, last);

            // 3. 元数据删除
            entityID2Unit_.erase(deletedEntityID);
            index2EntityID_.pop_back();
            activeGenerationPerUnit_.pop_back();
            activeCount_--;
        }

        // --------------------------------------------------------------------
        // preload 删除
        // --------------------------------------------------------------------
        void DeletePreloadUnit(size_t index){
            if(index >= preloadCount_){
                LOG_ERROR("ArchType::DeletePreloadUnit","oversize");
                return;
            }
            if(preloadCount_ == 0){
                return;
            }

            const size_t last = preloadCount_ - 1;
            ReservationID deletedReservationID = index2ReservationID_[index];

            // 1. 若不是最后一个 preload，把最后一个 preload 换到当前位置
            if(index != last){
                SwapUnitInArrays(preloadAddr2ComponentDenseArray_, index, last);

                ReservationID movedReservationID = index2ReservationID_[last];
                index2ReservationID_[index] = movedReservationID;
                reservationID2Index_[movedReservationID] = index;

                if(index < preloadGenerationPerUnit_.size()){
                    preloadGenerationPerUnit_[index]++;
                }
            }

            // 2. 删掉 preload 尾部组件对象
            DeleteUnitInArrays(preloadAddr2ComponentDenseArray_, last);

            // 3. 元数据删除
            reservationID2Index_.erase(deletedReservationID);
            index2ReservationID_.pop_back();
            preloadGenerationPerUnit_.pop_back();
            preloadCount_--;
        }
    };

} // namespace ECS::Core