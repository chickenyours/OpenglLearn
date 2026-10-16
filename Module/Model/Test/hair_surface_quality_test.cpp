#include "Model/Public/hair_asset.h"
#include "Model/Public/vmd_animation.h"
#include "Model/Private/hair_body_collision.h"
#include <json/json.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <numeric>
#include <set>
#include <stdexcept>

namespace {
using namespace Model;
using Clock=std::chrono::steady_clock;
namespace fs=std::filesystem;
constexpr double RawLimit=.005;
struct Options {
    fs::path body="Asset/Generated/MahiroCloth/Body/Package.asset.json";
    fs::path hair="Asset/Generated/MahiroCloth/Hair/Hair.hair.json";
    fs::path animation="Asset/Generated/Animations/MahiroTaitui.anim.json",output;
    std::size_t frames=553,stride=1;
    double startTime=0,holdAt=-1;
};
bool Finite(glm::dvec3 v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
double Elapsed(Clock::time_point start){return std::chrono::duration<double,std::milli>(Clock::now()-start).count();}
std::size_t PositiveInteger(const char* text){std::size_t consumed=0;const auto value=std::stoull(text,&consumed);if(consumed!=std::string(text).size()||value==0||value>1000000)throw std::runtime_error("Expected a positive integer <= 1000000");return static_cast<std::size_t>(value);}
Options Parse(int argc,char** argv){
    Options result;for(int i=1;i<argc;++i){const std::string arg=argv[i];if(arg=="--help"){std::puts("hair_surface_quality_test [--body FILE] [--hair FILE] [--animation FILE] [--frames N] [--stride N] [--start-time SECONDS] [--hold-at SECONDS] [--output FILE]\nAll simulation ticks are evaluated. --stride reduces diagnostic frames only. Defaults: 553 ticks at the asset's fixed step; every real weighted hair vertex is scanned.");std::exit(0);}
        if(i+1>=argc)throw std::runtime_error("Missing argument after "+arg);const char* value=argv[++i];
        if(arg=="--body")result.body=fs::u8path(value);else if(arg=="--hair")result.hair=fs::u8path(value);else if(arg=="--animation")result.animation=fs::u8path(value);else if(arg=="--output")result.output=fs::u8path(value);else if(arg=="--frames")result.frames=PositiveInteger(value);else if(arg=="--stride")result.stride=PositiveInteger(value);else if(arg=="--start-time"||arg=="--hold-at"){std::size_t used=0;const double time=std::stod(value,&used);if(used!=std::string(value).size()||!std::isfinite(time)||time<0)throw std::runtime_error("Expected finite nonnegative seconds");(arg=="--start-time"?result.startTime:result.holdAt)=time;}else throw std::runtime_error("Unknown argument: "+arg);
    }return result;
}
Json::Value Number(double value){return std::isfinite(value)?Json::Value(value):Json::Value();}
Json::Value Vector(glm::dvec3 value){Json::Value result(Json::arrayValue);for(int i=0;i<3;++i)result.append(Number(value[i]));return result;}
std::string PathText(const fs::path& value){const auto utf8=value.generic_u8string();return {reinterpret_cast<const char*>(utf8.data()),utf8.size()};}
double Percentile(std::vector<double> values,double fraction){if(values.empty())return 0;const auto index=std::min(values.size()-1,static_cast<std::size_t>(std::ceil(fraction*values.size()))-1);std::nth_element(values.begin(),values.begin()+index,values.end());return values[index];}
struct Times {
    std::vector<double> values;
    Json::Value ToJson()const {Json::Value result;result["meanMs"]=values.empty()?0:std::accumulate(values.begin(),values.end(),0.)/values.size();result["p95Ms"]=Percentile(values,.95);result["maxMs"]=values.empty()?0:*std::max_element(values.begin(),values.end());return result;}
};
// A bounded-memory summary of millions of scanned vertices. The reported p95
// is a conservative upper bound with at most one micrometer quantization.
struct Depths {
    std::vector<std::uint64_t> bins=std::vector<std::uint64_t>(30002,0);
    std::uint64_t samples=0,negative=0,over2=0,over5=0,over10=0,over30=0;
    double maximum=0;
    void Add(double depth){++samples;depth=std::max(0.,depth);maximum=std::max(maximum,depth);negative+=depth>1e-8;over2+=depth>.002;over5+=depth>.005;over10+=depth>.010;over30+=depth>.030;const auto bin=std::min<std::size_t>(bins.size()-1,static_cast<std::size_t>(std::ceil(depth*1e6)));++bins[bin];}
    double P95Upper()const {if(!samples)return 0;const auto target=static_cast<std::uint64_t>(std::ceil(samples*.95));std::uint64_t count=0;for(std::size_t i=0;i<bins.size();++i){count+=bins[i];if(count>=target)return i+1==bins.size()?maximum:i*1e-6;}return maximum;}
    Json::Value ToJson()const {Json::Value result;result["samples"]=Json::UInt64(samples);result["negativeSamples"]=Json::UInt64(negative);result["maxMm"]=maximum*1000;result["p95MmUpperBound"]=P95Upper()*1000;result["p95QuantizationMm"]=.001;result["over2Mm"]=Json::UInt64(over2);result["over5Mm"]=Json::UInt64(over5);result["over10Mm"]=Json::UInt64(over10);result["over30Mm"]=Json::UInt64(over30);return result;}
};
struct Sample {
    std::uint32_t mesh=0,vertex=0,instance=UINT32_MAX;
    bool rootOnly=false;
    bool attachment=false;
};
struct Worst {
    double depth=0,time=0,sourceDepth=0;
    Sample sample;
    detail::HairBodyNearest body;
    glm::dvec3 position{0};
    void Add(double value,double at,const Sample& s,const detail::HairBodyNearest& near,glm::dvec3 point,double baseline){if(value>depth){depth=value;time=at;sample=s;body=near;position=point;sourceDepth=baseline;}}
    Json::Value ToJson(const SkeletalAssetPackage& target)const {Json::Value result;result["depthMm"]=depth*1000;result["timeSeconds"]=time;result["hairMesh"]=sample.mesh;result["hairMeshName"]=sample.mesh<target.geometry->meshes.size()?target.geometry->meshes[sample.mesh].name:"?";result["hairVertex"]=sample.vertex;result["instance"]=sample.instance;result["bodyTriangle"]=body.triangle;result["bodyMesh"]=body.mesh;result["bodyMeshName"]=body.mesh<target.geometry->meshes.size()?target.geometry->meshes[body.mesh].name:"?";result["bodyEnclosureVerified"]=body.inside;result["bodyBackfacing"]=body.backfacing;result["sourceAnimatedDepthMm"]=sourceDepth*1000;result["positionMeters"]=Vector(position);result["bodyPointMeters"]=Vector(body.point);result["rootOnlyInfluence"]=sample.rootOnly;return result;}
};
glm::dvec3 RenderPoint(const StaticModel& rendered,const Sample& sample,double units){const auto p=rendered.meshes[sample.mesh].vertices[sample.vertex].position;return (sample.instance==UINT32_MAX?p:glm::dvec3(rendered.instances[sample.instance].transform*glm::dvec4(p,1)))/units;}
double Penetration(const detail::HairBodyNearest& near){return near.found&&near.inside?std::max(0.,-near.signedDistance):0;}
bool SamePatch(const detail::HairBodyNearest& actual,const detail::HairBodyNearest& source){return actual.found&&source.found&&(actual.triangle==source.triangle||(actual.mesh==source.mesh&&glm::dot(actual.normal,source.normal)>.9&&glm::length(actual.point-source.point)<.004));}
detail::HairBodyNearest CheckedNearest(const detail::HairBodyCollision& body,glm::dvec3 point,std::uint64_t& beyondRange){auto near=body.Nearest(point,.03);if(!near.found){++beyondRange;near=body.Nearest(point);}return near;}
void PrintDepth(const char* label,const Depths& value){std::printf("%-25s max %8.3f mm  p95 <= %8.3f mm  >2/>5/>10 mm %llu / %llu / %llu  (n=%llu)\n",label,value.maximum*1000,value.P95Upper()*1000,static_cast<unsigned long long>(value.over2),static_cast<unsigned long long>(value.over5),static_cast<unsigned long long>(value.over10),static_cast<unsigned long long>(value.samples));}
}

int main(int argc,char** argv){
    try{
        const auto options=Parse(argc,argv);const auto entireStart=Clock::now();std::string error;
        auto target=LoadSkeletalAssetPackage(options.body,&error);if(!target)throw std::runtime_error("Body: "+error);
        auto hair=LoadHairAsset(options.hair,*target,&error);if(!hair)throw std::runtime_error("Hair: "+error);
        auto animation=LoadAnimationSequenceAsset(options.animation,*target,&error);if(!animation)throw std::runtime_error("Animation: "+error);
        if(!target->geometry||!std::isfinite(hair->unitsPerMeter)||hair->unitsPerMeter<=0)throw std::runtime_error("Invalid body geometry or hair units");
        std::vector<bool> active(target->skeleton.bones.size(),false),distal(active.size(),false);std::size_t boneCount=0;
        for(const auto& chain:hair->chains)for(std::size_t link=0;link<chain.links.size();++link){const auto bone=chain.links[link].bone;if(bone>=active.size())throw std::runtime_error("Hair link outside skeleton");if(!active[bone])++boneCount;active[bone]=true;if(link>0)distal[bone]=true;}
        std::vector<Sample> samples,scalpSamples;std::uint64_t rigidScalpVertices=0;std::set<std::uint32_t> hairMeshIds(hair->hairMeshes.begin(),hair->hairMeshes.end());
        std::set<std::uint64_t> authoredAttachments;for(const auto& a:hair->attachments)authoredAttachments.insert((std::uint64_t(a.mesh)<<32)|a.vertex);
        for(std::uint32_t mesh=0;mesh<target->skins.size();++mesh){const auto& skin=target->skins[mesh];for(std::uint32_t vertex=0;vertex<skin.vertices.size();++vertex){const auto& weights=skin.vertices[vertex];bool effective=false,hasDistal=false;
            for(int k=0;k<4;++k)if(weights.weights[k]>0&&std::isfinite(weights.weights[k])&&weights.joints[k]<active.size()){effective|=active[weights.joints[k]];hasDistal|=distal[weights.joints[k]];}
            if(!effective&&!hairMeshIds.contains(mesh))continue;
            if(!effective)++rigidScalpVertices;auto& destination=effective?samples:scalpSamples;bool instanced=false;
            const bool attachment=authoredAttachments.contains((std::uint64_t(mesh)<<32)|vertex);
            for(std::uint32_t instance=0;instance<target->geometry->instances.size();++instance)if(target->geometry->instances[instance].meshIndex==mesh){destination.push_back({mesh,vertex,instance,!hasDistal,attachment});instanced=true;}
            if(!instanced)destination.push_back({mesh,vertex,UINT32_MAX,!hasDistal,attachment});
        }}
        if(samples.empty())throw std::runtime_error("No actual rendered vertices have positive chain-bone skin weights");
        detail::HairBodyCollision independentBody;if(!independentBody.Initialize(*target,hair->hairMeshes,hair->unitsPerMeter,&error))throw std::runtime_error("Independent body BVH: "+error);
        const double firstTime=options.holdAt>=0?std::min(options.startTime,options.holdAt):options.startTime;
        auto pose=EvaluateSkeletalPose(*target,&*animation,firstTime,false);HairSimulator solver;const auto resetStart=Clock::now();if(!solver.Reset(*hair,*target,pose,&error))throw std::runtime_error("Hair reset: "+error);const double resetMs=Elapsed(resetStart);
        const double dt=hair->settings.fixedTimeStep;if(!std::isfinite(dt)||dt<=0)throw std::runtime_error("Invalid hair fixed timestep");
        Depths actualDepths,sourceDepths,additionalDepths,novelSurfaceDepths,rootSourceDepths,scalpSourceDepths;Worst worst,worstAdditional,worstSource,worstNovel,worstScalp;
        Depths rootRawDepths,freeRawDepths,freeSourceDepths,freeAdditionalDepths;Worst worstFree,worstFreeSource,worstFreeAdditional,worstRoot;
        Depths movableRaw,movableSource,movableAdditional,attachmentRaw,freeFaceRaw;Worst worstMovable;double maximumAttachmentDrift=0,maximumProbePenetration=0;Json::Value worstFreeFace;
        struct FaceSample {std::uint32_t mesh=0,triangle=0,instance=UINT32_MAX;std::array<std::uint32_t,3> vertices{};};
        std::set<std::uint64_t> movableIds;for(const auto& s:samples)if(!s.attachment)movableIds.insert((std::uint64_t(s.mesh)<<32)|s.vertex);
        std::vector<FaceSample> freeFaces;for(const auto mesh:hair->hairMeshes){const auto& indices=target->geometry->meshes[mesh].indices;
            for(std::uint32_t at=0;at+2<indices.size();at+=3){bool free=true;for(int k=0;k<3;++k)free=free&&movableIds.contains((std::uint64_t(mesh)<<32)|indices[at+k]);if(!free)continue;
                bool instanced=false;for(std::uint32_t i=0;i<target->geometry->instances.size();++i)if(target->geometry->instances[i].meshIndex==mesh){freeFaces.push_back({mesh,at/3,i,{indices[at],indices[at+1],indices[at+2]}});instanced=true;}
                if(!instanced)freeFaces.push_back({mesh,at/3,UINT32_MAX,{indices[at],indices[at+1],indices[at+2]}});
            }}
        Times stepTimes,collisionTimes,bodyRefitTimes,scanTimes,surfaceTimes;double maximumRuntimeMeshResidual=0,maximumMeshCorrection=0;std::uint64_t totalSurfaceSweeps=0,totalSurfaceRejects=0;std::uint64_t outsideRange=0,sourceOutsideRange=0,nonfinite=0,sourceOverlapObservations=0,newDynamicObservations=0,rootOverlapObservations=0;
        std::vector<bool> everActual(samples.size(),false),everSource(samples.size(),false),everNew(samples.size(),false);
        Json::Value frames(Json::arrayValue),worstRuntimeProbe;StaticModel rendered,baseline;std::size_t scannedFrames=0;double maximumRootError=0,reportedProbeMaximum=0;
        std::printf("Hair surface quality: %zu chain bones, %zu probes, %zu effective rendered vertex instances, %llu rigid scalp vertices; independent body %zu triangles\n",boneCount,hair->probes.size(),samples.size(),static_cast<unsigned long long>(rigidScalpVertices),independentBody.TriangleCount());
        std::printf("%zu fixed ticks, dt %.9f s, diagnostic stride %zu; movable raw limit %.3f mm, authored scalp attachment drift limit 1.501 mm\n",options.frames,dt,options.stride,RawLimit*1000);
        const auto simulationStart=Clock::now();
        for(std::size_t frame=0;frame<options.frames;++frame){const double requested=options.startTime+frame*dt,time=options.holdAt>=0?std::min(requested,options.holdAt):requested;pose=EvaluateSkeletalPose(*target,&*animation,time,false);SkeletalPose finalPose;
            const auto previousSteps=solver.Stats().steps;const auto stepStart=Clock::now();if(!solver.Step(*hair,*target,pose,frame?dt:0,hair->settings,finalPose,&error))throw std::runtime_error("Hair step "+std::to_string(frame)+": "+error);stepTimes.values.push_back(Elapsed(stepStart));collisionTimes.values.push_back(solver.Stats().collisionMilliseconds);maximumRootError=std::max(maximumRootError,solver.Stats().maxRootError);
            maximumProbePenetration=std::max(maximumProbePenetration,solver.Stats().maxPenetration);
            if(solver.Stats().steps>previousSteps){surfaceTimes.values.push_back(solver.Stats().surfaceMilliseconds);totalSurfaceSweeps+=solver.Stats().surfaceSweeps;totalSurfaceRejects+=solver.Stats().surfaceBroadphaseRejects;}
            maximumRuntimeMeshResidual=std::max(maximumRuntimeMeshResidual,solver.Stats().maxSurfacePenetration);maximumMeshCorrection=std::max(maximumMeshCorrection,solver.Stats().maxMeshCorrection);
            if(frame%options.stride!=0&&frame+1!=options.frames)continue;
            const auto scanStart=Clock::now();if(!independentBody.Update(pose,&error))throw std::runtime_error("Independent body refit: "+error);bodyRefitTimes.values.push_back(independentBody.LastUpdateMilliseconds());
            if(!SkinSkeletalMesh(*target,finalPose,rendered,&error)||!SkinSkeletalMesh(*target,pose,baseline,&error))throw std::runtime_error("Actual rendered mesh skin: "+error);
            if(!ApplyHairSurfaceOffsets(*target,finalPose,solver.SurfaceOffsets(),rendered,&error))throw std::runtime_error("Actual rendered hair mesh contact: "+error);
            if(solver.Stats().worstPenetrationProbe<hair->probes.size()&&solver.Stats().maxPenetration>reportedProbeMaximum){
                reportedProbeMaximum=solver.Stats().maxPenetration;const auto index=solver.Stats().worstPenetrationProbe;const auto& p=hair->probes[index];
                glm::dvec3 point(0),sourcePoint(0);std::uint32_t instance=UINT32_MAX;for(std::uint32_t i=0;i<target->geometry->instances.size();++i)if(target->geometry->instances[i].meshIndex==p.mesh){instance=i;break;}
                for(int k=0;k<3;++k){Sample s{p.mesh,p.triangle[k],instance};point+=RenderPoint(rendered,s,hair->unitsPerMeter)*p.barycentric[k];sourcePoint+=RenderPoint(baseline,s,hair->unitsPerMeter)*p.barycentric[k];}
                const auto near=independentBody.Nearest(point),sourceNear=independentBody.Nearest(sourcePoint);worstRuntimeProbe["index"]=index;worstRuntimeProbe["timeSeconds"]=time;worstRuntimeProbe["chain"]=p.chain;worstRuntimeProbe["mesh"]=p.mesh;worstRuntimeProbe["vertex"]=p.vertex;worstRuntimeProbe["triangleSample"]=p.triangleSample;worstRuntimeProbe["triangle"]=Json::Value(Json::arrayValue);for(auto v:p.triangle)worstRuntimeProbe["triangle"].append(v);
                worstRuntimeProbe["marginResidualMm"]=reportedProbeMaximum*1000;worstRuntimeProbe["rawDepthMm"]=Penetration(near)*1000;worstRuntimeProbe["sourceDepthMm"]=Penetration(sourceNear)*1000;worstRuntimeProbe["positionMeters"]=Vector(point);worstRuntimeProbe["sourcePositionMeters"]=Vector(sourcePoint);worstRuntimeProbe["bodyMesh"]=near.mesh;worstRuntimeProbe["bodyMeshName"]=near.mesh<target->geometry->meshes.size()?target->geometry->meshes[near.mesh].name:"?";worstRuntimeProbe["bodyTriangle"]=near.triangle;worstRuntimeProbe["bodyPointMeters"]=Vector(near.point);
                worstRuntimeProbe["influences"]=Json::Value(Json::arrayValue);for(const auto& i:p.influences){Json::Value v;v["bone"]=i.bone;v["name"]=target->skeleton.bones[i.bone].name;v["weight"]=i.weight;worstRuntimeProbe["influences"].append(v);}
                worstRuntimeProbe["links"]=Json::Value(Json::arrayValue);for(const auto& link:hair->chains[p.chain].links){Json::Value v;v["bone"]=link.bone;v["name"]=target->skeleton.bones[link.bone].name;auto q=glm::normalize(finalPose.local[link.bone].rotation*glm::inverse(glm::normalize(pose.local[link.bone].rotation)));v["angleRadians"]=2*std::acos(std::clamp(std::abs(q.w),0.,1.));v["limitRadians"]=link.maxAngle;worstRuntimeProbe["links"].append(v);}
            }
            std::vector<double> frameDepths;frameDepths.reserve(samples.size());double frameMax=0,frameSourceMax=0,frameAdditionalMax=0;std::uint64_t over2=0,over5=0,over10=0,frameSource=0,frameNew=0;
            double frameMovableMax=0;
            for(std::size_t index=0;index<samples.size();++index){const auto& sample=samples[index];const auto point=RenderPoint(rendered,sample,hair->unitsPerMeter),sourcePoint=RenderPoint(baseline,sample,hair->unitsPerMeter);
                if(!Finite(point)||!Finite(sourcePoint)){++nonfinite;continue;}
                const auto near=CheckedNearest(independentBody,point,outsideRange),sourceNear=CheckedNearest(independentBody,sourcePoint,sourceOutsideRange);
                if((near.found&&!std::isfinite(near.signedDistance))||(sourceNear.found&&!std::isfinite(sourceNear.signedDistance))){++nonfinite;continue;}
                const double depth=Penetration(near),sourceDepth=Penetration(sourceNear);
                // Baseline is diagnostic only. In addition to the strict raw
                // limit below, a different body patch receives no subtraction.
                const bool samePatch=SamePatch(near,sourceNear);const double additional=std::max(0.,depth-(samePatch?sourceDepth:0.));
                const double novel=!samePatch?depth:0.;actualDepths.Add(depth);sourceDepths.Add(sourceDepth);additionalDepths.Add(additional);novelSurfaceDepths.Add(novel);
                if(sample.attachment)attachmentRaw.Add(depth);
                else{movableRaw.Add(depth);movableSource.Add(sourceDepth);movableAdditional.Add(additional);worstMovable.Add(depth,time,sample,near,point,sourceDepth);frameMovableMax=std::max(frameMovableMax,depth);}
                if(sample.rootOnly){rootSourceDepths.Add(sourceDepth);rootRawDepths.Add(depth);worstRoot.Add(depth,time,sample,near,point,sourceDepth);}
                else{freeRawDepths.Add(depth);freeSourceDepths.Add(sourceDepth);freeAdditionalDepths.Add(additional);worstFree.Add(depth,time,sample,near,point,sourceDepth);worstFreeSource.Add(sourceDepth,time,sample,sourceNear,sourcePoint,sourceDepth);worstFreeAdditional.Add(additional,time,sample,near,point,sourceDepth);}
                everActual[index]=everActual[index]||depth>1e-8;everSource[index]=everSource[index]||sourceDepth>1e-8;everNew[index]=everNew[index]||additional>1e-8;
                sourceOverlapObservations+=sourceDepth>1e-8;newDynamicObservations+=additional>1e-8;rootOverlapObservations+=sample.rootOnly&&sourceDepth>1e-8;
                frameDepths.push_back(depth);frameMax=std::max(frameMax,depth);frameSourceMax=std::max(frameSourceMax,sourceDepth);frameAdditionalMax=std::max(frameAdditionalMax,additional);over2+=depth>.002;over5+=depth>.005;over10+=depth>.010;frameSource+=sourceDepth>1e-8;frameNew+=additional>1e-8;
                worst.Add(depth,time,sample,near,point,sourceDepth);worstSource.Add(sourceDepth,time,sample,sourceNear,sourcePoint,sourceDepth);worstAdditional.Add(additional,time,sample,near,point,sourceDepth);worstNovel.Add(novel,time,sample,near,point,sourceDepth);
            }
            for(const auto& attachment:hair->attachments){Sample sample{attachment.mesh,attachment.vertex,UINT32_MAX,true,true};for(std::uint32_t i=0;i<target->geometry->instances.size();++i)if(target->geometry->instances[i].meshIndex==attachment.mesh){sample.instance=i;break;}
                const auto actual=RenderPoint(rendered,sample,hair->unitsPerMeter),source=RenderPoint(baseline,sample,hair->unitsPerMeter);
                const auto anchor=hair->chains[attachment.chain].anchorBone;const auto inverse=glm::inverse(pose.global[anchor]);
                maximumAttachmentDrift=std::max(maximumAttachmentDrift,glm::length(glm::dvec3(inverse*glm::dvec4((actual-source)*hair->unitsPerMeter,0)))/hair->unitsPerMeter);
            }
            for(const auto& face:freeFaces){glm::dvec3 point(0);for(auto vertex:face.vertices)point+=RenderPoint(rendered,{face.mesh,vertex,face.instance},hair->unitsPerMeter)/3.;if(!Finite(point)){++nonfinite;continue;}const auto near=CheckedNearest(independentBody,point,outsideRange);if(near.found&&!std::isfinite(near.signedDistance)){++nonfinite;continue;}const auto depth=Penetration(near);
                if(depth>freeFaceRaw.maximum){worstFreeFace["depthMm"]=depth*1000;worstFreeFace["timeSeconds"]=time;worstFreeFace["hairMesh"]=face.mesh;worstFreeFace["triangle"]=face.triangle;worstFreeFace["bodyMesh"]=near.mesh;worstFreeFace["bodyTriangle"]=near.triangle;worstFreeFace["positionMeters"]=Vector(point);}
                freeFaceRaw.Add(depth);
            }
            // Rigid scalp hair is deliberately outside the simulated effective
            // area. Scan its animated source overlap separately, rather than
            // using it to hide a failure in the driven hair surface above.
            for(const auto& sample:scalpSamples){const auto point=RenderPoint(baseline,sample,hair->unitsPerMeter);if(!Finite(point)){++nonfinite;continue;}const auto near=CheckedNearest(independentBody,point,sourceOutsideRange);if(near.found&&!std::isfinite(near.signedDistance)){++nonfinite;continue;}const double depth=Penetration(near);scalpSourceDepths.Add(depth);worstScalp.Add(depth,time,sample,near,point,depth);}
            const double scanMs=Elapsed(scanStart);scanTimes.values.push_back(scanMs);++scannedFrames;Json::Value record;record["frame"]=Json::UInt64(frame);record["timeSeconds"]=time;record["rawMaxMm"]=frameMax*1000;record["rawP95Mm"]=Percentile(frameDepths,.95)*1000;record["sourceAnimatedMaxMm"]=frameSourceMax*1000;record["additionalMaxMm"]=frameAdditionalMax*1000;record["over2Mm"]=Json::UInt64(over2);record["over5Mm"]=Json::UInt64(over5);record["over10Mm"]=Json::UInt64(over10);record["sourceOverlapVertices"]=Json::UInt64(frameSource);record["newDynamicVertices"]=Json::UInt64(frameNew);record["hairStepMs"]=stepTimes.values.back();record["hairCollisionMs"]=collisionTimes.values.back();record["independentBodyRefitMs"]=independentBody.LastUpdateMilliseconds();record["offlineScanMs"]=scanMs;frames.append(record);
            frames[frames.size()-1]["movableMaxMm"]=frameMovableMax*1000;frames[frames.size()-1]["runtimeProbeMaxMm"]=solver.Stats().maxPenetration*1000;
            if(frame%60==0||frame+1==options.frames){std::printf("frame %3zu t=%5.3f s: raw max/p95 %.3f/%.3f mm; movable %.3f mm, probes %.3f mm; source %.3f mm; step %.3f ms, offline scan %.3f ms\n",frame,time,frameMax*1000,record["rawP95Mm"].asDouble(),frameMovableMax*1000,solver.Stats().maxPenetration*1000,frameSourceMax*1000,stepTimes.values.back(),scanMs);std::fflush(stdout);}
        }
        const double simulationWallMs=Elapsed(simulationStart);const bool allRawWithinLimit=actualDepths.maximum<=RawLimit;
        const bool passed=nonfinite==0&&movableRaw.maximum<=RawLimit&&freeFaceRaw.maximum<=RawLimit&&maximumAttachmentDrift<=.001501&&maximumRootError<1e-7;
        Json::Value report;report["passed"]=passed;report["test"]="all effective rendered hair vertices against independent animated body triangles";report["body"]=PathText(options.body);report["hair"]=PathText(options.hair);report["animation"]=PathText(options.animation);report["ticks"]=Json::UInt64(options.frames);report["diagnosticFrames"]=Json::UInt64(scannedFrames);report["diagnosticStride"]=Json::UInt64(options.stride);report["fixedTimeStepSeconds"]=dt;report["animationDurationSeconds"]=animation->duration;report["chainBones"]=Json::UInt64(boneCount);report["probes"]=Json::UInt64(hair->probes.size());report["effectiveRenderedVertexInstances"]=Json::UInt64(samples.size());report["rigidScalpVerticesNotDrivenByChains"]=Json::UInt64(rigidScalpVertices);report["bodyTriangles"]=Json::UInt64(independentBody.TriangleCount());report["bodySkinVertices"]=Json::UInt64(independentBody.VertexCount());report["rawLimitMm"]=RawLimit*1000;report["newDynamicLimitMm"]=RawLimit*1000;report["baselineExemptsRawLimit"]=false;report["baselinePatchRule"]="same triangle, or same body mesh with normal dot > .9 and closest points < 4 mm; different body patches receive no baseline subtraction";report["nonfiniteVertices"]=Json::UInt64(nonfinite);report["maxRootErrorMeters"]=maximumRootError;
        report["bodyConnectedComponents"]=Json::UInt64(independentBody.ComponentCount());report["bodyOpenComponents"]=Json::UInt64(independentBody.OpenComponentCount());report["insideGeometryMethod"]="two of three non-axis bidirectional ray pairs with outward exits in one edge-connected component; nested components queried independently and deepest enclosing actual boundary selected; raw oriented-nearest backfaces are not solid penetration";report["openGeometryLimitation"]="arbitrary open/nonmanifold surfaces do not define a unique solid; open garments require two independent enclosing intervals, while isolated thin sheets collide only by real swept crossings";
        const auto queryStats=independentBody.QueryStats();report["independentQueries"]["nearest"]=Json::UInt64(queryStats.nearestQueries);report["independentQueries"]["inside"]=Json::UInt64(queryStats.insideQueries);report["independentQueries"]["rays"]=Json::UInt64(queryStats.rayQueries);report["independentQueries"]["rayTriangleTests"]=Json::UInt64(queryStats.rayTriangleTests);report["independentQueries"]["nearestTriangleTests"]=Json::UInt64(queryStats.triangleTests);
        report["raw"]=actualDepths.ToJson();report["sourceAnimatedBaseline"]=sourceDepths.ToJson();report["additionalDynamic"]=additionalDepths.ToJson();report["novelBodySurface"]=novelSurfaceDepths.ToJson();report["sourceRootOnlyInfluence"]=rootSourceDepths.ToJson();report["rigidScalpSourceOverlap"]=scalpSourceDepths.ToJson();report["sourceOverlapObservations"]=Json::UInt64(sourceOverlapObservations);report["newDynamicObservations"]=Json::UInt64(newDynamicObservations);report["sourceRootOverlapObservations"]=Json::UInt64(rootOverlapObservations);report["uniqueVerticesEverRawOverlap"]=Json::UInt64(std::count(everActual.begin(),everActual.end(),true));report["uniqueVerticesEverSourceOverlap"]=Json::UInt64(std::count(everSource.begin(),everSource.end(),true));report["uniqueVerticesEverNewOverlap"]=Json::UInt64(std::count(everNew.begin(),everNew.end(),true));report["worstRaw"]=worst.ToJson(*target);report["worstSource"]=worstSource.ToJson(*target);report["worstAdditional"]=worstAdditional.ToJson(*target);report["worstNovelBodySurface"]=worstNovel.ToJson(*target);report["worstRigidScalpSource"]=worstScalp.ToJson(*target);report["beyond30MmQueryRange"]=Json::UInt64(outsideRange);report["sourceBeyond30MmQueryRange"]=Json::UInt64(sourceOutsideRange);report["queryRangeFallback"]="An unbounded BVH nearest diagnostic prevents penetrations deeper than 30 mm from being silently dropped; no unbounded contact correction is used";
        report["hairStep"]=stepTimes.ToJson();report["hairCollision"]=collisionTimes.ToJson();report["independentBodyRefit"]=bodyRefitTimes.ToJson();report["offlineRenderedSurfaceScan"]=scanTimes.ToJson();report["resetMilliseconds"]=resetMs;report["simulationAndScanWallMilliseconds"]=simulationWallMs;report["totalWallMilliseconds"]=Elapsed(entireStart);report["performanceScope"]="hairStep excludes full rendered CPU skin, independent diagnostic BVH and all-vertex scanning; diagnostic wall time is not preview frame rate";report["frames"]=frames;
        report["startTimeSeconds"]=options.startTime;report["holdAtSeconds"]=options.holdAt;report["rootOnlyRaw"]=rootRawDepths.ToJson();report["freeDistalRaw"]=freeRawDepths.ToJson();report["freeDistalSource"]=freeSourceDepths.ToJson();report["freeDistalAdditional"]=freeAdditionalDepths.ToJson();report["worstRootOnly"]=worstRoot.ToJson(*target);report["worstFreeDistal"]=worstFree.ToJson(*target);report["worstFreeDistalSource"]=worstFreeSource.ToJson(*target);report["worstFreeDistalAdditional"]=worstFreeAdditional.ToJson(*target);report["groupDefinition"]="rootOnly has no positive influence from a second or later bone in any selected chain; freeDistal has such an influence. Groups are diagnostic and do not exempt the all-vertex raw pass limit.";
        report["allVerticesRawWithinLimit"]=allRawWithinLimit;report["movableRaw"]=movableRaw.ToJson();report["movableSource"]=movableSource.ToJson();report["movableAdditional"]=movableAdditional.ToJson();report["worstMovable"]=worstMovable.ToJson(*target);report["authoredAttachmentRaw"]=attachmentRaw.ToJson();report["authoredAttachmentVertices"]=Json::UInt64(hair->attachments.size());report["maximumAttachmentHeadLocalDriftMm"]=maximumAttachmentDrift*1000;report["attachmentDriftLimitMm"]=1.501;report["passRule"]="All movable rendered vertices must remain within 5 mm without source-baseline subtraction. Only explicit validated bind-pose scalp attachments, with bounded distal motion capacity, use their separate 1.501 mm head-local displacement bound. All attachment and scalp raw overlaps remain reported.";report["groupDefinition"]="rootOnly/freeDistal are influence-only diagnostics; movable excludes only validated authored attachment metadata, never all root-only hair.";
        report["meshSurfaceContact"]=surfaceTimes.ToJson();report["maximumRuntimeMeshContactResidualMm"]=maximumRuntimeMeshResidual*1000;report["maximumMeshCorrectionMm"]=maximumMeshCorrection*1000;report["surfaceSweeps"]=Json::UInt64(totalSurfaceSweeps);report["surfaceClusterRejects"]=Json::UInt64(totalSurfaceRejects);
        report["allFreeTriangleCentroids"]=freeFaceRaw.ToJson();report["freeTrianglesPerFrame"]=Json::UInt64(freeFaces.size());report["worstFreeTriangleCentroid"]=worstFreeFace;report["freeFaceRule"]="Every triangle with all three vertices in the movable set is independently sampled at its centroid against actual animated body geometry; same raw 5 mm limit, no baseline subtraction.";
        PrintDepth("Actual rendered hair",actualDepths);PrintDepth("Animated source baseline",sourceDepths);PrintDepth("New dynamic penetration",additionalDepths);PrintDepth("New body surface",novelSurfaceDepths);PrintDepth("Source root-only region",rootSourceDepths);PrintDepth("Root-only actual",rootRawDepths);PrintDepth("Free distal actual",freeRawDepths);PrintDepth("Free distal source",freeSourceDepths);PrintDepth("Free distal additional",freeAdditionalDepths);PrintDepth("Rigid scalp source",scalpSourceDepths);
        report["maximumRuntimeProbePenetrationMm"]=maximumProbePenetration*1000;
        report["worstRuntimeProbe"]=worstRuntimeProbe;
        PrintDepth("All movable actual",movableRaw);PrintDepth("Authored scalp attachment",attachmentRaw);std::printf("Scalp attachments %zu; maximum head-local drift %.6f mm; maximum runtime probe residual %.6f mm; all-vertex raw within 5 mm: %s\n",hair->attachments.size(),maximumAttachmentDrift*1000,maximumProbePenetration*1000,allRawWithinLimit?"yes":"no (authored roots separately reported)");
        PrintDepth("All-free face centroids",freeFaceRaw);
        std::printf("Worst raw: %.3f mm at %.6f s, hair mesh %u (%s), vertex %u, body triangle %u; source same vertex %.3f mm\n",worst.depth*1000,worst.time,worst.sample.mesh,target->geometry->meshes[worst.sample.mesh].name.c_str(),worst.sample.vertex,worst.body.triangle,worst.sourceDepth*1000);
        const auto timings=stepTimes.ToJson();std::printf("Hair step mean/p95/max %.3f/%.3f/%.3f ms; reset %.3f ms; independent scan wall %.3f s; %s\n",timings["meanMs"].asDouble(),timings["p95Ms"].asDouble(),timings["maxMs"].asDouble(),resetMs,simulationWallMs/1000,passed?"PASSED (movable raw + authored attachment drift)":"FAILED (movable raw, attachment drift or finite/root check)");
        if(!options.output.empty()){if(!options.output.parent_path().empty())fs::create_directories(options.output.parent_path());std::ofstream stream(options.output,std::ios::binary);if(!stream)throw std::runtime_error("Cannot create JSON report");Json::StreamWriterBuilder builder;builder["indentation"]="  ";builder["precision"]=17;builder["emitUTF8"]=true;stream<<Json::writeString(builder,report)<<'\n';if(!stream)throw std::runtime_error("Cannot write JSON report");std::printf("JSON report: %s\n",PathText(options.output).c_str());}
        return passed?0:1;
    }catch(const std::exception& exception){std::fprintf(stderr,"Hair surface quality error: %s\n",exception.what());return 2;}
}
