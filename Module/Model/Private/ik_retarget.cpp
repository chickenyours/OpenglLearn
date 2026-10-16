#include "Model/Public/skeletal_asset.h"
#include <glm/gtx/quaternion.hpp>
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <functional>
#include <limits>
#include <set>
#include <unordered_map>

namespace Model {
namespace {
struct Role { std::string name,parent,next; std::vector<std::string> aliases; };
std::string Normalize(std::string text) {
    // The last namespace is useful for Mixamo and FBX node namespaces. Keep
    // UTF-8 bytes intact so Japanese/Chinese aliases are exact, not transliterated.
    if(auto colon=text.find_last_of(':');colon!=std::string::npos)text=text.substr(colon+1);
    std::string result;
    for(unsigned char c:text)if(c>=128||std::isalnum(c))result+=c>=128?static_cast<char>(c):static_cast<char>(std::tolower(c));
    for(const auto prefix:{"mixamorig","bip001","bip01","jbip"})if(result.starts_with(prefix)){result.erase(0,std::char_traits<char>::length(prefix));break;}
    return result;
}
bool Auxiliary(const SkeletonBone& bone) {
    const auto n=Normalize(bone.name)+" "+Normalize(bone.englishName);
    for(const auto word:{"twist","helper","dummy","nub","jsec","skirt","hair","sleeve","secondary","捩","補助","辅助","袖","髪","スカート","ｉｋ","ＩＫ"})
        if(n.find(word)!=std::string::npos)return true;
    return n.find("ik")!=std::string::npos || n.ends_with("end") || n.ends_with("tip");
}
std::vector<Role> Roles() {
    std::vector<Role> roles={
        {"root","","hips",{"全ての親","全親","总骨骼","root","armature","master","global"}},
        {"hips","root","spine",{"hips","pelvis","腰","下半身","body_under","c_hips","センター","center"}},
        {"spine","hips","chest",{"spine","spine0","spine01","c_spine","上半身","脊椎"}},
        {"chest","spine","upperChest",{"chest","spine1","spine02","c_chest","上半身2","胸"}},
        {"upperChest","chest","neck",{"upperchest","spine2","spine03","c_upperchest","上半身3","胸上"}},
        {"neck","upperChest","head",{"neck","neck1","neck01","c_neck","首","颈"}},
        {"head","neck","",{"head","c_head","頭","头"}},
        {"jaw","head","",{"jaw","c_jaw","顎","下颚"}}
    };
    for(bool left:{true,false}) {
        const std::string side=left?"left":"right",letter=left?"l":"r",jp=left?"左":"右";
        auto add=[&](const std::string& part,const std::string& parent,const std::string& next,std::vector<std::string> aliases) {
            const auto upper=static_cast<char>(std::toupper(static_cast<unsigned char>(part.front())));
            const std::string role=side+upper+part.substr(1);
            aliases.push_back(role);aliases.push_back(letter+part);aliases.push_back(part+letter);
            roles.push_back({role,parent,next,std::move(aliases)});
        };
        add("shoulder","upperChest",side+"UpperArm",{jp+"肩",side+"clavicle",letter+"clavicle","clavicle"+letter});
        add("upperArm",side+"Shoulder",side+"LowerArm",{jp+"腕",side+"arm",letter+"arm","arm"+letter});
        add("lowerArm",side+"UpperArm",side+"Hand",{jp+"ひじ",jp+"肘",side+"forearm",letter+"forearm","forearm"+letter});
        add("hand",side+"LowerArm","",{jp+"手首",side+"wrist",letter+"wrist","wrist"+letter});
        add("upperLeg","hips",side+"LowerLeg",{jp+"足",jp+"太もも",side+"upleg",side+"thigh",letter+"thigh","thigh"+letter});
        add("lowerLeg",side+"UpperLeg",side+"Foot",{jp+"ひざ",jp+"膝",side+"leg",side+"calf",letter+"calf","calf"+letter});
        add("foot",side+"LowerLeg",side+"Toes",{jp+"足首",side+"ankle",letter+"ankle"});
        add("toes",side+"Foot","",{jp+"つま先",jp+"足先",side+"toebase",letter+"toebase",side+"toe",letter+"toe"});
        add("eye","head","",{jp+"目",jp+"眼"});
        const std::array<std::string,5> fingers={"thumb","index","middle","ring","little"};
        const std::array<std::string,5> fingerJp={"親指","人指","中指","薬指","小指"};
        for(std::size_t f=0;f<fingers.size();++f)for(int segment=1;segment<=3;++segment) {
            const std::string& finger=fingers[f];
            const auto capital=std::string(1,static_cast<char>(std::toupper(finger.front())))+finger.substr(1);
            const std::string suffix=segment==1?"Proximal":segment==2?"Intermediate":"Distal";
            const std::string part=finger+suffix;
            const std::string parent=segment==1?side+"Hand":side+capital+(segment==2?"Proximal":"Intermediate");
            const std::string next=segment<3?side+capital+(segment==1?"Intermediate":"Distal"):"";
            const int mmd=static_cast<int>(f==0?segment-1:segment);
            const std::array<std::string,4> fullDigits={"０","１","２","３"};
            std::vector<std::string> aliases={side+finger+std::to_string(segment),letter+finger+std::to_string(segment),
                side+"hand"+finger+std::to_string(segment),
                jp+fingerJp[f]+std::to_string(mmd),jp+fingerJp[f]+fullDigits[mmd]};
            if(f==1){aliases.push_back(jp+"人差指"+fullDigits[mmd]);aliases.push_back(jp+"人差し指"+fullDigits[mmd]);}
            if(f==4){aliases.push_back(side+"pinky"+std::to_string(segment));aliases.push_back(letter+"pinky"+std::to_string(segment));}
            add(part,parent,next,std::move(aliases));
        }
    }
    for(auto& role:roles)for(auto& alias:role.aliases)alias=Normalize(alias);
    return roles;
}
bool IsAncestor(const SkeletonAsset& skeleton,int ancestor,int child) {
    std::size_t visited=0;
    while(child>=0&&static_cast<std::size_t>(child)<skeleton.bones.size()&&visited++<skeleton.bones.size()) {
        if(child==ancestor)return true;child=skeleton.bones[child].parent;
    }
    return false;
}
const HumanoidBoneMapping* Find(const IkRigAsset& rig,const std::string& role) {
    for(const auto& map:rig.humanoid)if(map.role==role&&map.bone>=0)return &map;
    return nullptr;
}
glm::dquat RotationOf(const glm::dmat4& matrix) {
    glm::dmat3 linear(matrix);
    for(int i=0;i<3;++i){double length=glm::length(linear[i]);if(length>1e-12)linear[i]/=length;}
    auto q=glm::quat_cast(linear);return glm::dot(q,q)>1e-12?glm::normalize(q):glm::dquat(1,0,0,0);
}
glm::dmat3 CharacterAxes(const IkRigAsset& rig,const std::vector<glm::dmat4>& global) {
    auto point=[&](const std::string& role)->std::optional<glm::dvec3> {
        if(const auto mapping=Find(rig,role);mapping&&static_cast<std::size_t>(mapping->bone)<global.size())return glm::dvec3(global[mapping->bone][3]);
        return {};
    };
    glm::dquat fallback(1,0,0,0);
    if(const auto root=Find(rig,"root");root&&static_cast<std::size_t>(root->bone)<global.size())fallback=RotationOf(global[root->bone]);
    glm::dvec3 up=fallback*glm::dvec3(0,1,0),left=fallback*glm::dvec3(1,0,0);
    auto hips=point("hips"),head=point("head");
    if(hips&&head&&glm::length2(*head-*hips)>1e-12)up=glm::normalize(*head-*hips);
    // Named anatomical sides determine facing even when the entire asset was
    // authored along a different DCC forward axis or under a rotated root.
    for(const auto part:{"Shoulder","UpperLeg","UpperArm"}) {
        auto a=point(std::string("left")+part),b=point(std::string("right")+part);
        if(a&&b&&glm::length2(*a-*b)>1e-12){left=*a-*b;break;}
    }
    left-=up*glm::dot(left,up);
    if(glm::length2(left)<1e-12)left=glm::cross(up,std::abs(up.z)<.95?glm::dvec3(0,0,1):glm::dvec3(1,0,0));
    left=glm::normalize(left);const auto forward=glm::normalize(glm::cross(left,up));
    return {left,up,forward};
}
glm::dquat DirectionBasis(const SkeletonAsset& skeleton,const IkRigAsset& rig,
    const std::vector<glm::dmat4>& global,const std::string& role,int index) {
    const auto character=CharacterAxes(rig,global);
    int next=-1;
    for(const auto& def:Roles())if(def.name==role&&!def.next.empty())if(auto map=Find(rig,def.next))next=map->bone;
    glm::dvec3 direction(0);
    if(next>=0&&static_cast<std::size_t>(next)<global.size())direction=glm::dvec3(global[next][3]-global[index][3]);
    if(glm::length2(direction)<1e-12) {
        for(std::size_t i=0;i<skeleton.bones.size();++i)if(skeleton.bones[i].parent==index&&!Auxiliary(skeleton.bones[i])) {
            auto delta=glm::dvec3(global[i][3]-global[index][3]);if(glm::length2(delta)>glm::length2(direction))direction=delta;
        }
    }
    if(glm::length2(direction)<1e-12) {
        const int parent=skeleton.bones[index].parent;
        if(parent>=0&&static_cast<std::size_t>(parent)<global.size())direction=glm::dvec3(global[index][3]-global[parent][3]);
    }
    if(glm::length2(direction)<1e-12)return glm::normalize(glm::quat_cast(character));
    const auto y=glm::normalize(direction);
    glm::dvec3 z=character[2];if(std::abs(glm::dot(y,z))>0.95)z=character[0];
    auto x=glm::normalize(glm::cross(y,z));z=glm::normalize(glm::cross(x,y));
    return glm::normalize(glm::quat_cast(glm::dmat3(x,y,z)));
}
double RigLegLength(const SkeletonAsset& skeleton,const IkRigAsset& rig,const std::vector<glm::dmat4>& global) {
    double total=0;int count=0;
    for(const auto side:{"left","right"}) {
        auto a=Find(rig,std::string(side)+"UpperLeg"),b=Find(rig,std::string(side)+"LowerLeg"),c=Find(rig,std::string(side)+"Foot");
        if(a&&b&&c&&static_cast<std::size_t>(a->bone)<global.size()&&static_cast<std::size_t>(b->bone)<global.size()&&static_cast<std::size_t>(c->bone)<global.size()) {total+=glm::length(glm::dvec3(global[a->bone][3]-global[b->bone][3]))+
            glm::length(glm::dvec3(global[b->bone][3]-global[c->bone][3]));++count;}
    }
    if(count)return total/count;
    double minY=std::numeric_limits<double>::max(),maxY=-minY;
    for(const auto& transform:global){minY=std::min(minY,transform[3].y);maxY=std::max(maxY,transform[3].y);}
    return global.empty()?0:maxY-minY;
}
}

IkRigAsset RecognizeHumanoidRig(const SkeletonAsset& skeleton) {
    IkRigAsset rig;rig.skeletonId=skeleton.id;rig.id=skeleton.id+"/ikrig";rig.name=skeleton.name+" IK Rig";
    const auto roles=Roles();const auto rest=GlobalBoneTransforms(skeleton,RestPose(skeleton));
    struct Candidate{std::size_t role;int bone;float score;std::string method;};
    std::vector<Candidate> candidates;
    for(std::size_t r=0;r<roles.size();++r)for(std::size_t b=0;b<skeleton.bones.size();++b) {
        if(Auxiliary(skeleton.bones[b]))continue;
        const auto native=Normalize(skeleton.bones[b].name),english=Normalize(skeleton.bones[b].englishName);
        for(std::size_t a=0;a<roles[r].aliases.size();++a)if(roles[r].aliases[a]==native||(!english.empty()&&roles[r].aliases[a]==english)) {
            float score=0.99f;
            if(roles[r].name=="hips"&&(roles[r].aliases[a]==Normalize("センター")||roles[r].aliases[a]=="center"||roles[r].aliases[a]==Normalize("下半身")||roles[r].aliases[a]=="bodyunder"))score=0.86f;
            if(roles[r].name=="root"&&native!="全ての親"&&native!="全親")score=0.96f;
            candidates.push_back({r,static_cast<int>(b),score,"exact alias: "+(roles[r].aliases[a]==native?skeleton.bones[b].name:skeleton.bones[b].englishName)});break;
        }
    }
    std::stable_sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){return a.score>b.score;});
    std::vector<int> assigned(roles.size(),-1);std::set<int> used;
    for(const auto& candidate:candidates)if(assigned[candidate.role]<0&&!used.contains(candidate.bone)) {
        assigned[candidate.role]=candidate.bone;used.insert(candidate.bone);
        rig.humanoid.push_back({roles[candidate.role].name,candidate.bone,candidate.score,candidate.method});
    }
    if(!Find(rig,"root")) {
        int root=-1;bool multiple=false;
        for(std::size_t b=0;b<skeleton.bones.size();++b)if(skeleton.bones[b].parent<0){if(root>=0)multiple=true;root=static_cast<int>(b);}
        if(root>=0&&!multiple&&!used.contains(root)){used.insert(root);rig.humanoid.push_back({"root",root,0.75f,"unique hierarchy root"});}
    }
    // A missing named bone can be identified from the path between semantic
    // anchors. Unlike selecting nearest coordinates alone, this preserves limbs
    // even with helper/twist nodes and never guesses a hair or skirt bone.
    for(int pass=0;pass<3;++pass)for(const auto& role:roles)if(!Find(rig,role.name)&&!role.parent.empty()&&!role.next.empty()) {
        const auto parent=Find(rig,role.parent),next=Find(rig,role.next);if(!parent||!next)continue;
        std::vector<int> choices;int index=skeleton.bones[next->bone].parent;
        std::size_t visits=0;
        while(index>=0&&static_cast<std::size_t>(index)<skeleton.bones.size()&&index!=parent->bone&&visits++<skeleton.bones.size()) {
            if(!used.contains(index)&&!Auxiliary(skeleton.bones[index]))choices.push_back(index);
            index=skeleton.bones[index].parent;
        }
        if(index!=parent->bone||choices.empty())continue;
        const auto midpoint=(glm::dvec3(rest[parent->bone][3])+glm::dvec3(rest[next->bone][3]))*0.5;
        const auto selected=*std::min_element(choices.begin(),choices.end(),[&](int a,int b){return glm::length2(glm::dvec3(rest[a][3])-midpoint)<glm::length2(glm::dvec3(rest[b][3])-midpoint);});
        used.insert(selected);rig.humanoid.push_back({role.name,selected,choices.size()==1?0.78f:0.64f,"hierarchy path + rest-position midpoint"});
    }
    // Extend a semantic chain through an unnamed, unambiguous direct child.
    // Geometry is used only when a role anchor already fixes the limb and side.
    for(int pass=0;pass<3;++pass)for(const auto& role:roles)if(!Find(rig,role.name)) {
        const auto parent=Find(rig,role.parent);if(!parent)continue;
        if(role.name=="hips"||role.name=="jaw"||role.name.ends_with("Eye")||role.name.ends_with("Shoulder")||role.name.ends_with("UpperLeg"))continue;
        std::vector<int> choices;
        for(std::size_t b=0;b<skeleton.bones.size();++b)if(skeleton.bones[b].parent==parent->bone&&!used.contains(static_cast<int>(b))&&!Auxiliary(skeleton.bones[b])&&
            glm::length2(glm::dvec3(rest[b][3]-rest[parent->bone][3]))>1e-12)choices.push_back(static_cast<int>(b));
        if(choices.size()==1){used.insert(choices[0]);rig.humanoid.push_back({role.name,choices[0],0.55f,"single-child hierarchy + nonzero rest segment (review)"});}
    }
    for(const auto& role:roles)if(!Find(rig,role.name))rig.diagnostics.push_back("Unmapped humanoid role: "+role.name);
    for(const auto& mapping:rig.humanoid) {
        auto role=std::find_if(roles.begin(),roles.end(),[&](const auto& def){return def.name==mapping.role;});
        if(role!=roles.end())if(auto parent=Find(rig,role->parent))if(!IsAncestor(skeleton,parent->bone,mapping.bone))
            rig.diagnostics.push_back("Hierarchy differs for "+mapping.role+"; rest-space retarget correction is required");
        if(mapping.confidence<0.8f)rig.diagnostics.push_back("Review inferred role "+mapping.role+" -> "+skeleton.bones[mapping.bone].name);
    }
    for(const auto side:{"left","right"})for(bool leg:{true,false}) {
        const std::string prefix(side),upper=prefix+(leg?"UpperLeg":"UpperArm"),lower=prefix+(leg?"LowerLeg":"LowerArm"),end=prefix+(leg?"Foot":"Hand");
        auto a=Find(rig,upper),b=Find(rig,lower),c=Find(rig,end);if(!a||!b||!c)continue;
        IkChain chain;chain.name=prefix+(leg?"Leg":"Arm");chain.role=end;chain.root=a->bone;chain.effector=c->bone;
        chain.target=glm::dvec3(rest[c->bone][3]);chain.iterations=48;chain.angleLimit=0.18;
        chain.links.push_back({static_cast<std::uint32_t>(b->bone),false,glm::dvec3(0),glm::dvec3(0)});
        chain.links.push_back({static_cast<std::uint32_t>(a->bone),false,glm::dvec3(0),glm::dvec3(0)});
        // Generic limb planes vary by DCC; only native PMX chains carry exact
        // authored joint limits. Do not invent a knee axis here.
        rig.chains.push_back(std::move(chain));
    }
    rig.diagnostics.push_back("Recognition is deterministic and one-to-one; unmatched and inferred roles require review. Rest positions cannot determine anatomical twist uniquely.");
    return rig;
}

