#include "Render/Private/RHI/rhi_command_queue.h"

namespace Render::RHI {

    void RHICommandQueue::Push(RHICommand&& cmd) {
        std::lock_guard<std::mutex> lock(mutex_);
        commands_.push_back(std::move(cmd));
    }

    void RHICommandQueue::PopAll(std::vector<RHICommand>& out) {
        std::lock_guard<std::mutex> lock(mutex_);
        out.insert(out.end(), 
                   std::make_move_iterator(commands_.begin()),
                   std::make_move_iterator(commands_.end()));
        commands_.clear();
    }

    bool RHICommandQueue::Empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return commands_.empty();
    }

    size_t RHICommandQueue::Size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return commands_.size();
    }

} // namespace Render::RHI
