#include "cloth_preview_worker.h"
#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>
#include <utility>
#include <glm/gtx/matrix_decompose.hpp>

namespace Model::detail {
namespace {
StaticModel GeometryOnly(const StaticModel& source){
    StaticModel result;
    result.meshes=source.meshes;
    result.instances=source.instances;
    result.bounds=source.bounds;
    return result;
}
bool SameIdentity(const ClothAsset& a,const ClothAsset& b){
    return a.version==b.version&&a.id==b.id&&a.garmentId==b.garmentId&&
        a.skeletonId==b.skeletonId&&a.topologyHash==b.topologyHash&&a.motionBone==b.motionBone&&
        a.particles.size()==b.particles.size()&&a.triangles.size()==b.triangles.size()&&
        a.renderBindings.size()==b.renderBindings.size()&&a.capsules.size()==b.capsules.size()&&a.convexes.size()==b.convexes.size()&&
        a.stretch.size()==b.stretch.size()&&a.bend.size()==b.bend.size()&&a.tethers.size()==b.tethers.size();
}
bool SettingsFiniteAndBounded(const ClothSolverSettings& s){
    if(!std::isfinite(s.fixedTimeStep)||s.fixedTimeStep<.001||s.fixedTimeStep>.1||
        s.substeps<1||s.substeps>16||s.iterations<1||s.iterations>64||
        !std::isfinite(s.convergenceTolerance)||s.convergenceTolerance<0||s.convergenceTolerance>.01)return false;
    for(double value:{s.stretchCompliance,s.areaCompliance,s.animationCompliance})
        if(!std::isfinite(value)||value<0||value>1e4)return false;
    if(!std::isfinite(s.bendCompliance)||s.bendCompliance<0||s.bendCompliance>1e8)return false;
    for(double value:{s.damping,s.localDamping,s.animationDamping})
        if(!std::isfinite(value)||value<0||value>1000)return false;
    if(!std::isfinite(s.friction)||s.friction<0||s.friction>1||
        !std::isfinite(s.thickness)||s.thickness<1e-5||s.thickness>.25)return false;
    for(const auto value:{s.gravity,s.wind})
        if(!std::isfinite(value.x)||!std::isfinite(value.y)||!std::isfinite(value.z)||glm::length(value)>1000)return false;
    return true;
}

glm::dquat BlendRotation(glm::dquat a,glm::dquat b,double fraction){
    const double aa=glm::dot(a,a),bb=glm::dot(b,b);
    if(!std::isfinite(aa)||!std::isfinite(bb)||aa<1e-20||bb<1e-20)
        throw std::runtime_error("Cannot interpolate a nonfinite or zero bone rotation");
    a/=std::sqrt(aa);b/=std::sqrt(bb);
    if(glm::dot(a,b)<0)b=-b;
    return glm::normalize(glm::slerp(a,b,fraction));
}
struct MatrixBlend {
    bool changed=false;
    glm::dvec3 scaleA{1},scaleB{1},translationA{0},translationB{0},skewA{0},skewB{0};
    glm::dquat rotationA{1,0,0,0},rotationB{1,0,0,0};
    MatrixBlend(const glm::dmat4& a,const glm::dmat4& b):changed(a!=b){
        if(!changed)return;
        for(const auto* matrix:{&a,&b})for(int c=0;c<4;++c)for(int r=0;r<4;++r)
            if(!std::isfinite((*matrix)[c][r]))throw std::runtime_error("Cannot interpolate a nonfinite bone transform");
        glm::dvec4 perspectiveA,perspectiveB;
        if(!glm::decompose(a,scaleA,rotationA,translationA,skewA,perspectiveA)||
           !glm::decompose(b,scaleB,rotationB,translationB,skewB,perspectiveB)||
           glm::length(perspectiveA-glm::dvec4(0,0,0,1))>1e-10||
           glm::length(perspectiveB-glm::dvec4(0,0,0,1))>1e-10)
            throw std::runtime_error("Cannot interpolate a singular or perspective bone transform; reset the preview");
    }
    glm::dmat4 At(const glm::dmat4& unchanged,double fraction) const {
        if(!changed)return unchanged;
        const auto translation=translationA+(translationB-translationA)*fraction;
        const auto scale=scaleA+(scaleB-scaleA)*fraction;
        const auto skew=skewA+(skewB-skewA)*fraction;
        glm::dmat4 shear(1);shear[1][0]=skew.z;shear[2][0]=skew.y;shear[2][1]=skew.x;
        return glm::translate(glm::dmat4(1),translation)*glm::mat4_cast(BlendRotation(rotationA,rotationB,fraction))*
            shear*glm::scale(glm::dmat4(1),scale);
    }
};
// Decompose changed matrices once for the whole input interval, rather than
// repeating work for every publication. Exact, unchanged input needs no scratch.
struct InputInterpolation {
    const SkeletalPose& beginPose;const SkeletalPose& endPose;
    const StaticModel& beginMesh;const StaticModel& endMesh;
    const SkeletonAsset* skeleton=nullptr;
    std::vector<MatrixBlend> global,instances;
    bool poseChanged=false,meshChanged=false;
    InputInterpolation(const SkeletalPose& a,const SkeletalPose& b,const StaticModel& x,const StaticModel& y,const SkeletonAsset* hierarchy=nullptr):
            beginPose(a),endPose(b),beginMesh(x),endMesh(y),skeleton(hierarchy){
        if(a.global.size()!=b.global.size()||a.local.size()!=b.local.size()||a.morphWeights.size()!=b.morphWeights.size()||
           x.meshes.size()!=y.meshes.size()||x.instances.size()!=y.instances.size())
            throw std::runtime_error("Cloth input topology changed; reset the preview generation");
        if(skeleton&&skeleton->bones.size()!=a.local.size())throw std::runtime_error("Preview source skeleton differs from the input pose");
        global.reserve(a.global.size());instances.reserve(x.instances.size());
        for(std::size_t i=0;i<a.global.size();++i){if(skeleton)poseChanged|=a.global[i]!=b.global[i];
            else {global.emplace_back(a.global[i],b.global[i]);poseChanged|=global.back().changed;}}
        for(std::size_t i=0;i<a.local.size();++i)poseChanged|=a.local[i].translation!=b.local[i].translation||
            a.local[i].rotation!=b.local[i].rotation||a.local[i].scale!=b.local[i].scale;
        poseChanged|=a.morphWeights!=b.morphWeights;
        for(std::size_t m=0;m<x.meshes.size();++m){
            if(x.meshes[m].vertices.size()!=y.meshes[m].vertices.size())
                throw std::runtime_error("Cloth vertex layout changed; reset the preview generation");
            for(std::size_t i=0;i<x.meshes[m].vertices.size();++i)
                meshChanged|=x.meshes[m].vertices[i].position!=y.meshes[m].vertices[i].position;
        }
        for(std::size_t i=0;i<x.instances.size();++i){
            if(x.instances[i].meshIndex!=y.instances[i].meshIndex)
                throw std::runtime_error("Cloth instance layout changed; reset the preview generation");
            instances.emplace_back(x.instances[i].transform,y.instances[i].transform);meshChanged|=instances.back().changed;
        }
    }
    void PoseAt(double fraction,SkeletalPose& output) const {
        output=endPose;
        if(!skeleton)for(std::size_t i=0;i<global.size();++i)output.global[i]=global[i].At(endPose.global[i],fraction);
        for(std::size_t i=0;i<output.local.size();++i){
            const auto& a=beginPose.local[i];const auto& b=endPose.local[i];auto& out=output.local[i];
            out.translation=a.translation+(b.translation-a.translation)*fraction;
            out.scale=a.scale+(b.scale-a.scale)*fraction;
            out.rotation=a.rotation==b.rotation?b.rotation:BlendRotation(a.rotation,b.rotation,fraction);
        }
        for(std::size_t i=0;i<output.morphWeights.size();++i)
            output.morphWeights[i]=float(beginPose.morphWeights[i]+double(endPose.morphWeights[i]-beginPose.morphWeights[i])*fraction);
        // A child follows its parent's interpolated rotation along an arc.
        // Independently blending its global translation produces a chord and
        // disagrees with the local hierarchy used by hair and garment skinning.
        if(skeleton)output.global=GlobalBoneTransforms(*skeleton,output.local);
    }
    void MeshAt(double fraction,StaticModel& output) const {
        output=GeometryOnly(endMesh);
        for(std::size_t m=0;m<output.meshes.size();++m){
            auto& mesh=output.meshes[m];mesh.bounds={};
            for(std::size_t i=0;i<mesh.vertices.size();++i){
                const auto a=beginMesh.meshes[m].vertices[i].position,b=endMesh.meshes[m].vertices[i].position;
                mesh.vertices[i].position=a+(b-a)*fraction;mesh.bounds.Include(mesh.vertices[i].position);
            }
        }
        output.bounds={};
        for(std::size_t i=0;i<output.instances.size();++i){
            auto& instance=output.instances[i];instance.transform=instances[i].At(endMesh.instances[i].transform,fraction);
            if(instance.meshIndex>=output.meshes.size())throw std::runtime_error("Invalid cloth mesh instance");
            for(const auto& vertex:output.meshes[instance.meshIndex].vertices)
                output.bounds.Include(glm::dvec3(instance.transform*glm::dvec4(vertex.position,1)));
        }
    }
};
} // namespace

struct ClothPreviewWorker::Impl {
    struct Submission {
        std::shared_ptr<const ClothAsset> cloth;
        std::shared_ptr<const SkeletalAssetPackage> garmentSource;
        std::shared_ptr<const HairAsset> hairAsset;
        std::shared_ptr<const SkeletalAssetPackage> bodySource;
        HairSolverSettings hairSettings;
        bool simulateCloth=true;
        SkeletalPose pose;
        StaticModel garment;
        ClothSolverSettings settings;
        double delta=0,submittedSeconds=0;
        std::uint64_t generation=0,serial=0,pauseEpoch=0;
        std::string error;
    };

