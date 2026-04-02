#include "engine/ECS/Schedule/chunk_schedule.h"

#include <mutex>

#include "engine/DebugTool/ConsoleHelp/color_log.h"

namespace ECS::Core {

    ChunkSchedule::WaitGuard::WaitGuard(ArchChunkMeta& meta, ChunkHeadState type) noexcept
        : meta_(&meta), type_(type) {
        if (IsReadableRequest(type_)) {
            ++meta_->waitingReaders;
        } else if (IsWriteLikeRequest(type_)) {
            ++meta_->waitingWriters;
        }
    }

    ChunkSchedule::WaitGuard::~WaitGuard() {
        if (!meta_) {
            return;
        }

        if (IsReadableRequest(type_)) {
            --meta_->waitingReaders;
        } else if (IsWriteLikeRequest(type_)) {
            --meta_->waitingWriters;
        }
    }

    bool ChunkSchedule::IsChunkBusy(ArchType* archtype, size_t chunkIndex) const {
        ArchChunkMeta* meta = GetChunkMeta(archtype, chunkIndex);
        if (!meta) {
            return false;
        }

        std::lock_guard<std::mutex> lock(meta->requestMutex);
        return meta->state != ChunkHeadState::IDLE;
    }

    void ChunkSchedule::ReleaseChunk(ArchType* archtype,
                                     size_t chunkIndex,
                                     ChunkHeadState occupiedState) noexcept {
        ArchChunkMeta* meta = GetChunkMeta(archtype, chunkIndex);
        if (!meta) {
            return;
        }

        std::unique_lock<std::mutex> lock(meta->requestMutex);

        if (occupiedState == ChunkHeadState::READ) {
            if (meta->state != ChunkHeadState::READ || meta->activeReaders == 0) {
                LOG_ERROR("ChunkSchedule", "invalid read release");
                return;
            }

            --meta->activeReaders;
            if (meta->activeReaders > 0) {
                return;
            }

            meta->state = ChunkHeadState::IDLE;
        } else if (IsWriteLikeRequest(occupiedState)) {
            if (meta->state != occupiedState) {
                LOG_WARNING("ChunkSchedule", "release state mismatched with current occupancy");
            }
            meta->state = ChunkHeadState::IDLE;
        } else {
            LOG_ERROR("ChunkSchedule", "invalid release request type");
            return;
        }

        NotifyNextWaitersNoLock(*meta);
    }

    bool ChunkSchedule::IsReadableRequest(ChunkHeadState state) noexcept {
        return state == ChunkHeadState::READ;
    }

    bool ChunkSchedule::IsWriteLikeRequest(ChunkHeadState state) noexcept {
        return state == ChunkHeadState::WRITE || state == ChunkHeadState::CHANGE;
    }

    bool ChunkSchedule::IsSupportedRequest(ChunkHeadState state) noexcept {
        return IsReadableRequest(state) || IsWriteLikeRequest(state);
    }

    ArchChunkMeta* ChunkSchedule::GetChunkMeta(ArchType* archtype, size_t chunkIndex) noexcept {
        if (!archtype || chunkIndex >= archtype->chunkMetas_.size()) {
            return nullptr;
        }
        return &archtype->chunkMetas_[chunkIndex];
    }

    bool ChunkSchedule::CanAcquireNoLock(const ArchChunkMeta& meta, ChunkHeadState requestType) noexcept {
        if (requestType == ChunkHeadState::READ) {
            if (meta.state == ChunkHeadState::IDLE) {
                return true;
            }
            return meta.state == ChunkHeadState::READ && meta.waitingWriters == 0;
        }

        if (IsWriteLikeRequest(requestType)) {
            return meta.state == ChunkHeadState::IDLE;
        }

        return false;
    }

    void ChunkSchedule::AcquireNoLock(ArchChunkMeta& meta, ChunkHeadState requestType) noexcept {
        if (requestType == ChunkHeadState::READ) {
            ++meta.activeReaders;
            meta.state = ChunkHeadState::READ;
            return;
        }

        meta.state = requestType;
    }

    void ChunkSchedule::NotifyNextWaitersNoLock(ArchChunkMeta& meta) noexcept {
        if (meta.state != ChunkHeadState::IDLE) {
            return;
        }

        if (meta.waitingWriters > 0) {
            meta.writerCv.notify_one();
            return;
        }

        if (meta.waitingReaders > 0) {
            meta.readerCv.notify_all();
        }
    }

} // namespace ECS::Core
