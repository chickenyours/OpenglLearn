#include "Model/Public/skeletal_asset.h"
#include <json/json.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace {
namespace fs=std::filesystem;
int failures=0;
bool Expect(bool value,const char* message){std::printf("%-85s %s\n",message,value?"ok":"FAIL");failures+=!value;return value;}
bool Near(const glm::dmat4& a,const glm::dmat4& b){for(unsigned c=0;c<4;++c)for(unsigned r=0;r<4;++r)if(std::abs(a[c][r]-b[c][r])>1e-11)return false;return true;}
void Write(const fs::path& path,const Json::Value& value){std::ofstream stream(path,std::ios::binary);Json::StreamWriterBuilder builder;stream<<Json::writeString(builder,value);}
Json::Value Read(const fs::path& path){std::ifstream stream(path,std::ios::binary);Json::Value result;stream>>result;return result;}
std::string Bytes(const fs::path& path){std::ifstream stream(path,std::ios::binary);return std::string(std::istreambuf_iterator<char>(stream),{});}
Model::SkeletalAssetPackage Fixture() {
    using namespace Model;SkeletalAssetPackage asset;asset.id="fit-profile-model";asset.name="FitProfileFixture";
    asset.sourceFormat="blend";asset.sourcePath="unavailable-model.blend";asset.skeleton.id="fit-profile-skeleton";
    asset.skeleton.name="Skeleton";SkeletonBone root;root.name="Root";
    SkeletonBone leg;leg.name="Leg";leg.parent=0;leg.restLocal.translation={0,1,0};
    SkeletonBone toe;toe.name="Toe";toe.parent=1;toe.restLocal.translation={0,-1,.2};asset.skeleton.bones={root,leg,toe};
    const auto global=GlobalBoneTransforms(asset.skeleton,RestPose(asset.skeleton));
    for(std::size_t i=0;i<global.size();++i)asset.skeleton.bones[i].inverseBind=glm::inverse(global[i]);
    asset.geometry=std::make_shared<StaticModel>();StaticMesh mesh;mesh.name="Panel";
    for(const auto point:{glm::dvec3(0,0,0),glm::dvec3(1,0,0),glm::dvec3(0,1,0)}){StaticVertex vertex;vertex.position=point;mesh.vertices.push_back(vertex);mesh.bounds.Include(point);}
    mesh.indices={0,1,2};asset.geometry->meshes.push_back(mesh);asset.geometry->bounds=mesh.bounds;
    asset.geometry->instances.push_back({"Panel",0,glm::dmat4(1)});asset.geometry->materials.emplace_back();
    TextureImage texture;texture.name="Pixel";texture.width=texture.height=1;texture.rgba={31,47,61,255};asset.geometry->textures.push_back(texture);
    MeshSkin skin;skin.vertices.resize(3);skin.influenceOffsets={0};
    for(unsigned i=0;i<3;++i){skin.influences.push_back({0,.6f});skin.influences.push_back({2,.4f});skin.influenceOffsets.push_back(static_cast<std::uint32_t>(skin.influences.size()));}
    for(const auto& bone:asset.skeleton.bones)skin.boneFromMesh.push_back(bone.inverseBind);asset.skins.push_back(skin);
    MorphTarget smile;smile.name="Smile";smile.deltas.push_back({0,0,{0,.1,0},{0,0,0}});MorphTarget blink;blink.name="Blink";asset.morphs={smile,blink};
    asset.ikRig.id="fixture-ik";asset.ikRig.name="IK";asset.ikRig.skeletonId=asset.skeleton.id;
    AnimationSequenceAsset source;source.id=asset.id+"/animations/SourcePose";source.name="SourcePose";source.skeletonId=asset.skeleton.id;
    source.generated=true;source.duration=1;source.framesPerSecond=30;source.source="captured-source-frame=0";
    for(std::uint32_t i=0;i<asset.skeleton.bones.size();++i) {
        auto local=asset.skeleton.bones[i].restLocal;
        if(i==1)local.rotation=glm::angleAxis(.3,glm::dvec3(0,1,0));
        if(i==2){local.rotation=glm::angleAxis(.1,glm::dvec3(1,0,0));local.scale={1.1,1.2,.9};}
        BoneAnimationTrack track;track.bone=i;track.translations={{0,local.translation}};track.rotations={{0,local.rotation}};track.scales={{0,local.scale}};source.boneTracks.push_back(track);
    }
    source.morphTracks={{0,{{0,.4}}},{1,{{0,0}}}};asset.animations.push_back(source);
    auto authored=source;authored.id="authored-motion";authored.name="AuthoredMotion";authored.generated=false;authored.source="source-motion.vmd";asset.animations.push_back(authored);
    return asset;
}
Json::Value Profile(const Model::SkeletalAssetPackage& asset) {
    Json::Value profile;profile["assetType"]="SkeletalFitProfile";profile["version"]=1;profile["sourceAssetId"]=asset.id;
    profile["baseClip"]="SourcePose";profile["name"]="ShoesFit";Json::Value correction;correction["bone"]="Toe";
    const auto delta=glm::angleAxis(.08,glm::dvec3(0,0,1));correction["localRotationDelta"]=Json::Value(Json::arrayValue);
    for(unsigned axis=0;axis<4;++axis)correction["localRotationDelta"].append(delta[axis]);
    for(const double component:{.01,0.,-.02})correction["localTranslationDelta"].append(component);
    for(const double component:{1.,.9,1.1})correction["localScaleMultiplier"].append(component);
    profile["boneCorrections"].append(correction);return profile;
}
}