    mutable std::mutex mutex;
    std::condition_variable work;
    mutable std::condition_variable published;
    bool stopping=false,paused=false,rebaseNextInput=false;
    std::uint64_t generation=0,serial=0,revision=0,pauseEpoch=0;
    std::vector<std::pair<std::uint64_t,std::uint64_t>> cancelledSerials;
    double submittedSeconds=0;
    std::shared_ptr<const ClothAsset> asset;
    std::shared_ptr<const SkeletalAssetPackage> garmentSource;
    std::shared_ptr<const HairAsset> hairAsset;
    std::shared_ptr<const SkeletalAssetPackage> bodySource;
    bool simulateCloth=true;
    std::string fault;
    std::optional<Submission> pending;
    std::shared_ptr<const ClothPreviewSnapshot> latest;
    std::thread thread;

    // The following members belong exclusively to the worker thread.
    ClothSimulator simulator;
    HairSimulator hairSimulator;
    std::uint64_t activeGeneration=0,activePauseEpoch=0,totalFixedSteps=0,cumulativeIterations=0,lastCompletedSerial=0;
    double simulatedSeconds=0,remainderSeconds=0,epochTimeBase=0;
    SkeletalPose previousInputPose;
    StaticModel previousInputGarment;
    SkeletalPose completedPose,completedAnimatedPose;
    StaticModel completedGarment,completedInputGarment;

