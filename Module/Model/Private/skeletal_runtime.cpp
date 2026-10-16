#include "Model/Public/skeletal_asset.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace Model {
namespace {
constexpr double Epsilon = 1e-12;
glm::dquat SafeQuaternion(const glm::dquat& q) {
    const double length2 = glm::dot(q, q);
    return std::isfinite(length2) && length2 > Epsilon ? glm::normalize(q) : glm::dquat(1,0,0,0);
}
template<class Key, class Value, class Interpolate>
Value Sample(const std::vector<Key>& keys, double time, Value fallback, Interpolate interpolate) {
    if (keys.empty()) return fallback;
    if (time <= keys.front().time) return keys.front().value;
    if (time >= keys.back().time) return keys.back().value;
    auto right = std::upper_bound(keys.begin(), keys.end(), time,
        [](double t, const Key& key) { return t < key.time; });
    if (right == keys.begin() || right == keys.end()) return fallback;
    const auto& a = *(right-1);
    const double span = right->time-a.time;
    return interpolate(a.value, right->value, span > Epsilon ? (time-a.time)/span : 1.0);
}
glm::dvec3 SampleVectors(const std::vector<VectorKey>& keys, double time, glm::dvec3 fallback) {
    return Sample(keys,time,fallback,[](auto a,auto b,double f){return glm::mix(a,b,f);});
}
glm::dquat SampleRotations(const std::vector<RotationKey>& keys, double time, glm::dquat fallback) {
    return SafeQuaternion(Sample(keys,time,fallback,[](auto a,auto b,double f){
        a=SafeQuaternion(a); b=SafeQuaternion(b);
        if (glm::dot(a,b)<0) b=-b;
        return glm::slerp(a,b,f);
    }));
}
int FindRole(const IkRigAsset& rig,const std::string& role) {
    for(const auto& mapping:rig.humanoid) if(mapping.role==role) return mapping.bone;
    return -1;
}
}

glm::dmat4 TransformMatrix(const BoneTransform& transform) {
    return glm::translate(glm::dmat4(1),transform.translation) * glm::mat4_cast(SafeQuaternion(transform.rotation))
        * glm::scale(glm::dmat4(1),transform.scale);
}

std::vector<BoneTransform> RestPose(const SkeletonAsset& skeleton) {
    std::vector<BoneTransform> result; result.reserve(skeleton.bones.size());
    for(const auto& bone:skeleton.bones) result.push_back(bone.restLocal);
    return result;
}

std::vector<glm::dmat4> GlobalBoneTransforms(const SkeletonAsset& skeleton,
    const std::vector<BoneTransform>& localPose) {
    const std::size_t count=skeleton.bones.size();
    std::vector<glm::dmat4> result(count,glm::dmat4(1));
    std::vector<std::uint8_t> state(count,0);std::vector<std::size_t> path;
    for(std::size_t i=0;i<count;++i) {
        if(state[i]==2)continue;
        path.clear();std::int32_t cursor=static_cast<std::int32_t>(i);
        // Iterative traversal also supports very deep scene-node skeletons
        // without exhausting the C++ call stack.
        while(cursor>=0&&static_cast<std::size_t>(cursor)<count&&state[cursor]==0) {
            state[cursor]=1;path.push_back(static_cast<std::size_t>(cursor));cursor=skeleton.bones[cursor].parent;
        }
        while(!path.empty()) {
            const auto index=path.back();path.pop_back();
            const auto local=TransformMatrix(index<localPose.size()?localPose[index]:skeleton.bones[index].restLocal);
            const auto parent=skeleton.bones[index].parent;
            // Cycles are rejected by validation; an unvalidated cycle is
            // nevertheless kept finite while inspecting damaged data.
            result[index]=parent>=0&&static_cast<std::size_t>(parent)<count&&state[parent]==2?result[parent]*local:local;
            state[index]=2;
        }
    }
    return result;
}

