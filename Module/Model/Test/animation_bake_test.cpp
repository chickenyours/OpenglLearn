#include "Model/Public/vmd_animation.h"
#include "../Private/animation_limits.h"
#include <glm/gtx/quaternion.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <set>

namespace {
using namespace Model;
int failures=0;
bool Expect(bool condition,const char* description) {
    std::printf("  %-86s %s\n",description,condition?"ok":"FAIL");failures+=!condition;return condition;
}
bool Near(glm::dvec3 a,glm::dvec3 b,double tolerance=1e-7){return glm::length(a-b)<tolerance;}
bool Near(glm::dquat a,glm::dquat b,double tolerance=1e-7){return 1-std::abs(glm::dot(glm::normalize(a),glm::normalize(b)))<tolerance;}
int Bone(SkeletalAssetPackage& asset,std::string name,int parent,glm::dvec3 translation,std::string english={}) {
    SkeletonBone bone;bone.name=std::move(name);bone.englishName=std::move(english);bone.parent=parent;
    bone.restLocal.translation=translation;asset.skeleton.bones.push_back(bone);return static_cast<int>(asset.skeleton.bones.size()-1);
}
void Bind(SkeletalAssetPackage& asset) {
    auto global=GlobalBoneTransforms(asset.skeleton,RestPose(asset.skeleton));
    for(std::size_t i=0;i<global.size();++i)asset.skeleton.bones[i].inverseBind=glm::inverse(global[i]);
}
SkeletalAssetPackage LegFixture() {
    SkeletalAssetPackage asset;asset.id="fixture";asset.name="IK bake fixture";asset.skeleton.id="fixture/skeleton";
    Bone(asset,"全ての親",-1,{0,0,0},"root");
    Bone(asset,"左足",0,{.2,2,0},"J_Bip_L_UpperLeg");
    Bone(asset,"左ひざ",1,{0,-1,0},"J_Bip_L_LowerLeg");
    Bone(asset,"左足首",2,{0,-1,0},"J_Bip_L_Foot");
    Bone(asset,"左足ＩＫ",0,{.2,0,0},"leg IK_L");
    Bone(asset,"センター",0,{0,0,0},"center");Bind(asset);
    IkChain chain;chain.name="左足ＩＫ";chain.native=true;chain.root=1;chain.effector=3;chain.targetBone=4;chain.iterations=100;chain.angleLimit=.25;
    chain.links={{2,true,{0,0,0},{3.1,0,0}},{1,false,{0,0,0},{0,0,0}}};asset.ikRig.chains.push_back(chain);
    MorphTarget a;a.name="まばたき";a.englishName="Fcl_EYE_Close";asset.morphs.push_back(a);
    MorphTarget b;b.name="あ";b.englishName="Fcl_MTH_A";asset.morphs.push_back(b);
    return asset;
}
VmdBoneTrack Move(std::string name,glm::dvec3 translation,glm::dquat rotation=glm::dquat(1,0,0,0)) {
    VmdBoneTrack track;track.name=std::move(name);VmdBoneKey a,b;b.frame=30;b.translation=translation;b.rotation=rotation;
    track.keys={a,b};return track;
}
void CoreBakeTests() {
    std::puts("VMD matching and ordinary sequence baking:");auto target=LegFixture();VmdMotion motion;motion.id="move";motion.name="move";motion.modelName="MMD";motion.duration=1;
    motion.bones.push_back(Move("左足IK",{0,.6,-.6}));motion.bones.push_back(Move("center",{.3,0,0}));
    VmdMorphTrack blink;blink.name="blink";blink.keys={{0,0},{15,1},{30,0}};motion.morphs.push_back(blink);
    AnimationBakeReport report;std::string error;AnimationBakeOptions options;
    auto clip=BakeVmdAnimation(motion,target,options,"Lift",&report,&error);
    if(!Expect(clip.has_value(),"VMD delta tracks bake successfully")){std::printf("%s\n",error.c_str());return;}
    Expect(clip->baked&&!clip->generated&&clip->skeletonId==target.skeleton.id&&report.samples==31,"Sequence binds target skeleton, marks baked provenance and samples both endpoints");
    Expect(report.matchedBones==2&&report.matchedMorphs==1,"Fullwidth IK, English controller and VRM expression aliases match automatically");
    auto final=EvaluateSkeletalPose(target,&*clip,1,false),half=EvaluateSkeletalPose(target,&*clip,.5,false);
    Expect(glm::length(glm::dvec3(final.global[3][3])-glm::dvec3(final.global[4][3]))<.002,"Native IK result is baked into ordinary FK TRS playback");
    Expect(Near(final.local[5].translation,{.3,0,0})&&std::abs(half.morphWeights[0]-1)<1e-6,"Root/controller translation and named expression curves survive sampling");
    bool ends=true,continuous=true;
    for(const auto& track:clip->boneTracks) {
        if(track.rotations.size()>1){ends&=track.rotations.back().time==1;for(std::size_t i=1;i<track.rotations.size();++i)continuous&=glm::dot(track.rotations[i-1].value,track.rotations[i].value)>=0;}
    }
    Expect(ends&&continuous,"Animated channels retain final endpoint and quaternion sign continuity");
    auto changedOptions=options;changedOptions.solveNativeIk=false;
    auto differentBake=BakeVmdAnimation(motion,target,changedOptions,"Lift",nullptr,&error);
    Expect(differentBake&&differentBake->id!=clip->id,"Changing VMD IK bake options produces a distinct stable animation identity");
    auto switched=motion;switched.ikKeys={{0,true,{{"左足IK",true}}},{30,true,{{"左足IK",false}}}};
    auto englishTarget=target;englishTarget.skeleton.bones[4].name="LeftFootIK";englishTarget.ikRig.chains[0].name="LeftFootIK";
    auto noIk=BakeVmdAnimation(switched,englishTarget,options,"Switch",nullptr,&error);
    auto disabled=noIk?EvaluateSkeletalPose(englishTarget,&*noIk,1,false):SkeletalPose{};
    Expect(noIk&&Near(glm::dvec3(disabled.global[3][3]),{.2,0,0}),"Japanese IK switch aliases disable an English-named target controller stepwise");
    auto fk=motion;fk.bones.clear();fk.bones.push_back(Move("左足",{},glm::angleAxis(.6,glm::dvec3(1,0,0))));
    auto fkClip=BakeVmdAnimation(fk,target,options,"FK",&report,&error);
    auto fkPose=fkClip?EvaluateSkeletalPose(target,&*fkClip,1,false):SkeletalPose{};
    Expect(fkClip&&Near(fkPose.local[1].rotation,glm::angleAxis(.6,glm::dvec3(1,0,0))),"Authored FK-only limb motion is not pinned to an unanimated IK controller");
    Expect(fkClip&&std::any_of(report.warnings.begin(),report.warnings.end(),[](const auto& w){return w.find("Preserved authored FK")!=std::string::npos;}),"FK / IK inference is visible as a bake diagnostic");
    auto staticMotion=motion;staticMotion.duration=0;staticMotion.bones={Move("center",{})};staticMotion.bones[0].keys.resize(1);staticMotion.morphs.clear();
    auto zero=BakeVmdAnimation(staticMotion,target,options,"Zero",&report,&error);
    Expect(zero&&zero->duration==0&&report.samples==1,"A single-frame zero-duration animation is valid");
    auto shortMotion=staticMotion;shortMotion.duration=.015;auto shortClip=BakeVmdAnimation(shortMotion,target,options,"Subframe",&report,&error);
    Expect(shortClip&&report.samples==2,"Subframe duration appends its exact endpoint without duplicate times");
    auto invalid=options;invalid.framesPerSecond=std::numeric_limits<double>::quiet_NaN();
    Expect(!BakeVmdAnimation(motion,target,invalid,"Bad",nullptr,&error)&&!error.empty(),"Nonfinite bake options are rejected");
    auto huge=motion;huge.duration=1000000;
    Expect(!BakeVmdAnimation(huge,target,options,"Huge",nullptr,&error),"Unbounded duration is rejected before sampling allocation");
    VmdMotion camera;camera.id="camera";camera.cameraKeys=1;camera.duration=1;
    Expect(!BakeVmdAnimation(camera,target,options,"Camera",nullptr,&error)&&error.find("camera-only")!=std::string::npos,"Camera-only VMD cannot masquerade as a character sequence");
    auto ambiguous=target;Bone(ambiguous,"センター",0,{0,0,0},"center");
    VmdMotion duplicate;duplicate.id="ambiguous";duplicate.bones={Move("センター",{1,0,0})};
    Expect(!BakeVmdAnimation(duplicate,ambiguous,options,"Ambiguous",nullptr,&error),"Duplicate equally named target bones remain unmatched");
}
void AppendTests() {
    std::puts("PMX append and fixed-axis baking:");SkeletalAssetPackage asset;asset.id="append";asset.skeleton.id="append/skeleton";
    Bone(asset,"root",-1,{0,0,0});Bone(asset,"両目",0,{0,1,0});
    Bone(asset,"左目",0,{.1,1,0});Bone(asset,"右目",0,{-.1,1,0});
    Bone(asset,"half",0,{0,0,0});Bone(asset,"chained",0,{0,0,0});Bone(asset,"local",0,{0,0,0});
    for(int b:{2,3}){asset.skeleton.bones[b].sourceFlags=0x100;asset.skeleton.bones[b].inheritParent=1;asset.skeleton.bones[b].inheritWeight=1;}
    asset.skeleton.bones[4].sourceFlags=0x300;asset.skeleton.bones[4].inheritParent=1;asset.skeleton.bones[4].inheritWeight=.5f;
    asset.skeleton.bones[5].sourceFlags=0x300;asset.skeleton.bones[5].inheritParent=4;asset.skeleton.bones[5].inheritWeight=1;
    asset.skeleton.bones[6].sourceFlags=0x380;asset.skeleton.bones[6].inheritParent=4;asset.skeleton.bones[6].inheritWeight=1;
    Bind(asset);VmdMotion motion;motion.id="eyes";motion.duration=1;
    auto rotation=glm::angleAxis(.4,glm::dvec3(0,1,0));motion.bones={Move("両目",{2,0,0},rotation),Move("half",{.3,0,0})};
    std::string error;auto clip=BakeVmdAnimation(motion,asset,{},"Append",nullptr,&error);
    if(!Expect(clip.has_value(),"PMX append dependency graph bakes successfully")){std::puts(error.c_str());return;}
    auto pose=EvaluateSkeletalPose(asset,&*clip,1,false);
    Expect(Near(pose.local[2].rotation,rotation)&&Near(pose.local[3].rotation,rotation),"Both eyes receive the animated shared-eyes grant");
    Expect(Near(pose.local[4].translation,{1.3,0,0})&&Near(pose.local[5].translation,{1,0,0})&&Near(pose.local[6].translation,{.3,0,0}),"Nonlocal grants propagate grant delta while local grants use own animated delta");
    Expect(Near(pose.local[5].rotation,glm::angleAxis(.2,glm::dvec3(0,1,0)))&&Near(pose.local[6].rotation,glm::dquat(1,0,0,0)),"Local / nonlocal rotation grants preserve their different PMX semantics");
    auto cycle=asset;cycle.skeleton.bones[1].sourceFlags=0x100;cycle.skeleton.bones[1].inheritParent=2;
    Expect(!BakeVmdAnimation(motion,cycle,{},"Cycle",nullptr,&error)&&error.find("Cycle")!=std::string::npos,"Cyclic append dependency fails without recursion or an infinite update");
    auto fixed=asset;fixed.skeleton.bones[1].sourceFlags=0x400;fixed.skeleton.bones[1].fixedAxis={1,0,0};
    auto fixedClip=BakeVmdAnimation(motion,fixed,{},"Fixed",nullptr,&error);
    Expect(fixedClip&&Near(EvaluateSkeletalPose(fixed,&*fixedClip,1,false).local[1].rotation,glm::dquat(1,0,0,0)),"Fixed-axis bone animation removes disallowed swing before baking");
}
void RetargetTests() {
    std::puts("Cross-skeleton animation baking:");auto source=LegFixture(),target=LegFixture();source.skeleton.id="source/skeleton";target.skeleton.id="target/skeleton";
    // Rearranged expression indices deliberately prevent accidental index copy.
    std::swap(target.morphs[0],target.morphs[1]);target.skeleton.bones[0].restLocal.translation={5,0,0};Bind(target);
    AnimationSequenceAsset input;input.id="source-motion";input.name="Source";input.skeletonId=source.skeleton.id;input.duration=1;
    BoneAnimationTrack root;root.bone=0;root.translations={{0,{0,0,0}},{1,{.5,0,0}}};input.boneTracks.push_back(root);
    MorphAnimationTrack expression;expression.morph=0;expression.keys={{0,0},{1,1}};input.morphTracks.push_back(expression);
    RetargetMap map;map.sourceSkeletonId=source.skeleton.id;map.targetSkeletonId=target.skeleton.id;map.translationScale=2;
    map.bones.push_back({"root",0,0,1,glm::dquat(1,0,0,0),"test root"});
    std::string error;AnimationBakeReport report;auto baked=BakeRetargetedAnimation(source,input,target,map,{},"Target",&report,&error);
    if(!Expect(baked.has_value(),"Source animation is resampled into a standalone target sequence")){std::puts(error.c_str());return;}
    const auto pose=EvaluateSkeletalPose(target,&*baked,1,false);
    Expect(baked->sourceSkeletonId==source.skeleton.id&&baked->skeletonId==target.skeleton.id&&baked->baked,"Retargeted sequence records both skeleton identities and baked state");
    Expect(Near(pose.local[0].translation,{6,0,0}),"Target rest offset plus scaled root motion is baked at the endpoint");
    Expect(pose.morphWeights[0]==0&&pose.morphWeights[1]==1,"Expressions remap by names across reordered target morph arrays");
    auto edited=map;edited.bones.front().basisCorrection=glm::angleAxis(.2,glm::dvec3(0,1,0));
    auto revised=BakeRetargetedAnimation(source,input,target,edited,{},"Target",nullptr,&error);
    Expect(revised&&revised->id!=baked->id,"Changing a retarget basis mapping produces a distinct animation identity");
    auto wrong=map;wrong.targetSkeletonId="wrong";
    Expect(!BakeRetargetedAnimation(source,input,target,wrong,{},"Wrong",nullptr,&error),"Cross-skeleton baking rejects mismatched target identity");
    wrong=map;wrong.bones.push_back(map.bones.front());
    Expect(!BakeRetargetedAnimation(source,input,target,wrong,{},"Duplicate",nullptr,&error),"Cross-skeleton baking rejects duplicate source or target assignments");
}
SkeletalAssetPackage FlatFixture(std::size_t bones,std::size_t morphs) {
    SkeletalAssetPackage result;result.id="long-fixture";result.name="Long animation fixture";result.skeleton.id="long-fixture/skeleton";
    for(std::size_t b=0;b<bones;++b)Bone(result,"bone"+std::to_string(b),-1,{b*.01,(b%7)*.02,0});
    for(std::size_t m=0;m<morphs;++m){MorphTarget morph;morph.name="expression"+std::to_string(m);result.morphs.push_back(std::move(morph));}
    Bind(result);return result;
}
const BoneAnimationTrack* BoneTrack(const AnimationSequenceAsset& sequence,std::uint32_t bone) {
    for(const auto& track:sequence.boneTracks)if(track.bone==bone)return &track;return nullptr;
}
const MorphAnimationTrack* MorphTrack(const AnimationSequenceAsset& sequence,std::uint32_t morph) {
    for(const auto& track:sequence.morphTracks)if(track.morph==morph)return &track;return nullptr;
}
void LongBakeTests() {
    std::puts("Long animation bounded, lossless channel collection:");
    auto target=FlatFixture(186,57);VmdMotion motion;motion.id="long-synthetic";motion.duration=3451/30.0;
    const auto constantRotation=glm::angleAxis(.31,glm::normalize(glm::dvec3(1,2,3)));
    // One authored key has an exactly constant sampled value. Two equal,
    // nonidentity quaternion keys can acquire a one-ULP interpolation change;
    // the lossless collector intentionally preserves such a real difference.
    VmdBoneTrack constant;constant.name="bone0";VmdBoneKey initial;initial.translation={.25,-.375,.125};initial.rotation=constantRotation;
    constant.keys={initial};motion.bones.push_back(constant);
    VmdBoneTrack late;late.name="bone1";VmdBoneKey still,last;still.frame=3449;last.frame=3451;last.translation={.125,.25,-.375};late.keys={VmdBoneKey{},still,last};motion.bones.push_back(late);
    VmdBoneTrack rotating;rotating.name="bone2";last.translation={0,0,0};last.rotation=-glm::angleAxis(.7,glm::dvec3(0,1,0));rotating.keys={VmdBoneKey{},still,last};motion.bones.push_back(rotating);
    VmdMorphTrack expression;expression.name="expression0";expression.keys={{0,0},{3449,0},{3451,.75}};motion.morphs.push_back(expression);
    VmdMorphTrack held;held.name="expression1";held.keys={{0,.375},{3451,.375}};motion.morphs.push_back(held);
    AnimationBakeReport report;std::string error;
    auto clip=BakeVmdAnimation(motion,target,{},"Long",&report,&error);
    if(!Expect(clip.has_value(),"186 bones / 57 expressions bake 115.033 seconds at the requested full 30 FPS")){std::puts(error.c_str());return;}
    Expect(report.samples==3452&&clip->framesPerSecond==30&&clip->duration==motion.duration,"Long bake keeps all 3452 sampling times and its exact final endpoint");
    const auto constantTrack=BoneTrack(*clip,0),lateTrack=BoneTrack(*clip,1),rotationTrack=BoneTrack(*clip,2);
    const auto face=MorphTrack(*clip,0),heldFace=MorphTrack(*clip,1);
    std::printf("  Long keys: constant T/R/S=%zu/%zu/%zu; late T=%zu; rotating R=%zu; expression=%zu; held=%zu; tracks=%zu+%zu\n",
        constantTrack?constantTrack->translations.size():0,constantTrack?constantTrack->rotations.size():0,constantTrack?constantTrack->scales.size():0,
        lateTrack?lateTrack->translations.size():0,rotationTrack?rotationTrack->rotations.size():0,face?face->keys.size():0,heldFace?heldFace->keys.size():0,
        clip->boneTracks.size(),clip->morphTracks.size());
    const bool layout=constantTrack&&constantTrack->translations.size()==1&&constantTrack->rotations.size()==1&&constantTrack->scales.empty()
        &&lateTrack&&lateTrack->translations.size()==3452&&rotationTrack&&rotationTrack->rotations.size()==3452
        &&face&&face->keys.size()==3452&&heldFace&&heldFace->keys.size()==1&&clip->boneTracks.size()==3&&clip->morphTracks.size()==2;
    Expect(layout,"Constant rest channels disappear, constant nonrest channels retain one key, changing channels keep every sample");
    if(!layout)return;
    bool precise=true,continuous=true;
    for(std::size_t i=0;i<report.samples;++i) {
        const double time=std::min(motion.duration,i/30.0);const auto delta=SampleVmdBone(late,time),rotation=SampleVmdBone(rotating,time);
        precise&=lateTrack->translations[i].time==time&&lateTrack->translations[i].value==target.skeleton.bones[1].restLocal.translation+delta.translation;
        precise&=rotationTrack->rotations[i].time==time&&Near(rotationTrack->rotations[i].value,rotation.rotation,1e-13);
        precise&=face->keys[i].time==time&&face->keys[i].value==static_cast<double>(static_cast<float>(SampleVmdMorph(expression,time)));
        if(i)continuous&=glm::dot(rotationTrack->rotations[i-1].value,rotationTrack->rotations[i].value)>=0;
    }
    Expect(precise&&continuous,"Late first changes reconstruct the complete constant prefix without rounding, missing times or quaternion sign jumps");
    auto retarget=target;retarget.skeleton.id="long-target/skeleton";retarget.skeleton.bones[1].restLocal.translation.x+=.5;Bind(retarget);
    RetargetMap mapping;mapping.sourceSkeletonId=target.skeleton.id;mapping.targetSkeletonId=retarget.skeleton.id;
    for(int b=0;b<3;++b)mapping.bones.push_back({b==1?"hips":"root",b,b,1,glm::dquat(1,0,0,0),"long exact fixture"});
    auto remapped=BakeRetargetedAnimation(target,*clip,retarget,mapping,{},"Long remap",&report,&error);
    if(Expect(remapped&&report.samples==3452&&remapped->duration==motion.duration,"Retargeting uses the same bounded collector for the entire long sequence")) {
        bool matches=true;
        for(int frame:{0,1,1700,3448,3449,3450,3451}) {
            const double time=frame/30.0;const auto sourcePose=EvaluateSkeletalPose(target,&*clip,time,false);
            auto reference=RetargetPose(target.skeleton,sourcePose,retarget.skeleton,mapping);
            const auto actual=EvaluateSkeletalPose(retarget,&*remapped,time,false);
            for(std::size_t b=0;b<target.skeleton.bones.size();++b)matches&=Near(reference.local[b].translation,actual.local[b].translation,1e-12)
                &&Near(reference.local[b].rotation,actual.local[b].rotation,1e-12)&&Near(reference.local[b].scale,actual.local[b].scale,1e-12);
            matches&=actual.morphWeights[0]==sourcePose.morphWeights[0]&&actual.morphWeights[1]==sourcePose.morphWeights[1];
        }
        Expect(matches,"Long retarget playback agrees with direct retarget poses before, during and at the exact last frame");
    }else std::puts(error.c_str());
    for(const double invalidDuration:{-1.,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()}) {
        auto invalid=motion;invalid.duration=invalidDuration;
        Expect(!BakeVmdAnimation(invalid,target,{},"Invalid duration",nullptr,&error)&&error.find("duration")!=std::string::npos,"Negative / nonfinite declared duration is rejected before scanning or allocating samples");
    }
    auto excessiveWork=motion;excessiveWork.duration=2300;
    Expect(!BakeVmdAnimation(excessiveWork,target,{},"Work",nullptr,&error)&&error.find("evaluation budget")!=std::string::npos
        &&error.find("69001")!=std::string::npos&&error.find("243")!=std::string::npos,"Evaluation work budget rejects excessive work with required sample/channel counts");
    auto smallTarget=FlatFixture(1,0);smallTarget.skeleton.id="small-target/skeleton";
    auto sourceHeavy=*clip;sourceHeavy.duration=2300;
    auto smallMap=mapping;smallMap.targetSkeletonId=smallTarget.skeleton.id;smallMap.bones.resize(1);
    Expect(!BakeRetargetedAnimation(target,sourceHeavy,smallTarget,smallMap,{},"Source work",nullptr,&error)
        &&error.find("evaluation budget")!=std::string::npos&&error.find("source + target")!=std::string::npos,
        "Retarget work includes the full source skeleton even when the output target is small");
    auto excessiveSamples=motion;excessiveSamples.duration=AnimationLimits::MaximumSamples/30.0;
    Expect(!BakeVmdAnimation(excessiveSamples,target,{},"Samples",nullptr,&error)&&error.find("sample budget")!=std::string::npos,"Independent sample schedule cap still rejects huge durations without frame truncation");
}
void StoredKeyBudgetTest() {
    std::puts("Actual retained animation-key bound:");
    auto target=FlatFixture(1,20);VmdMotion motion;motion.id="stored-key-budget";motion.duration=4000;
    auto bone=Move("bone0",{1,0,0});bone.keys.back().frame=120000;motion.bones.push_back(std::move(bone));
    for(std::size_t m=0;m<target.morphs.size();++m){VmdMorphTrack track;track.name=target.morphs[m].name;track.keys={{0,0},{120000,1}};motion.morphs.push_back(std::move(track));}
    std::string error;auto result=BakeVmdAnimation(motion,target,{},"Too many changing keys",nullptr,&error);
    Expect(!result&&error.find("stored-key budget is 2000000")!=std::string::npos&&error.find("sample ")!=std::string::npos,
        "Actually varying channels stop before exceeding the unchanged two-million-key storage limit");
}
int FindBone(const SkeletalAssetPackage& asset,const std::string& name) {
    for(std::size_t i=0;i<asset.skeleton.bones.size();++i)if(asset.skeleton.bones[i].name==name)return static_cast<int>(i);return -1;
}
void RealMotionTest() {
    const std::filesystem::path folder=LR"(C:\Users\16620\Downloads\抬抬腿_by_哈基鹿鹿鹿_8d36aca8ba85b7e747b1be0bae26388d)";
    const auto mainFile=folder/L"taitui.vmd",morphFile=folder/L"taitui表情.vmd";
    if(!std::filesystem::exists(mainFile)){std::puts("Real local VMD fixture unavailable; portable synthetic tests completed.");return;}
    std::puts("Real Mahiro / taitui VMD fixture:");std::string error;
    auto asset=LoadSkeletalAssetPackage(std::filesystem::path(MODEL_SOURCE_DIR)/"Asset/Generated/MahiroMaid",&error);
    if(!asset)asset=ImportSkeletalSource(LR"(C:\Users\16620\Downloads\真寻酱（女仆装）_by_Sa（艾拉漫剪）_07df9bce2f9eeb31bc903e14f16e8582\真寻酱（女仆装）\真寻酱（女仆装）.pmx)",{},&error);
    if(!Expect(static_cast<bool>(asset),"Real Mahiro skeleton loads for local integration test")){std::puts(error.c_str());return;}
    auto motion=LoadVmdMotion(mainFile,&error),morph=LoadVmdMotion(morphFile,&error);
    if(!Expect(motion.has_value()&&morph.has_value(),"User's body and expression VMD files parse successfully")){std::puts(error.c_str());return;}
    auto merged=MergeVmdMotions({*motion,*morph},"Taitui",&error);AnimationBakeReport report;
    auto clip=merged?BakeVmdAnimation(*merged,*asset,{},"Taitui Mahiro",&report,&error):std::nullopt;
    if(!Expect(clip.has_value(),"Merged real motion automatically bakes onto the Mahiro target")){std::puts(error.c_str());return;}
    Expect(clip->duration>9&&report.matchedBones>=50&&report.matchedMorphs>0,"Real bake retains duration, over fifty matched bones and expression animation");
    const int leftFoot=FindBone(*asset,"左足首"),rightFoot=FindBone(*asset,"右足首"),leftArm=FindBone(*asset,"左腕"),rightArm=FindBone(*asset,"右腕");
    auto rest=EvaluateSkeletalPose(*asset,nullptr,0);double footMovement=0,armMovement=0,morphActivity=0,lengthError=0;bool finite=true;
    for(int frame=0;frame<=276;frame+=7) {
        auto pose=EvaluateSkeletalPose(*asset,&*clip,frame/30.0,false);
        for(std::size_t b=0;b<pose.local.size();++b) {
            const auto p=pose.global[b][3];finite&=std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z);
            const auto& name=asset->skeleton.bones[b].name;
            if(name=="左ひざ"||name=="右ひざ"||name=="左足首"||name=="右足首")
                lengthError=std::max(lengthError,std::abs(glm::length(pose.local[b].translation)-glm::length(rest.local[b].translation)));
        }
        for(int b:{leftFoot,rightFoot})if(b>=0)footMovement=std::max(footMovement,glm::length(glm::dvec3(pose.global[b][3]-rest.global[b][3])));
        for(int b:{leftArm,rightArm})if(b>=0)armMovement=std::max(armMovement,1-std::abs(glm::dot(pose.local[b].rotation,rest.local[b].rotation)));
        for(float weight:pose.morphWeights)morphActivity=std::max(morphActivity,static_cast<double>(std::abs(weight)));
    }
    Expect(finite&&footMovement>.5&&armMovement>.001,"Ordinary clip playback visibly animates both leg and arm pose without runtime IK");
    Expect(lengthError<1e-5&&morphActivity>.1,"Real motion preserves untranslating knee / ankle segments and animates facial weights");
    std::set<int> mapped;bool unique=true;for(const auto& match:report.matches)if(!match.morph&&match.target>=0)unique&=mapped.insert(match.target).second;
    Expect(unique,"Real VMD bone assignments are one-to-one and diagnostics retain unmatched helpers");
    std::printf("  duration %.3fs; samples %zu; bones %zu matched / %zu unmatched; morphs %zu / %zu; tracks %zu + %zu; foot movement %.4f; max length error %.8g\n",
        clip->duration,report.samples,report.matchedBones,report.unmatchedBones,report.matchedMorphs,report.unmatchedMorphs,clip->boneTracks.size(),clip->morphTracks.size(),footMovement,lengthError);
}
}
int main() {
    CoreBakeTests();AppendTests();RetargetTests();LongBakeTests();StoredKeyBudgetTest();RealMotionTest();
    std::printf("Animation baking: %s (%d failures)\n",failures?"FAILED":"PASSED",failures);return failures?1:0;
}
