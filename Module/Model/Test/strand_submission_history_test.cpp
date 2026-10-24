#include "Model/Tools/strand_submission_history.h"
#include <iostream>
#include <stdexcept>

namespace {
void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
using State=Model::EditorPreview::StrandSubmissionState;
State MakeState(float x) {
    State result;auto points=std::make_shared<std::vector<Render::StrandVertex>>(2);
    (*points)[0].positionRadius={x,-1,0,.001f};(*points)[1].positionRadius={x,1,0,.001f};
    result.vertices=points;result.topology=std::make_shared<std::vector<std::uint32_t>>(std::initializer_list<std::uint32_t>{0,1});
    result.raster={1,0,.001f,1,12,1,1,1};return result;
}
}
int main() {
    try {
        using Model::EditorPreview::StrandSubmissionHistory;
        StrandSubmissionHistory history;const auto first=MakeState(0);
        auto moving=MakeState(.1f);moving.topology=first.topology;
        Require(!history.CanReproject(first),"First frame fabricated executed history");
        history.Stage(1,first);Require(!history.CanReproject(moving),"Staging advanced executed positions");
        Require(!history.Commit(2),"Wrong token committed staged data");
        Require(history.Commit(1)&&history.CanReproject(moving),"Executed first frame did not establish a baseline");
        Require(history.Executed().vertices==first.vertices,"Executed baseline lost its immutable vertex version");
        history.Stage(2,moving);auto afterDrop=MakeState(.2f);afterDrop.topology=first.topology;
        Require(history.CanReproject(afterDrop)&&history.Executed().vertices==first.vertices,
            "Unexecuted moving frame replaced last executed positions");
        history.Stage(3,afterDrop);Require(!history.Commit(2),"Stale completion consumed the newer pending frame");
        Require(history.Commit(3)&&history.Executed().vertices==afterDrop.vertices,"Correct completion failed after a dropped frame");
        auto transformOnly=afterDrop;transformOnly.model[3].x=.25f;
        Require(history.CanReproject(transformOnly)&&history.Executed().model[3].x==0,
            "Model motion was rejected or advanced before execution");
        history.Stage(4,transformOnly);Require(history.Commit(4)&&history.Executed().model[3].x==.25f,
            "Executed model transform was not retained");
        // A paused state refers to the same immutable position version after
        // execution, permitting a one-time previous=current mesh transition.
        Require(history.Executed().vertices==transformOnly.vertices&&history.CanReproject(transformOnly),
            "Paused state failed to settle onto current executed positions");
        std::uint64_t frame=5;
        for(std::size_t parameter=0;parameter<transformOnly.raster.size();++parameter) {
            auto changed=transformOnly;changed.raster[parameter]+=.125f;
            Require(!history.CanReproject(changed),"Changed width/LOD/coverage policy retained stale history");
            history.Stage(frame,changed);Require(history.Commit(frame++)&&history.CanReproject(changed),
                "Changed raster policy failed to establish its next valid baseline");
            history.Stage(frame,transformOnly);Require(history.Commit(frame++),"Restore raster baseline");
        }
        auto reset=transformOnly;++reset.reset;Require(!history.CanReproject(reset),"Simulation reset retained old motion");
        history.Stage(frame,reset);Require(history.Commit(frame++)&&history.CanReproject(reset),"Reset baseline did not recover");
        auto stride=reset;stride.topology=std::make_shared<std::vector<std::uint32_t>>(*reset.topology);
        Require(!history.CanReproject(stride),"Replaced topology version retained old correspondence");
        history.Stage(frame,stride);Require(history.Commit(frame++)&&history.CanReproject(stride),"Topology baseline did not recover");
        auto changedAppearance=stride;++changedAppearance.appearance;
        Require(!history.CanReproject(changedAppearance),"Source appearance reload retained history");
        auto missing=stride;missing.topology.reset();Require(!history.CanReproject(missing),"Missing topology accepted history");
        auto resized=stride;resized.vertices=std::make_shared<std::vector<Render::StrandVertex>>(3);
        Require(!history.CanReproject(resized),"Changed point count accepted history");
        history.Stage(frame,{});Require(history.Commit(frame++)&&!history.CanReproject(stride),"Executed hidden frame retained visible history");
        history.Stage(frame,stride);Require(history.Commit(frame++)&&history.CanReproject(stride),"Visible baseline failed after hide");
        history.Stage(frame,stride);Require(history.Commit(frame++,false)&&!history.CanReproject(stride),"Renderer without hair advanced its history");
        history.Stage(frame,stride);history.Clear();Require(!history.Commit(frame)&&!history.CanReproject(stride),"Release/reset retained pending submission");
        std::cout<<"Strand executed-frame history: staging, drop, token, transform, pause, raster, reset, topology, visibility PASS\n";
        return 0;
    } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