SkeletalPose EvaluateSkeletalPose(const SkeletalAssetPackage& asset,
    const AnimationSequenceAsset* sequence,double time,bool loop) {
    SkeletalPose pose; pose.local=RestPose(asset.skeleton);
    pose.morphWeights.resize(asset.morphs.size(),0);
    if(sequence) {
        if(!std::isfinite(time)) time=0;
        if(sequence->duration>0) {
            if(loop) {time=std::fmod(time,sequence->duration); if(time<0) time+=sequence->duration;}
            else time=std::clamp(time,0.0,sequence->duration);
        } else time=0;
        for(const auto& track:sequence->boneTracks) if(track.bone<pose.local.size()) {
            auto& transform=pose.local[track.bone];
            transform.translation=SampleVectors(track.translations,time,transform.translation);
            transform.rotation=SampleRotations(track.rotations,time,transform.rotation);
            transform.scale=SampleVectors(track.scales,time,transform.scale);
        }
        for(const auto& track:sequence->morphTracks) if(track.morph<pose.morphWeights.size()) {
            const double value=Sample(track.keys,time,0.0,[](double a,double b,double f){return a+(b-a)*f;});
            pose.morphWeights[track.morph]=std::isfinite(value)?static_cast<float>(value):0;
        }
    }
    pose.global=GlobalBoneTransforms(asset.skeleton,pose.local);
    return pose;
}

