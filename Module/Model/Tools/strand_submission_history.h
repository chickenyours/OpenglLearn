#pragma once
#include "Render/Public/Pipeline/strand_rendering.h"
#include <array>

namespace Model::EditorPreview {
// Application-thread history of executed frames, independent from simulation
// ticks, mesh uploads and recorded/cancelled command buffers.
struct StrandSubmissionState {
    std::shared_ptr<const std::vector<Render::StrandVertex>> vertices;
    std::shared_ptr<const std::vector<std::uint32_t>> topology;
    glm::mat4 model{1};
    std::array<float,9> raster{};
    std::uint64_t reset=0,appearance=0;
};
class StrandSubmissionHistory {
    StrandSubmissionState executed_,pending_;
    std::uint64_t pendingFrame_=0;
    bool pendingValid_=false;
public:
    bool CanReproject(const StrandSubmissionState& current) const {
        return executed_.vertices&&current.vertices&&executed_.topology&&current.topology&&
            executed_.vertices->size()==current.vertices->size()&&executed_.topology==current.topology&&
            executed_.reset==current.reset&&executed_.appearance==current.appearance&&executed_.raster==current.raster;
    }
    const StrandSubmissionState& Executed()const{return executed_;}
    void Stage(std::uint64_t frame,const StrandSubmissionState& current) {pendingFrame_=frame;pending_=current;pendingValid_=true;}
    bool Commit(std::uint64_t frame,bool hairExecuted=true) {
        if(!pendingValid_||pendingFrame_!=frame)return false;
        executed_=hairExecuted?pending_:StrandSubmissionState{};
        pending_={};pendingValid_=false;return true;
    }
    void Clear(){executed_={};pending_={};pendingFrame_=0;pendingValid_=false;}
};
} // namespace Model::EditorPreview
