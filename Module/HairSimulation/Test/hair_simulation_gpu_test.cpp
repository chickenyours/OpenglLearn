#include "HairSimulation/Public/hair_simulation.h"
#include "HairSimulation/Public/hair_asset.h"
#include "render_tangent_oracle.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

using namespace HairSimulation;
namespace {
void require(bool value,const std::string& reason) { if(!value) throw std::runtime_error(reason); }
void okay(bool result,const std::string& error) { require(result,error); }
std::vector<glm::vec4> snapshot(const Simulator& sim) {
    const auto points=sim.GetGuidePositions(); return {points.begin(),points.end()};
}
float difference(const std::vector<glm::vec4>& a,const std::vector<glm::vec4>& b) {
    require(a.size()==b.size(),"Guide snapshot size mismatch");
    float maximum=0;
    for(std::size_t i=0;i<a.size();++i) maximum=std::max(maximum,glm::length(glm::vec3(a[i]-b[i])));
    return maximum;
}
void healthy(const Simulator& sim,const char* stage) {
    const auto& s=sim.Stats();
    require(s.gpuReady,std::string(stage)+": Vulkan compute not ready");
    require(s.nonFiniteNodes==0 && s.gpuRecoveredNodes==0,std::string(stage)+": non-finite state or GPU recovery");
    require(s.maxRootError<2e-6f,std::string(stage)+": scalp root drift");
    require(s.maxOrientationError<5e-4f,std::string(stage)+": non-unit Cosserat orientation");
    require(s.maxSegmentRelativeError<0.25f,std::string(stage)+": excessive guide segment stretch");
}
}