bool SkinSkeletalMesh(const SkeletalAssetPackage& asset,const SkeletalPose& pose,
    StaticModel& output,std::string* error) {
    auto fail=[&](const std::string& message){if(error)*error=message;return false;};
    if(!asset.geometry) return fail("Skeletal asset has no geometry");
    if(pose.global.size()!=asset.skeleton.bones.size()) return fail("Pose global transform count does not match skeleton");
    if(asset.skins.size()!=asset.geometry->meshes.size()) return fail("Mesh skin count does not match geometry");
    // Copy geometry only when its layout changes. Material and texture storage
    // remains owned by the asset's renderer resource, not by every CPU pose.
    if(output.meshes.size()!=asset.geometry->meshes.size()) output.meshes=asset.geometry->meshes;
    output.instances=asset.geometry->instances;
    output.bounds={};
    for(std::size_t m=0;m<asset.geometry->meshes.size();++m) {
        const auto& source=asset.geometry->meshes[m]; auto& mesh=output.meshes[m];
        if(mesh.vertices.size()!=source.vertices.size() || mesh.indices.size()!=source.indices.size()) mesh=source;
        else {mesh.vertices=source.vertices;mesh.indices=source.indices;mesh.name=source.name;mesh.materialIndex=source.materialIndex;}
        const auto& skin=asset.skins[m];
        if(skin.vertices.size()!=mesh.vertices.size()) return fail("Vertex skin count does not match mesh "+std::to_string(m));
        if(!skin.influenceOffsets.empty()||!skin.influences.empty()) {
            if(skin.influenceOffsets.size()!=mesh.vertices.size()+1||skin.influenceOffsets.front()!=0||skin.influenceOffsets.back()!=skin.influences.size())
                return fail("Full skin influence offsets do not match mesh "+std::to_string(m));
            for(std::size_t v=0;v<mesh.vertices.size();++v)
                if(skin.influenceOffsets[v]>=skin.influenceOffsets[v+1]) return fail("Full skin influence ranges must be nonempty and monotonic in mesh "+std::to_string(m));
        }
    }
    // Vertex morphs are authored in bind-mesh space, before bone skinning.
    for(std::size_t i=0;i<asset.morphs.size() && i<pose.morphWeights.size();++i) {
        const double weight=pose.morphWeights[i]; if(!std::isfinite(weight)||std::abs(weight)<Epsilon) continue;
        for(const auto& delta:asset.morphs[i].deltas) if(delta.mesh<output.meshes.size() && delta.vertex<output.meshes[delta.mesh].vertices.size()) {
            auto& vertex=output.meshes[delta.mesh].vertices[delta.vertex];
            vertex.position+=delta.position*weight; vertex.normal+=delta.normal*static_cast<float>(weight);
        }
    }
    for(std::size_t m=0;m<output.meshes.size();++m) {
        auto& mesh=output.meshes[m];const auto& skin=asset.skins[m];mesh.bounds={};
        const double bindDet=glm::determinant(skin.bindTransform);
        if(!std::isfinite(bindDet)||std::abs(bindDet)<Epsilon) return fail("Singular mesh bind transform");
        const glm::dmat4 inverseMesh=glm::inverse(skin.bindTransform);
        std::vector<glm::dmat4> matrices(pose.global.size());
        std::vector<glm::dmat3> normalMatrices(pose.global.size());
        for(std::size_t j=0;j<matrices.size();++j) {
            const glm::dmat4 offset=j<skin.boneFromMesh.size()?skin.boneFromMesh[j]:asset.skeleton.bones[j].inverseBind*skin.bindTransform;
            matrices[j]=inverseMesh*pose.global[j]*offset;
            const auto linear=glm::dmat3(matrices[j]);
            normalMatrices[j]=std::abs(glm::determinant(linear))>Epsilon?glm::transpose(glm::inverse(linear)):glm::dmat3(1);
        }
        for(std::size_t v=0;v<mesh.vertices.size();++v) {
            auto& vertex=mesh.vertices[v];
            glm::dvec3 position(0),normal(0),tangent(0);double total=0;
            // SDEF/QDEF source modes and parameters survive cooking; this CPU
            // preview explicitly uses LBS approximation (reported by importer).
            ForEachSkinInfluence(skin,v,[&](std::uint32_t joint,float sourceWeight) {
                const double weight=sourceWeight;
                if(!std::isfinite(weight)||weight<=0||joint>=matrices.size()) return;
                position+=glm::dvec3(matrices[joint]*glm::dvec4(vertex.position,1))*weight;
                normal+=normalMatrices[joint]*glm::dvec3(vertex.normal)*weight;
                tangent+=glm::dmat3(matrices[joint])*glm::dvec3(vertex.tangent)*weight;
                total+=weight;
            });
            if(total>Epsilon) {
                vertex.position=position/total;
                if(glm::dot(normal,normal)>Epsilon) vertex.normal=glm::vec3(glm::normalize(normal));
                normal=glm::dvec3(vertex.normal);tangent-=normal*glm::dot(tangent,normal);
                if(glm::dot(tangent,tangent)>Epsilon) vertex.tangent=glm::vec4(glm::vec3(glm::normalize(tangent)),vertex.tangent.w);
            }
            mesh.bounds.Include(vertex.position);
        }
    }
    if(output.instances.empty()) for(const auto& mesh:output.meshes) if(mesh.bounds.valid) {output.bounds.Include(mesh.bounds.minimum);output.bounds.Include(mesh.bounds.maximum);}
    for(const auto& instance:output.instances) if(instance.meshIndex<output.meshes.size()) {
        const auto& bounds=output.meshes[instance.meshIndex].bounds;if(!bounds.valid)continue;
        for(int corner=0;corner<8;++corner) output.bounds.Include(glm::dvec3(instance.transform*glm::dvec4(
            corner&1?bounds.maximum.x:bounds.minimum.x,corner&2?bounds.maximum.y:bounds.minimum.y,
            corner&4?bounds.maximum.z:bounds.minimum.z,1)));
    }
    if(error)error->clear();return true;
}

