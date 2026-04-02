#pragma once

#include <mutex>
#include <utility>

#include "engine/DebugTool/ConsoleHelp/color_log.h"

namespace ECS::Core {

    template <typename ComponentT>
    ChunkExecuteHandle<ComponentT>::ChunkExecuteHandle(ChunkExecuteHandle&& other) noexcept {
        MoveFrom(std::move(other));
    }

    template <typename ComponentT>
    ChunkExecuteHandle<ComponentT>& ChunkExecuteHandle<ComponentT>::operator=(ChunkExecuteHandle&& other) noexcept {
        if (this != &other) {
            Reset();
            MoveFrom(std::move(other));
        }
        return *this;
    }

    template <typename ComponentT>
    ChunkExecuteHandle<ComponentT>::~ChunkExecuteHandle() {
        Reset();
    }

    template <typename ComponentT>
    bool ChunkExecuteHandle<ComponentT>::Valid() const noexcept {
        return owner_ != nullptr && ref_.archtype != nullptr && owns_occupied_;
    }

    template <typename ComponentT>
    ChunkExecuteHandle<ComponentT>::operator bool() const noexcept {
        return Valid();
    }

    template <typename ComponentT>
    void ChunkExecuteHandle<ComponentT>::Reset() noexcept {
        if (owner_ && ref_.archtype != nullptr && owns_occupied_) {
            owner_->ReleaseChunk(ref_.archtype, ref_.chunkIndex, occupyState_);
        }

        owner_ = nullptr;
        ref_.archtype = nullptr;
        ref_.chunkIndex = 0;
        occupyState_ = ChunkHeadState::IDLE;
        owns_occupied_ = false;
    }

    template <typename ComponentT>
    void ChunkExecuteHandle<ComponentT>::Bind(ChunkSchedule* owner,
                                              ArchType* archtype,
                                              size_t chunkIndex,
                                              ChunkHeadState state) noexcept {
        owner_ = owner;
        ref_.archtype = archtype;
        ref_.chunkIndex = chunkIndex;
        occupyState_ = state;
        owns_occupied_ = true;
    }

    template <typename ComponentT>
    void ChunkExecuteHandle<ComponentT>::MoveFrom(ChunkExecuteHandle&& other) noexcept {
        ref_ = other.ref_;
        owner_ = other.owner_;
        occupyState_ = other.occupyState_;
        owns_occupied_ = other.owns_occupied_;

        other.ref_.archtype = nullptr;
        other.ref_.chunkIndex = 0;
        other.owner_ = nullptr;
        other.occupyState_ = ChunkHeadState::IDLE;
        other.owns_occupied_ = false;
    }

    template <typename ComponentT>
    bool ChunkSchedule::GetChunk(FailOption option,
                                 ArchType* archtype,
                                 size_t chunkIndex,
                                 ChunkHeadState requestType,
                                 ChunkExecuteHandle<ComponentT>& handle) {
        if (!archtype) {
            LOG_ERROR("ChunkSchedule", "archtype is nullptr");
            return false;
        }

        FixedChunkArray<ComponentT>* chunkArray = archtype->TryCastActiveComponentArray<ComponentT>();
        if (!chunkArray) {
            LOG_ERROR("ChunkSchedule", "component array not exist");
            return false;
        }

        if (chunkIndex >= chunkArray->ChunkCount() || chunkIndex >= archtype->chunkMetas_.size()) {
            LOG_ERROR("ChunkSchedule", "chunkIndex out of range");
            return false;
        }

        return ProcessRequest(archtype, chunkIndex, requestType, option, handle);
    }

    template <typename ComponentT>
    bool ChunkSchedule::GetChunk(FailOption option,
                                 const ChunkRef<ComponentT>& ref,
                                 ChunkHeadState requestType,
                                 ChunkExecuteHandle<ComponentT>& handle) {
        if (!ref.Valid()) {
            LOG_ERROR("ChunkSchedule", "ChunkRef is null");
            return false;
        }

        return GetChunk<ComponentT>(option, ref.archtype, ref.chunkIndex, requestType, handle);
    }

    template <typename ComponentT>
    bool ChunkSchedule::ProcessRequest(ArchType* archtype,
                                       size_t chunkIndex,
                                       ChunkHeadState requestType,
                                       FailOption option,
                                       ChunkExecuteHandle<ComponentT>& handle) {
        ArchChunkMeta* meta = GetChunkMeta(archtype, chunkIndex);
        if (!meta) {
            LOG_ERROR("ChunkSchedule", "invalid chunk key");
            return false;
        }

        if (!IsSupportedRequest(requestType)) {
            LOG_ERROR("ChunkSchedule", "invalid request type");
            return false;
        }

        handle.Reset();

        std::unique_lock<std::mutex> lock(meta->requestMutex);

        if (CanAcquireNoLock(*meta, requestType)) {
            AcquireNoLock(*meta, requestType);
            handle.Bind(this, archtype, chunkIndex, requestType);
            return true;
        }

        if (option == FailOption::CANCEL) {
            return false;
        }

        WaitGuard waitGuard(*meta, requestType);

        if (requestType == ChunkHeadState::READ) {
            meta->readerCv.wait(lock, [&]() {
                return CanAcquireNoLock(*meta, requestType);
            });
        } else {
            meta->writerCv.wait(lock, [&]() {
                return CanAcquireNoLock(*meta, requestType);
            });
        }

        AcquireNoLock(*meta, requestType);
        handle.Bind(this, archtype, chunkIndex, requestType);
        return true;
    }

} // namespace ECS::Core
