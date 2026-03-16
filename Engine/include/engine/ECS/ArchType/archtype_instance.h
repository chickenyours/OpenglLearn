#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <vector>

#include "engine/ECS/data_type.h"
#include "engine/ToolAndAlgorithm/DateType/chunk_array.h"
#include "engine/DebugTool/ConsoleHelp/color_log.h"

namespace ECS::Core{
    class ArchTypeManager;
    class ArchTypeDescription;
    class Scene;

    constexpr size_t ARCHTYPE_SMALL  = 32;
    constexpr size_t ARCHTYPE_MEDIUM = 128;
    constexpr size_t ARCHTYPE_LARGE  = 512;
    constexpr size_t ARCHTYPE_INVALID_INDEX = static_cast<size_t>(-1);

    class ArchType{
        friend class ArchTypeDescription;
        friend class ArchTypeManager;
        friend class Scene;

        template <typename ComponentT>
        friend struct EntityComponentHandle;

    public:
        ArchType(ArchTypeDescription* description, ArchTypeManager* manager, size_t sizePerChuck);
        ArchType(const ArchType&) = delete;
        ArchType& operator=(const ArchType&) = delete;
        ArchType(ArchType&&) = delete;
        ArchType& operator=(ArchType&&) = delete;
        ~ArchType();

        bool Check() const;
        bool IsAlive() const { return !isDestroyed_; }

        template <typename ComponentT>
        const FixedChunkArray<ComponentT>* TryCastComponentArray() const;

        template <typename ComponentT>
        const FixedChunkArray<ComponentT>* TryCastActiveComponentArray() const;

        template <typename ComponentT>
        const FixedChunkArray<ComponentT>* TryCastPreloadComponentArray() const;

        size_t ActiveCount() const { return activeCount_; }
        size_t PreloadCount() const { return preloadCount_; }
        size_t TotalUsed() const { return activeCount_ + preloadCount_; }

        size_t CreateUnit(size_t num = 1);
        size_t CreateEntity(EntityID entity);
        size_t CreateReservation(ReservationID reservationID);
        size_t RegisterPreloadToEntity(size_t preloadIndex, EntityID entity);
        size_t RegisterPreloadToEntity(ReservationID reservationID, EntityID entity);

        void DeleteUnit(size_t index);
        void DeleteEntity(EntityID entity);
        void DeleteReservation(ReservationID reservationID);

    private:
        size_t activeCount_ = 0;
        size_t preloadCount_ = 0;
        size_t sizePerChuck_ = 0;

        std::vector<void*> activeAddr2ComponentDenseArray_;
        std::vector<EntityID> index2EntityID_;
        std::unordered_map<EntityID, size_t> entityID2Unit_;
        std::vector<uint32_t> activeGenerationPerUnit_;

        std::vector<void*> preloadAddr2ComponentDenseArray_;
        std::unordered_map<ReservationID, size_t> reservationID2Index_;
        std::vector<ReservationID> index2ReservationID_;
        std::vector<uint32_t> preloadGenerationPerUnit_;

        ArchTypeDescription* description_ = nullptr;
        ArchTypeManager* manager_ = nullptr;
        bool isDestroyed_ = false;
        bool storageReleased_ = false;

        

        void ReleaseOwnedResources();
        void DetachFromManager();
        void MarkStorageReleased();
        void ResetMetadataOnly();

        void AppendUnits(std::vector<void*>& arrays, size_t num);
        void DeleteUnitInArrays(std::vector<void*>& arrays, size_t index);
        void SwapUnitInArrays(std::vector<void*>& arrays, size_t indexA, size_t indexB);
        void SwapUnitBetween(std::vector<void*>& arraysA, size_t indexA,
                             std::vector<void*>& arraysB, size_t indexB);

        void DeleteActiveUnit(size_t index);
        void DeletePreloadUnit(size_t index);
    };
}

#include "engine/ECS/ArchType/archtype_instance_impl.h"