RetargetMap BuildRetargetMap(const SkeletonAsset& source,const IkRigAsset& sourceRig,
    const SkeletonAsset& target,const IkRigAsset& targetRig) {
    RetargetMap result;result.sourceSkeletonId=source.id;result.targetSkeletonId=target.id;
    if((!sourceRig.skeletonId.empty()&&sourceRig.skeletonId!=source.id)||(!targetRig.skeletonId.empty()&&targetRig.skeletonId!=target.id)) {
        result.diagnostics.push_back("IK rig skeleton reference does not match the supplied skeleton; retarget mapping was not built.");return result;
    }
    const auto sourceRest=GlobalBoneTransforms(source,RestPose(source)),targetRest=GlobalBoneTransforms(target,RestPose(target));
    const double sourceLength=RigLegLength(source,sourceRig,sourceRest),targetLength=RigLegLength(target,targetRig,targetRest);
    if(sourceLength>1e-10&&targetLength>1e-10)result.translationScale=targetLength/sourceLength;
    else result.diagnostics.push_back("Unable to infer translation scale; using 1");
    std::set<int> sourceUsed,targetUsed;
    for(const auto& sm:sourceRig.humanoid) {
        auto tm=Find(targetRig,sm.role);
        if(sm.bone<0||static_cast<std::size_t>(sm.bone)>=source.bones.size()||!tm||static_cast<std::size_t>(tm->bone)>=target.bones.size()) {
            result.diagnostics.push_back("No target for source role: "+sm.role);continue;
        }
        if(sourceUsed.contains(sm.bone)||targetUsed.contains(tm->bone))continue;
        const auto sourceBasis=DirectionBasis(source,sourceRig,sourceRest,sm.role,sm.bone);
        const auto targetBasis=DirectionBasis(target,targetRig,targetRest,sm.role,tm->bone);
        result.bones.push_back({sm.role,sm.bone,tm->bone,std::min(sm.confidence,tm->confidence),
            glm::normalize(targetBasis*glm::inverse(sourceBasis)),"semantic role + anatomical facing + rest segment basis"});
        sourceUsed.insert(sm.bone);targetUsed.insert(tm->bone);
    }
    // Exact full names provide safe auxiliary-bone mappings between exports of
    // the same rig. Ambiguous duplicate names remain explicitly unmapped.
    for(std::size_t s=0;s<source.bones.size();++s)if(!sourceUsed.contains(static_cast<int>(s))) {
        const auto name=Normalize(source.bones[s].name);if(name.empty())continue;
        int matched=-1;bool ambiguous=false;
        for(std::size_t t=0;t<target.bones.size();++t)if(!targetUsed.contains(static_cast<int>(t))&&Normalize(target.bones[t].name)==name){if(matched>=0)ambiguous=true;matched=static_cast<int>(t);}
        if(matched>=0&&!ambiguous) {
            result.bones.push_back({"",static_cast<int>(s),matched,0.8f,
                glm::normalize(RotationOf(targetRest[matched])*glm::inverse(RotationOf(sourceRest[s]))),"unique exact normalized bone name"});
            sourceUsed.insert(static_cast<int>(s));targetUsed.insert(matched);
        }
    }
    for(const auto& tm:targetRig.humanoid)if(!targetUsed.contains(tm.bone))result.diagnostics.push_back("Target role kept at rest: "+tm.role);
    result.diagnostics.push_back("Facing aligns from named left/right shoulders or hips and hips-to-head up; translations use target rest lengths and root/hips motion scales by average rest leg length. No contact solving or physical simulation is implied.");
    if(source.id!=target.id)result.diagnostics.push_back("Cross-skeleton morph curves require a separate named morph map; source morph indices are not copied to target expressions.");
    return result;
}