    // Start only after every member, including the simulator, is initialized.
    Impl(){thread=std::thread([this]{Run();});}
    ~Impl(){
        {
            std::lock_guard lock(mutex);
            stopping=true;
            pending.reset();
        }
        work.notify_all();published.notify_all();
        if(thread.joinable())thread.join();
    }

    bool Current(std::uint64_t expected) const {
        std::lock_guard lock(mutex);
        return !stopping&&generation==expected;
    }

    bool Running(const Submission& submission) const {
        std::lock_guard lock(mutex);
        return !stopping&&!paused&&generation==submission.generation&&pauseEpoch==submission.pauseEpoch;
    }

    // Caller holds mutex. Keep cancelled waits cancelled after later serials
    // complete; a new post-resume snapshot must not acknowledge discarded work.
    bool Cancelled(std::uint64_t value) const {
        return std::any_of(cancelledSerials.begin(),cancelledSerials.end(),[&](const auto& range){return value>=range.first&&value<=range.second;});
    }

    bool Publish(std::shared_ptr<ClothPreviewSnapshot> result){
        {
            std::lock_guard lock(mutex);
            if(stopping||result->generation!=generation)return false;
            if(!result->error.empty()){
                fault=result->error;
                if(pending)pending->error=fault;
            }
            result->revision=++revision;
            latest=std::move(result);
        }
        published.notify_all();return true;
    }

