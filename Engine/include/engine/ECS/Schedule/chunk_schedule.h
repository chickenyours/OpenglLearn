#pragma once

#include <unordered_map>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <stdexcept>

#include "engine/ECS/ArchType/archtype_instance.h"
#include "engine/ToolAndAlgorithm/DateType/chunk_array.h"
#include "engine/ToolAndAlgorithm/object.h"
#include "engine/ECS/Scene/scene_thread_local_context.h"

namespace ECS::Core {

    enum class FailOption {
        CANCEL,
        WAIT,
    };

    template <typename ComponentT>
    class ChunkArrayRef;

    template <typename ComponentT>
    class ChunkRef;

    class ChunkSchedule;

    template <typename ComponentT>
    class ChunkExecuteHandle {
    public:
        ChunkExecuteHandle() = default;

        ChunkExecuteHandle(const ChunkExecuteHandle&) = delete;
        ChunkExecuteHandle& operator=(const ChunkExecuteHandle&) = delete;

        ChunkExecuteHandle(ChunkExecuteHandle&& other) noexcept {
            MoveFrom(std::move(other));
        }

        ChunkExecuteHandle& operator=(ChunkExecuteHandle&& other) noexcept {
            if (this != &other) {
                Reset();
                MoveFrom(std::move(other));
            }
            return *this;
        }

        ~ChunkExecuteHandle() {
            Reset();
        }

        bool Valid() const noexcept {
            return owner_ != nullptr && ref.infoAddr != nullptr && owns_occupied_;
        }

        explicit operator bool() const noexcept {
            return Valid();
        }

        void Reset() noexcept {
            if (owner_ && ref.infoAddr && owns_occupied_) {
                owner_->ResponseChunkFinishOccupy(ref.infoAddr, occupyState_);
            }
            owner_ = nullptr;
            ref.infoAddr = nullptr;
            owns_occupied_ = false;
            occupyState_ = ChunkHeadState::IDLE;
        }

        ChunkRef<ComponentT> ref;

    private:
        friend class ChunkSchedule;

        void Bind(ChunkSchedule* owner, ChunkHead* head, ChunkHeadState state) {
            Reset();
            owner_ = owner;
            ref.infoAddr = head;
            occupyState_ = state;
            owns_occupied_ = true;
        }

        void MoveFrom(ChunkExecuteHandle&& other) noexcept {
            owner_ = other.owner_;
            ref = other.ref;
            occupyState_ = other.occupyState_;
            owns_occupied_ = other.owns_occupied_;

            other.owner_ = nullptr;
            other.ref.infoAddr = nullptr;
            other.occupyState_ = ChunkHeadState::IDLE;
            other.owns_occupied_ = false;
        }

    private:
        ChunkSchedule* owner_ = nullptr;
        ChunkHeadState occupyState_ = ChunkHeadState::IDLE;
        bool owns_occupied_ = false;
    };

    template <typename ComponentT>
    class ChunkArrayRef {
    public:
        ChunkArrayRef() = default;
        explicit ChunkArrayRef(FixedChunkArray<ComponentT>* array) : arrayAddr(array) {}

        bool Valid() const noexcept { return arrayAddr != nullptr; }

    private:
        friend class ChunkSchedule;
        FixedChunkArray<ComponentT>* arrayAddr = nullptr;
    };

    template <typename ComponentT>
    class ChunkRef {
    public:
        ChunkRef() = default;
        explicit ChunkRef(ChunkHead* info) : infoAddr(info) {}

        bool Valid() const noexcept { return infoAddr != nullptr; }

    private:
        friend class ChunkSchedule;
        friend class ChunkExecuteHandle<ComponentT>;
        ChunkHead* infoAddr = nullptr;
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
                      ChunkExecuteHandle<ComponentT>* handle = nullptr) {
            if (!archtype) {
                LOG_ERROR("ChunkSchedule", "archtype is nullptr");
                return false;
            }

            FixedChunkArray<ComponentT>* chunkArray =
                archtype->TryCastActiveComponentArray<ComponentT>();
            if (!chunkArray) {
                LOG_ERROR("ChunkSchedule", "component array not exist");
                return false;
            }

            if (chunkIndex >= chunkArray->chunkHeads_.size()) {
                LOG_ERROR("ChunkSchedule", "chunkIndex out of range");
                return false;
            }

            ChunkHead& head = chunkArray->chunkHeads_[chunkIndex];
            return ProcessRequest(requestType, &head, handle, option);
        }

        template <typename ComponentT>
        bool GetChunk(FailOption option,
                      const ChunkArrayRef<ComponentT>& ref,
                      size_t index,
                      ChunkHeadState requestType,
                      ChunkExecuteHandle<ComponentT>* handle = nullptr) {
            if (!ref.arrayAddr) {
                LOG_ERROR("ChunkSchedule", "ChunkArrayRef is null");
                return false;
            }

            if (index >= ref.arrayAddr->chunkHeads_.size()) {
                LOG_ERROR("ChunkSchedule", "chunk index out of range");
                return false;
            }

            ChunkHead& head = ref.arrayAddr->chunkHeads_[index];
            return ProcessRequest(requestType, &head, handle, option);
        }

        template <typename ComponentT>
        bool GetChunk(FailOption option,
                      const ChunkRef<ComponentT>& ref,
                      ChunkHeadState requestType,
                      ChunkExecuteHandle<ComponentT>* handle = nullptr) {
            if (!ref.infoAddr) {
                LOG_ERROR("ChunkSchedule", "ChunkRef is null");
                return false;
            }

            return ProcessRequest(requestType, ref.infoAddr, handle, option);
        }

        bool CheckChunk(ChunkHead* head) {
            if (!head) {
                return false;
            }

            std::lock_guard<std::mutex> lock(head->requestMutex);
            return head->isOccupied;
        }

