#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <typeindex>
#include <vector>

#include "engine/ECS/data_type.h"
#include "engine/ToolAndAlgorithm/DateType/chunk_array.h"
#include "engine/DebugTool/ConsoleHelp/color_log.h"

namespace ECS::Core{
    class Scene;
    class ArchType;
    class ArchTypeManager;
    class ArchTypeDescription;

    constexpr size_t ARCHTYPE_PRELOAD_INVALID_INDEX = static_cast<size_t>(-1);

    class ArchTypePreloadInstance{
        friend class ArchTypeManager;
        friend class ArchType;
        friend class Scene;

    public:
        ArchTypePreloadInstance(ArchTypeDescription* description,
                                ArchTypeManager* manager,
                                size_t sizePerChunk);

        ArchTypePreloadInstance(const ArchTypePreloadInstance&) = delete;
        ArchTypePreloadInstance& operator=(const ArchTypePreloadInstance&) = delete;
        ArchTypePreloadInstance(ArchTypePreloadInstance&&) = delete;
        ArchTypePreloadInstance& operator=(ArchTypePreloadInstance&&) = delete;

        ~ArchTypePreloadInstance();

        bool Check() const;
        bool IsAlive() const { return !isDestroyed_; }

        template <typename ComponentT>
        FixedChunkArray<ComponentT>* TryCastComponentArray();

        size_t Count() const { return count_; }

        size_t CreateUnit(size_t num = 1);
        void DeleteUnit(size_t index);
        void Clear();

        size_t CopyUnitFrom(const ArchTypePreloadInstance& other, size_t otherIndex);
        size_t CopyRangeFrom(const ArchTypePreloadInstance& other, size_t beginIndex, size_t num);
        size_t MergeFrom(const ArchTypePreloadInstance& other);

    private:
        size_t count_ = 0;
        size_t sizePerChunk_ = 0;

        std::vector<void*> addr2ComponentDenseArray_;

        ArchTypeDescription* description_ = nullptr;
        ArchTypeManager* manager_ = nullptr;

        bool isDestroyed_ = false;
        bool storageReleased_ = false;

    private:
        void ReleaseOwnedResources();
        void ResetMetadataOnly();

        void AppendUnits(size_t num);
        void DeleteUnitInArrays(size_t index);
        void SwapUnitInArrays(size_t indexA, size_t indexB);
        void CopyAppendUnitFrom(const ArchTypePreloadInstance& other, size_t otherIndex);

        size_t RegisterRangeToArchType(
            ArchType& target,
            const EntityID* entityIDs,
            size_t preloadBegin,
            size_t count
        );

        size_t RegisterRangeToArchTypeByMask(
            ArchType& target,
            const uint8_t* passMask,
            size_t preloadBegin,
            size_t count,
            size_t& outTargetBegin
        );

        size_t RegisterAllToArchType(
            ArchType& target,
            const EntityID* entityIDs,
            size_t entityCount
        );

        size_t RegisterAllToArchTypeByMask(
            ArchType& target,
            const uint8_t* passMask,
            size_t maskCount,
            size_t& outTargetBegin);
    };
}

#include "engine/ECS/ArchType/archtype_preload_instance_impl.h"