    void Process(Submission submission){
        auto started=std::chrono::steady_clock::now();
        auto result=std::make_shared<ClothPreviewSnapshot>();
        auto Snapshot=[&](bool complete,double processed){
            result=std::make_shared<ClothPreviewSnapshot>();
            result->generation=submission.generation;
            // A partial output is renderable, but must not satisfy Wait for an
            // input whose full physical interval is still being simulated.
            result->serial=complete?submission.serial:lastCompletedSerial;
            result->submittedSeconds=epochTimeBase+submission.submittedSeconds-submission.delta+processed;
            // Only a pose whose complete fixed ticks have actually been solved
            // may accompany the physical geometry. A render-rate input endpoint
            // can be ahead of the last tick by almost one fixed interval.
            result->pose=completedPose;result->garment=GeometryOnly(completedGarment);
        };
        auto Statistics=[&]{
            result->positions=simulator.Positions();result->stats=simulator.Stats();
            result->hairEnabled=bool(submission.hairAsset);
            result->clothEnabled=submission.simulateCloth;
            if(result->hairEnabled){result->hairStats=hairSimulator.Stats();result->hairPositions=hairSimulator.Positions();result->hairSurfaceOffsets=hairSimulator.SurfaceOffsets();}
            result->totalFixedSteps=totalFixedSteps;result->cumulativeIterations=cumulativeIterations;
            result->simulatedSeconds=simulatedSeconds;
            result->solveMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
        };
        try {
            std::string error;
            if(activeGeneration!=submission.generation){
                simulator=ClothSimulator{};activeGeneration=submission.generation;
                hairSimulator=HairSimulator{};
                totalFixedSteps=0;cumulativeIterations=0;simulatedSeconds=0;remainderSeconds=0;lastCompletedSerial=0;
                activePauseEpoch=submission.pauseEpoch;epochTimeBase=0;
                previousInputPose=submission.pose;previousInputGarment=GeometryOnly(submission.garment);
                completedPose=completedAnimatedPose=submission.pose;completedInputGarment=GeometryOnly(submission.garment);completedGarment=GeometryOnly(submission.garment);
                Snapshot(true,submission.delta);
                if(submission.simulateCloth&&!simulator.Reset(*submission.cloth,submission.pose,submission.garment,&error))
                    result->error="Cloth initialization: "+error;
                // Reset can begin inside a raised thigh. Complete the solver's
                // zero-time warm start before publishing the initial geometry,
                // even when the first input has less than one physical tick.
                if(submission.simulateCloth&&result->error.empty()&&submission.error.empty()){
                    if(!simulator.Step(*submission.cloth,completedPose,submission.garment,0,submission.settings,&error))
                        result->error="Cloth initialization: "+error;
                    else {
                        cumulativeIterations+=simulator.Stats().constraintIterations;
                        if(!simulator.Apply(*submission.cloth,completedGarment,&error))
                            result->error="Cloth output: "+error;
                    }
                }
                if(result->error.empty()&&submission.hairAsset){
                    if(!submission.bodySource||!hairSimulator.Reset(*submission.hairAsset,*submission.bodySource,submission.pose,&error)||
                       !hairSimulator.Step(*submission.hairAsset,*submission.bodySource,submission.pose,0,submission.hairSettings,completedPose,&error))
                        result->error="Hair initialization: "+error;
                }
            }
            // Epoch changes cancel an old backlog without invalidating the
            // simulator. Start the new input clock at its last completed tick,
            // using source geometry rather than Apply-deformed cloth as input.
            const bool rebased=activePauseEpoch!=submission.pauseEpoch;
            if(rebased){
                activePauseEpoch=submission.pauseEpoch;epochTimeBase=simulatedSeconds;remainderSeconds=0;
                previousInputPose=completedAnimatedPose;previousInputGarment=GeometryOnly(completedInputGarment);
            }
            if(!result->generation)Snapshot(true,submission.delta);
            if(result->error.empty()&&!submission.error.empty())result->error=submission.error;
            if(!result->error.empty()){Statistics();Publish(std::move(result));return;}
            const auto* hierarchy=submission.bodySource?&submission.bodySource->skeleton
                :submission.garmentSource?&submission.garmentSource->skeleton:nullptr;
            InputInterpolation interpolation(previousInputPose,submission.pose,previousInputGarment,submission.garment,hierarchy);
            SkeletalPose intermediatePose;StaticModel intermediateGarment;
            const double fixed=submission.simulateCloth?submission.settings.fixedTimeStep:std::clamp(submission.hairSettings.fixedTimeStep,1./240.,1./15.);
            // The worker, rather than the render-rate solver call, owns the
            // fractional input time. Passing only whole ticks keeps the solver's
            // previous target/capsule endpoint at the last physical endpoint.
            const double carried=remainderSeconds;
            double available=carried+submission.delta,stepped=0;
            const double roundoff=fixed*1e-10;
            bool first=true;
            while(first||available+roundoff>=fixed){
                if(!Running(submission))return;
                first=false;started=std::chrono::steady_clock::now();
                // Publish every completed fixed tick for minimum display lag. Do
                // not call Step for a fractional final input interval: its pins
                // would still describe the preceding tick while the body moved.
                const std::uint64_t ticks=available+roundoff>=fixed?1:0;
                const double delta=double(ticks)*fixed;
                const double next=std::clamp(stepped+delta-carried,0.0,submission.delta);
                const bool complete=available-delta+roundoff<fixed;
                double fraction=submission.delta>0?next/submission.delta:1;
                // Exact interval endpoints must retain the caller's exact pose
                // and source mesh, avoiding one final decomposed-matrix roundoff.
                if(submission.delta-next<=roundoff)fraction=1;
                const SkeletalPose* pose=&submission.pose;const StaticModel* geometry=&submission.garment;
                if(ticks&&fraction<1&&interpolation.poseChanged){interpolation.PoseAt(fraction,intermediatePose);pose=&intermediatePose;}
                if(ticks&&fraction<1&&(interpolation.poseChanged||interpolation.meshChanged)&&submission.garmentSource){
                    // The same quaternion pose must drive body/capsules and
                    // garment pins. Interpolating endpoint vertices follows a
                    // chord and detaches pins from rotating skinned bones.
                    if(!SkinSkeletalMesh(*submission.garmentSource,*pose,intermediateGarment,&error))
                        throw std::runtime_error("Cloth intermediate skinning: "+error);
                    geometry=&intermediateGarment;
                } else if(ticks&&fraction<1&&interpolation.meshChanged){
                    // Generic CPU geometry callers without a skeletal source
                    // explicitly use linear vertex trajectories.
                    interpolation.MeshAt(fraction,intermediateGarment);geometry=&intermediateGarment;
                }
                std::string simulationError;
                if(ticks){
                    if(submission.simulateCloth){
                    const auto previousSteps=simulator.Stats().steps,previousResets=simulator.Stats().resets;
                    if(!simulator.Step(*submission.cloth,*pose,*geometry,delta,submission.settings,&error)){
                        simulationError="Cloth simulation: "+error;
                    } else {
                        const auto& stats=simulator.Stats();
                        cumulativeIterations+=stats.constraintIterations;
                        if(stats.resets!=previousResets||stats.steps<previousSteps)
                            simulationError="Cloth pose changed discontinuously; reset the preview generation when seeking";
                        else {
                            const auto steps=stats.steps-previousSteps;
                            if(steps!=ticks)simulationError="Cloth fixed clock did not complete the requested whole ticks";
                            totalFixedSteps+=steps;simulatedSeconds+=double(steps)*fixed;
                        }
                    }
                    }else {totalFixedSteps+=ticks;simulatedSeconds+=double(ticks)*fixed;}
                    if(!Current(submission.generation))return;
                    if(simulationError.empty()){
                        completedPose=completedAnimatedPose=*pose;completedInputGarment=GeometryOnly(*geometry);completedGarment=GeometryOnly(*geometry);
                        if(submission.simulateCloth&&!simulator.Apply(*submission.cloth,completedGarment,&error))
                            simulationError="Cloth output: "+error;
                        if(simulationError.empty()&&submission.hairAsset&&
                           !hairSimulator.Step(*submission.hairAsset,*submission.bodySource,*pose,delta,submission.hairSettings,completedPose,&error))
                            simulationError="Hair simulation: "+error;
                    }
                }
                if(!Current(submission.generation))return;
                // A pause may arrive inside Step. Publish that finished quantum
                // coherently before stopping; throwing it away would leave the
                // retained simulator ahead of the displayed pose after resume.
                if(!Running(submission)){
                    Snapshot(false,next);result->submittedSeconds=simulatedSeconds;
                    result->error=std::move(simulationError);Statistics();Publish(std::move(result));
                    remainderSeconds=0;previousInputPose=completedAnimatedPose;previousInputGarment=GeometryOnly(completedInputGarment);
                    return;
                }
                Snapshot(complete,complete?submission.delta:next);
                result->error=std::move(simulationError);
                Statistics();
                if(!result->error.empty())result->serial=submission.serial;
                const bool failed=!result->error.empty();
                if(!Publish(std::move(result))||failed)return;
                available-=delta;stepped+=delta;
                if(std::abs(available)<=roundoff)available=0;
                if(complete){
                    // Preserve animated source targets, never the Apply-deformed
                    // output, as the start of the next coalesced interval.
                    if(!rebased||submission.delta>0){
                        previousInputPose=std::move(submission.pose);
                        previousInputGarment=std::move(submission.garment);
                    }
                    remainderSeconds=available;
                    lastCompletedSerial=submission.serial;
                    return;
                }
            }
        } catch(const std::exception& exception){
            if(!result)Snapshot(true,submission.delta);
            result->error=std::string("Cloth worker: ")+exception.what();
        } catch(...){
            if(!result)Snapshot(true,submission.delta);
            result->error="Cloth worker: unknown exception";
        }
        result->serial=submission.serial;Statistics();Publish(std::move(result));
    }

