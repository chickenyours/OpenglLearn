#include "Model/Private/blend_metadata.h"
#include <json/json.h>
#include <glm/gtx/matrix_decompose.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace Model::detail {
namespace {
void Require(bool condition,const std::string& message) {
    if(!condition)throw std::runtime_error(message);
}
Json::Value ReadDocument(const std::filesystem::path& metadataPath) {
    std::error_code ec;
    const auto size=std::filesystem::file_size(metadataPath,ec);
    Require(!ec&&size<=64ull*1024*1024,"Missing or oversized Blender source metadata");
    std::ifstream stream(metadataPath,std::ios::binary);
    Json::CharReaderBuilder builder;builder["collectComments"]=false;builder["rejectDupKeys"]=true;
    builder["allowSpecialFloats"]=false;builder["failIfExtra"]=true;
    Json::Value document;std::string error;
    Require(Json::parseFromStream(builder,stream,&document,&error)&&
        document["assetType"]=="BlenderSourceMetadata"&&document["version"]==1&&
        document["objects"].isArray()&&document["objects"].size()<=100000,
        "Invalid Blender source metadata: "+error);
    return document;
}
std::string PoseString(const Json::Value& value) {
    Require(value.isString(),"Expected a Blender source pose name");
    const auto name=value.asString();
    Require(!name.empty()&&name.size()<=16384&&name.find('\0')==std::string::npos,
        "Invalid Blender source pose name");
    return name;
}
void CheckPoseMatrix(const glm::dmat4& matrix) {
    for(int c=0;c<4;++c)for(int r=0;r<4;++r)
        Require(std::isfinite(matrix[c][r]),"Blender source pose contains a non-finite matrix");
    Require(std::abs(matrix[0][3])<=1e-8&&std::abs(matrix[1][3])<=1e-8&&
        std::abs(matrix[2][3])<=1e-8&&std::abs(matrix[3][3]-1)<=1e-8,
        "Blender source pose matrix must be affine");
    const double determinant=glm::determinant(glm::dmat3(matrix));
    const double scaleProduct=glm::length(glm::dvec3(matrix[0]))*
        glm::length(glm::dvec3(matrix[1]))*glm::length(glm::dvec3(matrix[2]));
    Require(std::isfinite(determinant)&&std::isfinite(scaleProduct)&&scaleProduct>0&&
        std::abs(determinant)>scaleProduct*1e-12,"Blender source pose contains a singular matrix");
}
glm::dmat4 ReadPoseMatrix(const Json::Value& rows) {
    Require(rows.isArray()&&rows.size()==4,"Blender source pose matrix must have four rows");
    glm::dmat4 matrix(1);
    for(unsigned r=0;r<4;++r) {
        Require(rows[r].isArray()&&rows[r].size()==4,"Blender source pose matrix row must have four columns");
        for(unsigned c=0;c<4;++c) {
            Require(rows[r][c].isNumeric(),"Blender source pose matrix component must be numeric");
            matrix[c][r]=rows[r][c].asDouble();
        }
    }
    CheckPoseMatrix(matrix);return matrix;
}
std::string Precision(double value) {
    std::ostringstream stream;stream<<std::scientific<<std::setprecision(9)<<value;return stream.str();
}
double LinearDifference(const glm::dmat3& a,const glm::dmat3& b) {
    double difference=0;
    for(unsigned c=0;c<3;++c)for(unsigned r=0;r<3;++r)difference=std::max(difference,std::abs(a[c][r]-b[c][r]));
    return difference;
}
bool PolarTransform(const glm::dmat4& matrix,BoneTransform& transform) {
    const auto linear=glm::dmat3(matrix);
    const double rms=std::sqrt((glm::dot(linear[0],linear[0])+glm::dot(linear[1],linear[1])+glm::dot(linear[2],linear[2]))/3);
    if(!std::isfinite(rms)||rms<=0)return false;
    auto rotation=linear/rms;
    for(unsigned iteration=0;iteration<32;++iteration) {
        const auto determinant=glm::determinant(rotation);
        if(!std::isfinite(determinant)||std::abs(determinant)<1e-20)return false;
        const auto next=(rotation+glm::transpose(glm::inverse(rotation)))*.5;
        for(unsigned c=0;c<3;++c)for(unsigned r=0;r<3;++r)if(!std::isfinite(next[c][r]))return false;
        const auto delta=LinearDifference(rotation,next);rotation=next;
        if(delta<=1e-13)break;
    }
    if(LinearDifference(glm::transpose(rotation)*rotation,glm::dmat3(1))>1e-10)return false;
    transform.translation=glm::dvec3(matrix[3]);
    for(unsigned axis=0;axis<3;++axis)transform.scale[axis]=glm::dot(rotation[axis],linear[axis]);
    // Polar decomposition also accepts reflected affine transforms. Move one
    // reflection into signed scale before constructing a proper quaternion.
    if(glm::determinant(rotation)<0) {
        unsigned axis=0;
        for(unsigned i=1;i<3;++i)if(std::abs(transform.scale[i])<std::abs(transform.scale[axis]))axis=i;
        rotation[axis]=-rotation[axis];transform.scale[axis]=-transform.scale[axis];
    }
    transform.rotation=glm::normalize(glm::quat_cast(rotation));
    for(unsigned axis=0;axis<3;++axis)if(!std::isfinite(transform.scale[axis])||std::abs(transform.scale[axis])<=1e-12)return false;
    return std::isfinite(transform.rotation.w)&&std::isfinite(transform.rotation.x)&&
        std::isfinite(transform.rotation.y)&&std::isfinite(transform.rotation.z);
}
BoneTransform PoseTransform(const glm::dmat4& matrix,const std::string& bone,std::vector<std::string>& diagnostics) {
    CheckPoseMatrix(matrix);
    BoneTransform transform;glm::dvec3 skew;glm::dvec4 perspective;
    Require(glm::decompose(matrix,transform.scale,transform.rotation,transform.translation,skew,perspective),
        "Cannot decompose Blender source pose transform");
    const auto lengthSquared=glm::dot(transform.rotation,transform.rotation);
    Require(std::isfinite(lengthSquared)&&lengthSquared>1e-16,"Invalid Blender source pose rotation");
    transform.rotation=glm::normalize(transform.rotation);
    for(unsigned axis=0;axis<3;++axis)
        Require(std::isfinite(transform.scale[axis])&&std::isfinite(transform.translation[axis])&&
            std::abs(transform.scale[axis])>1e-12,"Invalid Blender source pose translation or scale");
    auto reconstructed=TransformMatrix(transform);
    double magnitude=0;
    for(unsigned c=0;c<4;++c)for(unsigned r=0;r<4;++r) {
        Require(std::isfinite(reconstructed[c][r]),"Blender source pose cannot be represented by finite TRS");
        if(c<3&&r<3)magnitude=std::max(magnitude,std::abs(matrix[c][r]));
    }
    const double gramResidual=LinearDifference(glm::dmat3(matrix),glm::dmat3(reconstructed))/magnitude;
    constexpr double TrsTolerance=1e-5,PolarEntryLimit=2e-5;
    double residual=gramResidual;
    if(residual>TrsTolerance&&residual<=PolarEntryLimit) {
        BoneTransform best;
        if(PolarTransform(matrix,best)) {
            const auto bestMatrix=TransformMatrix(best);
            const double bestResidual=LinearDifference(glm::dmat3(matrix),glm::dmat3(bestMatrix))/magnitude;
            if(bestResidual<residual){transform=best;residual=bestResidual;}
        }
        if(residual<=TrsTolerance)diagnostics.push_back("Source pose bone '"+bone+
            "' uses bounded polar TRS fitting for float source precision: Gram-Schmidt relative residual "+
            Precision(gramResidual)+", polar "+Precision(residual)+", limit "+Precision(TrsTolerance)+".");
    }
    Require(residual<=TrsTolerance,"Blender source pose bone '"+bone+
        "' contains shear beyond bone TRS precision: Gram-Schmidt relative residual "+Precision(gramResidual)+
        ", fitted "+Precision(residual)+", limit "+Precision(TrsTolerance)+
        "; bake the source rig transforms");
    return transform;
}
std::vector<std::string> Strings(const Json::Value& array) {
    std::vector<std::string> result;
    if(!array.isArray() || array.size()>4096) throw std::runtime_error("Invalid Blender metadata string list");
    for(const auto& value:array) {
        if(!value.isString() || value.asString().size()>16384) throw std::runtime_error("Invalid Blender metadata string");
        result.push_back(value.asString());
    }
    return result;
}
}
std::vector<SourceMeshSemantics> ReadBlendMeshSemantics(SkeletalAssetPackage& asset,
    const std::filesystem::path& metadataPath) {
    const auto document=ReadDocument(metadataPath);
    for(const auto& warning:document["warnings"]) {
        if(warning.isString() && warning.asString().size()<16384 && asset.warnings.size()<10000)
            asset.warnings.push_back("Blender: "+warning.asString());
    }
    std::vector<SourceMeshSemantics> output;
    for(std::uint32_t index=0;index<asset.geometry->meshes.size();++index) {
        const auto& mesh=asset.geometry->meshes[index];
        const auto& material=asset.geometry->materials.at(mesh.materialIndex).name;
        std::set<std::string> nodes;
        for(const auto& instance:asset.geometry->instances)if(instance.meshIndex==index)nodes.insert(instance.nodeName);
        const Json::Value* selected=nullptr;
        std::set<std::uint32_t> slots;
        bool ambiguous=false;
        for(const auto& object:document["objects"]) {
            if(object["type"]!="MESH")continue;
            for(const auto& node:object["export"]["nodes"]) {
                if(!nodes.contains(node["nodeName"].asString()))continue;
                for(const auto& primitive:node["primitives"]) {
                    if(primitive["materialName"].asString()!=material)continue;
                    // Assimp glTF2Importer preserves the glTF mesh name and
                    // appends -primitiveIndex only for multi-primitive meshes.
                    // Include it so repeated material slots cannot cross-map.
                    auto expected=node["meshName"].asString();
                    if(node["primitives"].size()>1)expected+="-"+std::to_string(primitive["primitiveIndex"].asUInt());
                    if(mesh.name!=expected)continue;
                    if(selected && selected!=&object)ambiguous=true;
                    selected=&object;
                    for(const auto& slot:primitive["sourceMaterialSlots"])if(slot.isUInt())slots.insert(slot.asUInt());
                }
            }
        }
        if(!selected || ambiguous) {
            asset.warnings.push_back("Blender provenance could not uniquely map render section "+std::to_string(index)+
                " ("+mesh.name+", "+material+"); review its mesh region.");
            continue;
        }
        SourceMeshSemantics input;input.mesh=index;
        input.sourceObject=(*selected)["name"].asString();input.sourceMesh=(*selected)["dataName"].asString();
        input.sourceMaterial=material;input.collections=Strings((*selected)["collections"]);
        input.sourceObjectMaterialCount=std::max(1u,(*selected)["materials"].size());
        std::set<std::string> groups;
        for(const auto& section:(*selected)["materials"])if(slots.contains(section["slot"].asUInt()))
            for(const auto& group:section["vertexGroups"])if(group["weightedVertexCount"].asUInt64()>0)
                groups.insert(group["name"].asString());
        input.vertexGroups.assign(groups.begin(),groups.end());
        for(const auto& modifier:(*selected)["modifiers"]) {
            SourceModifierSemantics value;value.name=modifier["name"].asString();value.type=modifier["type"].asString();
            value.showViewport=modifier.get("showViewport",true).asBool();value.showRender=modifier.get("showRender",true).asBool();
            value.clothPinGroup=modifier["settings"].get("vertex_group_mass","").asString();
            input.modifiers.push_back(std::move(value));
        }
        output.push_back(std::move(input));
    }
    return output;
}

