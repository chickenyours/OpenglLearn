#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <vector>
#include <deque>
#include <mutex>
#include <condition_variable>

#include "engine/ECS/data_type.h"
#include "engine/ToolAndAlgorithm/DateType/chunk_array.h"
#include "engine/DebugTool/ConsoleHelp/color_log.h"

namespace ECS::Core{
    class ArchTypeManager;
    class ArchTypeDescription;
    class Scene;
    class ChunkSchedule;

    constexpr size_t ARCHTYPE_SMALL  = 32;
    constexpr size_t ARCHTYPE_MEDIUM = 128;
    constexpr size_t ARCHTYPE_LARGE  = 512;
    constexpr size_t ARCHTYPE_INVALID_INDEX = static_cast<size_t>(-1);

    struct ArchChunkMeta {
        mutable std::mutex requestMutex;
        std::condition_variable readerCv;
        std::condition_variable writerCv;

        ChunkHeadState state = ChunkHeadState::IDLE;
        size_t activeReaders = 0;
        size_t waitingReaders = 0;
        size_t waitingWriters = 0;

        ArchChunkMeta() = default;
        ArchChunkMeta(const ArchChunkMeta&) = delete;
        ArchChunkMeta& operator=(const ArchChunkMeta&) = delete;
        ArchChunkMeta(ArchChunkMeta&&) = delete;
        ArchChunkMeta& operator=(ArchChunkMeta&&) = delete;
    };

    class ArchType{
        friend class ArchTypePreloadInstance;
        friend class ArchTypeDescription;
        friend class ArchTypeManager;
        friend class Scene;
        friend class ChunkSchedule;

        template <typename ComponentT>
        friend struct EntityComponentHandle;

    public:
        ArchType(ArchTypeDescription* description, ArchTypeManager* manager, size_t sizePerChunk);
        ~ArchType();

        bool Check() const;
        bool IsAlive() const { return !isDestroyed_; }

        template <typename ComponentT>
        FixedChunkArray<ComponentT>* TryCastComponentArray();

        template <typename ComponentT>
        FixedChunkArray<ComponentT>* TryCastActiveComponentArray();

        size_t ActiveCount() const { return activeCount_; }
        size_t ChunkCount() const { return chunkMetas_.size(); }
        size_t SizePerChunk() const { return sizePerChunk_; }

        size_t CreateEntity(EntityID entity);
        size_t CreateEntities(const EntityID* entityIDs, size_t count);
        void DeleteEntity(EntityID entity);
        void DeleteEntities(const EntityID* entityIDs, size_t count);

        ArchTypeDescription* GetDescription(){ return description_;}

        const std::vector<EntityID>& GetIndexEntities(){ return index2EntityID_; }

    private:
        size_t activeCount_ = 0;
        size_t sizePerChunk_ = 0;

        std::vector<void*> activeAddr2ComponentDenseArray_;
        std::vector<EntityID> index2EntityID_;
        std::unordered_map<EntityID, size_t> entityID2Unit_;
        std::vector<uint32_t> activeGenerationPerUnit_;
        std::deque<ArchChunkMeta> chunkMetas_;

        ArchTypeDescription* description_ = nullptr;
        ArchTypeManager* manager_ = nullptr;
        bool isDestroyed_ = false;
        bool storageReleased_ = false;

    private:
        void ReleaseOwnedResources();
        void DetachFromManager();
        void MarkStorageReleased();
        void ResetMetadataOnly();

        void AppendUnits(size_t num);
        void DeleteUnitInArrays(size_t index);
        void SwapUnitInArrays(size_t indexA, size_t indexB);
        void SyncChunkMetaCount();

        size_t AllocateEntitySlots(const EntityID* entityIDs, size_t count);
        void DeleteActiveUnit(size_t index);
    };
}

#include "engine/ECS/ArchType/archtype_instance_impl.h"