int main() {
    using namespace Model;
    const auto directory=fs::path(MODEL_SOURCE_DIR)/"build/fit_profile_fixtures"/
        ("run-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        fs::create_directories(directory);const auto path=directory/"ShoesFit.profile.json";auto asset=Fixture();std::string error;
        const auto original=asset;const auto profile=Profile(asset);Write(path,profile);
        if(!Expect(SaveSkeletalAssetPackage(asset,directory/"Before",&error),"Save original complete-source-weight fixture before fitting"))throw std::runtime_error(error);
        const auto base=EvaluateSkeletalPose(asset,&asset.animations[0],0,false);
        if(!Expect(ApplySkeletalFitProfile(asset,path,&error),"Apply versioned profile only to its exact source model"))throw std::runtime_error(error);
        Expect(error.empty()&&asset.animations.size()==3,"Fit appends one fully constructed sequence and clears an earlier diagnostic");
        const auto& fit=asset.animations.back();const auto pose=EvaluateSkeletalPose(asset,&fit,.75,false);
        Expect(fit.generated&&fit.name=="ShoesFit"&&fit.id==asset.id+"/animations/ShoesFit"&&fit.duration==1&&
            fit.framesPerSecond==30&&fit.source.starts_with("fit-profile:"),"Fit clip records stable identity, profile provenance and constant generated status");
        bool full=fit.boneTracks.size()==asset.skeleton.bones.size();
        for(const auto& track:fit.boneTracks)full=full&&track.translations.size()==1&&track.rotations.size()==1&&track.scales.size()==1&&
            track.translations[0].time==0&&track.rotations[0].time==0&&track.scales[0].time==0;
        Expect(full,"Fitted sequence contains exactly one TRS sample per skeleton node");
        auto expected=base.local[2];expected.translation+=glm::dvec3(.01,0,-.02);
        expected.rotation=glm::normalize(expected.rotation*glm::angleAxis(.08,glm::dvec3(0,0,1)));expected.scale*=glm::dvec3(1,.9,1.1);
        Expect(Near(TransformMatrix(pose.local[2]),TransformMatrix(expected)),"Fit composes rotation in bone-local order, source-unit translation and positive scale");
        Expect(Near(TransformMatrix(pose.local[0]),TransformMatrix(base.local[0]))&&
            Near(TransformMatrix(pose.local[1]),TransformMatrix(base.local[1])),"Unspecified bones retain their exact source local pose");
        Expect(fit.morphTracks.size()==1&&fit.morphTracks[0].morph==0&&std::abs(pose.morphWeights[0]-.4f)<1e-7&&pose.morphWeights[1]==0,
            "Fit preserves nonzero source expression defaults and omits redundant zero curves");
        const auto late=EvaluateSkeletalPose(asset,&fit,100,false);
        Expect(Near(late.global[2],pose.global[2]),"Fitted wearing pose stays constant at the playback endpoint");
        Expect(SaveSkeletalAssetPackage(asset,directory/"After",&error),"Save fitted sequence alongside the original intermediate assets");
        Expect(Bytes(directory/"Before/SkeletalMeshes/model.skmesh.bin")==Bytes(directory/"After/SkeletalMeshes/model.skmesh.bin")&&
            Read(directory/"Before/SkeletalMeshes/model.skmesh.json")==Read(directory/"After/SkeletalMeshes/model.skmesh.json")&&
            Read(directory/"Before/Skeletons/model.skeleton.json")==Read(directory/"After/Skeletons/model.skeleton.json"),
            "Fitting leaves every geometry, full skin weight, texture, morph, rest and inverse-bind byte unchanged");
        const auto beforeManifest=Read(directory/"Before/Package.asset.json"),afterManifest=Read(directory/"After/Package.asset.json");
        Expect(Read(directory/"Before"/fs::u8path(beforeManifest["animations"][0].asString()))==
            Read(directory/"After"/fs::u8path(afterManifest["animations"][0].asString()))&&
            Read(directory/"Before"/fs::u8path(beforeManifest["animations"][1].asString()))==
            Read(directory/"After"/fs::u8path(afterManifest["animations"][1].asString())),
            "Original SourcePose and authored motion files remain byte-equivalent as JSON");
        const auto loaded=LoadSkeletalAssetPackage(directory/"After",&error);
        Expect(loaded&&loaded->animations.size()==3&&Near(EvaluateSkeletalPose(*loaded,&loaded->animations[2],0,false).global[2],pose.global[2]),
            "Fitted animation remains self-contained after package roundtrip");
        const auto fitGlobal=pose.global[2];Expect(ApplySkeletalFitProfile(asset,path,&error)&&asset.animations.size()==3&&
            Near(EvaluateSkeletalPose(asset,&asset.animations[2],0,false).global[2],fitGlobal),
            "Reapplying the same profile replaces its fit clip without accumulating corrections");
        auto rejected=[&](Json::Value invalid,const char* message) {
            Write(path,invalid);auto candidate=asset;const auto count=candidate.animations.size();
            const bool failed=!ApplySkeletalFitProfile(candidate,path,&error);
            Expect(failed&&!error.empty()&&candidate.animations.size()==count&&
                Near(EvaluateSkeletalPose(candidate,&candidate.animations[2],0,false).global[2],fitGlobal),message);
        };
        auto invalid=profile;invalid["version"]=2;rejected(invalid,"Reject unknown fit profile versions without changing an existing fit");
        invalid=profile;invalid["sourceAssetId"]="another-model";rejected(invalid,"Reject profiles for a different source asset even with matching skeleton names");
        invalid=profile;invalid["baseClip"]="BindPose";rejected(invalid,"Reject fit profiles based on a pose other than captured SourcePose");
        invalid=profile;invalid["name"]="SourcePose";rejected(invalid,"Refuse to replace the original source pose with a fitting clip");
        invalid=profile;invalid["name"]="AuthoredMotion";rejected(invalid,"Refuse collisions with existing authored animation names");
        invalid=profile;invalid["boneCorrections"]=Json::Value(Json::arrayValue);rejected(invalid,"Reject empty correction profiles");
        invalid=profile;invalid["boneCorrections"].append(invalid["boneCorrections"][0]);rejected(invalid,"Reject duplicate corrections for the same target bone atomically");
        invalid=profile;invalid["boneCorrections"][0]["bone"]="Missing";rejected(invalid,"Reject missing bone names instead of guessing a fitting target");
        invalid=profile;invalid["boneCorrections"][0].removeMember("localRotationDelta");rejected(invalid,"Require an explicit local xyzw rotation delta");
        invalid=profile;for(unsigned axis=0;axis<4;++axis)invalid["boneCorrections"][0]["localRotationDelta"][axis]=0;
        rejected(invalid,"Reject zero-length correction quaternions");
        invalid=profile;invalid["boneCorrections"][0]["localRotationDelta"][3]=2;rejected(invalid,"Reject rotation deltas outside the unit-quaternion tolerance");
        invalid=profile;invalid["boneCorrections"][0]["localRotationDelta"][0]=Json::Value();rejected(invalid,"Reject malformed rotation numbers before sequence publication");
        invalid=profile;invalid["boneCorrections"][0]["localRotationDelta"][0]=std::numeric_limits<double>::infinity();
        rejected(invalid,"Reject non-finite rotation data in the input profile");
        invalid=profile;invalid["boneCorrections"][0]["localTranslationDelta"][0]="wrong";rejected(invalid,"Reject nonnumeric translation corrections");
        invalid=profile;invalid["boneCorrections"][0]["localTranslationDelta"].resize(2);rejected(invalid,"Reject translation corrections with incorrect dimensions");
        invalid=profile;invalid["boneCorrections"][0]["localScaleMultiplier"][1]=0;rejected(invalid,"Reject zero scale multipliers");
        invalid=profile;invalid["boneCorrections"][0]["localScaleMultiplier"][1]=-.1;rejected(invalid,"Reject negative scale multipliers");
        invalid=profile;invalid["boneCorrections"][0]["localScaleMultiplier"][1]=std::numeric_limits<double>::max();
        rejected(invalid,"Reject finite input multipliers whose applied scale overflows");
        invalid=profile;invalid["boneCorrections"].resize(4);rejected(invalid,"Reject corrections exceeding the target skeleton budget");
        Write(path,profile);auto missingBase=original;missingBase.animations.erase(missingBase.animations.begin());
        Expect(!ApplySkeletalFitProfile(missingBase,path,&error)&&missingBase.animations.size()==1,"Reject models without captured SourcePose without appending a partial fit");
        auto ambiguous=original;ambiguous.skeleton.bones[1].name="Toe";
        Expect(!ApplySkeletalFitProfile(ambiguous,path,&error)&&ambiguous.animations.size()==2,"Reject ambiguous target bone names");
        auto animatedBase=original;animatedBase.animations[0].boneTracks[0].rotations.push_back({1,glm::dquat(1,0,0,0)});
        Expect(!ApplySkeletalFitProfile(animatedBase,path,&error)&&animatedBase.animations.size()==2,"Reject motion clips masquerading as a constant SourcePose");
        auto badBase=original;badBase.animations[0].boneTracks[0].translations[0].value.x=std::numeric_limits<double>::quiet_NaN();
        Expect(!ApplySkeletalFitProfile(badBase,path,&error)&&badBase.animations.size()==2,"Reject non-finite captured source data before fitting");
        auto cyclic=original;cyclic.skeleton.bones[0].parent=2;
        Expect(!ApplySkeletalFitProfile(cyclic,path,&error)&&cyclic.animations.size()==2,"Reject cyclic target skeletons without modifying animations");
        auto noFile=original;Expect(!ApplySkeletalFitProfile(noFile,{},&error)&&noFile.animations.size()==2,"Missing profile arguments fail atomically with an actionable diagnostic");
        auto optional=profile;optional["boneCorrections"][0].removeMember("localTranslationDelta");optional["boneCorrections"][0].removeMember("localScaleMultiplier");
        Write(path,optional);auto rotationOnly=original;
        Expect(ApplySkeletalFitProfile(rotationOnly,path,&error)&&
            EvaluateSkeletalPose(rotationOnly,&rotationOnly.animations.back(),0,false).local[2].translation==base.local[2].translation&&
            EvaluateSkeletalPose(rotationOnly,&rotationOnly.animations.back(),0,false).local[2].scale==base.local[2].scale,
            "Rotation-only profiles default to zero translation and identity scale multipliers");
        {
            auto morphProfile=profile;morphProfile["boneCorrections"]=Json::Value(Json::arrayValue);
            Json::Value delta;delta["mesh"]=0;delta["vertex"]=1;
            for(const double component:{1.,0.,0.})delta["expectedPosition"].append(component);
            for(const double component:{.05,-.02,.01})delta["positionDelta"].append(component);
            morphProfile["morphDeltas"].append(delta);Write(path,morphProfile);
            auto correctiveAsset=original;
            if(!Expect(ApplySkeletalFitProfile(correctiveAsset,path,&error),"Vertex-only profiles generate a derived corrective without bone changes"))throw std::runtime_error(error);
            Expect(correctiveAsset.morphs.size()==3&&correctiveAsset.morphs[2].name=="ShoesFit_Corrective"&&
                correctiveAsset.morphs[2].sourceType==1&&correctiveAsset.morphs[2].deltas.size()==1&&
                correctiveAsset.morphs[2].deltas[0].position==glm::dvec3(.05,-.02,.01)&&
                correctiveAsset.morphs[2].deltas[0].normal==glm::vec3(0),
                "Derived corrective stores guarded bind deltas and defaults optional normal changes to zero");
            auto correctivePose=EvaluateSkeletalPose(correctiveAsset,&correctiveAsset.animations[2],0,false);
            bool unchangedBones=true;
            for(std::size_t i=0;i<base.local.size();++i)unchangedBones=unchangedBones&&Near(TransformMatrix(base.local[i]),TransformMatrix(correctivePose.local[i]));
            Expect(unchangedBones&&correctivePose.morphWeights.size()==3&&correctivePose.morphWeights[2]==1&&
                std::abs(correctivePose.morphWeights[0]-.4f)<1e-7,"Corrective fit drives its own morph at one and preserves all original bone and expression defaults");
            const auto sourceAgain=EvaluateSkeletalPose(correctiveAsset,&correctiveAsset.animations[0],0,false);
            const auto authoredAgain=EvaluateSkeletalPose(correctiveAsset,&correctiveAsset.animations[1],0,false);
            Expect(sourceAgain.morphWeights[2]==0&&authoredAgain.morphWeights[2]==0,
                "Original SourcePose and source animations leave the derived corrective disabled by default");
            StaticModel sourceSurface,correctiveSurface,originalSurface;
            Expect(SkinSkeletalMesh(correctiveAsset,sourceAgain,sourceSurface,&error)&&
                SkinSkeletalMesh(correctiveAsset,correctivePose,correctiveSurface,&error)&&
                SkinSkeletalMesh(original,base,originalSurface,&error)&&
                glm::length(correctiveSurface.meshes[0].vertices[1].position-sourceSurface.meshes[0].vertices[1].position)>1e-4&&
                glm::length(originalSurface.meshes[0].vertices[1].position-sourceSurface.meshes[0].vertices[1].position)<1e-12,
                "Corrective affects real skinned vertices while unmodified SourcePose reproduces the original surface");
            Expect(correctiveAsset.geometry==original.geometry&&correctiveAsset.geometry->meshes[0].vertices[1].position==glm::dvec3(1,0,0)&&
                correctiveAsset.skins[0].influenceOffsets==original.skins[0].influenceOffsets&&
                correctiveAsset.skins[0].influences[1].weight==original.skins[0].influences[1].weight&&
                correctiveAsset.morphs[0].deltas[0].position==original.morphs[0].deltas[0].position,
                "Corrective fitting preserves original bind geometry, complete skin data and source morph deltas");
            Expect(ApplySkeletalFitProfile(correctiveAsset,path,&error)&&correctiveAsset.morphs.size()==3&&
                correctiveAsset.animations.size()==3&&correctiveAsset.morphs[2].deltas[0].position==glm::dvec3(.05,-.02,.01),
                "Reapplying a corrective replaces its identified derived morph without duplication or accumulation");
            auto replaced=morphProfile;replaced["morphDeltas"][0]["positionDelta"][0]=.025;Write(path,replaced);
            Expect(ApplySkeletalFitProfile(correctiveAsset,path,&error)&&correctiveAsset.morphs.size()==3&&
                correctiveAsset.morphs[2].deltas[0].position.x==.025,"Revised profiles replace corrective data at the stable morph index");
            if(!Expect(SaveSkeletalAssetPackage(correctiveAsset,directory/"Corrective",&error),"Save derived corrective and its fitted animation as a portable asset package"))throw std::runtime_error(error);
            const auto correctiveLoaded=LoadSkeletalAssetPackage(directory/"Corrective",&error);
            Expect(correctiveLoaded&&correctiveLoaded->morphs.size()==3&&correctiveLoaded->morphs[2].deltas[0].position.x==.025&&
                EvaluateSkeletalPose(*correctiveLoaded,&correctiveLoaded->animations[2],0,false).morphWeights[2]==1,
                "Corrective morph binding and unit activation survive independent package reload");
            const auto correctiveManifest=Read(directory/"Corrective/Package.asset.json");
            Expect(Read(directory/"Before"/fs::u8path(beforeManifest["animations"][0].asString()))==
                Read(directory/"Corrective"/fs::u8path(correctiveManifest["animations"][0].asString()))&&
                Read(directory/"Before/Skeletons/model.skeleton.json")==Read(directory/"Corrective/Skeletons/model.skeleton.json"),
                "Corrective export retains the original captured pose and skeleton files exactly");
            auto rejectMorph=[&](Json::Value invalidProfile,const char* message) {
                Write(path,invalidProfile);auto candidate=correctiveAsset;
                Expect(!ApplySkeletalFitProfile(candidate,path,&error)&&!error.empty()&&candidate.morphs.size()==3&&
                    candidate.animations.size()==3&&candidate.morphs[2].deltas[0].position.x==.025&&
                    EvaluateSkeletalPose(candidate,&candidate.animations[2],0,false).morphWeights[2]==1,message);
            };
            auto malformed=replaced;malformed["morphDeltas"][0]["expectedPosition"][0]=1.01;
            rejectMorph(malformed,"Reject a corrective authored for different original bind positions atomically");
            malformed=replaced;malformed["morphDeltas"][0].removeMember("expectedPosition");rejectMorph(malformed,"Require per-vertex original-position preconditions");
            malformed=replaced;malformed["morphDeltas"].append(malformed["morphDeltas"][0]);rejectMorph(malformed,"Reject duplicate corrective mesh and vertex pairs");
            malformed=replaced;malformed["morphDeltas"][0]["mesh"]=1;rejectMorph(malformed,"Reject corrective mesh indices outside source geometry");
            malformed=replaced;malformed["morphDeltas"][0]["vertex"]=3;rejectMorph(malformed,"Reject corrective vertex indices outside their source mesh");
            malformed=replaced;malformed["morphDeltas"][0]["mesh"]=.5;rejectMorph(malformed,"Reject fractional corrective mesh indices");
            malformed=replaced;malformed["morphDeltas"][0]["vertex"]=-1;rejectMorph(malformed,"Reject negative corrective vertex indices");
            malformed=replaced;malformed["morphDeltas"][0]["positionDelta"][0]=std::numeric_limits<double>::infinity();
            rejectMorph(malformed,"Reject non-finite corrective position deltas");
            malformed=replaced;for(const double value:{1e300,0.,0.})malformed["morphDeltas"][0]["normalDelta"].append(value);
            rejectMorph(malformed,"Reject normal changes that overflow the stored float representation");
            malformed=replaced;malformed["morphDeltas"].resize(4);rejectMorph(malformed,"Reject corrective record counts exceeding target geometry");
            malformed=replaced;malformed["morphDeltas"]=Json::Value(Json::objectValue);rejectMorph(malformed,"Require corrective records to use an array");
            Write(path,replaced);auto collision=original;MorphTarget sourceCollision;sourceCollision.name="ShoesFit_Corrective";collision.morphs.push_back(sourceCollision);
            Expect(!ApplySkeletalFitProfile(collision,path,&error)&&collision.morphs.size()==3&&collision.animations.size()==2,
                "First corrective fitting refuses to overwrite a coincidentally named source morph");
            auto brokenReference=correctiveAsset;brokenReference.animations[2].morphTracks.back().keys[0].value=.5;
            Expect(!ApplySkeletalFitProfile(brokenReference,path,&error)&&brokenReference.morphs[2].deltas[0].position.x==.025,
                "Corrective replacement requires an explicit generated-fit reference at unit weight");
            auto missingMarker=correctiveAsset;missingMarker.animations[2].diagnostics.clear();
            Expect(!ApplySkeletalFitProfile(missingMarker,path,&error)&&missingMarker.morphs[2].deltas[0].position.x==.025,
                "Corrective replacement requires generated ownership evidence as well as a fit clip");
            auto large=original;large.geometry=std::make_shared<StaticModel>(*original.geometry);
            large.geometry->meshes[0].vertices[1].position.x=1000000000000.125;
            auto precise=morphProfile;precise["morphDeltas"][0]["expectedPosition"][0]=1000000000000.125;Write(path,precise);
            auto correctLarge=large;
            Expect(ApplySkeletalFitProfile(correctLarge,path,&error),"Exact large-coordinate bind preconditions retain double precision");
            precise["morphDeltas"][0]["expectedPosition"][0]=1000000000001.125;Write(path,precise);
            Expect(!ApplySkeletalFitProfile(large,path,&error)&&large.morphs.size()==2&&large.animations.size()==2,
                "Large source coordinates never allow a one-unit mismatch through a percentage-based tolerance");
        }
    }catch(const std::exception& exception){std::printf("Exception: %s\n",exception.what());++failures;}
    std::printf("Fixtures retained: %s\n%d failures\n",directory.string().c_str(),failures);return failures?1:0;
}