void ImportBlendSourcePose(SkeletalAssetPackage& asset,const std::filesystem::path& metadataPath) {
    const auto document=ReadDocument(metadataPath);
    if(!document.isMember("sourcePose"))return;
    const auto& source=document["sourcePose"];
    Require(source.isObject()&&source["frame"].isInt64()&&source["boneGlobals"].isArray()&&
        source["boneGlobals"].size()<=100000,"Invalid Blender source pose header or bone count");
    const auto boneCount=asset.skeleton.bones.size();
    Require(boneCount>0&&boneCount<=100000,"Cannot capture Blender source pose without a valid imported skeleton");
    AnimationSequenceAsset sequence;sequence.id=asset.id+"/animations/SourcePose";sequence.name="SourcePose";
    sequence.skeletonId=asset.skeleton.id;sequence.duration=1;sequence.generated=true;
    const auto& fps=source.isMember("framesPerSecond")?source["framesPerSecond"]:document["scene"]["framesPerSecond"];
    if(!fps.isNull()) {
        Require(fps.isNumeric(),"Invalid Blender source pose frame rate");sequence.framesPerSecond=fps.asDouble();
    }
    Require(std::isfinite(sequence.framesPerSecond)&&sequence.framesPerSecond>0&&sequence.framesPerSecond<=1000,
        "Invalid Blender source pose frame rate");
    const auto pathBytes=asset.sourcePath.generic_u8string();
    sequence.source=std::string(reinterpret_cast<const char*>(pathBytes.data()),pathBytes.size())+
        "#source-pose-frame="+std::to_string(source["frame"].asInt64());
    sequence.diagnostics.push_back("Captured Blender evaluated source pose at frame "+
        std::to_string(source["frame"].asInt64())+" as a constant generated preview.");

    constexpr auto Ambiguous=std::numeric_limits<std::size_t>::max();
    std::unordered_map<std::string,std::size_t> namedBones;
    for(std::size_t i=0;i<boneCount;++i)
        if(!namedBones.emplace(asset.skeleton.bones[i].name,i).second)namedBones[asset.skeleton.bones[i].name]=Ambiguous;
    std::vector<glm::dmat4> desired(boneCount,glm::dmat4(1)),global(boneCount,glm::dmat4(1));
    std::vector<bool> captured(boneCount,false);std::set<std::string> capturedNames;
    std::size_t capturedCount=0,unmatchedBones=0;
    for(const auto& entry:source["boneGlobals"]) {
        const auto name=PoseString(entry["name"]);
        Require(capturedNames.insert(name).second,"Duplicate bone name in Blender source pose: "+name);
        glm::dmat4 matrix;
        try{matrix=ReadPoseMatrix(entry["matrix"]);}
        catch(const std::exception& error){throw std::runtime_error("Blender source pose bone '"+name+"': "+error.what());}
        const auto found=namedBones.find(name);
        if(found==namedBones.end()){++unmatchedBones;continue;}
        Require(found->second!=Ambiguous,"Blender source pose bone name is ambiguous in imported skeleton: "+name);
        desired[found->second]=matrix;captured[found->second]=true;++capturedCount;
    }
    Require(source["boneGlobals"].empty()||capturedCount>0,
        "No captured Blender source pose bones match the imported skeleton");
    if(unmatchedBones)sequence.diagnostics.push_back(std::to_string(unmatchedBones)+
        " captured source bones do not occur in the exported skeleton.");

    // The scene skeleton can contain armature, object and empty nodes in
    // addition to actual bones. Propagate their original local transforms in
    // the captured hierarchy, supporting forward parents without recursion.
    std::vector<std::uint8_t> states(boneCount,0);std::vector<std::size_t> path;
    std::vector<BoneTransform> locals(boneCount);
    for(std::size_t start=0;start<boneCount;++start) {
        if(states[start]==2)continue;
        path.clear();auto cursor=static_cast<std::int32_t>(start);
        while(cursor>=0) {
            Require(static_cast<std::size_t>(cursor)<boneCount,"Blender source pose skeleton parent is out of range");
            if(states[cursor]==2)break;
            Require(states[cursor]==0,"Cycle in Blender source pose skeleton hierarchy");
            states[cursor]=1;path.push_back(static_cast<std::size_t>(cursor));
            cursor=asset.skeleton.bones[cursor].parent;
            Require(cursor>=-1,"Blender source pose skeleton parent is invalid");
        }
        while(!path.empty()) {
            const auto index=path.back();path.pop_back();const auto& bone=asset.skeleton.bones[index];
            const auto parent=bone.parent>=0?global[bone.parent]:glm::dmat4(1);
            CheckPoseMatrix(parent);
            global[index]=captured[index]?desired[index]:parent*TransformMatrix(bone.restLocal);
            CheckPoseMatrix(global[index]);
            locals[index]=captured[index]?PoseTransform(glm::inverse(parent)*global[index],bone.name,sequence.diagnostics):bone.restLocal;
            states[index]=2;
        }
    }
    sequence.boneTracks.reserve(boneCount);
    for(std::size_t i=0;i<boneCount;++i) {
        BoneAnimationTrack track;track.bone=static_cast<std::uint32_t>(i);
        track.translations.push_back({0,locals[i].translation});track.rotations.push_back({0,locals[i].rotation});
        track.scales.push_back({0,locals[i].scale});sequence.boneTracks.push_back(std::move(track));
    }

    if(source.isMember("morphDefaults")) {
        Require(source["morphDefaults"].isArray()&&source["morphDefaults"].size()<=100000,
            "Invalid Blender source pose morph defaults");
        std::vector<std::string> meshObjects(asset.geometry?asset.geometry->meshes.size():0);
        for(const auto& region:asset.meshRegions)if(region.mesh<meshObjects.size())meshObjects[region.mesh]=region.sourceObject;
        std::unordered_map<std::string,std::vector<std::uint32_t>> namedMorphs;
        std::vector<std::set<std::string>> morphObjects(asset.morphs.size());
        for(std::size_t index=0;index<asset.morphs.size();++index) {
            namedMorphs[asset.morphs[index].name].push_back(static_cast<std::uint32_t>(index));
            for(const auto& delta:asset.morphs[index].deltas)
                if(delta.mesh<meshObjects.size()&&!meshObjects[delta.mesh].empty())morphObjects[index].insert(meshObjects[delta.mesh]);
        }
        std::map<std::pair<std::string,std::string>,double> sourceDefaults;
        std::map<std::uint32_t,double> targetDefaults;
        std::size_t unmatchedNonzero=0;
        for(const auto& entry:source["morphDefaults"]) {
            const auto object=PoseString(entry["sourceObject"]),name=PoseString(entry["name"]);
            Require(entry["value"].isNumeric(),"Invalid Blender source pose morph value");
            const double value=entry["value"].asDouble();
            Require(std::isfinite(value)&&std::abs(value)<=std::numeric_limits<float>::max(),
                "Invalid Blender source pose morph value");
            Require(sourceDefaults.emplace(std::pair{object,name},value).second,"Duplicate Blender source pose morph default");
            bool matched=false;const auto found=namedMorphs.find(name);
            if(found!=namedMorphs.end())for(const auto target:found->second) {
                if(!morphObjects[target].contains(object))continue;
                Require(morphObjects[target].size()==1,"Blender source pose morph spans ambiguous source objects");
                const auto [previous,inserted]=targetDefaults.emplace(target,value);
                Require(inserted||previous->second==value,"Conflicting Blender source pose morph defaults");matched=true;
            }
            if(!matched&&value!=0)++unmatchedNonzero;
        }
        Require(targetDefaults.size()<=100000,"Too many Blender source pose morph curves");
        for(const auto& [target,value]:targetDefaults)sequence.morphTracks.push_back({target,{{0,value}}});
        if(unmatchedNonzero)sequence.diagnostics.push_back(std::to_string(unmatchedNonzero)+
            " nonzero source morph defaults could not map to an exported object/morph pair; review the source pose.");
    }
    if(capturedCount==0&&sequence.morphTracks.empty())return;
    sequence.diagnostics.push_back(std::to_string(capturedCount)+" captured source bone transforms; original rest pose and inverse binds retained.");
    const auto existing=std::find_if(asset.animations.begin(),asset.animations.end(),
        [&](const auto& animation){return animation.id==sequence.id;});
    if(existing==asset.animations.end())asset.animations.push_back(std::move(sequence));
    else {
        Require(existing->generated&&existing->name=="SourcePose","Source pose sequence ID conflicts with an authored animation");
        *existing=std::move(sequence);
    }
}
}
