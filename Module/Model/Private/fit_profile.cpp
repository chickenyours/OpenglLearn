#include "Model/Public/skeletal_asset.h"
#include "animation_limits.h"
#include <json/json.h>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <set>
#include <stdexcept>
#include <type_traits>
#include <unordered_map>

namespace Model {
namespace {
constexpr std::uint64_t MaximumProfileBytes=64ull*1024*1024;
constexpr std::size_t MaximumFitBones=100000;
constexpr std::size_t MaximumFitMorphDeltas=1000000;
constexpr auto MissingIndex=std::numeric_limits<std::size_t>::max();
void Require(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
std::string Text(const Json::Value& value,const char* description) {
    Require(value.isString(),std::string("Fit profile requires ")+description);
    auto text=value.asString();
    Require(!text.empty()&&text.size()<=16384&&text.find('\0')==std::string::npos,
        std::string("Invalid fit profile ")+description);return text;
}
template<class Vector> void Finite(const Vector& value,const char* description) {
    for(glm::length_t i=0;i<value.length();++i)Require(std::isfinite(value[i]),std::string("Non-finite fit ")+description);
}
glm::dvec3 Vector(const Json::Value& value,const char* description) {
    Require(value.isArray()&&value.size()==3,std::string("Fit profile requires three components for ")+description);
    glm::dvec3 result;
    for(unsigned i=0;i<3;++i){Require(value[i].isNumeric(),std::string("Nonnumeric fit ")+description);result[i]=value[i].asDouble();}
    Finite(result,description);return result;
}
void CheckTransform(const BoneTransform& transform) {
    Finite(transform.translation,"translation");Finite(transform.scale,"scale");Finite(transform.rotation,"rotation");
    const auto norm=glm::dot(transform.rotation,transform.rotation);
    Require(std::isfinite(norm)&&norm>1e-16,"Fit pose has an invalid rotation quaternion");
    for(unsigned i=0;i<3;++i)Require(std::abs(transform.scale[i])>1e-12,"Fit pose has a singular scale");
}
Json::Value ReadProfile(const std::filesystem::path& path) {
    Require(!path.empty(),"A skeletal fit profile filename is required");std::error_code code;
    const auto bytes=std::filesystem::file_size(path,code);
    Require(!code&&bytes>0&&bytes<=MaximumProfileBytes,"Fit profile is missing, empty or exceeds 64 MiB");
    std::ifstream stream(path,std::ios::binary);Require(bool(stream),"Cannot open skeletal fit profile");
    Json::CharReaderBuilder builder;builder["rejectDupKeys"]=true;builder["failIfExtra"]=true;
    builder["allowSpecialFloats"]=false;builder["collectComments"]=false;builder["stackLimit"]=64;
    Json::Value document;std::string error;
    if(!Json::parseFromStream(builder,stream,&document,&error))throw std::runtime_error("Invalid skeletal fit profile JSON: "+error);
    Require(document.isObject()&&document["assetType"]=="SkeletalFitProfile"&&document["version"].isUInt()&&
        document["version"].asUInt()==1,"Unsupported skeletal fit profile type or version");return document;
}
void CheckBaseline(const SkeletalAssetPackage& asset,const AnimationSequenceAsset& base) {
    Require(!base.id.empty()&&base.generated&&base.skeletonId==asset.skeleton.id,"Fit profile base SourcePose must be generated for this skeleton");
    Require(std::isfinite(base.framesPerSecond)&&base.framesPerSecond>0&&base.framesPerSecond<=1000&&
        std::isfinite(base.duration)&&base.duration>=0,"Fit profile base SourcePose has an invalid frame rate or duration");
    Require(base.boneTracks.size()<=MaximumFitBones&&base.morphTracks.size()<=MaximumFitBones,
        "Fit profile base SourcePose exceeds the track budget");
    std::set<std::uint32_t> bones,morphs;std::size_t keys=0;
    const auto constant=[&](const auto& channel) {
        Require(channel.size()<=1,"Fit profile base SourcePose must contain constant channels");
        if(!channel.empty())Require(channel[0].time==0,"Fit profile base SourcePose keys must start at zero");
        keys+=channel.size();Require(keys<=AnimationLimits::MaximumStoredKeys,"Fit profile base exceeds the key budget");
    };
    for(const auto& track:base.boneTracks) {
        Require(track.bone<asset.skeleton.bones.size()&&bones.insert(track.bone).second,
            "Fit profile base SourcePose has invalid or duplicate bone tracks");
        Require(!track.translations.empty()||!track.rotations.empty()||!track.scales.empty(),"Fit profile base contains an empty bone track");
        constant(track.translations);constant(track.rotations);constant(track.scales);
        for(const auto& key:track.translations)Finite(key.value,"base translation");
        for(const auto& key:track.rotations){Finite(key.value,"base rotation");const auto n=glm::dot(key.value,key.value);Require(std::isfinite(n)&&n>1e-16,"Invalid fit base rotation");}
        for(const auto& key:track.scales){Finite(key.value,"base scale");for(unsigned axis=0;axis<3;++axis)Require(std::abs(key.value[axis])>1e-12,"Singular fit base scale");}
    }
    for(const auto& track:base.morphTracks) {
        Require(track.morph<asset.morphs.size()&&morphs.insert(track.morph).second&&!track.keys.empty(),
            "Fit profile base SourcePose has invalid or duplicate morph tracks");constant(track.keys);
        for(const auto& key:track.keys)Require(std::isfinite(key.value)&&std::abs(key.value)<=std::numeric_limits<float>::max(),"Invalid fit base morph value");
    }
}
}

bool ApplySkeletalFitProfile(SkeletalAssetPackage& asset,const std::filesystem::path& profilePath,std::string* error) {
    try {
        const auto profile=ReadProfile(profilePath);
        Require(!asset.id.empty()&&Text(profile["sourceAssetId"],"sourceAssetId")==asset.id,
            "Fit profile sourceAssetId differs from this model; re-author it for the exact source asset");
        Require(Text(profile["baseClip"],"baseClip")=="SourcePose","Fit profiles currently require baseClip SourcePose");
        const auto name=Text(profile["name"],"name");
        Require(name!="SourcePose"&&name!="BindPose"&&name!="RigPreview","Fit profile name conflicts with a reserved preview sequence");
        Require(!asset.skeleton.id.empty()&&!asset.skeleton.bones.empty()&&asset.skeleton.bones.size()<=MaximumFitBones,
            "Fit profile requires a valid bounded target skeleton");
        Require(asset.morphs.size()<=MaximumFitBones,"Fit profile target exceeds the morph budget");
        const auto& corrections=profile["boneCorrections"];
        Require(corrections.isArray()&&corrections.size()<=asset.skeleton.bones.size()&&corrections.size()<=MaximumFitBones,
            "Fit profile boneCorrections must be an array within the skeleton budget");
        const auto& morphDeltas=profile["morphDeltas"];
        Require(!profile.isMember("morphDeltas")||(morphDeltas.isArray()&&morphDeltas.size()<=MaximumFitMorphDeltas),
            "Fit profile morphDeltas must be an array within the corrective vertex budget");
        Require(!corrections.empty()||!morphDeltas.empty(),"Fit profile requires bone corrections or corrective vertex deltas");
        const AnimationSequenceAsset* base=nullptr;
        for(const auto& clip:asset.animations)if(clip.name=="SourcePose") {
            Require(!base,"Fit profile base SourcePose is ambiguous");base=&clip;
        }
        Require(base!=nullptr,"Fit profile requires the original captured SourcePose sequence");CheckBaseline(asset,*base);
        std::unordered_map<std::string,std::size_t> namedBones;
        std::vector<std::uint8_t> states(asset.skeleton.bones.size(),0);
        for(std::size_t i=0;i<asset.skeleton.bones.size();++i) {
            const auto& bone=asset.skeleton.bones[i];CheckTransform(bone.restLocal);
            Require(bone.parent>=-1&&(bone.parent<0||static_cast<std::size_t>(bone.parent)<asset.skeleton.bones.size()),"Invalid fit skeleton parent");
            if(!namedBones.emplace(bone.name,i).second)namedBones[bone.name]=std::numeric_limits<std::size_t>::max();
        }
        std::vector<std::size_t> path;
        for(std::size_t i=0;i<states.size();++i) {
            if(states[i]==2)continue;path.clear();auto cursor=static_cast<std::int32_t>(i);
            while(cursor>=0&&states[cursor]!=2) {
                Require(states[cursor]==0,"Cycle in fit profile target skeleton");states[cursor]=1;path.push_back(static_cast<std::size_t>(cursor));
                cursor=asset.skeleton.bones[cursor].parent;
            }
            for(const auto index:path)states[index]=2;
        }
        auto pose=EvaluateSkeletalPose(asset,base,0,false);
        for(const auto& transform:pose.local)CheckTransform(transform);
        AnimationSequenceAsset sequence;sequence.id=asset.id+"/animations/"+name;sequence.name=name;sequence.skeletonId=asset.skeleton.id;
        sequence.generated=true;sequence.duration=1;sequence.framesPerSecond=base->framesPerSecond;
        const auto pathBytes=profilePath.generic_u8string();
        sequence.source="fit-profile:"+std::string(reinterpret_cast<const char*>(pathBytes.data()),pathBytes.size())+";base="+base->id;
        sequence.diagnostics.push_back("Generated constant wearing fit from SourcePose using explicit fitting corrections.");
        sequence.diagnostics.push_back("Profile source asset: "+asset.id+"; base clip: "+base->id+".");
        std::size_t existingClip=MissingIndex;
        for(std::size_t i=0;i<asset.animations.size();++i) {
            const auto& clip=asset.animations[i];
            if(clip.id==sequence.id||clip.name==sequence.name) {
                Require(existingClip==MissingIndex&&clip.id==sequence.id&&clip.name==sequence.name&&clip.generated&&
                    clip.source.starts_with("fit-profile:"),"Fit sequence conflicts with an existing animation");existingClip=i;
            }
        }
        std::set<std::size_t> modified;
        Json::StreamWriterBuilder compact;compact["indentation"]="";compact["precision"]=17;
        for(const auto& correction:corrections) {
            Require(correction.isObject(),"Fit bone correction must be an object");const auto boneName=Text(correction["bone"],"bone name");
            const auto found=namedBones.find(boneName);
            Require(found!=namedBones.end(),"Fit profile bone is absent from target skeleton: "+boneName);
            Require(found->second!=std::numeric_limits<std::size_t>::max(),"Fit profile bone name is ambiguous: "+boneName);
            Require(modified.insert(found->second).second,"Fit profile corrects the same bone more than once: "+boneName);
            const auto& rotation=correction["localRotationDelta"];
            Require(rotation.isArray()&&rotation.size()==4,"Fit localRotationDelta must be an xyzw quaternion");
            glm::dvec4 xyzw;
            for(unsigned axis=0;axis<4;++axis){Require(rotation[axis].isNumeric(),"Nonnumeric fit rotation delta");xyzw[axis]=rotation[axis].asDouble();}
            Finite(xyzw,"rotation delta");const auto norm=glm::dot(xyzw,xyzw);
            Require(std::isfinite(norm)&&norm>1e-16&&std::abs(norm-1)<=.002,"Fit rotation delta must be a unit quaternion");
            auto& local=pose.local[found->second];
            local.rotation=glm::normalize(local.rotation*glm::normalize(glm::dquat(xyzw.w,xyzw.x,xyzw.y,xyzw.z)));
            if(correction.isMember("localTranslationDelta"))local.translation+=Vector(correction["localTranslationDelta"],"translation delta");
            if(correction.isMember("localScaleMultiplier")) {
                const auto scale=Vector(correction["localScaleMultiplier"],"scale multiplier");
                for(unsigned axis=0;axis<3;++axis)Require(scale[axis]>0,"Fit localScaleMultiplier must be strictly positive");local.scale*=scale;
            }
            CheckTransform(local);sequence.diagnostics.push_back("Local correction: "+Json::writeString(compact,correction));
        }
        const auto globals=GlobalBoneTransforms(asset.skeleton,pose.local);
        for(const auto& global:globals)for(unsigned column=0;column<4;++column)Finite(global[column],"global transform");
        MorphTarget corrective;std::size_t correctiveIndex=MissingIndex;bool appendCorrective=false;
        if(!morphDeltas.empty()) {
            Require(asset.geometry!=nullptr,"Corrective fit requires original model geometry");
            corrective.name=name+"_Corrective";corrective.englishName=corrective.name;corrective.sourceType=1;
            for(std::size_t i=0;i<asset.morphs.size();++i)if(asset.morphs[i].name==corrective.name) {
                Require(correctiveIndex==MissingIndex,"Corrective fit morph name is ambiguous");correctiveIndex=i;
            }
            if(correctiveIndex!=MissingIndex) {
                Require(existingClip!=MissingIndex,"Corrective fit name conflicts with an existing source morph");
                const auto& previous=asset.animations[existingClip];
                const auto marker="Generated corrective morph "+corrective.name+":";
                Require(std::any_of(previous.diagnostics.begin(),previous.diagnostics.end(),
                    [&](const auto& diagnostic){return diagnostic.starts_with(marker);}),
                    "Existing fit does not identify this morph as its generated corrective");
                const auto& tracks=previous.morphTracks;std::size_t references=0;
                for(const auto& track:tracks)if(track.morph==correctiveIndex) {
                    ++references;Require(track.keys.size()==1&&track.keys[0].time==0&&track.keys[0].value==1,
                        "Existing fit does not identify its generated corrective morph at weight one");
                }
                Require(references==1,"Existing fit does not identify its generated corrective morph");
            }else {
                Require(asset.morphs.size()<MaximumFitBones,"Corrective fit exceeds the package morph budget");
                correctiveIndex=asset.morphs.size();appendCorrective=true;
            }
            std::uint64_t vertices=0;
            for(const auto& mesh:asset.geometry->meshes) {
                Require(mesh.vertices.size()<=100000000&&vertices<=100000000-mesh.vertices.size(),"Corrective fit geometry exceeds the vertex budget");vertices+=mesh.vertices.size();
            }
            Require(morphDeltas.size()<=vertices,"Corrective fit has more vertex records than target vertices");
            corrective.deltas.reserve(morphDeltas.size());std::set<std::pair<std::uint32_t,std::uint32_t>> affected;
            for(const auto& entry:morphDeltas) {
                Require(entry.isObject()&&entry["mesh"].isUInt()&&entry["vertex"].isUInt(),"Corrective fit mesh and vertex must be unsigned integers");
                const auto mesh=entry["mesh"].asUInt(),vertex=entry["vertex"].asUInt();
                Require(mesh<asset.geometry->meshes.size()&&vertex<asset.geometry->meshes[mesh].vertices.size(),"Corrective fit vertex reference is out of range");
                Require(affected.emplace(mesh,vertex).second,"Corrective fit repeats a mesh/vertex pair");
                const auto expected=Vector(entry["expectedPosition"],"expected bind position");
                const auto& actual=asset.geometry->meshes[mesh].vertices[vertex].position;Finite(actual,"original bind position");
                for(unsigned axis=0;axis<3;++axis) {
                    const auto tolerance=std::max(1e-6,8*std::numeric_limits<double>::epsilon()*std::abs(actual[axis]));
                    Require(std::abs(expected[axis]-actual[axis])<=tolerance,
                        "Corrective fit expectedPosition differs from the original bind vertex; regenerate the profile for this import");
                }
                MorphVertexDelta delta;delta.mesh=mesh;delta.vertex=vertex;delta.position=Vector(entry["positionDelta"],"corrective position delta");
                Finite(actual+delta.position,"corrected bind position");
                if(entry.isMember("normalDelta")) {
                    const auto normal=Vector(entry["normalDelta"],"corrective normal delta");delta.normal=glm::vec3(normal);
                    Finite(delta.normal,"corrective stored normal delta");
                }
                corrective.deltas.push_back(delta);
            }
            sequence.diagnostics.push_back("Generated corrective morph "+corrective.name+": "+std::to_string(corrective.deltas.size())+
                " guarded bind-space vertex deltas at weight one; source mesh and source morphs retained.");
        }
        Require(pose.local.size()*3+pose.morphWeights.size()+(!morphDeltas.empty()?1:0)<=AnimationLimits::MaximumStoredKeys,"Fitted sequence exceeds the stored key budget");
        sequence.boneTracks.reserve(pose.local.size());
        for(std::size_t bone=0;bone<pose.local.size();++bone) {
            BoneAnimationTrack track;track.bone=static_cast<std::uint32_t>(bone);const auto& local=pose.local[bone];
            track.translations.push_back({0,local.translation});track.rotations.push_back({0,local.rotation});track.scales.push_back({0,local.scale});
            sequence.boneTracks.push_back(std::move(track));
        }
        for(std::uint32_t morph=0;morph<pose.morphWeights.size();++morph) {
            const auto value=pose.morphWeights[morph];Require(std::isfinite(value),"Fit base pose contains a non-finite morph value");
            if(value!=0&&morph!=correctiveIndex)sequence.morphTracks.push_back({morph,{{0,value}}});
        }
        if(!morphDeltas.empty())sequence.morphTracks.push_back({static_cast<std::uint32_t>(correctiveIndex),{{0,1}}});
        Require(sequence.morphTracks.size()<=MaximumFitBones,"Corrective fit exceeds the animation morph track budget");
        if(existingClip==MissingIndex)Require(asset.animations.size()<10000,"Fit profile would exceed the package animation count budget");
        // Allocate both containers before touching either logical asset. Their
        // value types have no-throw moves/swaps, so publication below cannot
        // leave a corrective morph installed without its matching fit clip.
        static_assert(std::is_nothrow_move_constructible_v<MorphTarget>&&std::is_nothrow_swappable_v<MorphTarget>);
        static_assert(std::is_nothrow_move_constructible_v<AnimationSequenceAsset>&&std::is_nothrow_swappable_v<AnimationSequenceAsset>);
        if(appendCorrective)asset.morphs.reserve(asset.morphs.size()+1);
        if(existingClip==MissingIndex)asset.animations.reserve(asset.animations.size()+1);
        if(!morphDeltas.empty()) {
            if(appendCorrective)asset.morphs.push_back(std::move(corrective));else std::swap(asset.morphs[correctiveIndex],corrective);
        }
        if(existingClip==MissingIndex)asset.animations.push_back(std::move(sequence));else std::swap(asset.animations[existingClip],sequence);
        if(error)error->clear();return true;
    }catch(const std::exception& exception){if(error)*error=exception.what();return false;}
}
} // namespace Model
