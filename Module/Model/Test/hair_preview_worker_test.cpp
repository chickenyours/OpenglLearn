#include "Model/Private/cloth_preview_worker.h"
#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <set>
#include <stdexcept>
#include <thread>

namespace {
using namespace Model;using namespace Model::detail;using namespace std::chrono_literals;namespace fs=std::filesystem;
int failures=0;
bool Expect(bool yes,const char* text){std::printf("  %-96s %s\n",text,yes?"ok":"FAIL");failures+=!yes;return yes;}
struct Hash {
    std::uint64_t value=14695981039346656037ull;
    void Scalar(double x){auto bits=std::bit_cast<std::uint64_t>(x);for(int i=0;i<8;++i){value^=(bits>>(i*8))&255;value*=1099511628211ull;}}
    void Text(const std::string& s){Scalar(double(s.size()));for(unsigned char c:s){value^=c;value*=1099511628211ull;}}
    template<class V>void Vec(const V& v){for(glm::length_t i=0;i<v.length();++i)Scalar(v[i]);}
};
std::uint64_t AnimationHash(const SkeletalAssetPackage& p){Hash h;h.Scalar(double(p.animations.size()));for(const auto& a:p.animations){h.Text(a.id);h.Text(a.name);h.Text(a.skeletonId);h.Text(a.source);h.Scalar(a.duration);h.Scalar(a.framesPerSecond);h.Scalar(a.generated);h.Scalar(a.baked);h.Text(a.sourceSkeletonId);
    for(const auto& t:a.boneTracks){h.Scalar(t.bone);for(const auto& k:t.translations){h.Scalar(k.time);h.Vec(k.value);}for(const auto& k:t.rotations){h.Scalar(k.time);h.Scalar(k.value.w);h.Scalar(k.value.x);h.Scalar(k.value.y);h.Scalar(k.value.z);}for(const auto& k:t.scales){h.Scalar(k.time);h.Vec(k.value);}}
    for(const auto& t:a.morphTracks){h.Scalar(t.morph);for(const auto& k:t.keys){h.Scalar(k.time);h.Scalar(k.value);}}}return h.value;}
std::shared_ptr<const ClothPreviewSnapshot> Finish(ClothPreviewWorker& w,std::uint64_t generation){const auto serial=w.SubmissionSerial();if(!Expect(w.Wait(generation,serial,30s),"The latest hair and cloth submission completes"))return {};const auto s=w.Latest();if(!Expect(s&&s->generation==generation&&s->serial>=serial,"Publication belongs to the latest complete generation and serial"))return {};if(!Expect(s->error.empty(),"Coupled worker completes without simulation or binding errors")){std::printf("    %s\n",s->error.c_str());return {};}return s;}
glm::dvec3 MeshPoint(const StaticModel& mesh,std::uint32_t m,std::uint32_t v){const auto p=mesh.meshes[m].vertices[v].position;for(const auto& i:mesh.instances)if(i.meshIndex==m)return glm::dvec3(i.transform*glm::dvec4(p,1));return p;}
void Coherent(const ClothPreviewSnapshot& s,const SkeletalPose& animated,const ModularCharacterBundle& b,const HairAsset& hair){
    std::vector<bool> simulated(b.body->skeleton.bones.size(),false);for(const auto& c:hair.chains)for(const auto& l:c.links)simulated[l.bone]=true;
    bool exact=s.pose.global.size()==animated.global.size()&&s.pose.local.size()==animated.local.size();
    for(std::size_t i=0;exact&&i<simulated.size();++i){const auto& a=animated.local[i];const auto& p=s.pose.local[i];exact&=p.translation==a.translation&&p.scale==a.scale;if(!simulated[i])exact&=p.rotation==a.rotation&&s.pose.global[i]==animated.global[i];}
    Expect(exact&&s.pose.morphWeights==animated.morphWeights,"Unrelated bones, all local translation/scale and morph curves match the exact animated endpoint");
    StaticModel target;std::string error;if(!Expect(SkinSkeletalMesh(*b.garment,animated,target,&error),"Independent animated garment skinning succeeds"))return;double maxPin=0;std::size_t pins=0;
    for(const auto& binding:b.cloth.renderBindings)if(b.cloth.particles[binding.particle].inverseMass==0){const auto expected=MeshPoint(target,binding.mesh,binding.vertex);maxPin=std::max(maxPin,glm::length(s.positions[binding.particle]-expected)/b.cloth.unitsPerMeter);maxPin=std::max(maxPin,glm::length(MeshPoint(s.garment,binding.mesh,binding.vertex)-expected)/b.cloth.unitsPerMeter);++pins;}
    std::printf("    source-aligned garment pins %zu, max difference %.9g m\n",pins,maxPin);Expect(pins>0&&maxPin<1e-8,"Rendered and simulated waist pins share the published body animation pose");
    std::size_t offset=0;double maxRoot=0;bool nodeCount=true;for(const auto& c:hair.chains){if(offset>=s.hairPositions.size()){nodeCount=false;break;}maxRoot=std::max(maxRoot,glm::length(s.hairPositions[offset]-glm::dvec3(animated.global[c.links.front().bone][3]))/hair.unitsPerMeter);offset+=c.links.size()+1;}
    Expect(s.hairEnabled&&nodeCount&&offset==s.hairPositions.size()&&maxRoot<1e-8&&s.hairStats.maxRootError<1e-8,"Every physical hair root stays exactly on its animated head attachment");
    Expect(s.hairStats.steps==s.totalFixedSteps&&std::abs(s.simulatedSeconds-double(s.totalFixedSteps)*b.cloth.settings.fixedTimeStep)<1e-10,"Hair advances exactly once per completed cloth tick on the same fixed clock");
    StaticModel skinned;bool surfaceOk=SkinSkeletalMesh(*b.body,s.pose,skinned,&error);const auto baseline=skinned.meshes;
    if(surfaceOk)surfaceOk=ApplyHairSurfaceOffsets(*b.body,s.pose,s.hairSurfaceOffsets,skinned,&error);
    std::set<std::uint64_t> freePoints;for(const auto& p:s.hairSurfaceOffsets)freePoints.insert((std::uint64_t(p.mesh)<<32)|p.vertex);
    const std::set<std::uint32_t> hairMeshes(hair.hairMeshes.begin(),hair.hairMeshes.end());
    for(std::size_t m=0;surfaceOk&&m<skinned.meshes.size();++m)for(std::size_t v=0;surfaceOk&&v<skinned.meshes[m].vertices.size();++v)
        if(!freePoints.contains((std::uint64_t(m)<<32)|v))surfaceOk=skinned.meshes[m].vertices[v].position==baseline[m].vertices[v].position;
    for(const auto& p:s.hairSurfaceOffsets)surfaceOk=surfaceOk&&hairMeshes.contains(p.mesh)&&p.anchorBone<s.pose.global.size();
    for(const auto& a:hair.attachments)surfaceOk=surfaceOk&&!freePoints.contains((std::uint64_t(a.mesh)<<32)|a.vertex);
    Expect(surfaceOk&&s.hairStats.surfaceVertices>0&&s.hairSurfaceOffsets.size()<=s.hairStats.surfaceVertices,"Completed snapshot carries matching mesh corrections without moving scalp attachments or unrelated vertices");
}

void HairOnlyCoherent(const ClothPreviewSnapshot& s,const SkeletalPose& animated,const ModularCharacterBundle& b,const HairAsset& hair){
    std::vector<bool> simulated(b.body->skeleton.bones.size(),false);for(const auto& chain:hair.chains)for(const auto& link:chain.links)simulated[link.bone]=true;
    bool exact=s.pose.global.size()==animated.global.size()&&s.pose.local.size()==animated.local.size();
    for(std::size_t bone=0;exact&&bone<simulated.size();++bone){const auto& source=animated.local[bone];const auto& current=s.pose.local[bone];
        exact&=current.translation==source.translation&&current.scale==source.scale;
        if(!simulated[bone])exact&=current.rotation==source.rotation&&s.pose.global[bone]==animated.global[bone];}
    Expect(exact&&s.pose.morphWeights==animated.morphWeights,"Hair-only publication preserves the exact completed animated head, unrelated bones and morphs");
    StaticModel expected;std::string error;if(!Expect(SkinSkeletalMesh(*b.garment,animated,expected,&error),"Hair-only independent animated garment skinning succeeds"))return;
    bool garmentExact=s.garment.meshes.size()==expected.meshes.size()&&s.garment.instances.size()==expected.instances.size();
    for(std::size_t mesh=0;garmentExact&&mesh<expected.meshes.size();++mesh){const auto& actual=s.garment.meshes[mesh];const auto& target=expected.meshes[mesh];
        garmentExact&=actual.vertices.size()==target.vertices.size()&&actual.indices==target.indices;
        for(std::size_t vertex=0;garmentExact&&vertex<target.vertices.size();++vertex){const auto& a=actual.vertices[vertex];const auto& t=target.vertices[vertex];
            garmentExact&=a.position==t.position&&a.normal==t.normal&&a.tangent==t.tangent;}}
    for(std::size_t i=0;garmentExact&&i<expected.instances.size();++i)garmentExact&=s.garment.instances[i].meshIndex==expected.instances[i].meshIndex
        &&s.garment.instances[i].transform==expected.instances[i].transform;
    Expect(garmentExact,"Hair-only garment is the exact ordinary animated skin for the completed tick, without cloth particle dependence");
    std::size_t offset=0;bool roots=true;double maximumRoot=0;
    for(const auto& chain:hair.chains){if(offset>=s.hairPositions.size()){roots=false;break;}
        maximumRoot=std::max(maximumRoot,glm::length(s.hairPositions[offset]-glm::dvec3(animated.global[chain.links.front().bone][3]))/hair.unitsPerMeter);
        offset+=chain.links.size()+1;}
    Expect(!s.clothEnabled&&s.hairEnabled&&roots&&offset==s.hairPositions.size()&&maximumRoot<1e-8&&s.hairStats.maxRootError<1e-8,
        "Hair-only physical roots and displayed bones share the completed animated head attachment");
    Expect(s.stats.steps==0&&s.stats.resets==0&&s.cumulativeIterations==0&&s.hairStats.steps==s.totalFixedSteps
        &&std::abs(s.simulatedSeconds-double(s.totalFixedSteps)*b.cloth.settings.fixedTimeStep)<1e-10,
        "Hair-only fixed clock advances hair exactly once per tick without resetting or stepping the cloth solver");
    StaticModel corrected;bool valid=SkinSkeletalMesh(*b.body,s.pose,corrected,&error)
        &&ApplyHairSurfaceOffsets(*b.body,s.pose,s.hairSurfaceOffsets,corrected,&error);
    const std::set<std::uint32_t> meshes(hair.hairMeshes.begin(),hair.hairMeshes.end());std::set<std::uint64_t> records;
    for(const auto& record:s.hairSurfaceOffsets){valid&=meshes.contains(record.mesh)&&record.anchorBone<s.pose.global.size()
        &&std::isfinite(record.displacement.x)&&std::isfinite(record.displacement.y)&&std::isfinite(record.displacement.z);
        records.insert((std::uint64_t(record.mesh)<<32)|record.vertex);}
    for(const auto& attachment:hair.attachments)valid&=!records.contains((std::uint64_t(attachment.mesh)<<32)|attachment.vertex);
    Expect(valid&&s.hairStats.surfaceVertices>0&&records.size()==s.hairSurfaceOffsets.size(),
        "Hair-only mesh offsets are valid for the same published pose and leave authored scalp attachments untouched");
}

bool HairOnlyWorkflow(ClothPreviewWorker& worker,const ModularCharacterBundle& b,const std::shared_ptr<const HairAsset>& hair,const AnimationSequenceAsset& clip){
    std::puts("Real VMD hair-only worker, fractional ticks, pause and cloth mode restoration:");std::string error;const double fixed=b.cloth.settings.fixedTimeStep;
    auto pose=EvaluateSkeletalPose(*b.body,&clip,0,false);StaticModel garment;if(!SkinSkeletalMesh(*b.garment,pose,garment,&error))throw std::runtime_error(error);
    const auto submit=[&](const SkeletalPose& p,const StaticModel& mesh,double delta,bool cloth){return worker.Submit(b.cloth,p,mesh,delta,b.cloth.settings,false,b.garment,hair,b.body,hair->settings,cloth);};
    const auto oldGeneration=worker.Generation(),generation=submit(pose,garment,0,false);auto previous=Finish(worker,generation);if(!previous)return false;
    Expect(generation>oldGeneration&&!previous->clothEnabled&&previous->totalFixedSteps==0&&previous->hairStats.resets==1,
        "Switching from cloth to hair-only creates a fresh configuration generation without an explicit reset");
    HairOnlyCoherent(*previous,pose,b,*hair);
    for(std::uint64_t tick=1;tick<=12;++tick){const double partialTime=(double(tick)-.6)*fixed;
        auto partial=EvaluateSkeletalPose(*b.body,&clip,partialTime,false);StaticModel partialGarment;
        if(!SkinSkeletalMesh(*b.garment,partial,partialGarment,&error))throw std::runtime_error(error);
        Expect(submit(partial,partialGarment,.4*fixed,false)==generation,"Hair-only fractional VMD input retains the active generation");
        const auto fraction=Finish(worker,generation);if(!fraction)return false;
        Expect(fraction->totalFixedSteps==tick-1&&fraction->hairStats.steps==tick-1&&fraction->pose.global==previous->pose.global
            &&fraction->hairPositions==previous->hairPositions&&fraction->hairSurfaceOffsets==previous->hairSurfaceOffsets,
            "A sub-tick publication retains the last complete physical pose, roots and mesh corrections");
        HairOnlyCoherent(*fraction,pose,b,*hair);
        pose=EvaluateSkeletalPose(*b.body,&clip,double(tick)*fixed,false);if(!SkinSkeletalMesh(*b.garment,pose,garment,&error))throw std::runtime_error(error);
        Expect(submit(pose,garment,.6*fixed,false)==generation,"Completing a fragmented hair-only tick retains the active generation");
        previous=Finish(worker,generation);if(!previous)return false;
        Expect(previous->totalFixedSteps==tick&&previous->hairStats.steps==tick&&previous->hairStats.resets==1
            &&std::abs(previous->submittedSeconds-double(tick)*fixed)<1e-10,"Two fractional submissions consume one hair tick and preserve all input time");
        HairOnlyCoherent(*previous,pose,b,*hair);
    }
    worker.SetPaused(true);const auto heldSerial=worker.SubmissionSerial();submit(pose,garment,3,false);
    Expect(worker.Generation()==generation&&worker.SubmissionSerial()==heldSerial&&worker.Latest()==previous,
        "Paused hair-only input creates no serial, generation or physical backlog");
    worker.SetPaused(false);submit(pose,garment,3,false);const auto resumed=Finish(worker,generation);if(!resumed)return false;
    Expect(resumed->totalFixedSteps==12&&resumed->hairStats.steps==12&&resumed->hairStats.resets==1
        &&resumed->pose.global==previous->pose.global&&resumed->hairPositions==previous->hairPositions&&resumed->hairSurfaceOffsets==previous->hairSurfaceOffsets,
        "Resuming hair-only preserves physical and mesh state without consuming elapsed paused time");
    HairOnlyCoherent(*resumed,pose,b,*hair);
    const auto restoredGeneration=submit(pose,garment,0,true);const auto restored=Finish(worker,restoredGeneration);if(!restored)return false;
    Expect(restoredGeneration>generation&&restored->clothEnabled&&restored->stats.resets==1&&restored->stats.steps==0
        &&restored->totalFixedSteps==0&&restored->hairStats.steps==0&&restored->hairStats.resets==1,
        "Switching hair-only back to cloth starts one clean coupled generation without inherited cloth state");
    Coherent(*restored,pose,b,*hair);
    submit(pose,garment,fixed,true);const auto advanced=Finish(worker,restoredGeneration);if(!advanced)return false;
    Expect(advanced->clothEnabled&&advanced->stats.steps==1&&advanced->totalFixedSteps==1&&advanced->hairStats.steps==1,
        "Restored cloth and hair consume the same fresh fixed tick");
    Coherent(*advanced,pose,b,*hair);return true;
}

bool CoalescedHierarchyWorkflow(ClothPreviewWorker& worker,const ModularCharacterBundle& b,const std::shared_ptr<const HairAsset>& hair,const AnimationSequenceAsset& clip){
    std::puts("Real coalesced parent rotations retain a hierarchical intermediate body, hair roots and garment pins:");
    const auto begin=EvaluateSkeletalPose(*b.body,&clip,0,false);auto end=begin;const auto anchor=hair->chains.front().anchorBone;
    end.local[0].rotation=glm::angleAxis(1.8,glm::dvec3(0,1,0))*begin.local[0].rotation;
    if(anchor>=0)end.local[anchor].rotation=glm::angleAxis(.5,glm::dvec3(0,0,1))*begin.local[anchor].rotation;
    end.local[0].translation.x+=.01*hair->unitsPerMeter;if(!end.morphWeights.empty())end.morphWeights[0]=.7f;
    end.global=GlobalBoneTransforms(b.body->skeleton,end.local);StaticModel beginMesh,endMesh;std::string error;
    if(!SkinSkeletalMesh(*b.garment,begin,beginMesh,&error)||!SkinSkeletalMesh(*b.garment,end,endMesh,&error))throw std::runtime_error(error);
    const auto generation=worker.Submit(b.cloth,begin,beginMesh,0,b.cloth.settings,true,b.garment,hair,b.body,hair->settings,true);
    const auto initial=Finish(worker,generation);if(!initial)return false;
    const double fixed=b.cloth.settings.fixedTimeStep,duration=24.5*fixed;
    worker.Submit(b.cloth,end,endMesh,duration,b.cloth.settings,false,b.garment,hair,b.body,hair->settings,true);
    const auto serial=worker.SubmissionSerial();std::shared_ptr<const ClothPreviewSnapshot> partial;
    const auto deadline=std::chrono::steady_clock::now()+30s;
    while(std::chrono::steady_clock::now()<deadline){const auto current=worker.Latest();
        if(current&&current->generation==generation&&current->totalFixedSteps>0&&current->totalFixedSteps<24){partial=current;break;}
        if(worker.Wait(generation,serial,1ms))break;
    }
    const auto final=Finish(worker,generation);if(!final)return false;
    // A half-tick remainder forces even the completed submission's published
    // pose to be an intermediate fraction, so this cannot accidentally test
    // only the caller's exact endpoint when a fast worker skips observations.
    Expect(final->totalFixedSteps==24&&final->simulatedSeconds<duration&&final->pose.global!=end.global,
        "A long 24.5-tick coalesced interval publishes a verified non-endpoint pose at its final complete tick");
    double maximumHierarchyError=0;bool consistent=true;
    const auto inspect=[&](const ClothPreviewSnapshot& snapshot){const double fraction=snapshot.simulatedSeconds/duration;auto expected=end;
        for(std::size_t bone=0;bone<expected.local.size();++bone){const auto& a=begin.local[bone];const auto& z=end.local[bone];auto& p=expected.local[bone];
            p.translation=a.translation+(z.translation-a.translation)*fraction;p.scale=a.scale+(z.scale-a.scale)*fraction;
            if(a.rotation==z.rotation)p.rotation=z.rotation;else {auto qa=a.rotation,qb=z.rotation;qa/=std::sqrt(glm::dot(qa,qa));qb/=std::sqrt(glm::dot(qb,qb));
                if(glm::dot(qa,qb)<0)qb=-qb;p.rotation=glm::normalize(glm::slerp(qa,qb,fraction));}}
        for(std::size_t m=0;m<expected.morphWeights.size();++m)expected.morphWeights[m]=float(begin.morphWeights[m]+double(end.morphWeights[m]-begin.morphWeights[m])*fraction);
        expected.global=GlobalBoneTransforms(b.body->skeleton,expected.local);const auto composed=GlobalBoneTransforms(b.body->skeleton,snapshot.pose.local);
        consistent&=fraction>0&&fraction<1&&composed.size()==snapshot.pose.global.size();
        for(std::size_t bone=0;bone<composed.size();++bone)for(int c=0;c<4;++c)for(int r=0;r<4;++r)
            maximumHierarchyError=std::max(maximumHierarchyError,std::abs(composed[bone][c][r]-snapshot.pose.global[bone][c][r]));
        Coherent(snapshot,expected,b,*hair);
    };
    if(partial)inspect(*partial);inspect(*final);
    std::printf("    Actual intermediate hierarchy discrepancy %.12g source units; additional partial publication %s\n",maximumHierarchyError,partial?"observed":"not sampled");
    Expect(consistent&&maximumHierarchyError<1e-9,
        "Every published intermediate bone global matches its local hierarchy, including simulated hair and untouched body bones");
    worker.Submit(b.cloth,end,endMesh,.5*fixed,b.cloth.settings,false,b.garment,hair,b.body,hair->settings,true);
    const auto endpoint=Finish(worker,generation);if(!endpoint)return false;
    Coherent(*endpoint,end,b,*hair);
    Expect(endpoint->totalFixedSteps==25&&std::abs(endpoint->simulatedSeconds-25*fixed)<1e-10,
        "Completing the remaining half tick reaches the exact caller endpoint without losing input time");
    return true;
}

bool MixedRateWorkflow(ClothPreviewWorker& worker,const ModularCharacterBundle& b,const std::shared_ptr<const HairAsset>& hair,const AnimationSequenceAsset& clip){
    std::puts("Real VMD 30 Hz hair asset on the shared 60 Hz cloth clock and its original hair-only clock:");
    auto cloned=*hair;cloned.settings.fixedTimeStep=1./30.;cloned.settings.substeps=4;
    const auto preview=std::make_shared<const HairAsset>(std::move(cloned));
    const auto settingsHash=[](const HairSolverSettings& settings){Hash hash;hash.Scalar(settings.fixedTimeStep);hash.Scalar(settings.substeps);hash.Scalar(settings.iterations);
        hash.Scalar(settings.thickness);hash.Scalar(settings.friction);hash.Scalar(settings.inertia);hash.Vec(settings.gravity);hash.Vec(settings.wind);hash.Scalar(settings.collision);return hash.value;};
    const auto originalSettings=settingsHash(hair->settings),previewSettings=settingsHash(preview->settings);
    const auto originalAnimation=AnimationHash(*b.body),originalGarmentAnimation=AnimationHash(*b.garment);
    const auto originalBinding=HairBindingHash(*b.body),originalGarmentBinding=ClothTopologyHash(*b.garment),originalHairBinding=preview->bindingHash;
    std::string error;auto pose=EvaluateSkeletalPose(*b.body,&clip,0,false);StaticModel garment;
    if(!SkinSkeletalMesh(*b.garment,pose,garment,&error))throw std::runtime_error(error);
    const auto submit=[&](double delta,bool cloth){return worker.Submit(b.cloth,pose,garment,delta,b.cloth.settings,false,b.garment,preview,b.body,preview->settings,cloth);};
    const auto generation=submit(0,true);const auto initial=Finish(worker,generation);if(!initial)return false;
    Expect(initial->clothEnabled&&initial->hairEnabled&&initial->totalFixedSteps==0,"The cloned 30 Hz hair asset initializes on a fresh coupled generation");
    pose=EvaluateSkeletalPose(*b.body,&clip,.1,false);if(!SkinSkeletalMesh(*b.garment,pose,garment,&error))throw std::runtime_error(error);
    Expect(submit(.1,true)==generation,"A mixed-rate VMD interval retains the shared configuration generation");
    const auto coupled=Finish(worker,generation);if(!coupled)return false;
    Expect(coupled->clothEnabled&&coupled->stats.steps==6&&coupled->totalFixedSteps==6&&coupled->hairStats.steps==6
        &&std::abs(coupled->simulatedSeconds-.1)<1e-10,
        "The shared 60 Hz clock performs six real hair steps for six cloth ticks despite the source hair asset being 30 Hz");
    Coherent(*coupled,pose,b,*preview);
    const auto hairGeneration=submit(0,false);const auto reset=Finish(worker,hairGeneration);if(!reset)return false;
    Expect(hairGeneration>generation&&!reset->clothEnabled&&reset->totalFixedSteps==0&&reset->stats.steps==0,
        "Switching the same 30 Hz source back to hair-only starts a clean independent clock");
    HairOnlyCoherent(*reset,pose,b,*preview);
    pose=EvaluateSkeletalPose(*b.body,&clip,.1+1./30.,false);if(!SkinSkeletalMesh(*b.garment,pose,garment,&error))throw std::runtime_error(error);
    Expect(submit(1./30.,false)==hairGeneration,"The hair-only interval keeps its new generation");
    const auto alone=Finish(worker,hairGeneration);if(!alone)return false;
    Expect(!alone->clothEnabled&&alone->stats.steps==0&&alone->stats.resets==0&&alone->cumulativeIterations==0
        &&alone->totalFixedSteps==1&&alone->hairStats.steps==1&&std::abs(alone->simulatedSeconds-1./30.)<1e-10,
        "Hair-only mode restores one original 30 Hz hair tick without resetting or advancing cloth");
    std::vector<bool> simulated(b.body->skeleton.bones.size(),false);for(const auto& chain:preview->chains)for(const auto& link:chain.links)simulated[link.bone]=true;
    bool exact=alone->pose.global.size()==pose.global.size()&&alone->pose.local.size()==pose.local.size();
    for(std::size_t bone=0;exact&&bone<simulated.size();++bone){exact&=alone->pose.local[bone].translation==pose.local[bone].translation&&alone->pose.local[bone].scale==pose.local[bone].scale;
        if(!simulated[bone])exact&=alone->pose.local[bone].rotation==pose.local[bone].rotation&&alone->pose.global[bone]==pose.global[bone];}
    exact&=alone->pose.morphWeights==pose.morphWeights;std::size_t offset=0;bool roots=true;
    for(const auto& chain:preview->chains){if(offset>=alone->hairPositions.size()){roots=false;break;}
        roots&=glm::length(alone->hairPositions[offset]-glm::dvec3(pose.global[chain.links.front().bone][3]))/preview->unitsPerMeter<1e-8;offset+=chain.links.size()+1;}
    StaticModel corrected;const bool valid=SkinSkeletalMesh(*b.body,alone->pose,corrected,&error)
        &&ApplyHairSurfaceOffsets(*b.body,alone->pose,alone->hairSurfaceOffsets,corrected,&error);
    Expect(exact&&roots&&offset==alone->hairPositions.size()&&alone->hairStats.maxRootError<1e-8&&valid,
        "The restored 30 Hz tick publishes matching animated head roots, unrelated pose, morphs and valid surface corrections");
    Expect(preview->settings.fixedTimeStep==1./30.&&preview->settings.substeps==4&&settingsHash(preview->settings)==previewSettings
        &&settingsHash(hair->settings)==originalSettings&&preview->bindingHash==originalHairBinding
        &&AnimationHash(*b.body)==originalAnimation&&AnimationHash(*b.garment)==originalGarmentAnimation
        &&HairBindingHash(*b.body)==originalBinding&&ClothTopologyHash(*b.garment)==originalGarmentBinding,
        "Runtime rate alignment preserves the original 30 Hz/four-substep settings, source animations and all immutable geometry bindings");
    return true;
}
int Run(const std::vector<fs::path>& args){
    const auto root=fs::path(MODEL_SOURCE_DIR);fs::path character=root/"Asset/Generated/MahiroCloth/Character.asset.json",hairPath;
    for(std::size_t i=1;i<args.size();++i){const auto key=args[i].string();if(key!="--character"&&key!="--hair")throw std::runtime_error("Unknown option: "+key);if(++i==args.size())throw std::runtime_error("Missing option value");if(key=="--character")character=args[i];else hairPath=args[i];}
    if(hairPath.empty())hairPath=(fs::is_directory(character)?character:character.parent_path())/"Hair/Hair.hair.json";
    std::string error;const auto bundle=LoadModularCharacter(character,&error);if(!bundle)throw std::runtime_error("Required real character load failed: "+error);const auto loadedHair=LoadHairAsset(hairPath,*bundle->body,&error);if(!loadedHair)throw std::runtime_error("Required generated hair load failed: "+error);const auto hair=std::make_shared<const HairAsset>(*loadedHair);const auto& b=*bundle;
    const AnimationSequenceAsset* clip=nullptr;for(const auto& a:b.body->animations)if(!a.generated&&a.baked&&!a.boneTracks.empty()){clip=&a;break;}if(!clip)throw std::runtime_error("Canonical character has no actual baked VMD clip.");
    const auto originalAnimation=AnimationHash(*b.body),originalGarmentAnimation=AnimationHash(*b.garment);const auto originalBodyBinding=HairBindingHash(*b.body),originalGarmentBinding=ClothTopologyHash(*b.garment);
    std::puts("Real VMD, generated hair and canonical skirt worker coupling:");ClothPreviewWorker worker;StaticModel garment;auto pose=EvaluateSkeletalPose(*b.body,clip,0,false);if(!SkinSkeletalMesh(*b.garment,pose,garment,&error))throw std::runtime_error(error);
    const auto submit=[&](const SkeletalPose& p,const StaticModel& mesh,double delta,bool reset,bool enabled){return worker.Submit(b.cloth,p,mesh,delta,b.cloth.settings,reset,b.garment,enabled?hair:std::shared_ptr<const HairAsset>{},b.body,hair->settings);};
    const auto generation=submit(pose,garment,0,true,true);auto initial=Finish(worker,generation);if(!initial)return 1;Expect(initial->hairEnabled&&initial->totalFixedSteps==0&&initial->hairStats.steps==0&&initial->hairStats.resets==1,"Zero-delta initialization warms contact without inventing hair ticks");Coherent(*initial,pose,b,*hair);
    for(double time:{.1,.2}){pose=EvaluateSkeletalPose(*b.body,clip,time,false);if(!SkinSkeletalMesh(*b.garment,pose,garment,&error))throw std::runtime_error(error);Expect(submit(pose,garment,.1,false,true)==generation,"Continuous animation keeps one immutable hair and cloth generation");const auto s=Finish(worker,generation);if(!s)return 1;Expect(s->totalFixedSteps==static_cast<std::uint64_t>(std::llround(time*60))&&s->hairStats.resets==1,"Each real 0.1-second VMD interval consumes six ticks without implicit resets");Coherent(*s,pose,b,*hair);}
    const auto beforePause=worker.Latest();StaticModel solvedBody,animatedBody;if(!SkinSkeletalMesh(*b.body,beforePause->pose,solvedBody,&error)||!ApplyHairSurfaceOffsets(*b.body,beforePause->pose,beforePause->hairSurfaceOffsets,solvedBody,&error)||!SkinSkeletalMesh(*b.body,pose,animatedBody,&error))throw std::runtime_error(error);double geometryMotion=0;for(auto m:hair->hairMeshes)for(std::uint32_t v=0;v<solvedBody.meshes[m].vertices.size();++v)geometryMotion=std::max(geometryMotion,glm::length(MeshPoint(solvedBody,m,v)-MeshPoint(animatedBody,m,v))/hair->unitsPerMeter);std::printf("    Rendered hair movement from animated skin %.6f mm\n",geometryMotion*1000);Expect(geometryMotion>1e-6,"Skinning the published pose and mesh corrections moves actual rendered hair rather than only debug nodes");
    worker.SetPaused(true);const auto heldSerial=worker.SubmissionSerial();submit(pose,garment,5,true,true);Expect(worker.Generation()==generation&&worker.SubmissionSerial()==heldSerial,"Paused input does not create a reset, job or physical backlog");std::this_thread::sleep_for(20ms);Expect(worker.Latest()==beforePause,"Paused preview retains the exact completed hair and garment snapshot");worker.SetPaused(false);submit(pose,garment,5,false,true);const auto resumed=Finish(worker,generation);if(!resumed)return 1;
    Expect(resumed->hairPositions==beforePause->hairPositions&&resumed->hairSurfaceOffsets==beforePause->hairSurfaceOffsets&&resumed->pose.global==beforePause->pose.global&&resumed->hairStats.steps==beforePause->hairStats.steps&&resumed->hairStats.resets==beforePause->hairStats.resets&&resumed->stats.resets==beforePause->stats.resets,"First resume preserves physical state, mesh corrections and roots without spending paused time or resetting");
    submit(pose,garment,b.cloth.settings.fixedTimeStep,false,true);auto advanced=Finish(worker,generation);if(!advanced)return 1;Expect(advanced->totalFixedSteps==13&&advanced->hairStats.steps==13&&advanced->hairStats.resets==1,"Resumed held animation advances hair and cloth by one fresh tick");Coherent(*advanced,pose,b,*hair);
    // Queue a real future endpoint then immediately disable hair. Any in-flight
    // old job must fail generation checks before it can publish over the reset.
    auto future=EvaluateSkeletalPose(*b.body,clip,.3,false);StaticModel futureGarment;if(!SkinSkeletalMesh(*b.garment,future,futureGarment,&error))throw std::runtime_error(error);submit(future,futureGarment,.1,false,true);const auto cancelledSerial=worker.SubmissionSerial();const auto disabledGeneration=submit(pose,garment,0,false,false);Expect(disabledGeneration>generation&&!worker.Wait(generation,cancelledSerial,1ms),"Disabling hair creates a new generation and rejects all old pending jobs");const auto disabled=Finish(worker,disabledGeneration);if(!disabled)return 1;
    Expect(!disabled->hairEnabled&&disabled->hairPositions.empty()&&disabled->hairSurfaceOffsets.empty()&&disabled->hairStats.steps==0&&disabled->pose.global==pose.global&&disabled->totalFixedSteps==0,"Disabled hair publishes exact animated body pose and clears dynamic hair output");
    const auto enabledGeneration=submit(pose,garment,0,false,true);Expect(enabledGeneration>disabledGeneration,"Re-enabling hair establishes a fresh matching configuration generation");const auto enabled=Finish(worker,enabledGeneration);if(!enabled)return 1;Coherent(*enabled,pose,b,*hair);Expect(enabled->hairStats.resets==1&&enabled->hairStats.steps==0,"Re-enabled hair starts once from the requested pose without inherited old-job time");
    if(!HairOnlyWorkflow(worker,b,hair,*clip))return 1;
    if(!MixedRateWorkflow(worker,b,hair,*clip))return 1;
    if(!CoalescedHierarchyWorkflow(worker,b,hair,*clip))return 1;
    Expect(AnimationHash(*b.body)==originalAnimation&&AnimationHash(*b.garment)==originalGarmentAnimation&&HairBindingHash(*b.body)==originalBodyBinding&&ClothTopologyHash(*b.garment)==originalGarmentBinding,"Source clips, sparse morphs, original skin and geometry remain immutable throughout worker simulation");
    std::printf("Hair preview worker: %s (%d failures)\n",failures?"FAILED":"PASSED",failures);return failures?1:0;
}
}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv){std::vector<fs::path> args;for(int i=0;i<argc;++i)args.emplace_back(argv[i]);try{return Run(args);}catch(const std::exception& e){std::fprintf(stderr,"Hair preview worker test: %s\n",e.what());return 1;}}
#else
int main(int argc,char** argv){std::vector<fs::path> args;for(int i=0;i<argc;++i)args.emplace_back(fs::u8path(argv[i]));try{return Run(args);}catch(const std::exception& e){std::fprintf(stderr,"Hair preview worker test: %s\n",e.what());return 1;}}
#endif