    void Run(){
        for(;;){
            std::optional<Submission> submission;
            {
                std::unique_lock lock(mutex);
                work.wait(lock,[&]{return stopping||(!paused&&pending.has_value());});
                if(stopping)return;
                submission=std::move(pending);pending.reset();
                if(!fault.empty())submission->error=fault;
            }
            Process(std::move(*submission));
        }
    }
};

ClothPreviewWorker::ClothPreviewWorker():impl_(std::make_unique<Impl>()){}
ClothPreviewWorker::~ClothPreviewWorker()=default;

std::uint64_t ClothPreviewWorker::Submit(const ClothAsset& cloth,const SkeletalPose& pose,
        const StaticModel& animatedGarment,double delta,const ClothSolverSettings& settings,bool reset,
        std::shared_ptr<const SkeletalAssetPackage> garmentSource,
        std::shared_ptr<const HairAsset> hairAsset,std::shared_ptr<const SkeletalAssetPackage> bodySource,
        const HairSolverSettings& hairSettings,bool simulateCloth){
    {std::lock_guard lock(impl_->mutex);if(impl_->paused)return impl_->generation;}
    Impl::Submission submission;
    submission.pose=pose;submission.garment=GeometryOnly(animatedGarment);submission.settings=settings;
    submission.garmentSource=std::move(garmentSource);
    submission.hairAsset=std::move(hairAsset);submission.bodySource=std::move(bodySource);submission.hairSettings=hairSettings;
    submission.simulateCloth=simulateCloth;
    // A shared publication must solve hair against every completed cloth pose.
    // Keep the original internal substep frequency when joining a different
    // clock (30 Hz x 4 becomes 60 Hz x 2), without editing the source asset.
    if(simulateCloth&&submission.hairAsset&&std::isfinite(hairSettings.fixedTimeStep)&&hairSettings.fixedTimeStep>0&&std::isfinite(settings.fixedTimeStep)&&settings.fixedTimeStep>0){
        submission.hairSettings.substeps=static_cast<std::uint32_t>(std::clamp(std::round(double(hairSettings.substeps)*settings.fixedTimeStep/hairSettings.fixedTimeStep),1.,8.));
        submission.hairSettings.fixedTimeStep=settings.fixedTimeStep;
    }
    if(!simulateCloth&&(!std::isfinite(hairSettings.fixedTimeStep)||hairSettings.fixedTimeStep<=0))submission.error="Hair preview has invalid fixed step";
    if(submission.hairAsset&&!submission.bodySource)submission.error="Hair simulation requires an immutable body source";
    if(!std::isfinite(delta)||delta<0)submission.error="Cloth submission delta must be finite and nonnegative";
    else submission.delta=delta;
    if(!SettingsFiniteAndBounded(settings))submission.error="Cloth submission has invalid solver settings";
    std::uint64_t generation;
    {
        std::lock_guard lock(impl_->mutex);
        if(impl_->paused)return impl_->generation;
        if(reset||!impl_->asset||!SameIdentity(*impl_->asset,cloth)||
           impl_->garmentSource.get()!=submission.garmentSource.get()||
           impl_->hairAsset.get()!=submission.hairAsset.get()||impl_->bodySource.get()!=submission.bodySource.get()||impl_->simulateCloth!=simulateCloth){
            auto asset=std::make_shared<const ClothAsset>(cloth);
            ++impl_->generation;impl_->asset=std::move(asset);
            impl_->garmentSource=submission.garmentSource;
            impl_->hairAsset=submission.hairAsset;impl_->bodySource=submission.bodySource;
            impl_->simulateCloth=simulateCloth;
            impl_->submittedSeconds=0;impl_->pending.reset();impl_->latest.reset();impl_->fault.clear();
        }
        generation=impl_->generation;
        submission.generation=generation;submission.serial=++impl_->serial;submission.pauseEpoch=impl_->pauseEpoch;submission.cloth=impl_->asset;
        if(impl_->rebaseNextInput){submission.delta=0;delta=0;impl_->rebaseNextInput=false;}
        if(impl_->pending)submission.delta+=impl_->pending->delta;
        // A zero-delta input never erases already queued physical time.
        if(std::isfinite(delta)&&delta>=0)impl_->submittedSeconds+=delta;
        if(!std::isfinite(submission.delta)||!std::isfinite(impl_->submittedSeconds)){
            submission.error="Cloth accumulated elapsed time exceeded the finite range";submission.delta=0;
        }
        if(!submission.error.empty()&&impl_->fault.empty())impl_->fault=submission.error;
        if(!impl_->fault.empty())submission.error=impl_->fault;
        submission.submittedSeconds=impl_->submittedSeconds;
        impl_->pending=std::move(submission);
    }
    impl_->work.notify_one();impl_->published.notify_all();
    return generation;
}

void ClothPreviewWorker::SetPaused(bool paused){
    {
        std::lock_guard lock(impl_->mutex);
        if(impl_->paused==paused)return;
        impl_->paused=paused;
        if(paused){
            ++impl_->pauseEpoch;impl_->pending.reset();impl_->submittedSeconds=0;
            const auto first=impl_->latest?impl_->latest->serial+1:1;
            if(first<=impl_->serial){
                if(!impl_->cancelledSerials.empty()&&first<=impl_->cancelledSerials.back().second+1)
                    impl_->cancelledSerials.back().second=impl_->serial;
                else impl_->cancelledSerials.emplace_back(first,impl_->serial);
            }
            impl_->rebaseNextInput=true;
        }
    }
    impl_->work.notify_all();impl_->published.notify_all();
}

std::shared_ptr<const ClothPreviewSnapshot> ClothPreviewWorker::Latest() const {
    std::lock_guard lock(impl_->mutex);return impl_->latest;
}
std::uint64_t ClothPreviewWorker::Generation() const {
    std::lock_guard lock(impl_->mutex);return impl_->generation;
}
std::uint64_t ClothPreviewWorker::SubmissionSerial() const {
    std::lock_guard lock(impl_->mutex);return impl_->serial;
}
bool ClothPreviewWorker::Wait(std::uint64_t generation,std::uint64_t serial,std::chrono::milliseconds timeout) const {
    std::unique_lock lock(impl_->mutex);
    const bool signaled=impl_->published.wait_for(lock,timeout,[&]{return impl_->stopping||impl_->generation!=generation||
        impl_->Cancelled(serial)||
        (impl_->latest&&impl_->latest->generation==generation&&impl_->latest->serial>=serial);});
    return signaled&&!impl_->stopping&&impl_->generation==generation&&!impl_->Cancelled(serial)&&impl_->latest&&impl_->latest->serial>=serial;
}

} // namespace Model::detail