bool SolveIk(const SkeletonAsset& skeleton,const IkChain& chain,const glm::dvec3& target,
    SkeletalPose& pose,std::string* error) {
    auto fail=[&](const std::string& message){if(error)*error=message;return false;};
    if(error)error->clear();
    if(!chain.enabled) return true;
    if(chain.effector<0||static_cast<std::size_t>(chain.effector)>=skeleton.bones.size()) return fail("IK effector is outside skeleton");
    if(pose.local.size()!=skeleton.bones.size())return fail("IK pose size does not match skeleton");
    if(!std::isfinite(target.x)||!std::isfinite(target.y)||!std::isfinite(target.z))return fail("IK target is not finite");
    for(const auto& link:chain.links)if(link.bone>=skeleton.bones.size())return fail("IK link is outside skeleton");
    pose.global=GlobalBoneTransforms(skeleton,pose.local);
    for(std::uint32_t iteration=0;iteration<std::min(chain.iterations,1024u);++iteration) {
        const auto end=glm::dvec3(pose.global[chain.effector][3]);
        if(glm::length2(end-target)<1e-12)break;
        for(const auto& link:chain.links) {
            if(static_cast<std::int32_t>(link.bone)==chain.effector)continue;
            const auto pivot=glm::dvec3(pose.global[link.bone][3]);
            auto a=glm::dvec3(pose.global[chain.effector][3])-pivot;auto b=target-pivot;
            if(glm::length2(a)<Epsilon||glm::length2(b)<Epsilon)continue;
            a=glm::normalize(a);b=glm::normalize(b);
            double angle=std::acos(std::clamp(glm::dot(a,b),-1.0,1.0));
            if(angle<1e-9)continue;
            angle=std::min(angle,std::max(1e-6,chain.angleLimit));
            auto axis=glm::cross(a,b);
            if(glm::length2(axis)<Epsilon)axis=glm::cross(a,std::abs(a.x)<0.8?glm::dvec3(1,0,0):glm::dvec3(0,1,0));
            const auto deltaWorld=glm::angleAxis(angle,glm::normalize(axis));
            auto& local=pose.local[link.bone];const auto parent=skeleton.bones[link.bone].parent;
            const glm::dquat parentRotation=parent>=0&&static_cast<std::size_t>(parent)<pose.global.size()
                ?SafeQuaternion(glm::quat_cast(glm::dmat3(pose.global[parent]))):glm::dquat(1,0,0,0);
            local.rotation=SafeQuaternion(glm::inverse(parentRotation)*deltaWorld*parentRotation*local.rotation);
            if(link.limited) {
                // PMX limits are rest-relative local Euler angles, converted to
                // engine space on import. Clamp all axes after each CCD step.
                const auto rest=SafeQuaternion(skeleton.bones[link.bone].restLocal.rotation);
                auto euler=glm::eulerAngles(SafeQuaternion(glm::inverse(rest)*local.rotation));
                euler=glm::clamp(euler,glm::min(link.minimum,link.maximum),glm::max(link.minimum,link.maximum));
                local.rotation=SafeQuaternion(rest*glm::dquat(euler));
            }
            pose.global=GlobalBoneTransforms(skeleton,pose.local);
        }
    }
    if(error)error->clear();return true;
}