        void ResponseChunkFinishOccupy(ChunkHead* head, ChunkHeadState occupiedState) {
            if (!head) {
                return;
            }

            EntryPtr entry = GetOrCreateEntry(head);

            {
                std::lock_guard<std::mutex> lock(head->requestMutex);

                if (occupiedState == ChunkHeadState::READ) {
                    if (entry->activeReaders == 0) {
                        LOG_ERROR("ChunkSchedule", "activeReaders underflow");
                        return;
                    }

                    --entry->activeReaders;
                    if (entry->activeReaders == 0) {
                        head->isOccupied = false;
                        head->state = ChunkHeadState::IDLE;
                    }
                } else {
                    if (!head->isOccupied) {
                        LOG_WARNING("ChunkSchedule", "release unoccupied chunk");
                    }
                    head->isOccupied = false;
                    head->state = ChunkHeadState::IDLE;
                }

                SyncWaitFlagsNoLock(head, entry.get());
            }

            entry->cv.notify_all();
        }

    private:
        struct ChunkSchedulingData {
            size_t activeReaders = 0;
            size_t waitingReaders = 0;
            size_t waitingWriters = 0;
            std::condition_variable cv;
        };

        using EntryPtr = std::shared_ptr<ChunkSchedulingData>;

        std::unordered_map<ChunkHead*, EntryPtr> chunk2Schedule_;
        std::mutex mapMutex_;

        EntryPtr GetOrCreateEntry(ChunkHead* head) {
            std::lock_guard<std::mutex> mapLock(mapMutex_);
            auto it = chunk2Schedule_.find(head);
            if (it != chunk2Schedule_.end()) {
                return it->second;
            }

            auto entry = std::make_shared<ChunkSchedulingData>();
            chunk2Schedule_.emplace(head, entry);
            return entry;
        }

        static bool IsReadRequest(ChunkHeadState state) noexcept {
            return state == ChunkHeadState::READ;
        }

        static bool IsWriteLikeRequest(ChunkHeadState state) noexcept {
            return state == ChunkHeadState::WRITE || state == ChunkHeadState::CHANGE;
        }

        static void SyncWaitFlagsNoLock(ChunkHead* head, ChunkSchedulingData* entry) {
            head->isWaited = (entry->waitingReaders > 0 || entry->waitingWriters > 0);
        }

        bool CanAcquireNoLock(ChunkHead* head,
                              ChunkSchedulingData* entry,
                              ChunkHeadState requestType) const {
            if (IsReadRequest(requestType)) {
                // 空闲可读；正在读也可共享进入。
                // 但如果有等待中的写者，为避免写者饥饿，这里阻止新读者插队。
                if (!head->isOccupied) {
                    return true;
                }
                if (head->state == ChunkHeadState::READ && entry->waitingWriters == 0) {
                    return true;
                }
                return false;
            }

            if (IsWriteLikeRequest(requestType)) {
                return !head->isOccupied;
            }

            return false;
        }

        void AcquireNoLock(ChunkHead* head,
                           ChunkSchedulingData* entry,
                           ChunkHeadState requestType) {
            if (IsReadRequest(requestType)) {
                ++entry->activeReaders;
                head->isOccupied = true;
                head->state = ChunkHeadState::READ;
                return;
            }

            head->isOccupied = true;
            head->state = requestType;
        }

        template <typename ComponentT>
        bool ProcessRequest(ChunkHeadState requestType,
                            ChunkHead* head,
                            ChunkExecuteHandle<ComponentT>* handle,
                            FailOption failOption) {
            if (!head) {
                LOG_ERROR("ChunkSchedule", "head is nullptr");
                return false;
            }

            if (!IsReadRequest(requestType) && !IsWriteLikeRequest(requestType)) {
                LOG_ERROR("ChunkSchedule", "invalid request type");
                return false;
            }

            EntryPtr entry = GetOrCreateEntry(head);

            std::unique_lock<std::mutex> lock(head->requestMutex);

            if (CanAcquireNoLock(head, entry.get(), requestType)) {
                AcquireNoLock(head, entry.get(), requestType);
                SyncWaitFlagsNoLock(head, entry.get());
                BindHandle(handle, head, requestType);
                return true;
            }

            if (failOption == FailOption::CANCEL) {
                return false;
            }

            WaitGuard waitGuard(entry.get(), requestType);

            SyncWaitFlagsNoLock(head, entry.get());

            entry->cv.wait(lock, [&]() {
                return CanAcquireNoLock(head, entry.get(), requestType);
            });

            AcquireNoLock(head, entry.get(), requestType);
            SyncWaitFlagsNoLock(head, entry.get());
            BindHandle(handle, head, requestType);
            return true;
        }

        class WaitGuard {
        public:
            WaitGuard(ChunkSchedulingData* entry, ChunkHeadState type)
                : entry_(entry), type_(type) {
                if (IsReadRequest(type_)) {
                    ++entry_->waitingReaders;
                } else {
                    ++entry_->waitingWriters;
                }
            }

            ~WaitGuard() {
                if (!entry_) {
                    return;
                }

                if (IsReadRequest(type_)) {
                    --entry_->waitingReaders;
                } else {
                    --entry_->waitingWriters;
                }
            }

            WaitGuard(const WaitGuard&) = delete;
            WaitGuard& operator=(const WaitGuard&) = delete;

        private:
            ChunkSchedulingData* entry_;
            ChunkHeadState type_;
        };

        template <typename ComponentT>
        void BindHandle(ChunkExecuteHandle<ComponentT>* handle,
                        ChunkHead* head,
                        ChunkHeadState state) {
            if (!handle) {
                return;
            }
            handle->Bind(this, head, state);
        }
    };

} // namespace ECS::Core