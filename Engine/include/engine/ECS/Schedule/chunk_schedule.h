#pragma once

#include <cstddef>
#include <cassert>

#include "engine/ECS/ArchType/archtype_instance.h"
#include "engine/ToolAndAlgorithm/DateType/chunk_array.h"

namespace ECS::Core {

    enum class FailOption {
        CANCEL,
        WAIT,
    };

    template <typename ComponentT>
    class ChunkRef;

    class ChunkSchedule;

    template <typename ComponentT>
    class ChunkExecuteHandle {
    public:
        ChunkExecuteHandle() = default;
        ChunkExecuteHandle(const ChunkExecuteHandle&) = delete;
        ChunkExecuteHandle& operator=(const ChunkExecuteHandle&) = delete;
        ChunkExecuteHandle(ChunkExecuteHandle&& other) noexcept;
        ChunkExecuteHandle& operator=(ChunkExecuteHandle&& other) noexcept;
        ~ChunkExecuteHandle();

        bool Valid() const noexcept;
        explicit operator bool() const noexcept;
        void Reset() noexcept;

        const ChunkRef<ComponentT>& GetRef() const noexcept { return ref_; }

        ArchType* GetArchType() const noexcept { return ref_.archtype; }
        FixedChunkArray<ComponentT>* GetChunkArray() const noexcept { return ref_.chunkArray; }
        size_t GetChunkIndex() const noexcept { return ref_.chunkIndex; }
        ChunkHeadState GetOccupyState() const noexcept { return occupyState_; }

        ComponentT* GetChunk() noexcept { return ref_.GetChunk(); }
        const ComponentT* GetChunk() const noexcept { return ref_.GetChunk(); }

        ComponentT* operator->() noexcept { return GetChunk(); }
        const ComponentT* operator->() const noexcept { return GetChunk(); }

        ComponentT& operator*() noexcept {
            assert(GetChunk() != nullptr);
            return *GetChunk();
        }

        const ComponentT& operator*() const noexcept {
            assert(GetChunk() != nullptr);
            return *GetChunk();
        }

    private:
        friend class ChunkSchedule;

        void Bind(ChunkSchedule* owner,
                  ArchType* archtype,
                  FixedChunkArray<ComponentT>* chunkArray,
                  size_t chunkIndex,
                  ChunkHeadState state) noexcept;

        void MoveFrom(ChunkExecuteHandle&& other) noexcept;

    private:
        ChunkRef<ComponentT> ref_;
        ChunkSchedule* owner_ = nullptr;
        ChunkHeadState occupyState_ = ChunkHeadState::IDLE;
        bool owns_occupied_ = false;
    };

    template <typename ComponentT>
    class ChunkRef {
    public:
        ChunkRef() = default;

        ChunkRef(ArchType* arch,
                 FixedChunkArray<ComponentT>* array,
                 size_t index)
            : archtype(arch), chunkArray(array), chunkIndex(index) {}

        bool Valid() const noexcept {
            return archtype != nullptr && chunkArray != nullptr;
        }

        ArchType* GetArchType() const noexcept { return archtype; }
        FixedChunkArray<ComponentT>* GetChunkArray() const noexcept { return chunkArray; }
        size_t GetChunkIndex() const noexcept { return chunkIndex; }

        ComponentT* GetChunk() noexcept {
            return chunkArray ? chunkArray->GetChunkData(chunkIndex) : nullptr;
        }

        const ComponentT* GetChunk() const noexcept {
            return chunkArray ? chunkArray->GetChunkData(chunkIndex) : nullptr;
        }

        explicit operator bool() const noexcept {
            return Valid();
        }

    private:
        friend class ChunkSchedule;
        friend class ChunkExecuteHandle<ComponentT>;

        ArchType* archtype = nullptr;
        FixedChunkArray<ComponentT>* chunkArray = nullptr;
        size_t chunkIndex = 0;
    };

    class ChunkSchedule {
    public:
        ChunkSchedule() = default;
        ~ChunkSchedule() = default;

        ChunkSchedule(const ChunkSchedule&) = delete;
        ChunkSchedule& operator=(const ChunkSchedule&) = delete;

        template <typename ComponentT>
        bool GetChunk(FailOption option,
                      ArchType* archtype,
                      size_t chunkIndex,
                      ChunkHeadState requestType,
                      ChunkExecuteHandle<ComponentT>& handle);

        template <typename ComponentT>
        bool GetChunk(FailOption option,
                      const ChunkRef<ComponentT>& ref,
                      ChunkHeadState requestType,
                      ChunkExecuteHandle<ComponentT>& handle);

        bool IsChunkBusy(ArchType* archtype, size_t chunkIndex) const;
        void ReleaseChunk(ArchType* archtype, size_t chunkIndex, ChunkHeadState occupiedState) noexcept;

    private:
        class WaitGuard {
        public:
            WaitGuard(ArchChunkMeta& meta, ChunkHeadState type) noexcept;
            ~WaitGuard();

            WaitGuard(const WaitGuard&) = delete;
            WaitGuard& operator=(const WaitGuard&) = delete;

        private:
            ArchChunkMeta* meta_ = nullptr;
            ChunkHeadState type_ = ChunkHeadState::IDLE;
        };

        static bool IsReadableRequest(ChunkHeadState state) noexcept;
        static bool IsWriteLikeRequest(ChunkHeadState state) noexcept;
        static bool IsSupportedRequest(ChunkHeadState state) noexcept;
        static ArchChunkMeta* GetChunkMeta(ArchType* archtype, size_t chunkIndex) noexcept;
        static bool CanAcquireNoLock(const ArchChunkMeta& meta, ChunkHeadState requestType) noexcept;
        static void AcquireNoLock(ArchChunkMeta& meta, ChunkHeadState requestType) noexcept;
        static void NotifyNextWaitersNoLock(ArchChunkMeta& meta) noexcept;

        template <typename ComponentT>
        bool ProcessRequest(ArchType* archtype,
                            FixedChunkArray<ComponentT>* chunkArray,
                            size_t chunkIndex,
                            ChunkHeadState requestType,
                            FailOption option,
                            ChunkExecuteHandle<ComponentT>& handle);
    };

} // namespace ECS::Core

#include "engine/ECS/Schedule/chunk_schedule_impl.h"