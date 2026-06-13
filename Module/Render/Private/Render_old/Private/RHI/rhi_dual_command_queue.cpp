#include "Render/Private/RHI/rhi_dual_command_queue.h"

namespace Render::RHI {

    void DualCommandQueue::PushCreateCommand(RHICommand&& cmd) {
        std::lock_guard<std::mutex> lock(createMutex_);
        createQueue_.push_back(std::move(cmd));
    }

    void DualCommandQueue::PushDestroyCommand(RHICommand&& cmd) {
        std::lock_guard<std::mutex> lock(destroyMutex_);
        destroyQueue_.push_back(std::move(cmd));
    }

    void DualCommandQueue::PopAllCreateCommands(std::vector<RHICommand>& out) {
        std::lock_guard<std::mutex> lock(createMutex_);
        out.insert(out.end(), createQueue_.begin(), createQueue_.end());
        createQueue_.clear();
    }

    void DualCommandQueue::PopAllDestroyCommands(std::vector<RHICommand>& out) {
        std::lock_guard<std::mutex> lock(destroyMutex_);
        out.insert(out.end(), destroyQueue_.begin(), destroyQueue_.end());
        destroyQueue_.clear();
    }

    bool DualCommandQueue::CreateQueueEmpty() const {
        std::lock_guard<std::mutex> lock(createMutex_);
        return createQueue_.empty();
    }

    bool DualCommandQueue::DestroyQueueEmpty() const {
        std::lock_guard<std::mutex> lock(destroyMutex_);
        return destroyQueue_.empty();
    }

    bool DualCommandQueue::AnyQueueNonEmpty() const {
        std::lock_guard<std::mutex> lock1(createMutex_);
        std::lock_guard<std::mutex> lock2(destroyMutex_);
        return !createQueue_.empty() || !destroyQueue_.empty();
    }

    size_t DualCommandQueue::CreateQueueSize() const {
        std::lock_guard<std::mutex> lock(createMutex_);
        return createQueue_.size();
    }

    size_t DualCommandQueue::DestroyQueueSize() const {
        std::lock_guard<std::mutex> lock(destroyMutex_);
        return destroyQueue_.size();
    }

    size_t DualCommandQueue::TotalSize() const {
        std::lock_guard<std::mutex> lock1(createMutex_);
        std::lock_guard<std::mutex> lock2(destroyMutex_);
        return createQueue_.size() + destroyQueue_.size();
    }

} // namespace Render::RHI
