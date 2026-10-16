#include "Model/Private/cloth_preview_worker.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <thread>
#include <glm/gtc/matrix_transform.hpp>

namespace {
using namespace Model;
using namespace Model::detail;
using namespace std::chrono_literals;
int failures=0;
bool Expect(bool condition,const char* message){
    std::printf("  %-92s %s\n",message,condition?"ok":"FAIL");failures+=!condition;return condition;
}
struct Fixture {ClothAsset cloth;StaticModel mesh;SkeletalPose pose;};
Fixture Sheet(){
    Fixture f;f.cloth.id="worker-sheet";f.cloth.skeletonId="worker-skeleton";
    f.cloth.settings.collision=false;f.cloth.settings.convergenceTolerance=0;
    f.mesh.meshes.resize(1);f.mesh.instances.push_back({"sheet",0,glm::dmat4(1)});
    const glm::dvec3 points[]={{-.1,1,0},{.1,1,0},{-.1,.8,.05},{.1,.8,.05}};
    auto& mesh=f.mesh.meshes[0];mesh.name="sheet";
    for(std::uint32_t i=0;i<4;++i){
        StaticVertex vertex;vertex.position=points[i];vertex.normal={0,0,1};
        vertex.uv={float(i%2),float(i/2)};mesh.vertices.push_back(vertex);mesh.bounds.Include(points[i]);
        f.cloth.particles.push_back({points[i],i<2?0.:1.,i<2?0.:1.0,0});
        f.cloth.renderBindings.push_back({0,i,i});
    }
    f.cloth.triangles={{{0,2,1}},{{1,2,3}}};mesh.indices={0,2,1,1,2,3};
    for(const auto edge:{std::array<std::uint32_t,2>{0,1},{0,2},{1,2},{1,3},{2,3}})
        f.cloth.stretch.push_back({edge[0],edge[1],glm::length(points[edge[0]]-points[edge[1]])});
    f.cloth.tethers={{2,0,glm::length(points[2]-points[0])},{3,1,glm::length(points[3]-points[1])}};
    f.mesh.bounds=mesh.bounds;f.mesh.materials.resize(1);
    TextureImage texture;texture.width=32;texture.height=32;texture.rgba.resize(32*32*4,255);f.mesh.textures.push_back(std::move(texture));
    f.pose.local.resize(1);f.pose.global={glm::dmat4(1)};
    return f;
}
std::shared_ptr<const ClothPreviewSnapshot> Finish(ClothPreviewWorker& worker,std::uint64_t generation){
    const auto serial=worker.SubmissionSerial();
    if(!Expect(worker.Wait(generation,serial,5s),"The latest submitted serial completes without blocking the render caller"))return {};
    const auto snapshot=worker.Latest();
    Expect(snapshot&&snapshot->generation==generation&&snapshot->serial>=serial,"Published snapshot belongs to the requested generation and serial");
    return snapshot;
}
void ElapsedAndOwnership(){
    std::puts("Async coalescing, geometry ownership and fixed time:");
    auto f=Sheet();ClothPreviewWorker worker;
    const auto generation=worker.Submit(f.cloth,f.pose,f.mesh,0,f.cloth.settings,true);
    auto initial=Finish(worker,generation);if(!initial)return;
    Expect(initial->error.empty()&&initial->positions.size()==4&&initial->totalFixedSteps==0,"Initialization publishes a valid zero-time cloth snapshot");
    Expect(initial->garment.materials.empty()&&initial->garment.textures.empty()&&initial->garment.meshes.size()==1&&initial->garment.instances.size()==1,
        "The worker transports only garment geometry and never duplicates decoded materials/textures");
    for(int i=0;i<400;++i)worker.Submit(f.cloth,f.pose,f.mesh,.001,f.cloth.settings);
    auto snapshot=Finish(worker,generation);if(!snapshot)return;
    Expect(snapshot->error.empty()&&std::abs(snapshot->submittedSeconds-.4)<1e-9&&
        std::abs(snapshot->simulatedSeconds-.4)<1e-9&&snapshot->totalFixedSteps==24,
        "Rapid latest-pose coalescing preserves every submitted second, including backlog over 0.1 seconds");
    Expect(snapshot->cumulativeIterations==24*f.cloth.settings.substeps*f.cloth.settings.iterations,
        "Cumulative work statistics retain the full fixed-step substep and iteration budget");
    const auto held=snapshot->positions;
    const auto heldPose=snapshot->pose;
    f.pose.global[0][3].x=.025;
    worker.Submit(f.cloth,f.pose,f.mesh,0,f.cloth.settings);
    snapshot=Finish(worker,generation);if(!snapshot)return;
    bool unchanged=snapshot->positions.size()==held.size();
    for(std::size_t i=0;unchanged&&i<held.size();++i)unchanged&=snapshot->positions[i]==held[i];
    Expect(unchanged&&snapshot->totalFixedSteps==24&&std::abs(snapshot->submittedSeconds-.4)<1e-9,
        "Zero-delta freeze retains physical geometry without advancing or discarding physical time");
    Expect(snapshot->pose.global==heldPose.global,"A frozen input cannot publish newer bones beside older physical geometry");

    const auto ownedGeneration=worker.Submit(f.cloth,f.pose,f.mesh,0,f.cloth.settings,true);
    const auto original=f.mesh.meshes[0].vertices[0].position;
    f.mesh.meshes[0].vertices[0].position.x=99;f.cloth.particles[0].restPosition.x=99;
    snapshot=Finish(worker,ownedGeneration);
    Expect(snapshot&&snapshot->error.empty()&&snapshot->positions[0]==original,
        "Submitted geometry and immutable generation assets remain valid after caller-side mutation");
}
void PauseAndReset(){
    std::puts("Pause settling and generation cancellation:");auto f=Sheet();ClothPreviewWorker worker;
    const auto generation=worker.Submit(f.cloth,f.pose,f.mesh,0,f.cloth.settings,true);
    auto initial=Finish(worker,generation);if(!initial)return;
    worker.Submit(f.cloth,f.pose,f.mesh,.1,f.cloth.settings);
    auto moved=Finish(worker,generation);if(!moved)return;
    double displacement=0;
    for(std::size_t i=0;i<moved->positions.size();++i)displacement=std::max(displacement,glm::length(moved->positions[i]-initial->positions[i]));
    Expect(moved->error.empty()&&moved->totalFixedSteps==6&&displacement>1e-5,
        "An unchanged animation pose still permits gravity-driven settling as physical time advances");
    worker.Submit(f.cloth,f.pose,f.mesh,3600,f.cloth.settings);
    const auto oldSerial=worker.SubmissionSerial();
    const auto replacement=worker.Submit(f.cloth,f.pose,f.mesh,0,f.cloth.settings,true);
    Expect(replacement>generation&&!worker.Wait(generation,oldSerial,1ms),"A reset immediately supersedes outstanding requests from the previous generation");
    const auto reset=Finish(worker,replacement);
    Expect(reset&&reset->error.empty()&&reset->submittedSeconds==0&&reset->simulatedSeconds==0&&
        reset->totalFixedSteps==0&&reset->cumulativeIterations==0&&reset->stats.resets==1,
        "Replacement generations clear pending elapsed time and cumulative statistics without stale publication");
    f.cloth.id="different-outfit";
    const auto newAsset=worker.Submit(f.cloth,f.pose,f.mesh,0,f.cloth.settings);
    Expect(newAsset>replacement,"Changing the immutable cloth identity starts a fresh generation");
    Finish(worker,newAsset);
}
void FocusSuspension(){
    std::puts("Focus suspension cancels backlog without resetting the cloth:");
    auto f=Sheet();ClothPreviewWorker worker;
    const auto generation=worker.Submit(f.cloth,f.pose,f.mesh,.1,f.cloth.settings,true);
    auto before=Finish(worker,generation);if(!before)return;
    const auto original=f.mesh;
    f.pose.local[0].translation.x=.12;f.pose.global[0][3].x=.12;
    for(auto& vertex:f.mesh.meshes[0].vertices)vertex.position.x+=.12;
    worker.Submit(f.cloth,f.pose,f.mesh,3600,f.cloth.settings);
    const auto activeSerial=worker.SubmissionSerial();
    worker.Submit(f.cloth,f.pose,f.mesh,3600,f.cloth.settings);
    const auto pendingSerial=worker.SubmissionSerial();
    const auto pauseStarted=std::chrono::steady_clock::now();worker.SetPaused(true);
    const auto atPause=worker.Latest();
    Expect(std::chrono::steady_clock::now()-pauseStarted<100ms,
        "Focus suspension does not wait for the active solver or its queued hour-long backlog");
    Expect(!worker.Wait(generation,activeSerial,1ms)&&!worker.Wait(generation,pendingSerial,1ms),
        "Suspension immediately cancels both active and pending serial waits");
    const auto heldSerial=worker.SubmissionSerial();
    auto badSettings=f.cloth.settings;badSettings.substeps=0;
    for(int i=0;i<16;++i)worker.Submit(f.cloth,f.pose,f.mesh,std::numeric_limits<double>::quiet_NaN(),badSettings,true);
    Expect(worker.Generation()==generation&&worker.SubmissionSerial()==heldSerial,
        "Paused submissions are ignored without geometry jobs, faults, resets or accepted time");
    // The portable sheet's current quantum is tiny. The two sleeps let an
    // already-entered Step finish, then detect any incorrectly draining loop.
    std::this_thread::sleep_for(30ms);const auto held=worker.Latest();
    std::this_thread::sleep_for(30ms);const auto stillHeld=worker.Latest();
    Expect(atPause&&held&&stillHeld&&held->error.empty()&&
        held->totalFixedSteps<=atPause->totalFixedSteps+1&&stillHeld==held&&held->generation==generation,
        "An in-flight quantum finishes at most one tick and the paused worker then remains asleep");
    if(!held)return;
    bool attached=held->pose.global.size()==1;
    const double heldTranslation=held->pose.global[0][3].x;
    for(const auto& binding:f.cloth.renderBindings)if(f.cloth.particles[binding.particle].inverseMass==0){
        const auto target=original.meshes[binding.mesh].vertices[binding.vertex].position+glm::dvec3(heldTranslation,0,0);
        attached&=glm::length(held->positions[binding.particle]-target)<1e-10&&
            glm::length(held->garment.meshes[binding.mesh].vertices[binding.vertex].position-target)<1e-10;
    }
    Expect(attached&&heldTranslation>=0&&heldTranslation<=.12&&
        std::abs(held->simulatedSeconds-held->totalFixedSteps*f.cloth.settings.fixedTimeStep)<1e-10,
        "A quantum completing during pause publishes body, rendered/physical pins and clock at the same motion time");
    f.pose=held->pose;f.mesh=original;
    for(auto& vertex:f.mesh.meshes[0].vertices)vertex.position.x+=heldTranslation;
    worker.SetPaused(false);
    worker.Submit(f.cloth,f.pose,f.mesh,3600,f.cloth.settings);
    const auto resumed=Finish(worker,generation);if(!resumed)return;
    Expect(resumed->error.empty()&&resumed->generation==generation&&resumed->positions==held->positions&&
        resumed->pose.global==held->pose.global&&resumed->totalFixedSteps==held->totalFixedSteps&&
        resumed->cumulativeIterations==held->cumulativeIterations&&resumed->stats.resets==held->stats.resets&&
        resumed->simulatedSeconds==held->simulatedSeconds&&resumed->submittedSeconds==held->simulatedSeconds,
        "First resume rebases to the complete held pose without reset, cloth jump or spending paused time");
    Expect(!worker.Wait(generation,activeSerial,1ms)&&!worker.Wait(generation,pendingSerial,1ms),
        "A later resumed serial cannot acknowledge the previously discarded backlog");
    worker.Submit(f.cloth,f.pose,f.mesh,1./60,f.cloth.settings);
    const auto advanced=Finish(worker,generation);if(!advanced)return;
    Expect(advanced->error.empty()&&advanced->totalFixedSteps==held->totalFixedSteps+1&&
        std::abs(advanced->submittedSeconds-held->simulatedSeconds-1./60)<1e-10&&
        advanced->stats.resets==held->stats.resets,
        "The retained simulation advances normally for exactly one newly accepted fixed tick");

    worker.Submit(f.cloth,f.pose,f.mesh,.01,f.cloth.settings);const auto fractional=Finish(worker,generation);
    if(!fractional)return;
    worker.SetPaused(true);worker.SetPaused(false);
    worker.Submit(f.cloth,f.pose,f.mesh,999,f.cloth.settings);const auto rebased=Finish(worker,generation);
    worker.Submit(f.cloth,f.pose,f.mesh,.01,f.cloth.settings);const auto afterFraction=Finish(worker,generation);
    Expect(rebased&&afterFraction&&afterFraction->error.empty()&&
        rebased->totalFixedSteps==fractional->totalFixedSteps&&afterFraction->totalFixedSteps==fractional->totalFixedSteps&&
        std::abs(afterFraction->submittedSeconds-afterFraction->simulatedSeconds-.01)<1e-10,
        "Pause discards the old sub-tick remainder rather than combining it with fresh resumed input");

    for(int i=0;i<32;++i){
        worker.SetPaused(true);worker.SetPaused(true);worker.SetPaused(false);worker.SetPaused(false);
        worker.Submit(f.cloth,f.pose,f.mesh,3600,f.cloth.settings);
    }
    worker.SetPaused(true);const auto pausedSteps=worker.Latest()->totalFixedSteps;
    worker.SetPaused(false);worker.Submit(f.cloth,f.pose,f.mesh,3600,f.cloth.settings);
    const auto toggled=Finish(worker,generation);
    Expect(toggled&&toggled->error.empty()&&toggled->generation==generation&&toggled->totalFixedSteps==pausedSteps&&
        toggled->stats.resets==held->stats.resets,
        "Rapid duplicate focus toggles preserve one simulator and never reactivate an old epoch backlog");
}
Fixture MovingCollider(){
    Fixture f;f.cloth.id="worker-moving-collider";f.cloth.skeletonId="worker-skeleton";
    auto& settings=f.cloth.settings;settings.gravity={0,0,0};settings.damping=0;
    settings.localDamping=0;settings.animationDamping=0;settings.friction=0;settings.convergenceTolerance=0;
    f.mesh.meshes.resize(1);f.mesh.instances.push_back({"particles",0,glm::dmat4(1)});
    for(const auto position:{glm::dvec3(0,1,0),glm::dvec3(0,0,0)}){
        StaticVertex vertex;vertex.position=position;f.mesh.meshes[0].vertices.push_back(vertex);
        const auto i=std::uint32_t(f.cloth.particles.size());
        f.cloth.particles.push_back({position,i==0?0.:1.,i==0?0.:10.,0});
        f.cloth.renderBindings.push_back({0,i,i});
    }
    f.cloth.capsules.push_back({"moving-thigh",0,0,{0,-.1,0},{0,.1,0},.08});
    f.pose.local.resize(1);f.pose.local[0].translation={-.3,0,0};
    f.pose.global={glm::translate(glm::dmat4(1),f.pose.local[0].translation)};f.pose.morphWeights={0};
    return f;
}
void MoveEndpoint(Fixture& f,double fraction){
    f.pose.local[0].translation={-.3+.6*fraction,0,0};
    f.pose.global[0]=glm::translate(glm::dmat4(1),f.pose.local[0].translation);
    f.pose.morphWeights[0]=float(fraction);
    f.mesh.meshes[0].vertices[0].position={.08*fraction,1,0};
    f.mesh.meshes[0].vertices[1].position={.08*fraction,0,0};
}
void SingleTickPublications(){
    std::puts("Every completed physics tick is immediately publishable:");
    auto f=MovingCollider();ClothPreviewWorker worker;ClothSimulator reference;std::string error;
    const auto generation=worker.Submit(f.cloth,f.pose,f.mesh,0,f.cloth.settings,true);
    const auto initial=Finish(worker,generation);if(!initial)return;
    bool valid=reference.Step(f.cloth,f.pose,f.mesh,0,f.cloth.settings,&error);
    for(int i=1;i<=6&&valid;++i){MoveEndpoint(f,i/6.);valid=reference.Step(f.cloth,f.pose,f.mesh,1./60,f.cloth.settings,&error);}
    worker.Submit(f.cloth,f.pose,f.mesh,.1,f.cloth.settings);
    const auto serial=worker.SubmissionSerial();auto previous=initial;std::size_t observed=0;
    const auto deadline=std::chrono::steady_clock::now()+5s;
    while(std::chrono::steady_clock::now()<deadline){
        const auto current=worker.Latest();
        if(current&&current->revision>previous->revision){
            // Latest may skip publications while this caller is descheduled.
            // The revision delta still counts every intervening publication.
            valid&=current->error.empty()&&current->generation==generation&&
                current->totalFixedSteps-previous->totalFixedSteps==current->revision-previous->revision&&
                current->totalFixedSteps<=6;
            const double fraction=current->simulatedSeconds/.1;
            valid&=std::abs(current->pose.global[0][3].x-(-.3+.6*fraction))<1e-10&&
                std::abs(current->positions[0].x-.08*fraction)<1e-10&&
                std::abs(current->garment.meshes[0].vertices[0].position.x-.08*fraction)<1e-10;
            if(current->totalFixedSteps<6)valid&=current->serial==initial->serial;
            previous=current;++observed;
            if(current->serial>=serial)break;
        }
        worker.Wait(generation,serial,1ms);
    }
    const auto final=Finish(worker,generation);if(!final)return;
    double difference=0;
    for(std::size_t i=0;i<final->positions.size();++i)difference=std::max(difference,glm::length(final->positions[i]-reference.Positions()[i]));
    Expect(valid&&observed>0&&final->totalFixedSteps==6&&final->revision-initial->revision==6&&
        final->pose.global==f.pose.global&&std::abs(final->simulatedSeconds-.1)<1e-10&&difference<1e-10,
        "A 0.1-second interval publishes six coherent tick snapshots and matches six synchronous ticks without lost time");
}
void MovingInputIntervals(){
    std::puts("Coalesced motion timing, incremental publication and rotation interpolation:");
    auto f=MovingCollider();ClothPreviewWorker worker;ClothSimulator reference,compressed;
    std::string error;
    auto generation=worker.Submit(f.cloth,f.pose,f.mesh,0,f.cloth.settings,true);
    auto initial=Finish(worker,generation);if(!initial)return;
    bool ok=reference.Step(f.cloth,f.pose,f.mesh,0,f.cloth.settings,&error)&&
        compressed.Step(f.cloth,f.pose,f.mesh,0,f.cloth.settings,&error);
    for(int i=1;i<=4&&ok;++i){
        MoveEndpoint(f,double(i)/4);
        ok=reference.Step(f.cloth,f.pose,f.mesh,.1,f.cloth.settings,&error);
    }
    worker.Submit(f.cloth,f.pose,f.mesh,.4,f.cloth.settings);
    auto result=Finish(worker,generation);if(!result)return;
    double difference=0;
    for(std::size_t i=0;ok&&i<result->positions.size();++i)
        difference=std::max(difference,glm::length(result->positions[i]-reference.Positions()[i]));
    Expect(ok&&result->error.empty()&&result->totalFixedSteps==24&&difference<1e-7,
        "A coalesced 0.4-second moving collider agrees with four evenly timed synchronous endpoints");
    Expect(result->positions[0]==f.mesh.meshes[0].vertices[0].position&&result->pose.global==f.pose.global&&
        result->pose.morphWeights==f.pose.morphWeights,
        "The final attachment and body/morph pose exactly reach the submitted source endpoint");
    for(int i=0;i<4&&ok;++i)ok=compressed.Step(f.cloth,f.pose,f.mesh,.1,f.cloth.settings,&error);
    Expect(ok&&glm::length(result->positions[1]-compressed.Positions()[1])>1e-3,
        "The regression detects the old first-segment acceleration followed by stationary collider motion");
    Expect(result->revision==initial->revision+24,
        "A long coalesced interval publishes every one of its twenty-four complete fixed ticks");

    // A long stationary interval keeps the worker alive while the caller checks
    // partial serial semantics. It is canceled immediately after that check.
    const auto previousSerial=result->serial,previousRevision=result->revision;
    worker.Submit(f.cloth,f.pose,f.mesh,3600,f.cloth.settings);
    const auto pendingSerial=worker.SubmissionSerial();
    const auto deadline=std::chrono::steady_clock::now()+2s;
    std::shared_ptr<const ClothPreviewSnapshot> partial;
    while(std::chrono::steady_clock::now()<deadline){
        partial=worker.Latest();
        if(partial&&partial->revision>previousRevision)break;
        std::this_thread::yield();
    }
    Expect(partial&&partial->revision>previousRevision&&partial->serial==previousSerial&&
        !worker.Wait(generation,pendingSerial,1ms),
        "Partial publications advance revision while Wait continues to require the complete input serial");
    generation=worker.Submit(f.cloth,f.pose,f.mesh,0,f.cloth.settings,true);Finish(worker,generation);

    auto rotation=MovingCollider();rotation.cloth.id="worker-half-turn";
    rotation.cloth.capsules[0].localA={.25,-.1,0};rotation.cloth.capsules[0].localB={.25,.1,0};
    rotation.pose.local[0].translation={0,0,0};rotation.pose.global[0]=glm::dmat4(1);
    generation=worker.Submit(rotation.cloth,rotation.pose,rotation.mesh,0,rotation.cloth.settings,true);Finish(worker,generation);
    const double pi=std::acos(-1.);
    rotation.pose.local[0].rotation=glm::angleAxis(pi,glm::dvec3(0,1,0));
    rotation.pose.global[0]=glm::mat4_cast(rotation.pose.local[0].rotation);
    worker.Submit(rotation.cloth,rotation.pose,rotation.mesh,.4,rotation.cloth.settings);
    result=Finish(worker,generation);
    bool finite=result&&result->error.empty();
    if(result)for(const auto& p:result->positions)finite&=std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);
    Expect(finite&&result->totalFixedSteps==24&&result->pose.global==rotation.pose.global,
        "A 180-degree bone rotation follows nonsingular quaternion intermediates and retains its exact endpoint");
}
void SkinnedRotationIntervals(){
    std::puts("Skinned garment attachments share the body quaternion trajectory:");
    Fixture f;f.cloth.id="worker-skinned-quarter-turn";f.cloth.skeletonId="worker-skeleton";
    f.cloth.settings.collision=false;f.cloth.settings.gravity={0,-.7,0};
    f.cloth.settings.convergenceTolerance=0;f.mesh.meshes.resize(1);
    f.mesh.instances.push_back({"rotating-garment",0,glm::dmat4(1)});
    const glm::dvec3 points[]={{1,0,0},{1,-.25,0},{1.25,-.25,0}};
    for(std::uint32_t i=0;i<3;++i){
        StaticVertex vertex;vertex.position=points[i];vertex.normal={0,0,1};
        f.mesh.meshes[0].vertices.push_back(vertex);
        f.cloth.particles.push_back({points[i],i==0?0.:1.,i==0?0.:5.,0});
        f.cloth.renderBindings.push_back({0,i,i});
    }
    f.mesh.meshes[0].indices={0,1,2};f.cloth.triangles={{{0,1,2}}};
    for(const auto edge:{std::array<std::uint32_t,2>{0,1},{1,2},{0,2}})
        f.cloth.stretch.push_back({edge[0],edge[1],glm::length(points[edge[0]]-points[edge[1]])});
    auto source=std::make_shared<SkeletalAssetPackage>();source->id="immutable-rotating-garment";
    source->skeleton.id=f.cloth.skeletonId;source->skeleton.bones.resize(1);
    source->geometry=std::make_shared<StaticModel>(f.mesh);source->skins.resize(1);source->skins[0].vertices.resize(3);
    source->geometry->materials.resize(1);source->geometry->textures.resize(1);
    source->geometry->textures[0].rgba.resize(32*32*4,255);
    f.pose.local.resize(1);f.pose.global={glm::dmat4(1)};
    const auto initialMesh=f.mesh;
    const auto endpointRotation=glm::angleAxis(std::acos(-1.)*.5,glm::dvec3(0,1,0));
    auto finalPose=f.pose;finalPose.local[0].rotation=endpointRotation;
    finalPose.global[0]=glm::mat4_cast(endpointRotation);
    StaticModel finalMesh;std::string error;
    bool ok=SkinSkeletalMesh(*source,finalPose,finalMesh,&error);
    ClothPreviewWorker worker;ClothSimulator reference,chord;
    const auto generation=worker.Submit(f.cloth,f.pose,f.mesh,0,f.cloth.settings,true,source);
    auto initial=Finish(worker,generation);if(!initial)return;
    ok=ok&&reference.Step(f.cloth,f.pose,f.mesh,0,f.cloth.settings,&error)&&
        chord.Step(f.cloth,f.pose,f.mesh,0,f.cloth.settings,&error);
    for(int i=1;i<=24&&ok;++i){
        const double fraction=double(i)/24;
        f.pose.local[0].rotation=glm::angleAxis(std::acos(-1.)*.5*fraction,glm::dvec3(0,1,0));
        f.pose.global[0]=glm::mat4_cast(f.pose.local[0].rotation);
        StaticModel skinned,chordMesh=initialMesh;
        ok=SkinSkeletalMesh(*source,f.pose,skinned,&error);
        for(std::size_t v=0;v<chordMesh.meshes[0].vertices.size();++v){
            const auto a=initialMesh.meshes[0].vertices[v].position,b=finalMesh.meshes[0].vertices[v].position;
            chordMesh.meshes[0].vertices[v].position=a+(b-a)*fraction;
        }
        if(i==12){
            const auto expected=glm::dvec3(std::sqrt(.5),0,-std::sqrt(.5));
            Expect(glm::length(skinned.meshes[0].vertices[0].position-expected)<1e-12&&
                glm::length(skinned.meshes[0].vertices[0].position-chordMesh.meshes[0].vertices[0].position)>.29,
                "A half-interval source skin follows the 45-degree bone arc rather than its endpoint chord");
        }
        ok=ok&&reference.Step(f.cloth,f.pose,skinned,f.cloth.settings.fixedTimeStep,f.cloth.settings,&error)&&
            chord.Step(f.cloth,f.pose,chordMesh,f.cloth.settings.fixedTimeStep,f.cloth.settings,&error);
    }
    worker.Submit(f.cloth,finalPose,finalMesh,.4,f.cloth.settings,false,source);
    const auto result=Finish(worker,generation);if(!result)return;
    double difference=0,chordDifference=0;
    for(std::size_t i=1;ok&&i<result->positions.size();++i){
        difference=std::max(difference,glm::length(result->positions[i]-reference.Positions()[i]));
        chordDifference=std::max(chordDifference,glm::length(reference.Positions()[i]-chord.Positions()[i]));
    }
    Expect(ok&&result->error.empty()&&result->totalFixedSteps==24&&difference<1e-7,
        "Coalesced source skinning matches dynamic cloth positions from explicit one-tick pose/skin reference steps");
    Expect(ok&&chordDifference>1e-4,
        "Dynamic cloth positions detect the arc/chord mismatch even though both paths end at identical pins");
    Expect(result->garment.materials.empty()&&result->garment.textures.empty()&&result->pose.global==finalPose.global,
        "Intermediate source skinning retains geometry-only snapshots and the exact final caller pose");
    const std::weak_ptr<const SkeletalAssetPackage> heldSource=source;source.reset();
    Expect(!heldSource.expired(),"The immutable skeletal source remains owned for the complete preview generation");
}
void HierarchicalSkinnedIntervals(){
    std::puts("Multi-level source interpolation follows one local hierarchy at non-endpoint publications:");
    auto f=Sheet();f.cloth.id="worker-hierarchical-parent-arcs";f.cloth.settings.gravity={0,0,0};
    auto source=std::make_shared<SkeletalAssetPackage>();source->id="immutable-hierarchical-garment";source->skeleton.id=f.cloth.skeletonId;
    source->skeleton.bones.resize(2);source->skeleton.bones[1].parent=0;source->skeleton.bones[1].restLocal.translation={.4,.2,.15};
    const auto rest=GlobalBoneTransforms(source->skeleton,RestPose(source->skeleton));
    for(std::size_t i=0;i<rest.size();++i)source->skeleton.bones[i].inverseBind=glm::inverse(rest[i]);
    source->geometry=std::make_shared<StaticModel>(f.mesh);source->skins.resize(1);source->skins[0].vertices.resize(4);
    for(auto& v:source->skins[0].vertices)v.joints={1,0,0,0};
    f.pose=EvaluateSkeletalPose(*source,nullptr,0,false);auto endpoint=f.pose;
    endpoint.local[0].rotation=glm::angleAxis(2.,glm::dvec3(0,1,0));endpoint.local[1].rotation=glm::angleAxis(.6,glm::dvec3(1,0,0));
    endpoint.global=GlobalBoneTransforms(source->skeleton,endpoint.local);StaticModel endMesh;std::string error;
    bool okay=SkinSkeletalMesh(*source,f.pose,f.mesh,&error)&&SkinSkeletalMesh(*source,endpoint,endMesh,&error);
    const auto originalVertices=source->geometry->meshes[0].vertices;const auto originalChild=source->skeleton.bones[1].restLocal;
    ClothPreviewWorker worker;const auto generation=worker.Submit(f.cloth,f.pose,f.mesh,0,f.cloth.settings,true,source);
    const auto initial=Finish(worker,generation);if(!initial)return;
    const auto fixed=f.cloth.settings.fixedTimeStep;worker.Submit(f.cloth,endpoint,endMesh,24.5*fixed,f.cloth.settings,false,source);
    const auto middle=Finish(worker,generation);if(!middle)return;
    okay&=middle->error.empty()&&middle->totalFixedSteps==24&&middle->pose.global!=endpoint.global;
    const auto composed=GlobalBoneTransforms(source->skeleton,middle->pose.local);double matrixError=0,maxPin=0;
    for(std::size_t bone=0;bone<composed.size();++bone)for(int c=0;c<4;++c)for(int r=0;r<4;++r)
        matrixError=std::max(matrixError,std::abs(composed[bone][c][r]-middle->pose.global[bone][c][r]));
    auto hierarchical=middle->pose;hierarchical.global=composed;StaticModel expected;okay=okay&&SkinSkeletalMesh(*source,hierarchical,expected,&error);
    if(okay)for(const auto& binding:f.cloth.renderBindings)if(f.cloth.particles[binding.particle].inverseMass==0){
        const auto point=expected.meshes[binding.mesh].vertices[binding.vertex].position;
        maxPin=std::max({maxPin,glm::length(middle->positions[binding.particle]-point),glm::length(middle->garment.meshes[binding.mesh].vertices[binding.vertex].position-point)});}
    std::printf("    Actual non-endpoint hierarchy/pin discrepancy %.12g / %.12g m\n",matrixError,maxPin);
    Expect(okay&&matrixError<1e-11&&maxPin<1e-10,
        "A completed 24.5-tick input retains hierarchical intermediate globals and physical/render pins on the same curved trajectory");
    worker.Submit(f.cloth,endpoint,endMesh,.5*fixed,f.cloth.settings,false,source);const auto complete=Finish(worker,generation);
    Expect(complete&&complete->error.empty()&&complete->totalFixedSteps==25&&complete->pose.global==endpoint.global,
        "The remaining half tick preserves exact caller endpoint matrices and the complete fixed clock");
    bool immutable=source->skeleton.bones[1].restLocal.translation==originalChild.translation&&source->skeleton.bones[1].restLocal.rotation==originalChild.rotation;
    for(std::size_t v=0;v<originalVertices.size();++v)immutable&=source->geometry->meshes[0].vertices[v].position==originalVertices[v].position;
    Expect(immutable,"Hierarchical intermediate reconstruction does not modify the immutable source skeleton or garment geometry");
    if(!okay)std::printf("    %s\n",error.c_str());
}