void AddGeneratedPreviewSequences(SkeletalAssetPackage& asset) {
    auto exists=[&](const std::string& name){return std::any_of(asset.animations.begin(),asset.animations.end(),[&](const auto& a){return a.generated&&a.name==name;});};
    if(!exists("BindPose")) {
        AnimationSequenceAsset bind;bind.id=asset.id+"/animations/BindPose";bind.name="BindPose";
        bind.skeletonId=asset.skeleton.id;bind.generated=true;bind.source="generated: bind pose (no source motion)";
        bind.duration=1;asset.animations.push_back(std::move(bind));
    }
    AnimationSequenceAsset preview;preview.id=asset.id+"/animations/RigPreview";preview.name="RigPreview";
    preview.skeletonId=asset.skeleton.id;preview.generated=true;preview.duration=4;preview.framesPerSecond=30;
    preview.source="procedural: rig inspection motion; not an imported performance";
    auto sourcePose=std::find_if(asset.animations.begin(),asset.animations.end(),[](const auto& sequence){return sequence.generated&&sequence.name=="ShoesFit";});
    if(sourcePose==asset.animations.end())sourcePose=std::find_if(asset.animations.begin(),asset.animations.end(),[](const auto& sequence){return sequence.generated&&sequence.name=="SourcePose";});
    const bool hasSourcePose=sourcePose!=asset.animations.end();
    SkeletalPose baseline;
    if(hasSourcePose){
        baseline=EvaluateSkeletalPose(asset,&*sourcePose,0,false);
        // Blender garments can be authored around posed heels, legs and
        // helper bones. Preserve that complete fit while adding a small upper
        // body inspection motion; untouched bones must not revert to rest.
        for(std::uint32_t bone=0;bone<baseline.local.size();++bone){const auto& value=baseline.local[bone];BoneAnimationTrack track;track.bone=bone;
            track.translations.push_back({0,value.translation});track.rotations.push_back({0,value.rotation});track.scales.push_back({0,value.scale});preview.boneTracks.push_back(std::move(track));}
        for(std::uint32_t morph=0;morph<baseline.morphWeights.size();++morph)if(baseline.morphWeights[morph]!=0){MorphAnimationTrack track;track.morph=morph;track.keys.push_back({0,baseline.morphWeights[morph]});preview.morphTracks.push_back(std::move(track));}
        preview.source+="; relative to generated "+sourcePose->name;
        preview.diagnostics.push_back("Inspection motion baseline: "+sourcePose->name+". Constant bone TRS and nonzero expressions are retained before adding upper-body motion.");
    }
    struct Motion {const char* role;glm::dvec3 axis;double amplitude;};
    const Motion motions[]={{"head",{0,1,0},0.17},{"neck",{0,0,1},0.06},{"chest",{0,1,0},0.045},
        {"leftUpperArm",{0,0,1},-0.13},{"rightUpperArm",{0,0,1},0.13},
        {"leftLowerArm",{0,1,0},0.18},{"rightLowerArm",{0,1,0},-0.18}};
    for(const auto& motion:motions) {
        const int bone=FindRole(asset.ikRig,motion.role);if(bone<0||static_cast<std::size_t>(bone)>=asset.skeleton.bones.size())continue;
        auto existing=std::find_if(preview.boneTracks.begin(),preview.boneTracks.end(),[&](const auto& track){return track.bone==static_cast<std::uint32_t>(bone);});
        if(existing==preview.boneTracks.end()){preview.boneTracks.emplace_back();existing=preview.boneTracks.end()-1;existing->bone=bone;}
        auto& track=*existing;track.rotations.clear();
        const auto rest=hasSourcePose?baseline.local[bone].rotation:asset.skeleton.bones[bone].restLocal.rotation;
        for(int frame=0;frame<=120;++frame) {
            const double t=frame/30.0;const double angle=motion.amplitude*std::sin(t*glm::two_pi<double>()/preview.duration);
            track.rotations.push_back({t,SafeQuaternion(rest*glm::angleAxis(angle,motion.axis))});
        }
    }
    for(std::size_t i=0;i<asset.morphs.size();++i) {
        const auto& morph=asset.morphs[i];const std::string names=morph.name+" "+morph.englishName;
        if(names.find("まばたき")!=std::string::npos||names.find("Blink")!=std::string::npos||names.find("blink")!=std::string::npos) {
            auto existing=std::find_if(preview.morphTracks.begin(),preview.morphTracks.end(),[&](const auto& track){return track.morph==i;});
            if(existing==preview.morphTracks.end()){preview.morphTracks.emplace_back();existing=preview.morphTracks.end()-1;existing->morph=static_cast<std::uint32_t>(i);}
            const double rest=hasSourcePose?baseline.morphWeights[i]:0;
            existing->keys={{0,rest},{1.65,rest},{1.78,1},{1.93,rest},{4,rest}};break;
        }
    }
    const auto existing=std::find_if(asset.animations.begin(),asset.animations.end(),[](const auto& sequence){return sequence.generated&&sequence.name=="RigPreview";});
    if(existing==asset.animations.end())asset.animations.push_back(std::move(preview));
    else *existing=std::move(preview);
}
} // namespace Model
