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
#include <limits>
#include <map>
#include <set>
#include <stdexcept>

namespace {
using namespace Model;
namespace fs=std::filesystem;
using Clock=std::chrono::steady_clock;
constexpr double TravelLimit=.002,SpeedLimit=.02,PenetrationLimit=.005,AttachmentLimit=.001501;
struct Options{
    fs::path body=fs::path(MODEL_SOURCE_DIR)/"Asset/Generated/MahiroCloth/Body/Package.asset.json";
    fs::path hair=fs::path(MODEL_SOURCE_DIR)/"Asset/Generated/MahiroCloth/Hair/Hair_Realtime30.hair.json";
    fs::path animation=fs::path(MODEL_SOURCE_DIR)/"Asset/Generated/Animations/MahiroTaitui.anim.json",output;
    std::size_t frames=301;double time=2.8;
};
std::string PathText(const fs::path& p){const auto utf8=p.generic_u8string();return {reinterpret_cast<const char*>(utf8.data()),utf8.size()};}
bool Finite(glm::dvec3 p){return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);}
double Milliseconds(Clock::time_point start){return std::chrono::duration<double,std::milli>(Clock::now()-start).count();}
Json::Value Number(double n){return std::isfinite(n)?Json::Value(n):Json::Value();}
Json::Value Vector(glm::dvec3 p){Json::Value r(Json::arrayValue);for(int i=0;i<3;++i)r.append(Number(p[i]));return r;}
void Require(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
Options Parse(int argc,char** argv){
    Options result;for(int i=1;i<argc;++i){const std::string argument=argv[i];if(argument=="--help"){
        std::puts("hair_rest_stability_test [--hair FILE] [--frames 301] [--time 2.8] [--output FILE]\n"
            "                         [--body FILE] [--animation FILE]\n"
            "Holds one real animated pose without wind. Steps, skins and applies real hair offsets each tick.\n"
            "At least four simulated seconds are required; the last two must settle below 2 mm travel and 0.02 m/s.\n"
            "Only the last frame receives the independent full vertex/face collision scan.");std::exit(0);}
        Require(i+1<argc,"Missing value after "+argument);const std::string value=argv[++i];
        if(argument=="--hair")result.hair=fs::u8path(value);else if(argument=="--body")result.body=fs::u8path(value);else if(argument=="--animation")result.animation=fs::u8path(value);else if(argument=="--output")result.output=fs::u8path(value);
        else if(argument=="--frames"){std::size_t used=0;const auto frames=std::stoull(value,&used);Require(!value.empty()&&value[0]!='-'&&used==value.size()&&frames>=2&&frames<=100000,"Frames must be an integer in 2..100000");result.frames=static_cast<std::size_t>(frames);}
        else if(argument=="--time"){std::size_t used=0;result.time=std::stod(value,&used);Require(used==value.size()&&std::isfinite(result.time)&&result.time>=0,"Time must be finite and nonnegative");}
        else throw std::runtime_error("Unknown argument: "+argument);
    }return result;
}
struct Sample{std::uint32_t mesh=0,vertex=0,instance=UINT32_MAX,chain=UINT32_MAX;bool attachment=false;};
struct Face{std::uint32_t mesh=0,triangle=0,instance=UINT32_MAX;std::array<std::uint32_t,3> vertices{};};
struct BoneSample{std::uint32_t chain=0,node=0,bone=0;glm::dvec3 restTip{0};bool root=false,tip=false;};
Json::Value BoneIdentity(const BoneSample& sample,std::size_t index,const HairAsset& hair,const SkeletalAssetPackage& target){Json::Value r;r["nodeIndex"]=Json::UInt64(index);r["nodeInChain"]=sample.node;r["chain"]=sample.chain;r["chainName"]=hair.chains[sample.chain].name;r["bone"]=sample.bone;r["boneName"]=target.skeleton.bones[sample.bone].name;r["root"]=sample.root;r["chainTip"]=sample.tip;return r;}
glm::dvec3 PoseNode(const BoneSample& sample,const SkeletalPose& pose,double units){return glm::dvec3(pose.global[sample.bone]*glm::dvec4(sample.root?glm::dvec3(0):sample.restTip,1))/units;}
glm::dvec3 RenderPoint(const StaticModel& rendered,const Sample& s,double units){const auto p=rendered.meshes[s.mesh].vertices[s.vertex].position;return (s.instance==UINT32_MAX?p:glm::dvec3(rendered.instances[s.instance].transform*glm::dvec4(p,1)))/units;}
struct Trajectory{
    glm::dvec3 minimum{std::numeric_limits<double>::infinity()},maximum{-std::numeric_limits<double>::infinity()},previous{0};
    double maxSpeed=0;bool initialized=false;
    void Add(glm::dvec3 point,double dt){if(initialized)maxSpeed=std::max(maxSpeed,glm::length(point-previous)/dt);minimum=glm::min(minimum,point);maximum=glm::max(maximum,point);previous=point;initialized=true;}
    double Diameter()const{return initialized?glm::length(maximum-minimum):0;}
};
struct Depths{
    std::uint64_t samples=0,negative=0,overLimit=0;double maximum=0;Json::Value worst;
    void Add(const detail::HairBodyNearest& nearest,Json::Value identity,glm::dvec3 point){++samples;const double depth=nearest.found&&nearest.inside?std::max(0.,-nearest.signedDistance):0;if(depth>0)++negative;if(depth>PenetrationLimit)++overLimit;
        if(depth>maximum){maximum=depth;worst=std::move(identity);worst["depthMm"]=depth*1000;worst["bodyMesh"]=nearest.mesh;worst["bodyTriangle"]=nearest.triangle;worst["positionMeters"]=Vector(point);}}
    Json::Value ToJson()const{Json::Value r;r["samples"]=Json::UInt64(samples);r["negativeSamples"]=Json::UInt64(negative);r["over5Mm"]=Json::UInt64(overLimit);r["maxMm"]=maximum*1000;r["worst"]=worst;return r;}
};
Json::Value SampleIdentity(const Sample& s){Json::Value r;r["mesh"]=s.mesh;r["vertex"]=s.vertex;r["instance"]=s.instance;return r;}
Json::Value Times(std::vector<double> samples){Json::Value r;if(samples.empty())return r;double total=0;for(auto value:samples)total+=value;std::sort(samples.begin(),samples.end());r["meanMs"]=total/samples.size();r["p95Ms"]=samples[std::min(samples.size()-1,static_cast<std::size_t>(std::ceil(samples.size()*.95))-1)];r["maxMs"]=samples.back();return r;}
int Run(const Options& options,Json::Value& report){
    const auto entireStart=Clock::now();std::string error;
    auto target=LoadSkeletalAssetPackage(options.body,&error);Require(bool(target),"Body load: "+error);
    auto loadedHair=LoadHairAsset(options.hair,*target,&error);Require(bool(loadedHair),"Hair load: "+error);
    auto animation=LoadAnimationSequenceAsset(options.animation,*target,&error);Require(bool(animation),"Animation load: "+error);
    Require(options.time<=animation->duration+1e-10,"Requested fixed pose exceeds the animation duration");
    HairAsset hair=*loadedHair;hair.settings.wind={0,0,0}; // In-memory rest experiment, never writes source assets.
    const double dt=hair.settings.fixedTimeStep,duration=(options.frames-1)*dt;
    Require(std::isfinite(dt)&&dt>0&&duration>=4.-1e-9,"At least four simulated seconds are required: two seconds of warmup plus the final two-second stability window");
    const auto animated=EvaluateSkeletalPose(*target,&*animation,options.time,false);
    std::vector<bool> active(target->skeleton.bones.size(),false);for(const auto& c:hair.chains)for(const auto& link:c.links)active[link.bone]=true;
    std::map<std::uint64_t,std::uint32_t> attachments;for(const auto& a:hair.attachments)attachments.emplace((std::uint64_t(a.mesh)<<32)|a.vertex,a.chain);
    std::set<std::uint32_t> hairMeshes(hair.hairMeshes.begin(),hair.hairMeshes.end());std::vector<Sample> samples,rigid;std::set<std::uint64_t> freeIds;
    for(std::uint32_t mesh=0;mesh<target->skins.size();++mesh)for(std::uint32_t vertex=0;vertex<target->skins[mesh].vertices.size();++vertex){const auto& skin=target->skins[mesh].vertices[vertex];bool effective=false;
        for(int k=0;k<4;++k)if(std::isfinite(skin.weights[k])&&skin.weights[k]>0&&skin.joints[k]<active.size())effective|=active[skin.joints[k]];
        if(!effective&&!hairMeshes.contains(mesh))continue;const auto attachment=attachments.find((std::uint64_t(mesh)<<32)|vertex);Sample s;s.mesh=mesh;s.vertex=vertex;s.attachment=attachment!=attachments.end();if(s.attachment)s.chain=attachment->second;
        for(std::uint32_t i=0;i<target->geometry->instances.size();++i)if(target->geometry->instances[i].meshIndex==mesh){s.instance=i;break;}
        if(effective){samples.push_back(s);if(!s.attachment)freeIds.insert((std::uint64_t(mesh)<<32)|vertex);}else rigid.push_back(s);
    }
    Require(!samples.empty()&&!freeIds.empty(),"No actual positively hair-weighted free render vertices");
    std::vector<Face> faces;for(const auto mesh:hair.hairMeshes){std::uint32_t instance=UINT32_MAX;for(std::uint32_t i=0;i<target->geometry->instances.size();++i)if(target->geometry->instances[i].meshIndex==mesh){instance=i;break;}
        const auto& indices=target->geometry->meshes[mesh].indices;for(std::uint32_t t=0;t+2<indices.size();t+=3){bool free=true;for(int k=0;k<3;++k)free&=freeIds.contains((std::uint64_t(mesh)<<32)|indices[t+k]);if(free)faces.push_back({mesh,t/3,instance,{indices[t],indices[t+1],indices[t+2]}});}}
    StaticModel baseline;Require(SkinSkeletalMesh(*target,animated,baseline,&error),"Fixed animated skin: "+error);
    std::vector<glm::dvec3> baselinePoints;baselinePoints.reserve(samples.size());for(const auto& s:samples)baselinePoints.push_back(RenderPoint(baseline,s,hair.unitsPerMeter));
    std::vector<glm::dmat3> inverseAnchors;for(const auto& c:hair.chains)inverseAnchors.emplace_back(glm::inverse(glm::dmat3(animated.global[c.anchorBone])));
    detail::HairBodyCollision independentBody;Require(independentBody.Initialize(*target,hair.hairMeshes,hair.unitsPerMeter,&error),"Independent body initialize: "+error);
    Require(independentBody.Update(animated,&error),"Independent fixed body refit: "+error);independentBody.ResetMotion();
    HairSimulator simulator;const auto resetStarted=Clock::now();Require(simulator.Reset(hair,*target,animated,&error),"Hair Reset: "+error);const double resetMs=Milliseconds(resetStarted);
    std::vector<BoneSample> boneSamples;for(std::uint32_t chain=0;chain<hair.chains.size();++chain){const auto& c=hair.chains[chain];boneSamples.push_back({chain,0,c.links.front().bone,{0,0,0},true,false});for(std::uint32_t link=0;link<c.links.size();++link)boneSamples.push_back({chain,link+1,c.links[link].bone,c.links[link].restTip,false,link+1==c.links.size()});}
    const auto resetCount=simulator.Stats().resets;std::vector<Trajectory> trajectories(samples.size()),boneTrajectories;std::vector<double> stepTimes,windowStepTimes;
    std::vector<glm::dvec3> previousVertices(samples.size(),glm::dvec3(0)),previousBones(boneSamples.size(),glm::dvec3(0));Json::Value frameRecords(Json::arrayValue);
    std::uint64_t nonfinite=0;double maximumRoot=0,maximumAttachment=0,maximumDebugPoseDifference=0;bool exactClock=true;std::size_t measuredFrames=0,firstMeasured=options.frames;
    StaticModel rendered;SkeletalPose solved;const double windowStart=duration-2.;const auto simulationStart=Clock::now();
    std::printf("Hair rest stability: pose %.6f s; %zu samples, %zu free, %zu attachments, %zu rigid diagnostics; %zu frames / %.6f s\n",options.time,samples.size(),freeIds.size(),attachments.size(),rigid.size(),options.frames,duration);
    for(std::size_t frame=0;frame<options.frames;++frame){const auto stepStarted=Clock::now();Require(simulator.Step(hair,*target,animated,frame?dt:0,hair.settings,solved,&error),"Hair Step "+std::to_string(frame)+": "+error);if(frame)stepTimes.push_back(Milliseconds(stepStarted));
        const auto& stats=simulator.Stats();exactClock&=stats.steps==frame&&stats.resets==resetCount;maximumRoot=std::max(maximumRoot,stats.maxRootError);
        if(!std::isfinite(stats.maxRootError)||!std::isfinite(stats.maxAttachmentDrift))++nonfinite;
        Require(SkinSkeletalMesh(*target,solved,rendered,&error)&&ApplyHairSurfaceOffsets(*target,solved,simulator.SurfaceOffsets(),rendered,&error),"Rendered skin and surface correction: "+error);
        const bool measuring=frame*dt+1e-9>=windowStart;if(measuring){if(firstMeasured==options.frames)firstMeasured=frame;++measuredFrames;if(frame)windowStepTimes.push_back(stepTimes.back());}
        double frameVertexSpeed=0,frameBoneMotion=0,frameBoneMotion2=0,frameTipDeviation=0;std::size_t movingBoneNodes=0;Json::Value frameWorstVertex,frameWorstBone,frameWorstTip;
        for(std::size_t i=0;i<samples.size();++i){const auto point=RenderPoint(rendered,samples[i],hair.unitsPerMeter);if(!Finite(point)){++nonfinite;continue;}
            if(samples[i].attachment){const auto local=inverseAnchors[samples[i].chain]*(point-baselinePoints[i]);maximumAttachment=std::max(maximumAttachment,glm::length(local));}
            else {if(measuring)trajectories[i].Add(point,dt);if(frame){const double speed=glm::length(point-previousVertices[i])/dt;if(speed>frameVertexSpeed){frameVertexSpeed=speed;frameWorstVertex=SampleIdentity(samples[i]);}}}
            previousVertices[i]=point;
        }
        const auto& nodes=simulator.Positions();if(boneTrajectories.empty())boneTrajectories.resize(nodes.size());if(nodes.size()!=boneTrajectories.size())++nonfinite;
        std::size_t nodeOffset=0;for(const auto& c:hair.chains){if(nodeOffset>=nodes.size()){++nonfinite;break;}maximumRoot=std::max(maximumRoot,glm::length(nodes[nodeOffset]-glm::dvec3(animated.global[c.links.front().bone][3]))/hair.unitsPerMeter);nodeOffset+=c.links.size()+1;}
        for(std::size_t i=0;i<nodes.size();++i){if(!Finite(nodes[i])){++nonfinite;continue;}const auto point=nodes[i]/hair.unitsPerMeter;if(measuring&&i<boneTrajectories.size())boneTrajectories[i].Add(point,dt);
            if(i>=boneSamples.size()){++nonfinite;continue;}const auto identity=BoneIdentity(boneSamples[i],i,hair,*target);const auto posed=PoseNode(boneSamples[i],solved,hair.unitsPerMeter);maximumDebugPoseDifference=std::max(maximumDebugPoseDifference,glm::length(posed-point));
            if(frame){const double motion=glm::length(point-previousBones[i]);if(motion>frameBoneMotion){frameBoneMotion=motion;frameWorstBone=identity;frameWorstBone["previousMeters"]=Vector(previousBones[i]);frameWorstBone["currentMeters"]=Vector(point);}if(!boneSamples[i].root){frameBoneMotion2+=motion*motion;++movingBoneNodes;}}
            if(!boneSamples[i].root){const double deviation=glm::length(point-PoseNode(boneSamples[i],animated,hair.unitsPerMeter));if(deviation>frameTipDeviation){frameTipDeviation=deviation;frameWorstTip=identity;}}
            previousBones[i]=point;
        }
        for(const auto& matrix:solved.global)for(int c=0;c<4;++c)for(int r=0;r<4;++r)if(!std::isfinite(matrix[c][r]))++nonfinite;
        Json::Value record;record["frame"]=Json::UInt64(frame);record["simulationSeconds"]=frame*dt;record["inFinalWindow"]=measuring;record["sleeping"]=stats.sleeping;record["sleepTicks"]=Json::UInt64(stats.sleepTicks);record["activeSolves"]=Json::UInt64(stats.steps-stats.sleepTicks);record["maxFrameVertexSpeedMetersPerSecond"]=frameVertexSpeed;record["worstVertexSpeed"]=frameWorstVertex;record["maxFrameBoneMotionMm"]=frameBoneMotion*1000;record["maxFrameBoneSpeedMetersPerSecond"]=frameBoneMotion/dt;record["rmsFrameBoneMotionMm"]=movingBoneNodes?std::sqrt(frameBoneMotion2/movingBoneNodes)*1000:0.;record["worstBoneMotion"]=frameWorstBone;record["maxTipDisplacementMm"]=stats.maxTipDisplacement*1000;record["independentMaxTipDisplacementMm"]=frameTipDeviation*1000;record["worstTipDisplacement"]=frameWorstTip;record["contacts"]=Json::UInt64(stats.contacts);record["surfaceContacts"]=Json::UInt64(stats.surfaceContacts);record["maxProbePenetrationMm"]=stats.maxPenetration*1000;record["maxMeshContactResidualMm"]=stats.maxSurfacePenetration*1000;record["physicsStepMs"]=frame?stepTimes.back():0.;frameRecords.append(std::move(record));
        if(frame%60==0||frame+1==options.frames){std::printf("  frame %zu / %.3f s: step %.3f ms; bone motion %.6f mm (%s node %u); vertex speed %.6f m/s; tip %.6f mm; contacts %zu/%zu\n",frame,frame*dt,frame?stepTimes.back():0.,frameBoneMotion*1000,frameWorstBone.isObject()?frameWorstBone["chainName"].asCString():"none",frameWorstBone.isObject()?frameWorstBone["nodeInChain"].asUInt():0,frameVertexSpeed,frameTipDeviation*1000,stats.contacts,stats.surfaceContacts);std::fflush(stdout);}
    }
    const double simulationWallMs=Milliseconds(simulationStart);double maximumTravel=0,maximumSpeed=0,boneTravel=0,boneSpeed=0;Json::Value worstTravel,worstSpeed;
    for(std::size_t i=0;i<samples.size();++i)if(!samples[i].attachment){const auto& t=trajectories[i];if(t.Diameter()>maximumTravel){maximumTravel=t.Diameter();worstTravel=SampleIdentity(samples[i]);worstTravel["minimumMeters"]=Vector(t.minimum);worstTravel["maximumMeters"]=Vector(t.maximum);}if(t.maxSpeed>maximumSpeed){maximumSpeed=t.maxSpeed;worstSpeed=SampleIdentity(samples[i]);}}
    Json::Value worstBoneTravel,worstBoneSpeed,perChain(Json::arrayValue);for(std::size_t chain=0;chain<hair.chains.size();++chain){Json::Value r;r["chain"]=Json::UInt64(chain);r["name"]=hair.chains[chain].name;r["maxNodeTrajectoryBoxDiameterMm"]=0.;r["maxNodeSpeedMetersPerSecond"]=0.;perChain.append(std::move(r));}
    for(std::size_t i=0;i<boneTrajectories.size()&&i<boneSamples.size();++i){const auto& t=boneTrajectories[i];auto identity=BoneIdentity(boneSamples[i],i,hair,*target);identity["minimumMeters"]=Vector(t.minimum);identity["maximumMeters"]=Vector(t.maximum);
        if(t.Diameter()>boneTravel){boneTravel=t.Diameter();worstBoneTravel=identity;}if(t.maxSpeed>boneSpeed){boneSpeed=t.maxSpeed;worstBoneSpeed=identity;}
        auto& chain=perChain[boneSamples[i].chain];if(t.Diameter()*1000>chain["maxNodeTrajectoryBoxDiameterMm"].asDouble()){chain["maxNodeTrajectoryBoxDiameterMm"]=t.Diameter()*1000;chain["worstTravelNode"]=identity;}if(t.maxSpeed>chain["maxNodeSpeedMetersPerSecond"].asDouble()){chain["maxNodeSpeedMetersPerSecond"]=t.maxSpeed;chain["worstSpeedNode"]=identity;}
    }
    const auto scanStarted=Clock::now();Depths freeDepths,faceDepths,rigidDepths;independentBody.ResetQueryStats();
    for(const auto& s:samples)if(!s.attachment){const auto point=RenderPoint(rendered,s,hair.unitsPerMeter);if(!Finite(point))continue;const auto nearest=independentBody.Nearest(point);if(nearest.found&&!std::isfinite(nearest.signedDistance)){++nonfinite;continue;}freeDepths.Add(nearest,SampleIdentity(s),point);}
    for(const auto& f:faces){glm::dvec3 center(0);for(auto v:f.vertices)center+=RenderPoint(rendered,{f.mesh,v,f.instance},hair.unitsPerMeter)/3.;if(!Finite(center)){++nonfinite;continue;}const auto nearest=independentBody.Nearest(center);if(nearest.found&&!std::isfinite(nearest.signedDistance)){++nonfinite;continue;}Json::Value identity;identity["mesh"]=f.mesh;identity["triangle"]=f.triangle;faceDepths.Add(nearest,identity,center);}
    for(const auto& s:rigid){const auto point=RenderPoint(rendered,s,hair.unitsPerMeter);if(!Finite(point)){++nonfinite;continue;}const auto nearest=independentBody.Nearest(point);if(nearest.found&&!std::isfinite(nearest.signedDistance)){++nonfinite;continue;}rigidDepths.Add(nearest,SampleIdentity(s),point);}
    const double scanMs=Milliseconds(scanStarted);
    const bool settled=maximumTravel<TravelLimit&&maximumSpeed<SpeedLimit;
    const bool passed=settled&&nonfinite==0&&exactClock&&maximumRoot<1e-7&&maximumAttachment<=AttachmentLimit&&freeDepths.maximum<=PenetrationLimit&&faceDepths.maximum<=PenetrationLimit;
    report["passed"]=passed;report["settled"]=settled;report["body"]=PathText(options.body);report["hair"]=PathText(options.hair);report["animation"]=PathText(options.animation);report["fixedAnimationTimeSeconds"]=options.time;report["fixedTimeStepSeconds"]=dt;report["frames"]=Json::UInt64(options.frames);report["physicalTicks"]=Json::UInt64(simulator.Stats().steps);report["simulatedSeconds"]=duration;report["wind"]=Vector(hair.settings.wind);
    report["effectiveHairVertices"]=Json::UInt64(samples.size());report["freeVertices"]=Json::UInt64(freeIds.size());report["validatedAttachmentVertices"]=Json::UInt64(attachments.size());report["rigidHeadVerticesDiagnosticOnly"]=Json::UInt64(rigid.size());
    report["window"]["requestedSeconds"]=2.;report["window"]["startSeconds"]=firstMeasured*dt;report["window"]["endSeconds"]=duration;report["window"]["samples"]=Json::UInt64(measuredFrames);report["window"]["maxVertexTrajectoryBoxDiameterMm"]=maximumTravel*1000;report["window"]["maxVertexSpeedMetersPerSecond"]=maximumSpeed;report["window"]["worstTravel"]=worstTravel;report["window"]["worstSpeed"]=worstSpeed;report["window"]["maxBoneNodeTrajectoryBoxDiameterMm"]=boneTravel*1000;report["window"]["maxBoneNodeSpeedMetersPerSecond"]=boneSpeed;
    report["window"]["worstBoneNodeTravel"]=worstBoneTravel;report["window"]["worstBoneNodeSpeed"]=worstBoneSpeed;report["window"]["chains"]=perChain;report["frameDiagnostics"]=frameRecords;report["maximumDebugNodeVsSolvedPoseDifferenceMm"]=maximumDebugPoseDifference*1000;
    report["limits"]["trajectoryBoxDiameterMm"]=TravelLimit*1000;report["limits"]["vertexSpeedMetersPerSecond"]=SpeedLimit;report["limits"]["rawPenetrationMm"]=PenetrationLimit*1000;report["limits"]["attachmentHeadLocalDriftMm"]=AttachmentLimit*1000;
    report["finalFrameFreeVertices"]=freeDepths.ToJson();report["finalFrameAllFreeTriangleCentroids"]=faceDepths.ToJson();report["finalFrameRigidHeadDiagnosticOnly"]=rigidDepths.ToJson();report["nonfiniteValues"]=Json::UInt64(nonfinite);report["exactFixedClock"]=exactClock;report["maximumRootErrorMm"]=maximumRoot*1000;report["maximumAttachmentHeadLocalDriftMm"]=maximumAttachment*1000;
    report["sleeping"]=simulator.Stats().sleeping;report["sleepTicks"]=Json::UInt64(simulator.Stats().sleepTicks);report["activeSolves"]=Json::UInt64(simulator.Stats().steps-simulator.Stats().sleepTicks);report["physicsStep"]=Times(stepTimes);report["finalWindowPhysicsStep"]=Times(windowStepTimes);report["resetMs"]=resetMs;report["simulationSkinAndTrajectoryWallMs"]=simulationWallMs;report["independentFinalScanMs"]=scanMs;report["totalOfflineWallMs"]=Milliseconds(entireStart);report["timingScope"]="Offline fixed-pose stability experiment: physics timing excludes CPU skin/apply/trajectory sampling and the final independent scan; wall time is not interactive FPS.";
    report["scanScope"]="All actual positive hair-chain weighted render vertices; only validated authored attachments excluded from free tests. Rigid Head-only vertices are diagnostic only. Independent actual-body raw vertex and all-free face-centroid penetration scanned once at the final frame, without baseline subtraction.";
    std::printf("Final two seconds: max free vertex travel %.6f mm, speed %.9f m/s; bone travel %.6f mm\n",maximumTravel*1000,maximumSpeed,boneTravel*1000);
    std::printf("Final raw penetration free/face %.6f / %.6f mm; root %.9f mm; attachments %.6f mm; %s\n",freeDepths.maximum*1000,faceDepths.maximum*1000,maximumRoot*1000,maximumAttachment*1000,passed?"PASSED":"FAILED");
    return passed?0:1;
}
}
int main(int argc,char** argv){
    Options options;Json::Value report;int result=2;
    try{options=Parse(argc,argv);result=Run(options,report);}catch(const std::exception& exception){report["passed"]=false;report["error"]=exception.what();std::fprintf(stderr,"Hair rest stability error: %s\n",exception.what());}
    if(!options.output.empty())try{if(!options.output.parent_path().empty())fs::create_directories(options.output.parent_path());std::ofstream stream(options.output,std::ios::binary);Json::StreamWriterBuilder writer;writer["indentation"]="  ";stream<<Json::writeString(writer,report)<<'\n';stream.close();Require(bool(stream),"Cannot write requested rest stability report");std::printf("JSON report: %s\n",PathText(options.output).c_str());}catch(const std::exception& exception){std::fprintf(stderr,"Report error: %s\n",exception.what());return 2;}
    return result;
}