void FractionalInputClock(){
    std::puts("Render-rate input fractions retain a coherent physical pose and garment:");
    const double irregular[]={.002,.007,.011,.019};
    for(int scenario=0;scenario<5;++scenario){
        auto f=Sheet();f.cloth.id="worker-fractional-clock-"+std::to_string(scenario);
        auto source=std::make_shared<SkeletalAssetPackage>();source->id="fractional-skinned-sheet";
        source->skeleton.id=f.cloth.skeletonId;source->skeleton.bones.resize(1);
        source->geometry=std::make_shared<StaticModel>(f.mesh);source->skins.resize(1);
        source->skins[0].vertices.resize(f.mesh.meshes[0].vertices.size());
        const auto original=f.mesh;
        const bool skeletal=scenario<4;
        const int rate=scenario==0?120:scenario==1?144:240;
        const int inputs=scenario<3?rate:80;
        ClothPreviewWorker worker;
        const auto generation=worker.Submit(f.cloth,f.pose,f.mesh,0,f.cloth.settings,true,skeletal?source:nullptr);
        bool valid=worker.Wait(generation,worker.SubmissionSerial(),5s);
        auto previous=worker.Latest();valid&=previous&&previous->error.empty();
        double elapsed=0,maximumPinError=0,maximumRenderPinError=0,maximumClockError=0;
        std::size_t zeroTickInputs=0;
        for(int i=0;i<inputs&&valid;++i){
            const double delta=scenario<3?1./rate:irregular[i%4];elapsed+=delta;
            f.pose.local[0].translation={.12*elapsed,.025*elapsed,.02*elapsed};
            f.pose.local[0].rotation=skeletal?glm::angleAxis(.9*elapsed,glm::dvec3(0,1,0)):glm::dquat(1,0,0,0);
            f.pose.global[0]=glm::translate(glm::dmat4(1),f.pose.local[0].translation)*glm::mat4_cast(f.pose.local[0].rotation);
            std::string error;
            if(skeletal)valid=SkinSkeletalMesh(*source,f.pose,f.mesh,&error);
            else {
                f.mesh=original;
                for(auto& vertex:f.mesh.meshes[0].vertices)vertex.position+=f.pose.local[0].translation;
            }
            if(!valid)break;
            worker.Submit(f.cloth,f.pose,f.mesh,delta,f.cloth.settings,false,skeletal?source:nullptr);
            valid&=worker.Wait(generation,worker.SubmissionSerial(),5s);
            const auto snapshot=worker.Latest();valid&=snapshot&&snapshot->error.empty();
            if(!valid)break;
            StaticModel expected;
            if(skeletal)valid=SkinSkeletalMesh(*source,snapshot->pose,expected,&error);
            else {
                expected=original;
                const auto translation=glm::dvec3(snapshot->pose.global[0][3]);
                for(auto& vertex:expected.meshes[0].vertices)vertex.position+=translation;
            }
            if(!valid)break;
            for(const auto& binding:f.cloth.renderBindings)if(f.cloth.particles[binding.particle].inverseMass==0){
                const auto target=expected.meshes[binding.mesh].vertices[binding.vertex].position;
                maximumPinError=std::max(maximumPinError,glm::length(snapshot->positions[binding.particle]-target));
                maximumRenderPinError=std::max(maximumRenderPinError,
                    glm::length(snapshot->garment.meshes[binding.mesh].vertices[binding.vertex].position-target));
            }
            maximumClockError=std::max(maximumClockError,
                std::abs(snapshot->pose.global[0][3].x-.12*snapshot->simulatedSeconds));
            const auto expectedSteps=static_cast<std::uint64_t>(std::floor((elapsed+1e-10)/f.cloth.settings.fixedTimeStep));
            valid&=snapshot->totalFixedSteps==expectedSteps&&std::abs(snapshot->submittedSeconds-elapsed)<1e-10&&
                snapshot->simulatedSeconds<=elapsed+1e-10&&elapsed-snapshot->simulatedSeconds<f.cloth.settings.fixedTimeStep+1e-10;
            if(previous&&snapshot->totalFixedSteps==previous->totalFixedSteps){
                ++zeroTickInputs;
                valid&=snapshot->pose.global==previous->pose.global&&snapshot->positions==previous->positions;
            }
            previous=snapshot;
        }
        char message[180];
        std::snprintf(message,sizeof(message),"%s: every physical/render pin matches the same completed bone pose, including zero-tick inputs",
            scenario==0?"120 Hz":scenario==1?"144 Hz":scenario==2?"240 Hz":scenario==3?"2/7/11/19 ms skinned":"2/7/11/19 ms generic");
        Expect(valid&&zeroTickInputs>0&&maximumPinError<1e-10&&maximumRenderPinError<1e-10&&maximumClockError<1e-10,message);
        if(!valid||maximumPinError>=1e-10||maximumRenderPinError>=1e-10||maximumClockError>=1e-10)
            std::printf("    clock diagnostic: input %.12f, zero ticks %zu, pin %.9g, render pin %.9g, clock %.9g\n",
                elapsed,zeroTickInputs,maximumPinError,maximumRenderPinError,maximumClockError);
        // A fractional remainder is included in the next job, and cancellation
        // must still avoid draining its arbitrarily long physical interval.
        worker.Submit(f.cloth,f.pose,f.mesh,3600.003,f.cloth.settings,false,skeletal?source:nullptr);
        const auto oldSerial=worker.SubmissionSerial();
        const auto replacement=worker.Submit(f.cloth,f.pose,f.mesh,0,f.cloth.settings,true,skeletal?source:nullptr);
        valid=replacement>generation&&!worker.Wait(generation,oldSerial,1ms)&&
            worker.Wait(replacement,worker.SubmissionSerial(),5s);
        const auto reset=worker.Latest();
        Expect(valid&&reset&&reset->error.empty()&&reset->generation==replacement&&reset->totalFixedSteps==0&&
            reset->submittedSeconds==0&&reset->simulatedSeconds==0,
            "Reset discards a fractional remainder and cancels coalesced backlog without publishing stale physical time");
    }
}
void FractionalInitialization(){
    std::puts("Fractional first input still performs the zero-time collision warm start:");
    auto f=Sheet();f.cloth.id="worker-fractional-initialization";f.cloth.settings.collision=true;
    ClothCapsule capsule;capsule.name="initial-thigh";capsule.boneA=capsule.boneB=0;
    capsule.localA={0,.7,0};capsule.localB={0,1.1,0};capsule.radius=.08;
    capsule.restA={.4,.7,0};capsule.restB={.4,1.1,0};capsule.referenceValid=true;
    f.cloth.capsules.push_back(capsule);
    ClothSimulator reference;std::string error;
    bool valid=reference.Reset(f.cloth,f.pose,f.mesh,&error)&&reference.Step(f.cloth,f.pose,f.mesh,0,f.cloth.settings,&error);
    ClothPreviewWorker worker;const auto generation=worker.Submit(f.cloth,f.pose,f.mesh,.001,f.cloth.settings,true);
    valid&=worker.Wait(generation,worker.SubmissionSerial(),5s);
    const auto snapshot=worker.Latest();valid&=snapshot&&snapshot->error.empty();
    double difference=0;
    if(valid)for(std::size_t i=0;i<snapshot->positions.size();++i)
        difference=std::max(difference,glm::length(snapshot->positions[i]-reference.Positions()[i]));
    Expect(valid&&snapshot->totalFixedSteps==0&&snapshot->simulatedSeconds==0&&
        std::abs(snapshot->submittedSeconds-.001)<1e-12&&snapshot->cumulativeIterations>0&&difference<1e-10,
        "An initial sub-tick input publishes the complete collision warm start without inventing animation time");
}
void ErrorRecovery(){
    std::puts("Error snapshots and explicit recovery:");auto f=Sheet();ClothPreviewWorker worker;
    const auto generation=worker.Submit(f.cloth,f.pose,f.mesh,std::numeric_limits<double>::quiet_NaN(),f.cloth.settings,true);
    worker.Submit(f.cloth,f.pose,f.mesh,.1,f.cloth.settings);
    auto failed=Finish(worker,generation);
    Expect(failed&&!failed->error.empty()&&failed->totalFixedSteps==0,
        "A nonfinite submission becomes a safe error snapshot and cannot be erased by coalescing");
    const auto recovered=worker.Submit(f.cloth,f.pose,f.mesh,.05,f.cloth.settings,true);
    auto snapshot=Finish(worker,recovered);
    Expect(snapshot&&snapshot->error.empty()&&snapshot->totalFixedSteps==3,"An explicit reset clears a latched error and resumes fixed-step simulation");
    auto settings=f.cloth.settings;settings.substeps=0;
    worker.Submit(f.cloth,f.pose,f.mesh,0,settings);
    worker.Submit(f.cloth,f.pose,f.mesh,.01,f.cloth.settings);
    failed=Finish(worker,recovered);
    Expect(failed&&!failed->error.empty(),"Invalid solver settings stay latched when a subsequent pose replaces pending work");
    auto invalid=f;invalid.cloth.renderBindings[0].vertex=999;
    const auto badAsset=worker.Submit(invalid.cloth,invalid.pose,invalid.mesh,0,invalid.cloth.settings,true);
    failed=Finish(worker,badAsset);
    Expect(failed&&!failed->error.empty(),"Malformed geometry bindings report initialization errors across the worker boundary");
    const auto valid=worker.Submit(f.cloth,f.pose,f.mesh,0,f.cloth.settings,true);
    snapshot=Finish(worker,valid);
    Expect(snapshot&&snapshot->error.empty(),"The same worker recovers after malformed asset initialization");
}
void Shutdown(){
    std::puts("Idle and active destruction:");const auto started=std::chrono::steady_clock::now();
    for(int i=0;i<8;++i){ClothPreviewWorker worker;Expect(!worker.Latest()&&worker.Generation()==0,"An idle worker begins without a published snapshot");}
    auto f=Sheet();
    {
        ClothPreviewWorker worker;
        const auto generation=worker.Submit(f.cloth,f.pose,f.mesh,0,f.cloth.settings,true);
        Finish(worker,generation);
        worker.Submit(f.cloth,f.pose,f.mesh,3600,f.cloth.settings);
        std::this_thread::sleep_for(2ms);
    }
    {
        ClothPreviewWorker worker;
        const auto generation=worker.Submit(f.cloth,f.pose,f.mesh,0,f.cloth.settings,true);
        Finish(worker,generation);worker.Submit(f.cloth,f.pose,f.mesh,3600,f.cloth.settings);worker.SetPaused(true);
    }
    Expect(std::chrono::steady_clock::now()-started<5s,"Destruction wakes idle threads and joins active work without draining a long pending backlog");
}
} // namespace

int main(){
    ElapsedAndOwnership();PauseAndReset();FocusSuspension();SingleTickPublications();MovingInputIntervals();SkinnedRotationIntervals();HierarchicalSkinnedIntervals();FractionalInputClock();FractionalInitialization();ErrorRecovery();Shutdown();
    std::printf("Cloth preview worker: %s (%d failures)\n",failures?"FAILED":"PASSED",failures);
    return failures?1:0;
}
