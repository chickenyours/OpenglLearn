#include "Model/Public/skeletal_asset.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <set>
#include <string>

namespace {
using namespace Model;
int failures=0;
bool Expect(bool condition,const char* description) {
    std::printf("  %-78s %s\n",description,condition?"ok":"FAIL");failures+=!condition;return condition;
}
bool Near(glm::dvec3 a,glm::dvec3 b,double tolerance=1e-7){return glm::length(a-b)<tolerance;}
bool Near(glm::dmat4 a,glm::dmat4 b,double tolerance=1e-7) {
    for(int c=0;c<4;++c)for(int r=0;r<4;++r)if(std::abs(a[c][r]-b[c][r])>tolerance)return false;return true;
}
int FindRole(const IkRigAsset& rig,const std::string& role) {
    for(const auto& m:rig.humanoid)if(m.role==role)return m.bone;return -1;
}
void Bind(SkeletonAsset& skeleton) {
    const auto globals=GlobalBoneTransforms(skeleton,RestPose(skeleton));
    for(std::size_t i=0;i<skeleton.bones.size();++i)skeleton.bones[i].inverseBind=glm::inverse(globals[i]);
}
int Bone(SkeletonAsset& skeleton,std::string name,int parent,glm::dvec3 translation,std::string english={}) {
    SkeletonBone bone;bone.name=std::move(name);bone.englishName=std::move(english);bone.parent=parent;
    bone.restLocal.translation=translation;skeleton.bones.push_back(bone);return static_cast<int>(skeleton.bones.size()-1);
}
SkeletalAssetPackage Fixture() {
    SkeletalAssetPackage asset;asset.id="test";asset.skeleton.id="test/skeleton";asset.skeleton.name="test rig";
    // Deliberately not topological: the tip references a later parent.
    Bone(asset.skeleton,"tip",2,{0,1,0});
    Bone(asset.skeleton,"root",-1,{2,0,0});
    Bone(asset.skeleton,"joint",1,{0,1,0});Bind(asset.skeleton);
    asset.geometry=std::make_shared<StaticModel>();asset.geometry->meshes.resize(1);auto& mesh=asset.geometry->meshes[0];
    StaticVertex vertex;vertex.position={2,2,0};vertex.normal={0,1,0};vertex.tangent={1,0,0,1};mesh.vertices.push_back(vertex);
    mesh.bounds.Include(vertex.position);asset.geometry->instances.push_back({"test",0,glm::dmat4(1)});
    MeshSkin skin;VertexSkin weights;weights.joints={0,0,0,0};weights.weights={1,0,0,0};skin.vertices.push_back(weights);
    asset.skins.push_back(skin);MorphTarget morph;morph.name="move";morph.deltas.push_back({0,0,{1,0,0},{0,0,0}});asset.morphs.push_back(morph);
    return asset;
}
void RuntimeTests() {
    std::puts("Skeletal runtime:");auto asset=Fixture();
    auto rest=EvaluateSkeletalPose(asset,nullptr,0);Expect(Near(glm::dvec3(rest.global[0][3]),{2,2,0}),"FK resolves a parent with a greater bone index");
    StaticModel skinned;std::string error;
    Expect(SkinSkeletalMesh(asset,rest,skinned,&error)&&Near(skinned.meshes[0].vertices[0].position,{2,2,0}),"Bind pose skinning does not move geometry");
    AnimationSequenceAsset sequence;sequence.duration=2;sequence.skeletonId=asset.skeleton.id;
    BoneAnimationTrack track;track.bone=2;track.rotations={{0,{1,0,0,0}},{2,glm::angleAxis(glm::pi<double>(),glm::dvec3(0,0,1))}};
    track.translations={{0,{0,1,0}},{2,{0,3,0}}};sequence.boneTracks.push_back(track);
    MorphAnimationTrack morph;morph.morph=0;morph.keys={{0,0},{2,1}};sequence.morphTracks.push_back(morph);
    auto half=EvaluateSkeletalPose(asset,&sequence,1,false);
    Expect(Near(half.local[2].rotation*glm::dvec3(1,0,0),{0,1,0})&&Near(half.local[2].translation,{0,2,0}),"Animation interpolates shortest quaternion arc and TRS values");
    Expect(std::abs(half.morphWeights[0]-0.5f)<1e-7,"Morph curve interpolation uses the same sequence time");
    auto wrapped=EvaluateSkeletalPose(asset,&sequence,3,true),negative=EvaluateSkeletalPose(asset,&sequence,-1,true);
    Expect(Near(wrapped.global[0],half.global[0])&&Near(negative.global[0],half.global[0]),"Positive and negative looping times wrap consistently");
    Expect(SkinSkeletalMesh(asset,half,skinned,&error)&&Near(skinned.meshes[0].vertices[0].position,{1,2.5,0}),"Morph displacement is applied before bone skinning");
    Expect(glm::length(skinned.meshes[0].vertices[0].normal-glm::vec3(-1,0,0))<1e-6,"Skinned normals follow bone rotation");
    auto scaled=rest;scaled.local[0].scale={2,3,1};scaled.global=GlobalBoneTransforms(asset.skeleton,scaled.local);
    asset.geometry->meshes[0].vertices[0].normal=glm::normalize(glm::vec3(1,1,0));
    Expect(SkinSkeletalMesh(asset,scaled,skinned,&error)&&glm::length(skinned.meshes[0].vertices[0].normal-glm::normalize(glm::vec3(0.5f,1.0f/3,0)))<1e-6,"Nonuniform scale uses inverse-transpose normals");
    // A mesh with a translated node must also stay still in its bind pose.
    auto nodeAsset=Fixture();nodeAsset.skins[0].bindTransform=glm::translate(glm::dmat4(1),glm::dvec3(4,0,0));
    nodeAsset.skins[0].boneFromMesh.resize(3);
    for(int i=0;i<3;++i)nodeAsset.skins[0].boneFromMesh[i]=nodeAsset.skeleton.bones[i].inverseBind*nodeAsset.skins[0].bindTransform;
    Expect(SkinSkeletalMesh(nodeAsset,rest,skinned,&error)&&Near(skinned.meshes[0].vertices[0].position,{2,2,0}),"Mesh-specific offsets preserve a translated mesh node at bind");
    auto fullAsset=Fixture();fullAsset.skeleton.bones.clear();
    for(int i=0;i<6;++i)Bone(fullAsset.skeleton,"influence "+std::to_string(i),-1,{0,0,0});
    Bind(fullAsset.skeleton);auto& fullSkin=fullAsset.skins[0];
    fullSkin.vertices[0].joints={0,1,2,3};fullSkin.vertices[0].weights=glm::vec4(.25f);
    fullSkin.influenceOffsets={0,6};
    for(std::uint32_t i=0;i<6;++i)fullSkin.influences.push_back({i,1.f/6});
    auto fullPose=EvaluateSkeletalPose(fullAsset,nullptr,0);fullPose.local[4].translation={6,0,0};fullPose.local[5].translation={0,12,0};
    fullPose.global=GlobalBoneTransforms(fullAsset.skeleton,fullPose.local);
    Expect(SkinSkeletalMesh(fullAsset,fullPose,skinned,&error)&&Near(skinned.meshes[0].vertices[0].position,{3,4,0}),
        "CPU skinning retains fifth and sixth joint motion instead of the top-four compatibility summary");
    fullSkin.influenceOffsets={0,7};
    Expect(!SkinSkeletalMesh(fullAsset,fullPose,skinned,&error)&&!error.empty(),"CPU skinning rejects full influence ranges beyond their storage");
    fullSkin.influenceOffsets={1,6};
    Expect(!SkinSkeletalMesh(fullAsset,fullPose,skinned,&error),"CPU skinning rejects malformed nonzero initial influence offsets");
    fullSkin.influenceOffsets.clear();fullSkin.influences.clear();
    Expect(SkinSkeletalMesh(fullAsset,fullPose,skinned,&error)&&Near(skinned.meshes[0].vertices[0].position,{2,2,0}),
        "Legacy four-joint assets retain their original deformation when full influences are absent");
    auto topology=Fixture();const auto vertex=topology.geometry->meshes[0].vertices.front();const auto weight=topology.skins[0].vertices.front();
    topology.geometry->meshes[0].vertices={vertex,vertex,vertex};topology.skins[0].vertices={weight,weight,weight};
    topology.geometry->meshes[0].indices={0,1,2};SkinSkeletalMesh(topology,rest,skinned,&error);
    topology.geometry->meshes[0].indices={2,1,0};
    Expect(SkinSkeletalMesh(topology,rest,skinned,&error)&&skinned.meshes[0].indices==topology.geometry->meshes[0].indices,"Reused CPU output updates a changed topology with the same index count");
    SkeletonAsset deep;deep.bones.resize(12000);
    for(std::size_t i=0;i<deep.bones.size();++i){deep.bones[i].parent=i+1<deep.bones.size()?static_cast<int>(i+1):-1;deep.bones[i].restLocal.translation={0,0.01,0};}
    const auto deepGlobal=GlobalBoneTransforms(deep,RestPose(deep));
    Expect(std::abs(deepGlobal[0][3].y-120)<1e-7,"Deep reversed scene hierarchies evaluate without recursive stack overflow");
}
void IkTests() {
    std::puts("IK:");SkeletonAsset skeleton;Bone(skeleton,"root",-1,{0,0,0});Bone(skeleton,"elbow",0,{1,0,0});Bone(skeleton,"hand",1,{1,0,0});Bind(skeleton);
    SkeletalPose pose;pose.local=RestPose(skeleton);pose.global=GlobalBoneTransforms(skeleton,pose.local);
    IkChain chain;chain.root=0;chain.effector=2;chain.iterations=100;chain.angleLimit=0.3;
    chain.links={{1,false,glm::dvec3(0),glm::dvec3(0)},{0,false,glm::dvec3(0),glm::dvec3(0)}};
    const glm::dvec3 target(1,1,0);const double before=glm::length(glm::dvec3(pose.global[2][3])-target);std::string error;
    Expect(SolveIk(skeleton,chain,target,pose,&error)&&glm::length(glm::dvec3(pose.global[2][3])-target)<before*0.01,"CCD reaches an attainable hand target");
    pose.local=RestPose(skeleton);pose.global=GlobalBoneTransforms(skeleton,pose.local);
    chain.links[0].limited=true;chain.links[0].minimum={0,0,0};chain.links[0].maximum={0,0,0.1};
    Expect(SolveIk(skeleton,chain,{0,1,0},pose,&error)&&std::abs(glm::eulerAngles(pose.local[1].rotation).z)<=0.100001,"CCD honors authored local joint angle limits");
    pose.local=RestPose(skeleton);pose.global=GlobalBoneTransforms(skeleton,pose.local);chain.links[0].limited=false;
    Expect(SolveIk(skeleton,chain,{-1,0,0},pose,&error)&&std::isfinite(pose.global[2][3].x),"Antiparallel CCD directions produce a finite pose");
    chain.effector=900;Expect(!SolveIk(skeleton,chain,target,pose,&error)&&!error.empty(),"IK rejects invalid indices with a diagnostic");
}
SkeletonAsset Humanoid(double scale=1,bool angled=false) {
    SkeletonAsset skeleton;skeleton.id=angled?"target":"source";skeleton.name="test humanoid";
    Bone(skeleton,"root",-1,{0,0,0});Bone(skeleton,"hips",0,{0,scale,0});Bone(skeleton,"spine",1,{0,0.25*scale,0});
    Bone(skeleton,"chest",2,{0,0.25*scale,0});Bone(skeleton,"upperChest",3,{0,0.1*scale,0});Bone(skeleton,"neck",4,{0,0.15*scale,0});Bone(skeleton,"head",5,{0,0.15*scale,0});
    for(bool left:{true,false}) {
        const std::string side=left?"left":"right";const double sign=left?1:-1;
        const int upperLeg=Bone(skeleton,side+"UpperLeg",1,{sign*0.15*scale,-0.1*scale,0});
        const int lowerLeg=Bone(skeleton,side+"LowerLeg",upperLeg,{0,-0.4*scale,0});
        const int foot=Bone(skeleton,side+"Foot",lowerLeg,{0,-0.4*scale,0});Bone(skeleton,side+"Toes",foot,{0,0,0.1*scale});
        const int shoulder=Bone(skeleton,side+"Shoulder",4,{sign*0.1*scale,0,0});
        const int arm=Bone(skeleton,side+"UpperArm",shoulder,{sign*0.1*scale,0,0});
        const int elbow=Bone(skeleton,side+"LowerArm",arm,{sign*0.3*scale,angled?-0.2*scale:0,0});
        Bone(skeleton,side+"Hand",elbow,{sign*0.3*scale,angled?-0.2*scale:0,0});
    }
    Bind(skeleton);return skeleton;
}
void SourcePosePreviewTests(){
    std::puts("Procedural preview preserves authored source-pose garment fit:");
    SkeletalAssetPackage asset;asset.id="source-pose-fixture";asset.skeleton=Humanoid();asset.ikRig=RecognizeHumanoidRig(asset.skeleton);
    asset.morphs.resize(2);asset.morphs[0].name="Smile";asset.morphs[1].name="Blink";
    auto legacy=asset;AddGeneratedPreviewSequences(legacy);
    const auto legacyPreview=std::find_if(legacy.animations.begin(),legacy.animations.end(),[](const auto& value){return value.name=="RigPreview";});
    const auto legacyStart=EvaluateSkeletalPose(legacy,&*legacyPreview,0,false);bool legacyUnchanged=true;
    for(std::size_t i=0;i<legacy.skeleton.bones.size();++i)legacyUnchanged&=Near(TransformMatrix(legacyStart.local[i]),TransformMatrix(legacy.skeleton.bones[i].restLocal));
    Expect(legacyUnchanged&&legacyStart.morphWeights==std::vector<float>({0,0}),"Assets without SourcePose still begin the procedural preview in their original rest pose");
    const int hips=FindRole(asset.ikRig,"hips"),foot=FindRole(asset.ikRig,"leftFoot"),arm=FindRole(asset.ikRig,"leftUpperArm");
    AnimationSequenceAsset source;source.id="source-pose-fixture/SourcePose";source.skeletonId=asset.skeleton.id;source.name="SourcePose";source.generated=true;source.duration=1;
    for(const int bone:{hips,foot,arm}){BoneAnimationTrack track;track.bone=bone;const auto& rest=asset.skeleton.bones[bone].restLocal;
        track.translations={{0,rest.translation+glm::dvec3(.03,.02,-.01)}};
        track.rotations={{0,rest.rotation*glm::angleAxis(bone==foot?.42:bone==hips?.18:.65,glm::normalize(glm::dvec3(1,1,0)))}};
        track.scales={{0,{.95,1.03,1.01}}};source.boneTracks.push_back(std::move(track));}
    source.morphTracks={MorphAnimationTrack{0,{{0,.35}}},MorphAnimationTrack{1,{{0,.2}}}};asset.animations.push_back(source);
    const auto baseline=EvaluateSkeletalPose(asset,&asset.animations[0],0,false);AddGeneratedPreviewSequences(asset);
    const auto preview=std::find_if(asset.animations.begin(),asset.animations.end(),[](const auto& value){return value.name=="RigPreview";});
    if(!Expect(preview!=asset.animations.end(),"A source-pose character receives a procedural preview"))return;
    std::set<std::uint32_t> bones,morphs;bool unique=true;for(const auto& track:preview->boneTracks)unique&=bones.insert(track.bone).second;for(const auto& track:preview->morphTracks)unique&=morphs.insert(track.morph).second;
    Expect(unique&&bones.size()==asset.skeleton.bones.size(),"Procedural motion replaces baseline tracks without duplicate bone or expression tracks");
    bool fit=true,expression=true;
    for(const double time:{0.,1.,1.78,3.5}){const auto pose=EvaluateSkeletalPose(asset,&*preview,time,false);
        fit&=Near(TransformMatrix(pose.local[hips]),TransformMatrix(baseline.local[hips]))&&Near(TransformMatrix(pose.local[foot]),TransformMatrix(baseline.local[foot]))&&Near(pose.global[foot],baseline.global[foot]);
        expression&=std::abs(pose.morphWeights[0]-.35f)<1e-7;}
    Expect(fit,"Posed hips and heel rotations, translations and scales remain fixed throughout the inspection motion");
    Expect(expression,"Non-blink authored expression defaults survive the complete preview");
    const auto moving=EvaluateSkeletalPose(asset,&*preview,1,false);auto expected=baseline.local[arm];expected.rotation*=glm::angleAxis(-.13,glm::dvec3(0,0,1));
    Expect(Near(TransformMatrix(moving.local[arm]),TransformMatrix(expected))&&!Near(TransformMatrix(moving.local[arm]),TransformMatrix(baseline.local[arm])),
        "The animated arm rotates relative to SourcePose while preserving its authored translation and scale");
    const auto start=EvaluateSkeletalPose(asset,&*preview,0,false),blink=EvaluateSkeletalPose(asset,&*preview,1.78,false),after=EvaluateSkeletalPose(asset,&*preview,3.5,false);
    Expect(std::abs(start.morphWeights[1]-.2f)<1e-7&&std::abs(blink.morphWeights[1]-1)<1e-7&&std::abs(after.morphWeights[1]-.2f)<1e-7,
        "Blink overlays replace their baseline curve and return to the authored nonzero default");
    const auto count=asset.animations.size();AddGeneratedPreviewSequences(asset);Expect(asset.animations.size()==count,"Source-relative preview creation remains idempotent");
    auto shoes=source;shoes.name="ShoesFit";shoes.id=asset.id+"/ShoesFit";const auto toe=FindRole(asset.ikRig,"leftToes");
    BoneAnimationTrack fittedToe;fittedToe.bone=toe;fittedToe.rotations={{0,asset.skeleton.bones[toe].restLocal.rotation*glm::angleAxis(.2,glm::dvec3(1,0,0))}};
    fittedToe.scales={{0,{.8,.8,.8}}};shoes.boneTracks.push_back(fittedToe);asset.animations.push_back(shoes);
    AnimationSequenceAsset authored;authored.name="RigPreview";authored.id=asset.id+"/authored-inspection";authored.skeletonId=asset.skeleton.id;authored.duration=1;
    BoneAnimationTrack authoredTrack;authoredTrack.bone=foot;authoredTrack.translations={{0,{7,8,9}}};authored.boneTracks.push_back(authoredTrack);asset.animations.push_back(authored);
    const auto withProfiles=asset.animations.size();AddGeneratedPreviewSequences(asset);
    auto regenerated=std::find_if(asset.animations.begin(),asset.animations.end(),[](const auto& value){return value.name=="RigPreview"&&value.generated;});
    auto fitted=EvaluateSkeletalPose(asset,&*regenerated,1,false),shoeBaseline=EvaluateSkeletalPose(asset,&shoes,0,false);
    Expect(asset.animations.size()==withProfiles&&Near(TransformMatrix(fitted.local[toe]),TransformMatrix(shoeBaseline.local[toe]))&&regenerated->source.find("ShoesFit")!=std::string::npos,
        "Applying ShoesFit regenerates an existing procedural preview relative to the explicit wearing pose");
    const auto kept=std::find_if(asset.animations.begin(),asset.animations.end(),[&](const auto& value){return value.id==authored.id;});
    Expect(kept!=asset.animations.end()&&!kept->generated&&Near(kept->boneTracks[0].translations[0].value,{7,8,9}),"Refreshing the generated preview preserves an authored sequence with the same display name");
    asset.animations.erase(std::remove_if(asset.animations.begin(),asset.animations.end(),[](const auto& value){return value.name=="ShoesFit"&&value.generated;}),asset.animations.end());AddGeneratedPreviewSequences(asset);
    regenerated=std::find_if(asset.animations.begin(),asset.animations.end(),[](const auto& value){return value.name=="RigPreview"&&value.generated;});fitted=EvaluateSkeletalPose(asset,&*regenerated,1,false);
    Expect(Near(TransformMatrix(fitted.local[toe]),TransformMatrix(baseline.local[toe]))&&regenerated->source.find("SourcePose")!=std::string::npos,
        "Removing the wearing profile refreshes the procedural preview back to its unmodified SourcePose baseline");
}
void RecognitionAndRetargetTests() {
    std::puts("Humanoid recognition and retarget:");auto source=Humanoid();
    Bone(source,"leftUpperArmTwist",0,{0,0,0});Bone(source,"左足ＩＫ",0,{0,0,0});
    auto sourceRig=RecognizeHumanoidRig(source);const int arm=FindRole(sourceRig,"leftUpperArm");
    Expect(arm>=0&&source.bones[arm].name=="leftUpperArm","Exact semantic aliases outrank similarly named twist and IK bones");
    std::set<int> bones;for(const auto& mapping:sourceRig.humanoid)bones.insert(mapping.bone);
    Expect(bones.size()==sourceRig.humanoid.size(),"Humanoid role assignments are one-to-one");
    Expect(sourceRig.chains.size()==4,"Humanoid recognition creates both arm and leg IK chains");
    auto japanese=Humanoid();const auto english=RecognizeHumanoidRig(japanese);
    const int knee=FindRole(english,"leftLowerLeg"),hand=FindRole(english,"rightHand");
    japanese.bones[knee].name="左ひざ";japanese.bones[knee].englishName.clear();japanese.bones[hand].name="J_Bip_R_Hand";
    auto japaneseRig=RecognizeHumanoidRig(japanese);
    Expect(FindRole(japaneseRig,"leftLowerLeg")==knee&&FindRole(japaneseRig,"rightHand")==hand,"MMD Japanese and VRM prefixed names map to common semantic roles");
    auto unnamed=Humanoid();auto namedRig=RecognizeHumanoidRig(unnamed);int elbow=FindRole(namedRig,"leftLowerArm");
    unnamed.bones[elbow].name="joint_142";auto inferred=RecognizeHumanoidRig(unnamed);
    Expect(FindRole(inferred,"leftLowerArm")==elbow,"Unnamed limb joints are inferred between semantic hierarchy anchors");
    auto target=Humanoid(2,true);auto targetRig=RecognizeHumanoidRig(target);auto mapping=BuildRetargetMap(source,sourceRig,target,targetRig);
    Expect(std::abs(mapping.translationScale-2)<1e-7,"Retarget translation scale follows source and target leg lengths");
    SkeletalPose sourcePose;sourcePose.local=RestPose(source);sourcePose.global=GlobalBoneTransforms(source,sourcePose.local);
    auto output=RetargetPose(source,sourcePose,target,mapping);const auto targetRest=GlobalBoneTransforms(target,RestPose(target));
    bool restMatches=true;for(std::size_t i=0;i<targetRest.size();++i)restMatches&=Near(output.global[i],targetRest[i]);
    Expect(restMatches,"Retargeting bind pose preserves a target with different arm rest directions");
    sourcePose.local[0].translation={0.5,0,0};sourcePose.local[arm].rotation=glm::angleAxis(0.4,glm::dvec3(0,0,1));
    sourcePose.global=GlobalBoneTransforms(source,sourcePose.local);output=RetargetPose(source,sourcePose,target,mapping);
    const int targetArm=FindRole(targetRig,"leftUpperArm"),targetElbow=FindRole(targetRig,"leftLowerArm");
    Expect(Near(glm::dvec3(output.global[0][3]),{1,0,0}),"Retarget scales root motion without changing target rest lengths");
    const double restLength=glm::length(glm::dvec3(targetRest[targetElbow][3]-targetRest[targetArm][3]));
    Expect(std::abs(glm::length(glm::dvec3(output.global[targetElbow][3]-output.global[targetArm][3]))-restLength)<1e-7,"A/T pose retarget keeps target bone segment lengths");
    bool basisFound=false;for(const auto& m:mapping.bones)if(m.role=="leftUpperArm")basisFound=std::abs(m.basisCorrection.w)<0.999;
    Expect(basisFound,"Retarget map stores a nonidentity rest-direction basis correction");
    auto wrongRig=targetRig;wrongRig.skeletonId="another-skeleton";
    const auto rejected=BuildRetargetMap(source,sourceRig,target,wrongRig);
    Expect(rejected.bones.empty()&&!rejected.diagnostics.empty(),"Retarget refuses an IK rig referencing a different skeleton");
    auto turnedSource=Humanoid();
    // An FBX/VRM may carry a whole-character DCC orientation on its root.
    turnedSource.bones[0].restLocal.rotation=glm::angleAxis(glm::half_pi<double>(),glm::dvec3(0,1,0));Bind(turnedSource);
    const auto turnedRig=RecognizeHumanoidRig(turnedSource);
    const auto turnedMap=BuildRetargetMap(turnedSource,turnedRig,target,targetRig);
    SkeletalPose turnedPose;turnedPose.local=RestPose(turnedSource);turnedPose.local[0].translation={0.5,0,0};
    turnedPose.global=GlobalBoneTransforms(turnedSource,turnedPose.local);
    const auto aligned=RetargetPose(turnedSource,turnedPose,target,turnedMap);
    Expect(Near(glm::dvec3(aligned.global[0][3]),{0,0,1}),"DCC root facing rotated by 90 degrees aligns forward root motion");
}
void RealAssetTest() {
    const std::filesystem::path path=LR"(C:\Users\16620\Downloads\真寻酱（女仆装）_by_Sa（艾拉漫剪）_07df9bce2f9eeb31bc903e14f16e8582\真寻酱（女仆装）\真寻酱（女仆装）.pmx)";
    if(!std::filesystem::exists(path)){std::puts("Real PMX fixture unavailable on this machine; portable synthetic tests completed.");return;}
    std::puts("Real Mahiro PMX fixture:");std::string error;auto asset=ImportSkeletalSource(path,{},&error);
    if(!Expect(static_cast<bool>(asset),"Native PMX importer loads the real character")){std::printf("%s\n",error.c_str());return;}
    Expect(asset->skeleton.bones.size()==186&&asset->morphs.size()==57,"Real asset retains all 186 bones and 57 expressions");
    auto pose=EvaluateSkeletalPose(*asset,nullptr,0);StaticModel skinned;
    bool skinOk=SkinSkeletalMesh(*asset,pose,skinned,&error);double maximumError=0;
    if(skinOk)for(std::size_t m=0;m<skinned.meshes.size();++m)for(std::size_t v=0;v<skinned.meshes[m].vertices.size();++v)
        maximumError=std::max(maximumError,glm::length(skinned.meshes[m].vertices[v].position-asset->geometry->meshes[m].vertices[v].position));
    Expect(skinOk&&maximumError<1e-6,"Real character bind pose has no CPU skinning drift");
    const auto rig=RecognizeHumanoidRig(asset->skeleton);const auto leftArm=FindRole(rig,"leftUpperArm"),rightArm=FindRole(rig,"rightUpperArm");
    Expect(leftArm>=0&&rightArm>=0&&rig.humanoid.size()>=50,"MMD/VRM aliases recognize the real character body and fingers");
    AddGeneratedPreviewSequences(*asset);const std::size_t sequenceCount=asset->animations.size();AddGeneratedPreviewSequences(*asset);
    Expect(asset->animations.size()==sequenceCount,"Generated preview creation is idempotent");
    auto preview=std::find_if(asset->animations.begin(),asset->animations.end(),[](const auto& sequence){return sequence.generated&&sequence.name=="RigPreview";});
    bool moves=false;if(preview!=asset->animations.end()) {
        auto animation=EvaluateSkeletalPose(*asset,&*preview,1,false);
        for(std::size_t i=0;i<pose.global.size();++i)if(!Near(pose.global[i],animation.global[i],1e-5)){moves=true;break;}
    }
    Expect(moves&&preview->source.find("procedural")!=std::string::npos,"RigPreview visibly moves and is explicitly labeled procedural");
    auto native=std::count_if(asset->ikRig.chains.begin(),asset->ikRig.chains.end(),[](const auto& chain){return chain.native;});
    Expect(native==4,"Real asset retains all four authored PMX IK chains");
    std::printf("  recognized roles: %zu; bind maximum error: %.10g\n",rig.humanoid.size(),maximumError);
}
}
int main() {
    RuntimeTests();IkTests();SourcePosePreviewTests();RecognitionAndRetargetTests();RealAssetTest();
    std::printf("Skeletal runtime: %s (%d failures)\n",failures?"FAILED":"PASSED",failures);return failures?1:0;
}