SkeletalPose RetargetPose(const SkeletonAsset& source,const SkeletalPose& sourcePose,
    const SkeletonAsset& target,const RetargetMap& mapping) {
    SkeletalPose result;result.local=RestPose(target);
    if(source.id==target.id)result.morphWeights=sourcePose.morphWeights;
    if((!mapping.sourceSkeletonId.empty()&&mapping.sourceSkeletonId!=source.id)||(!mapping.targetSkeletonId.empty()&&mapping.targetSkeletonId!=target.id)) {
        result.global=GlobalBoneTransforms(target,result.local);return result;
    }
    const auto sourceRest=GlobalBoneTransforms(source,RestPose(source));
    const auto targetRest=GlobalBoneTransforms(target,result.local);
    const auto sourceGlobal=sourcePose.global.size()==source.bones.size()?sourcePose.global:GlobalBoneTransforms(source,sourcePose.local);
    std::vector<const RetargetBoneMapping*> byTarget(target.bones.size(),nullptr);
    for(const auto& bone:mapping.bones)if(bone.sourceBone>=0&&static_cast<std::size_t>(bone.sourceBone)<source.bones.size()&&
        bone.targetBone>=0&&static_cast<std::size_t>(bone.targetBone)<target.bones.size())byTarget[bone.targetBone]=&bone;
    std::vector<std::uint8_t> visited(target.bones.size(),0);result.global.resize(target.bones.size(),glm::dmat4(1));
    std::vector<int> order,path;
    for(std::size_t i=0;i<target.bones.size();++i)if(!visited[i]) {
        path.clear();int cursor=static_cast<int>(i);
        while(cursor>=0&&static_cast<std::size_t>(cursor)<target.bones.size()&&!visited[cursor]) {
            visited[cursor]=1;path.push_back(cursor);cursor=target.bones[cursor].parent;
        }
        while(!path.empty()){order.push_back(path.back());path.pop_back();}
    }
    std::fill(visited.begin(),visited.end(),0);
    for(int t:order) {
        const int parent=target.bones[t].parent;
        const bool validParent=parent>=0&&static_cast<std::size_t>(parent)<target.bones.size()&&visited[parent];
        if(const auto bone=byTarget[t]) {
            const int s=bone->sourceBone;const auto correction=bone->basisCorrection;
            const auto delta=RotationOf(sourceGlobal[s])*glm::inverse(RotationOf(sourceRest[s]));
            const auto desired=glm::normalize(correction*delta*glm::inverse(correction)*RotationOf(targetRest[t]));
            const auto parentRotation=validParent?RotationOf(result.global[parent]):glm::dquat(1,0,0,0);
            result.local[t].rotation=glm::normalize(glm::inverse(parentRotation)*desired);
            if(bone->role=="root"||bone->role=="hips") {
                const auto displacement=glm::dvec3(sourceGlobal[s][3]-sourceRest[s][3])*mapping.translationScale;
                // Preserve target rest translation and apply only local motion;
                // parent displacement is removed to avoid double root motion.
                auto desiredPosition=glm::dvec3(targetRest[t][3])+correction*displacement;
                if(validParent)result.local[t].translation=glm::dvec3(glm::inverse(result.global[parent])*glm::dvec4(desiredPosition,1));
                else result.local[t].translation=desiredPosition;
            }
            if(static_cast<std::size_t>(s)<sourcePose.local.size()) {
                const auto sourceScale=source.bones[s].restLocal.scale;
                for(int axis=0;axis<3;++axis)if(std::abs(sourceScale[axis])>1e-12)
                    result.local[t].scale[axis]*=sourcePose.local[s].scale[axis]/sourceScale[axis];
            }
        }
        result.global[t]=(validParent?result.global[parent]:glm::dmat4(1))*TransformMatrix(result.local[t]);visited[t]=2;
    }
    return result;
}
} // namespace Model