int main(int argc,char** argv) {
    try {
#ifdef HAIR_ASSET_PATH
        const std::filesystem::path path=argc>1?argv[1]:HAIR_ASSET_PATH;
#else
        require(argc>1,"Provide a portable Diana hair asset path");
        const std::filesystem::path path=argv[1];
#endif
        Simulator sim; std::string error;
        okay(sim.LoadAsset(path,&error),error);
        require(sim.Asset()->header.guideCount==120 && sim.Asset()->header.renderVertexCount==436975,
                "Diana extraction counts changed");
        require(Parameters{}.reconstructRenderTangents,"Actual render tangents must default enabled");
        okay(sim.Initialize(&error),error);
        std::cout<<"Device: "<<sim.Stats().deviceName<<'\n';
        healthy(sim,"initial identity");
        float identityError=0;
        for(std::size_t i=0;i<sim.GetRenderVertices().size();++i) {
            const auto& r=sim.Asset()->renderRest[i];
            identityError=std::max(identityError,glm::length(glm::vec3(sim.GetRenderVertices()[i].positionRadius)-glm::vec3(r.x,r.y,r.z)));
        }
        require(identityError<3e-6f,"Rest interpolation did not reproduce extracted geometry");
        const double restTangentError=HairTangentOracle::ActualCurves(sim);
        const std::vector<RenderVertex> renderRest(sim.GetRenderVertices().begin(),sim.GetRenderVertices().end());
        const auto initialRevision=sim.Stats().renderRevision,initialResets=sim.Stats().resets;
        const auto initialDispatches=sim.Stats().dispatches;
        sim.Settings().reconstructRenderTangents=false;okay(sim.Step(0,&error),error);
        HairTangentOracle::SameGeometry(renderRest,sim.GetRenderVertices());HairTangentOracle::GuideDirections(sim);
        require(sim.Stats().renderRevision==initialRevision+1&&sim.Stats().dispatches==initialDispatches+1&&
            sim.Stats().fixedSteps==0&&sim.Stats().simulationSeconds==0&&sim.Stats().resets==initialResets,
            "Paused tangent-mode refresh advanced physics or did not publish its revision");
        float maximumModeDifference=0;for(std::size_t i=0;i<renderRest.size();++i)
            maximumModeDifference=std::max(maximumModeDifference,glm::length(glm::vec3(renderRest[i].tangentArc-sim.GetRenderVertices()[i].tangentArc)));
        require(maximumModeDifference>.1f,"Diana tangent comparison did not exercise real guide/render differences");
        const auto unchangedRevision=sim.Stats().renderRevision;okay(sim.Step(0,&error),error);
        require(sim.Stats().renderRevision==unchangedRevision,"Unchanged paused tangent mode dispatched redundant work");
        sim.Settings().reconstructRenderTangents=true;okay(sim.RefreshRenderTangents(&error),error);
        HairTangentOracle::SameGeometry(renderRest,sim.GetRenderVertices());HairTangentOracle::ActualCurves(sim);
        require(sim.Stats().renderRevision==initialRevision+2&&sim.Stats().fixedSteps==0&&
            sim.Stats().simulationSeconds==0&&sim.Stats().resets==initialResets,"Explicit tangent refresh reset/advanced physics");
        std::cout<<"Diana actual-curve tangent rest max vector error="<<restTangentError
                 <<", maximum guide/render difference="<<maximumModeDifference<<"; paused A/B preserves geometry, arc, time and reset\n";
        const auto rest=snapshot(sim);
        sim.Settings().wind={0,0,0}; sim.Settings().collision=false;
        sim.SetGravityMode(GravityMode::Zero);
        for(int i=0;i<8;++i) okay(sim.Step(sim.Settings().fixedTimeStep,&error),error);
        healthy(sim,"force-free zero gravity");
        const auto zero=snapshot(sim);
        require(difference(rest,zero)<1e-4f,"Force-free zero-gravity rest state drifted");
        std::vector<std::vector<glm::vec4>> outputs;
        for(auto mode:{GravityMode::Normal,GravityMode::Low,GravityMode::Zero}) {
            sim.SetGravityMode(mode); okay(sim.Reset(&error),error);
            for(int i=0;i<16;++i) okay(sim.Step(sim.Settings().fixedTimeStep,&error),error);
            healthy(sim,"gravity scenario"); outputs.push_back(snapshot(sim));
            HairTangentOracle::ActualCurves(sim);
            std::cout<<"gravity "<<static_cast<int>(mode)<<" displacement="<<sim.Stats().maxGuideDisplacement
                     <<" strain="<<sim.Stats().maxSegmentRelativeError<<'\n';
        }
        require(difference(outputs[0],outputs[1])>1e-4f,"Normal and low gravity produced identical guide motion");
        require(difference(outputs[1],outputs[2])>1e-5f,"Low and zero gravity produced identical guide motion");

        // Exercise the actual preview defaults for three simulated seconds;
        // short force-isolation tests cannot detect accumulated strain, capsule
        // conflicts, or angular-energy growth after the hair has settled.
        sim.Settings().wind={.25f,0,.1f}; sim.Settings().windPulse=.3f;
        sim.Settings().collision=true; sim.SetGravityMode(GravityMode::Normal);
        okay(sim.Reset(&error),error);
        float peakStrain=0,peakDisplacement=0;
        for(int i=0;i<180;++i) {
            okay(sim.Step(sim.Settings().fixedTimeStep,&error),error);
            healthy(sim,"default normal-gravity long run");
            peakStrain=std::max(peakStrain,sim.Stats().maxSegmentRelativeError);
            peakDisplacement=std::max(peakDisplacement,sim.Stats().maxGuideDisplacement);
        }
        std::cout<<"default normal 180 ticks: peak strain="<<peakStrain
                 <<" current strain="<<sim.Stats().maxSegmentRelativeError
                 <<" peak displacement="<<peakDisplacement
                 <<" current displacement="<<sim.Stats().maxGuideDisplacement
                 <<" recovered="<<sim.Stats().gpuRecoveredNodes<<'\n';
        require(peakStrain<.25f,"Default long-run guide strain exceeded the measured regression envelope");

        // With pulsing wind enabled, every fixed tick must receive its own
        // simulation time even when several ticks share one Vulkan batch.
        sim.Settings().wind={.4f,.1f,.2f}; sim.Settings().windPulse=.6f;
        sim.Settings().collision=false;
        sim.SetGravityMode(GravityMode::Normal); okay(sim.Reset(&error),error);
        const double tick=sim.Settings().fixedTimeStep;
        for(int i=0;i<8;++i) okay(sim.Step(tick,&error),error);
        const auto individual=snapshot(sim);
        okay(sim.Reset(&error),error);
        for(int i=0;i<4;++i) okay(sim.Step(tick*2,&error),error);
        healthy(sim,"grouped fixed ticks");
        require(difference(individual,snapshot(sim))<2e-6f,"Fixed-tick grouping changed the GPU result");

        sim.Settings().collision=true; okay(sim.Reset(&error),error);
        for(int i=0;i<8;++i) okay(sim.Step(tick,&error),error);
        healthy(sim,"original head capsules");
        sim.SetRootTranslation({.01f,0,0}); okay(sim.Step(tick,&error),error);
        healthy(sim,"scalp translation");
        HairTangentOracle::ActualCurves(sim);
        sim.SetRootTranslation({0,0,0}); okay(sim.Reset(&error),error);
        require(difference(rest,snapshot(sim))<1e-7f,"Reset did not exactly restore guide positions");
        sim.Settings().substeps=0; sim.Settings().iterations=0;
        sim.Settings().stretchCompliance=std::numeric_limits<float>::quiet_NaN();
        okay(sim.Step(tick,&error),error);
        require(sim.Settings().substeps==1 && sim.Settings().iterations==1 &&
                std::isfinite(sim.Settings().stretchCompliance),"Parameter sanitizer failed");
        const auto before=sim.Stats().fixedSteps;
        require(!sim.Step(-1,&error) && !error.empty(),"Negative frame delta was accepted");
        okay(sim.Step(1,&error),error);
        require(sim.Stats().fixedSteps-before<=sim.Settings().maxFrameSteps && sim.Stats().droppedSeconds>0,
                "Long-frame catch-up was not bounded and reported");
        require(sim.Stats().readbackBytes==436975ull*sizeof(RenderVertex)+3000ull*64,
                "Readback bridge statistics incorrect");
        HairTangentOracle::SyntheticGpu();
        std::cout<<"GPU hair simulation passed: identity, gravity, fixed-step grouping, capsules, root pinning, reset, guards.\n";
        return 0;
    } catch(const std::exception& failure) {
        std::cerr<<"Hair GPU test failed: "<<failure.what()<<'\n'; return 1;
    }
}
