#include "Model/Public/vmd_animation.h"
#include "animation_limits.h"
#include <glm/gtx/quaternion.hpp>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <functional>
#include <iomanip>
#include <limits>
#include <queue>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace Model {
namespace {
constexpr double Epsilon=1e-12;
bool Finite(double v){return std::isfinite(v);}
bool Finite(glm::dvec3 v){return Finite(v.x)&&Finite(v.y)&&Finite(v.z);}
bool Finite(glm::dquat q){return Finite(q.w)&&Finite(q.x)&&Finite(q.y)&&Finite(q.z)&&Finite(glm::dot(q,q))&&glm::dot(q,q)>Epsilon;}
glm::dquat Unit(glm::dquat q){if(!Finite(q))throw std::runtime_error("Animation contains a nonfinite or zero quaternion");return glm::normalize(q);}
std::string Normalize(std::string text) {
    if(auto colon=text.find_last_of(':');colon!=std::string::npos)text.erase(0,colon+1);
    std::string result;
    for(std::size_t i=0;i<text.size();) {
        const auto byte=static_cast<unsigned char>(text[i]);
        if(byte<128){if(std::isalnum(byte))result.push_back(static_cast<char>(std::tolower(byte)));++i;continue;}
        std::size_t length=(byte&0xe0)==0xc0?2:(byte&0xf0)==0xe0?3:(byte&0xf8)==0xf0?4:1;
        if(i+length>text.size())length=1;
        std::uint32_t code=byte&((1u<<(7-length))-1u);
        for(std::size_t k=1;k<length;++k)code=(code<<6)|(static_cast<unsigned char>(text[i+k])&0x3f);
        if(code>=0xff01&&code<=0xff5e){auto c=static_cast<unsigned char>(code-0xfee0);if(std::isalnum(c))result.push_back(static_cast<char>(std::tolower(c)));}
        else if(code!=0x3000)result.append(text,i,length);
        i+=length;
    }
    for(auto prefix:{"mixamorig","bip001","bip01","jbip"})if(result.starts_with(prefix)){result.erase(0,std::char_traits<char>::length(prefix));break;}
    for(const auto& [from,to]:std::initializer_list<std::pair<const char*,const char*>>{{"膝","ひざ"},{"肘","ひじ"},{"人差し指","人指"},{"人差指","人指"}}) {
        for(std::size_t pos=0;(pos=result.find(from,pos))!=std::string::npos;pos+=std::char_traits<char>::length(to))result.replace(pos,std::char_traits<char>::length(from),to);
    }
    return result;
}
std::string MorphRole(const std::string& name) {
    const auto n=Normalize(name);
    const std::vector<std::vector<std::string>> groups={
        {"blink","まばたき","瞬き","fcleyeclose","eyesclosed"},
        {"blinkleft","ウィンク","ウィンク2","fcleyecloser","wink","winkleft"},
        {"blinkright","ウィンク右","ｳｨﾝｸ2右","fcleyeclosel","winkright"},
        {"aa","あ","fclmtha","a"},{"ih","い","fclmthi","i"},
        {"ou","う","fclmthu","u"},{"ee","え","fclmthe","e"},{"oh","お","fclmtho","o"},
        {"angry","怒","fclallangry"},{"happy","喜","joy","fclalljoy"},
        {"relaxed","楽","fun","fclallfun"},{"sad","哀","sorrow","fclallsorrow"},
        {"surprised","驚","fclallsurprised"},
        {"mouthUp","口角上げ","fclmthup"},{"mouthDown","口角下げ","fclmthdown"}};
    for(const auto& group:groups)for(const auto& alias:group)if(n==Normalize(alias))return group.front();
    return {};
}
std::string ControlRole(const std::string& name) {
    const auto n=Normalize(name);
    for(const auto& [role,aliases]:std::vector<std::pair<std::string,std::vector<std::string>>>{
        {"center",{"センター","center"}},{"groove",{"グルーブ","groove"}},
        {"eyes",{"両目","eyes","botheyes"}},
        {"leftLegIk",{"左足ik","legikl","leftlegik","leftfootik"}},
        {"rightLegIk",{"右足ik","legikr","rightlegik","rightfootik"}},
        {"leftToeIk",{"左つま先ik","toeikl","lefttoeik"}},
        {"rightToeIk",{"右つま先ik","toeikr","righttoeik"}},
        {"leftArmTwist",{"左腕捩","左腕twist","leftarmtwist"}},
        {"rightArmTwist",{"右腕捩","右腕twist","rightarmtwist"}},
        {"leftWristTwist",{"左手捩","左ひじtwist","leftwristtwist"}},
        {"rightWristTwist",{"右手捩","右ひじtwist","rightwristtwist"}}})
        for(const auto& alias:aliases)if(n==Normalize(alias))return role;
    return {};
}
struct TargetName {std::string name,english,role;};
struct Matched {int target=-1;float score=0;std::string method;};
std::vector<Matched> MatchNames(const std::vector<std::string>& names,const std::vector<TargetName>& targets,
    const std::vector<std::string>& sourceRoles,bool morph) {
    struct Candidate{std::size_t source,target;float score;const char* method;};
    std::vector<Candidate> candidates;
    for(std::size_t s=0;s<names.size();++s)for(std::size_t t=0;t<targets.size();++t) {
        const auto n=Normalize(names[s]),a=Normalize(targets[t].name),b=Normalize(targets[t].english);
        float score=0;const char* method="";
        if(names[s]==targets[t].name&&!names[s].empty()){score=1;method="exact native name";}
        else if(names[s]==targets[t].english&&!names[s].empty()){score=.99f;method="exact English name";}
        else if(!n.empty()&&(n==a||n==b)){score=.97f;method="normalized name / fullwidth / namespace alias";}
        else if(!sourceRoles[s].empty()&&sourceRoles[s]==targets[t].role){score=morph?.88f:.90f;method=morph?"named expression alias":"MMD / humanoid semantic alias";}
        if(score>0)candidates.push_back({s,t,score,method});
    }
    std::stable_sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){return a.score>b.score;});
    std::vector<Matched> result(names.size());std::set<std::size_t> used;
    for(const auto& candidate:candidates)if(result[candidate.source].target<0&&!used.contains(candidate.target)) {
        // Duplicate target aliases with equal evidence are ambiguous, rather
        // than depending on arbitrary bone order in the imported file.
        bool ambiguous=false;
        for(const auto& other:candidates)if(other.source==candidate.source&&other.target!=candidate.target&&
            other.score==candidate.score&&!used.contains(other.target)){ambiguous=true;break;}
        if(ambiguous)continue;
        result[candidate.source]={static_cast<int>(candidate.target),candidate.score,candidate.method};used.insert(candidate.target);
    }
    return result;
}
std::vector<std::string> BoneRoles(const std::vector<std::string>& names) {
    SkeletonAsset named;named.id="vmd-name-probe";
    for(const auto& name:names){SkeletonBone bone;bone.name=name;named.bones.push_back(std::move(bone));}
    std::vector<std::string> roles(names.size());
    for(const auto& map:RecognizeHumanoidRig(named).humanoid)if(map.bone>=0&&static_cast<std::size_t>(map.bone)<roles.size())roles[map.bone]=map.role;
    for(std::size_t i=0;i<names.size();++i)if(auto special=ControlRole(names[i]);!special.empty())roles[i]=std::move(special);
    return roles;
}
std::vector<TargetName> BoneTargets(const SkeletalAssetPackage& target) {
    std::vector<TargetName> targets;for(const auto& bone:target.skeleton.bones)targets.push_back({bone.name,bone.englishName,{}});
    const auto rig=target.ikRig.humanoid.empty()?RecognizeHumanoidRig(target.skeleton):target.ikRig;
    for(const auto& map:rig.humanoid)if(map.bone>=0&&static_cast<std::size_t>(map.bone)<targets.size())targets[map.bone].role=map.role;
    for(auto& t:targets){auto control=ControlRole(t.name);if(control.empty())control=ControlRole(t.english);if(!control.empty())t.role=control;}
    return targets;
}
std::vector<TargetName> MorphTargets(const SkeletalAssetPackage& target) {
    std::vector<TargetName> result;for(const auto& morph:target.morphs) {
        auto role=MorphRole(morph.name);if(role.empty())role=MorphRole(morph.englishName);
        result.push_back({morph.name,morph.englishName,std::move(role)});
    }return result;
}
std::vector<double> SampleTimes(double duration,const AnimationBakeOptions& options,std::size_t channels,
    const char* channelDescription="target bone/expression channels") {
    if(!Finite(duration)||duration<0)throw std::runtime_error("Animation duration must be finite and nonnegative");
    if(!Finite(options.framesPerSecond)||options.framesPerSecond<1||options.framesPerSecond>240)
        throw std::runtime_error("Bake rate must be finite and between 1 and 240 frames per second");
    if(!Finite(options.translationScale)||options.translationScale<=0||options.translationScale>1000000)
        throw std::runtime_error("Bake translation scale must be finite, positive and at most 1000000");
    const double intervals=std::ceil(duration*options.framesPerSecond);
    if(!Finite(intervals)||intervals>=AnimationLimits::MaximumSamples) {
        std::ostringstream detail;detail<<"Animation bake requires "<<intervals+1<<" samples at "<<options.framesPerSecond
            <<" FPS; sample budget is "<<AnimationLimits::MaximumSamples<<". No frames were truncated.";
        throw std::runtime_error(detail.str());
    }
    const auto samples=static_cast<std::size_t>(intervals)+1;
    if(samples>AnimationLimits::MaximumSampledChannels/std::max<std::size_t>(1,channels)) {
        std::ostringstream detail;detail<<"Animation bake requires "<<samples<<" samples x "<<channels
            <<' '<<channelDescription<<"; evaluation budget is "<<AnimationLimits::MaximumSampledChannels
            <<" channel samples. No frames were truncated.";
        throw std::runtime_error(detail.str());
    }
    std::vector<double> times;times.reserve(static_cast<std::size_t>(intervals)+1);
    for(std::size_t i=0;i<=static_cast<std::size_t>(intervals);++i) {
        const auto time=std::min(duration,i/options.framesPerSecond);
        if(times.empty()||time>times.back())times.push_back(time);
    }
    return times;
}
std::vector<int> DependencyOrder(const SkeletonAsset& skeleton,bool append) {
    if(skeleton.bones.empty()||skeleton.id.empty())throw std::runtime_error("Target skeleton is empty or has no identity");
    std::vector<std::vector<int>> outgoing(skeleton.bones.size());std::vector<int> degree(skeleton.bones.size(),0);
    for(std::size_t b=0;b<skeleton.bones.size();++b) {
        const auto& bone=skeleton.bones[b];
        if(!Finite(bone.restLocal.translation)||!Finite(bone.restLocal.rotation)||!Finite(bone.restLocal.scale))throw std::runtime_error("Nonfinite target rest pose");
        if(bone.parent < -1||bone.inheritParent < -1)throw std::runtime_error("Invalid negative skeleton / append parent index");
        for(int axis=0;axis<3;++axis)if(std::abs(bone.restLocal.scale[axis])<Epsilon)throw std::runtime_error("Zero rest-pose scale cannot be baked");
        std::set<int> dependencies;
        for(int parent:{bone.parent,append&&(bone.sourceFlags&0x300)?bone.inheritParent:-1})if(parent>=0) {
            if(static_cast<std::size_t>(parent)>=skeleton.bones.size()||parent==static_cast<int>(b))throw std::runtime_error("Invalid skeleton parent / append dependency");
            dependencies.insert(parent);
        }
        for(int parent:dependencies){outgoing[parent].push_back(static_cast<int>(b));++degree[b];}
        if(!Finite(bone.inheritWeight))throw std::runtime_error("Nonfinite PMX append weight");
    }
    std::priority_queue<int,std::vector<int>,std::greater<>> ready;
    for(std::size_t b=0;b<degree.size();++b)if(!degree[b])ready.push(static_cast<int>(b));
    std::vector<int> order;while(!ready.empty()){auto b=ready.top();ready.pop();order.push_back(b);for(int next:outgoing[b])if(!--degree[next])ready.push(next);}
    if(order.size()!=skeleton.bones.size())throw std::runtime_error("Cycle in skeleton hierarchy / PMX append dependencies");
    return order;
}
void AddMatches(const std::vector<std::string>& names,const std::vector<Matched>& matches,const std::vector<TargetName>& targets,
    bool morph,AnimationBakeReport& report) {
    for(std::size_t i=0;i<names.size();++i) {
        const auto& m=matches[i];report.matches.push_back({names[i],m.target>=0?targets[m.target].name:"",m.target>=0?m.method:"unmatched or ambiguous",m.target,m.score,morph});
        if(m.target>=0){if(morph)++report.matchedMorphs;else ++report.matchedBones;}
        else {if(morph)++report.unmatchedMorphs;else ++report.unmatchedBones;report.warnings.push_back(std::string("Unmatched ")+(morph?"expression: ":"VMD bone: ")+names[i]);}
    }
}
std::string Identifier(const std::string& seed) {
    std::uint64_t hash=1469598103934665603ull;for(unsigned char c:seed){hash^=c;hash*=1099511628211ull;}
    std::ostringstream out;out<<"animation-"<<std::hex<<hash;return out.str();
}
std::string Utf8Path(const std::filesystem::path& path) {
    const auto bytes=path.generic_u8string();return {reinterpret_cast<const char*>(bytes.data()),bytes.size()};
}
std::string TargetSignature(const SkeletalAssetPackage& asset) {
    std::ostringstream out;out<<std::setprecision(17)<<asset.skeleton.id<<'\n';
    for(const auto& b:asset.skeleton.bones) {
        out<<b.name.size()<<':'<<b.name<<b.englishName.size()<<':'<<b.englishName<<' '<<b.parent<<' '<<b.sourceFlags<<' '<<b.inheritParent<<' '<<b.inheritWeight;
        for(auto v:{b.restLocal.translation,b.restLocal.scale,b.fixedAxis})out<<' '<<v.x<<' '<<v.y<<' '<<v.z;
        auto q=b.restLocal.rotation;out<<' '<<q.w<<' '<<q.x<<' '<<q.y<<' '<<q.z<<'\n';
    }
    for(const auto& m:asset.morphs)out<<m.name.size()<<':'<<m.name<<m.englishName.size()<<':'<<m.englishName<<'\n';
    std::vector<std::string> roles;for(const auto& m:asset.ikRig.humanoid)roles.push_back(m.role+":"+std::to_string(m.bone));
    std::sort(roles.begin(),roles.end());for(const auto& role:roles)out<<role<<'\n';
    for(const auto& c:asset.ikRig.chains)if(c.native) {
        out<<c.name<<' '<<c.targetBone<<' '<<c.effector<<' '<<c.iterations<<' '<<c.angleLimit<<' '<<c.enabled;
        for(const auto& l:c.links)out<<' '<<l.bone<<' '<<l.limited<<' '<<l.minimum.x<<' '<<l.minimum.y<<' '<<l.minimum.z<<' '<<l.maximum.x<<' '<<l.maximum.y<<' '<<l.maximum.z;
        out<<'\n';
    }
    return out.str();
}
std::string OptionsSignature(const AnimationBakeOptions& options) {
    std::ostringstream out;out<<std::setprecision(17)<<options.framesPerSecond<<' '<<options.translationScale<<' '<<options.solveNativeIk<<' '<<options.evaluateAppend;return out.str();
}
std::string MappingSignature(const RetargetMap& mapping) {
    std::vector<std::string> entries;
    for(const auto& m:mapping.bones) {
        std::ostringstream out;out<<std::setprecision(17)<<m.role.size()<<':'<<m.role<<' '<<m.sourceBone<<' '<<m.targetBone<<' ';
        const auto q=m.basisCorrection;out<<q.w<<' '<<q.x<<' '<<q.y<<' '<<q.z;entries.push_back(out.str());
    }
    std::sort(entries.begin(),entries.end());std::ostringstream out;out<<std::setprecision(17)<<mapping.translationScale<<'\n';
    for(const auto& entry:entries)out<<entry<<'\n';return out.str();
}
struct Collector {
    const SkeletalAssetPackage& target;std::vector<BoneAnimationTrack> bones;std::vector<MorphAnimationTrack> morphs;
    const std::vector<double>& times;std::size_t sample=0,storedKeys=0;
    Collector(const SkeletalAssetPackage& a,const std::vector<double>& sampleTimes)
        :target(a),bones(a.skeleton.bones.size()),morphs(a.morphs.size()),times(sampleTimes) {
        for(std::size_t b=0;b<bones.size();++b)bones[b].bone=static_cast<std::uint32_t>(b);
        for(std::size_t m=0;m<morphs.size();++m)morphs[m].morph=static_cast<std::uint32_t>(m);
    }
    template<class Key> void AddKey(std::vector<Key>& keys,const Key& key,std::size_t channel,const char* kind) {
        // A still-constant channel only owns its first value. When it first
        // changes, materialize every prior sample at its original exact time;
        // animated channels then keep every sample, with no lossy reduction.
        if(keys.size()==1&&keys.front().value==key.value)return;
        const auto growth=keys.size()==1?sample:std::size_t(1);
        if(growth>AnimationLimits::MaximumStoredKeys-storedKeys) {
            std::ostringstream detail;detail<<"Animation bake would retain "<<storedKeys+growth<<" keys at sample "
                <<sample+1<<" / "<<times.size()<<" ("<<kind<<" "<<channel
                <<"); stored-key budget is "<<AnimationLimits::MaximumStoredKeys<<". No frames were truncated.";
            throw std::runtime_error(detail.str());
        }
        if(keys.size()==1) {
            const auto value=keys.front().value;keys.reserve(sample+1);
            for(std::size_t i=1;i<sample;++i)keys.push_back({times[i],value});
        }
        keys.push_back(key);storedKeys+=growth;
    }
    void Add(double time,const SkeletalPose& pose) {
        if(pose.local.size()!=bones.size())throw std::runtime_error("Baked pose bone count changed");
        if(sample>=times.size()||time!=times[sample])throw std::runtime_error("Baked sample schedule changed");
        for(std::size_t b=0;b<bones.size();++b) {
            const auto& p=pose.local[b];auto q=Unit(p.rotation);
            if(!Finite(p.translation)||!Finite(p.scale))throw std::runtime_error("Baked pose contains a nonfinite transform");
            if(!bones[b].rotations.empty()&&glm::dot(q,bones[b].rotations.back().value)<0)q=-q;
            AddKey(bones[b].translations,VectorKey{time,p.translation},b,"bone translation");
            AddKey(bones[b].rotations,RotationKey{time,q},b,"bone rotation");
            AddKey(bones[b].scales,VectorKey{time,p.scale},b,"bone scale");
        }
        for(std::size_t m=0;m<morphs.size();++m) {
            const double weight=m<pose.morphWeights.size()?pose.morphWeights[m]:0;
            if(!Finite(weight))throw std::runtime_error("Baked pose contains a nonfinite expression weight");
            AddKey(morphs[m].keys,ScalarKey{time,weight},m,"expression");
        }
        ++sample;
    }
    void Finish(AnimationSequenceAsset& sequence) {
        // Only remove truly constant channels. Animated sampled keys are kept
        // without a lossy key-reduction tolerance, including the last endpoint.
        auto vectorChannel=[](std::vector<VectorKey>& keys,glm::dvec3 rest) {
            bool same=true;for(const auto& k:keys)same&=k.value==keys.front().value;
            if(same){if(keys.front().value==rest)keys.clear();else keys.resize(1);}
        };
        for(std::size_t b=0;b<bones.size();++b) {
            auto& track=bones[b];const auto& rest=target.skeleton.bones[b].restLocal;
            vectorChannel(track.translations,rest.translation);vectorChannel(track.scales,rest.scale);
            bool same=true;for(const auto& k:track.rotations)same&=k.value==track.rotations.front().value;
            if(same){auto r=Unit(rest.rotation);auto q=track.rotations.front().value;if(q==r||q==-r)track.rotations.clear();else track.rotations.resize(1);}
            if(!track.translations.empty()||!track.rotations.empty()||!track.scales.empty())sequence.boneTracks.push_back(std::move(track));
        }
        for(auto& track:morphs) {
            bool same=true;for(const auto& k:track.keys)same&=k.value==track.keys.front().value;
            if(same){if(track.keys.front().value==0)track.keys.clear();else track.keys.resize(1);}
            if(!track.keys.empty())sequence.morphTracks.push_back(std::move(track));
        }
    }
};
void AppendPose(const SkeletonAsset& skeleton,const std::vector<int>& order,const std::vector<BoneTransform>& base,
    const std::vector<glm::dquat>& ikCorrection,bool append,SkeletalPose& pose,std::vector<glm::dquat>& appendRotation) {
    pose.local=base;appendRotation.assign(base.size(),glm::dquat(1,0,0,0));std::vector<glm::dvec3> appendTranslation(base.size(),glm::dvec3(0));
    for(int b:order) {
        const auto& bone=skeleton.bones[b];const int source=bone.inheritParent;
        if(append&&source>=0&&(bone.sourceFlags&0x300)) {
            const auto& parent=skeleton.bones[source];const bool local=(bone.sourceFlags&0x80)!=0;
            if(bone.sourceFlags&0x100) {
                // PMX grants are animation deltas, never absolute bind/world
                // rotations. Nonlocal grants propagate a source grant chain;
                // local grants take its own animated delta instead. IK enters
                // both paths. This follows PMXNode's MMD append convention.
                auto delta=(!local&&parent.inheritParent>=0&&(parent.sourceFlags&0x100))?appendRotation[source]:Unit(glm::inverse(Unit(parent.restLocal.rotation))*base[source].rotation);
                delta=Unit(ikCorrection[source]*delta);if(delta.w<0)delta=-delta;
                appendRotation[b]=Unit(glm::slerp(glm::dquat(1,0,0,0),delta,static_cast<double>(bone.inheritWeight)));
            }
            if(bone.sourceFlags&0x200) {
                const auto delta=(!local&&parent.inheritParent>=0&&(parent.sourceFlags&0x200))?appendTranslation[source]:base[source].translation-parent.restLocal.translation;
                appendTranslation[b]=delta*static_cast<double>(bone.inheritWeight);
            }
        }
        pose.local[b].translation+=appendTranslation[b];
        pose.local[b].rotation=Unit(ikCorrection[b]*base[b].rotation*appendRotation[b]);
    }
    pose.global=GlobalBoneTransforms(skeleton,pose.local);
}
bool HasAnimatedAncestor(int bone,const SkeletonAsset& skeleton,const std::vector<bool>& animated,bool controlsOnly) {
    for(std::size_t n=0;bone>=0&&static_cast<std::size_t>(bone)<skeleton.bones.size()&&n<skeleton.bones.size();++n,bone=skeleton.bones[bone].parent)
        if(animated[bone]&&(!controlsOnly||Normalize(skeleton.bones[bone].name).find("ik")!=std::string::npos||Normalize(skeleton.bones[bone].englishName).find("ik")!=std::string::npos))return true;
    return false;
}
bool ToggleMatches(const std::string& name,const IkChain& chain,const SkeletonAsset& skeleton) {
    if(chain.targetBone<0||static_cast<std::size_t>(chain.targetBone)>=skeleton.bones.size())return false;
    const auto& bone=skeleton.bones[chain.targetBone];const auto normalized=Normalize(name),role=ControlRole(name);
    for(const auto& candidate:{chain.name,bone.name,bone.englishName}) {
        if(!normalized.empty()&&normalized==Normalize(candidate))return true;
        if(!role.empty()&&role==ControlRole(candidate))return true;
    }
    return false;
}
void CopyDiagnostics(AnimationSequenceAsset& sequence,const AnimationBakeReport& report) {
    sequence.diagnostics=report.warnings;
    sequence.diagnostics.insert(sequence.diagnostics.begin(),"Baked ordinary local TRS and named expression curves; playback does not require the source motion, IK or PMX append evaluation.");
}
void CheckAnimation(const AnimationSequenceAsset& sequence,const SkeletalAssetPackage& source) {
    std::size_t total=0;std::set<std::uint32_t> bones,morphs;
    auto check=[&](const auto& keys) {
        double previous=-1;
        for(const auto& key:keys) {
            if(!Finite(key.time)||key.time<0||key.time>sequence.duration||key.time<=previous)throw std::runtime_error("Source animation key times must be finite, strictly ordered and inside duration");
            previous=key.time;
            if(!Finite(key.value))throw std::runtime_error("Source animation contains nonfinite / zero rotation key data");
        }
        if(keys.size()>AnimationLimits::MaximumStoredKeys-total)throw std::runtime_error("Source animation exceeds the 2000000 stored-key budget");
        total+=keys.size();
    };
    for(const auto& track:sequence.boneTracks) {
        if(track.bone>=source.skeleton.bones.size()||!bones.insert(track.bone).second)throw std::runtime_error("Invalid or duplicate source animation bone track");
        check(track.translations);check(track.rotations);check(track.scales);
        for(const auto& key:track.scales)for(int axis=0;axis<3;++axis)if(std::abs(key.value[axis])<Epsilon)throw std::runtime_error("Source animation scale keys must be nonzero");
    }
    for(const auto& track:sequence.morphTracks) {
        if(track.morph>=source.morphs.size()||!morphs.insert(track.morph).second)throw std::runtime_error("Invalid or duplicate source animation expression track");
        check(track.keys);
    }
}
} // namespace

std::optional<AnimationSequenceAsset> BakeVmdAnimation(const VmdMotion& motion,const SkeletalAssetPackage& target,
    const AnimationBakeOptions& options,const std::string& name,AnimationBakeReport* output,std::string* error) {
    AnimationBakeReport report;
    try {
        const auto order=DependencyOrder(target.skeleton,options.evaluateAppend);
        if(!Finite(motion.duration)||motion.duration<0)throw std::runtime_error("VMD animation duration must be finite and nonnegative");
        double duration=motion.duration;
        for(const auto& track:motion.bones) {std::int64_t previous=-1;for(const auto& key:track.keys) {
            if(!Finite(key.translation)||!Finite(key.rotation))throw std::runtime_error("Nonfinite VMD bone key");
            if(key.frame<=previous)throw std::runtime_error("VMD bone frames must be strictly ordered");previous=key.frame;
            duration=std::max(duration,key.frame/30.0);
        }}
        for(const auto& track:motion.morphs) {std::int64_t previous=-1;for(const auto& key:track.keys) {
            if(!Finite(key.weight))throw std::runtime_error("Nonfinite VMD expression key");
            if(key.frame<=previous)throw std::runtime_error("VMD expression frames must be strictly ordered");previous=key.frame;
            duration=std::max(duration,key.frame/30.0);
        }}
        const auto times=SampleTimes(duration,options,target.skeleton.bones.size()+target.morphs.size());
        std::vector<std::string> boneNames,morphNames,morphRoles;
        for(const auto& track:motion.bones)boneNames.push_back(track.name);
        for(const auto& track:motion.morphs){morphNames.push_back(track.name);morphRoles.push_back(MorphRole(track.name));}
        const auto boneTargets=BoneTargets(target),morphTargets=MorphTargets(target);
        auto boneMatches=MatchNames(boneNames,boneTargets,BoneRoles(boneNames),false);
        auto morphMatches=MatchNames(morphNames,morphTargets,morphRoles,true);
        // Empty tracks are not valid evidence for motion applicability.
        for(std::size_t i=0;i<boneMatches.size();++i)if(motion.bones[i].keys.empty())boneMatches[i]={};
        for(std::size_t i=0;i<morphMatches.size();++i)if(motion.morphs[i].keys.empty())morphMatches[i]={};
        AddMatches(boneNames,boneMatches,boneTargets,false,report);AddMatches(morphNames,morphMatches,morphTargets,true,report);
        if(!report.matchedBones&&!report.matchedMorphs)throw std::runtime_error("VMD has no usable bone or expression tracks matching the target; camera-only motion is not a character animation");
        report.warnings.insert(report.warnings.end(),motion.warnings.begin(),motion.warnings.end());
        if(report.unmatchedMorphs) {
            std::size_t active=0,zero=0;
            for(std::size_t m=0;m<morphMatches.size();++m)if(morphMatches[m].target<0) {
                const bool changes=std::any_of(motion.morphs[m].keys.begin(),motion.morphs[m].keys.end(),[](const auto& k){return std::abs(k.weight)>1e-8;});
                if(changes)++active;else ++zero;
            }
            report.warnings.push_back("Unmatched expression tracks: "+std::to_string(active)+" contain nonzero weights; "+std::to_string(zero)+" are constant zero / empty and carry no visible expression motion.");
        }
        report.warnings.push_back("VMD contains local MMD deltas but no source rest skeleton or body dimensions. Name/semantic matching uses target rest transforms and the explicit translation scale; cross-body contact accuracy requires a source skeleton.");
        if(motion.cameraKeys||motion.lightKeys||motion.shadowKeys)report.warnings.push_back("Camera, light and shadow keys are not character-animation curves and were not baked.");
        if(std::any_of(motion.ikKeys.begin(),motion.ikKeys.end(),[](const auto& k){return !k.visible;}))report.warnings.push_back("VMD model visibility is not representable in AnimationSequence; IK switches are baked, visibility keys are metadata only.");
        if(target.sourceRigidBodies||target.sourceJoints)report.warnings.push_back("Rigid-body physics, cloth/hair simulation and PMX bone/group/material morph evaluation are not part of this TRS bake.");
        std::vector<bool> animated(target.skeleton.bones.size(),false);
        for(const auto& m:boneMatches)if(m.target>=0)animated[m.target]=true;
        std::vector<const IkChain*> chains;
        for(const auto& chain:target.ikRig.chains)if(chain.native&&chain.enabled&&options.solveNativeIk&&report.matchedBones) {
            if(chain.effector<0||static_cast<std::size_t>(chain.effector)>=target.skeleton.bones.size()||chain.targetBone<0||static_cast<std::size_t>(chain.targetBone)>=target.skeleton.bones.size())throw std::runtime_error("Native IK has an invalid target / effector index");
            bool explicitSwitch=false;for(const auto& key:motion.ikKeys)for(const auto& toggle:key.states)if(ToggleMatches(toggle.name,chain,target.skeleton))explicitSwitch=true;
            const bool control=animated[chain.targetBone]||HasAnimatedAncestor(chain.targetBone,target.skeleton,animated,true);
            bool fk=animated[chain.effector];for(const auto& link:chain.links){if(link.bone>=animated.size())throw std::runtime_error("Native IK link index is invalid");fk=fk||animated[link.bone];}
            if(!control&&!explicitSwitch&&fk){report.warnings.push_back("Preserved authored FK for "+chain.name+": limb FK tracks exist without animated IK controls or explicit IK switches.");continue;}
            if(control||explicitSwitch||HasAnimatedAncestor(chain.effector,target.skeleton,animated,false))chains.push_back(&chain);
        }
        if(options.evaluateAppend&&std::any_of(target.skeleton.bones.begin(),target.skeleton.bones.end(),[](const auto& b){return (b.sourceFlags&0x300)!=0;}))
            report.warnings.push_back("PMX local/nonlocal append deltas are baked in parent/append dependency order, including IK results. Imported packages without deformation-layer metadata use stable bone order for independent IK controllers; external-parent and after-physics evaluation are not simulated.");
        AnimationSequenceAsset sequence;sequence.name=name.empty()?motion.name:name;sequence.skeletonId=target.skeleton.id;
        sequence.source="VMD:"+motion.id;for(const auto& path:motion.sourcePaths)sequence.source+=" | "+Utf8Path(path);
        sequence.duration=duration;sequence.framesPerSecond=options.framesPerSecond;sequence.baked=true;sequence.generated=false;
        sequence.sourceSkeletonId="VMD model: "+motion.modelName+" (rest skeleton absent)";
        sequence.id=Identifier(sequence.source+TargetSignature(target)+sequence.name+OptionsSignature(options));
        Collector collector(target,times);
        for(double time:times) {
            auto base=RestPose(target.skeleton);
            for(std::size_t i=0;i<boneMatches.size();++i)if(boneMatches[i].target>=0) {
                const int b=boneMatches[i].target;const auto delta=SampleVmdBone(motion.bones[i],time);
                base[b].translation+=delta.translation*options.translationScale;base[b].rotation=Unit(base[b].rotation*Unit(delta.rotation));
                const auto& bone=target.skeleton.bones[b];
                if((bone.sourceFlags&0x400)&&glm::length2(bone.fixedAxis)>Epsilon) {
                    auto q=Unit(glm::inverse(Unit(bone.restLocal.rotation))*base[b].rotation);const auto axis=glm::normalize(bone.fixedAxis);
                    const auto projected=axis*glm::dot(glm::dvec3(q.x,q.y,q.z),axis);glm::dquat twist(q.w,projected.x,projected.y,projected.z);
                    base[b].rotation=Unit(bone.restLocal.rotation*(Finite(twist)?Unit(twist):glm::dquat(1,0,0,0)));
                }
            }
            SkeletalPose pose;std::vector<glm::dquat> corrections(base.size(),glm::dquat(1,0,0,0)),appendRotations;
            AppendPose(target.skeleton,order,base,corrections,options.evaluateAppend,pose,appendRotations);
            for(const auto* chain:chains) {
                bool enabled=true;
                for(const auto& key:motion.ikKeys)if(key.frame/30.0<=time+1e-10)for(const auto& toggle:key.states)
                    if(ToggleMatches(toggle.name,*chain,target.skeleton))enabled=toggle.enabled;
                if(!enabled)continue;
                const auto goal=glm::dvec3(pose.global[chain->targetBone][3]);std::string reason;
                if(!SolveIk(target.skeleton,*chain,goal,pose,&reason))throw std::runtime_error("Native IK bake failed: "+reason);
                for(const auto& link:chain->links)corrections[link.bone]=Unit(pose.local[link.bone].rotation*glm::inverse(Unit(base[link.bone].rotation*appendRotations[link.bone])));
                AppendPose(target.skeleton,order,base,corrections,options.evaluateAppend,pose,appendRotations);
            }
            pose.morphWeights.assign(target.morphs.size(),0);
            for(std::size_t i=0;i<morphMatches.size();++i)if(morphMatches[i].target>=0)pose.morphWeights[morphMatches[i].target]=static_cast<float>(SampleVmdMorph(motion.morphs[i],time));
            collector.Add(time,pose);
        }
        collector.Finish(sequence);report.samples=times.size();CopyDiagnostics(sequence,report);
        if(error)error->clear();if(output)*output=std::move(report);return sequence;
    }catch(const std::exception& e){if(error)*error=e.what();if(output)*output=std::move(report);return {};}
}

std::optional<AnimationSequenceAsset> BakeRetargetedAnimation(const SkeletalAssetPackage& source,const AnimationSequenceAsset& animation,
    const SkeletalAssetPackage& target,const RetargetMap& mapping,const AnimationBakeOptions& options,const std::string& name,
    AnimationBakeReport* output,std::string* error) {
    AnimationBakeReport report;
    try {
        DependencyOrder(source.skeleton,false);DependencyOrder(target.skeleton,false);
        if(animation.skeletonId!=source.skeleton.id||mapping.sourceSkeletonId!=source.skeleton.id||mapping.targetSkeletonId!=target.skeleton.id)
            throw std::runtime_error("Retarget animation / mapping skeleton identities do not match the supplied source and target");
        const auto times=SampleTimes(animation.duration,options,source.skeleton.bones.size()+source.morphs.size()
            +target.skeleton.bones.size()+target.morphs.size(),"source + target bone/expression channels");
        CheckAnimation(animation,source);
        std::set<int> sources,targets;
        for(const auto& m:mapping.bones) {
            if(m.sourceBone<0||m.targetBone<0||static_cast<std::size_t>(m.sourceBone)>=source.skeleton.bones.size()||static_cast<std::size_t>(m.targetBone)>=target.skeleton.bones.size()||!Finite(m.basisCorrection)||!Finite(m.confidence))
                throw std::runtime_error("Invalid retarget bone mapping");
            if(!sources.insert(m.sourceBone).second||!targets.insert(m.targetBone).second)throw std::runtime_error("Retarget bone mapping must be one-to-one");
            report.matches.push_back({source.skeleton.bones[m.sourceBone].name,target.skeleton.bones[m.targetBone].name,m.method,m.targetBone,m.confidence,false});++report.matchedBones;
        }
        std::vector<std::string> names,roles;std::vector<std::uint32_t> sourceMorphs;
        for(const auto& track:animation.morphTracks) {
            if(track.morph>=source.morphs.size())throw std::runtime_error("Source animation expression index is outside its package");
            sourceMorphs.push_back(track.morph);names.push_back(source.morphs[track.morph].name);
            auto role=MorphRole(names.back());if(role.empty())role=MorphRole(source.morphs[track.morph].englishName);roles.push_back(std::move(role));
        }
        const auto morphTargets=MorphTargets(target);auto matches=MatchNames(names,morphTargets,roles,true);
        // The source's English name is also a useful exact/normalized alias.
        std::vector<std::string> english;for(auto m:sourceMorphs)english.push_back(source.morphs[m].englishName);
        auto englishMatches=MatchNames(english,morphTargets,roles,true);std::set<int> used;
        for(const auto& m:matches)if(m.target>=0)used.insert(m.target);
        for(std::size_t i=0;i<matches.size();++i)if(matches[i].target<0&&englishMatches[i].target>=0&&!used.contains(englishMatches[i].target)) {
            matches[i]=englishMatches[i];matches[i].method="source English expression alias";used.insert(matches[i].target);
        }
        AddMatches(names,matches,morphTargets,true,report);
        for(const auto& track:animation.boneTracks) {
            if(track.bone>=source.skeleton.bones.size())throw std::runtime_error("Source animation bone index is outside its skeleton");
            if(!sources.contains(track.bone)){++report.unmatchedBones;report.warnings.push_back("Unmapped source animated bone: "+source.skeleton.bones[track.bone].name);}
        }
        if(!report.matchedBones&&!report.matchedMorphs)throw std::runtime_error("Retarget map and expression names have no usable target matches");
        auto adjusted=mapping;adjusted.translationScale*=options.translationScale;
        if(!Finite(adjusted.translationScale)||adjusted.translationScale<=0)throw std::runtime_error("Invalid combined retarget translation scale");
        AnimationSequenceAsset sequence;sequence.name=name.empty()?animation.name+" Retargeted":name;sequence.skeletonId=target.skeleton.id;
        sequence.sourceSkeletonId=source.skeleton.id;sequence.source="Retarget:"+animation.id;sequence.duration=animation.duration;
        sequence.framesPerSecond=options.framesPerSecond;sequence.generated=false;sequence.baked=true;
        sequence.id=Identifier(sequence.source+TargetSignature(source)+TargetSignature(target)+sequence.name+OptionsSignature(options)+MappingSignature(mapping));
        report.warnings.insert(report.warnings.end(),mapping.diagnostics.begin(),mapping.diagnostics.end());
        report.warnings.push_back("Retarget sampling uses rest-basis corrections and scaled root/hips displacement. No contact/foot-lock constraints or physics simulation are added; unmapped target bones stay at rest.");
        Collector collector(target,times);
        for(double time:times) {
            const auto sourcePose=EvaluateSkeletalPose(source,&animation,time,false);auto pose=RetargetPose(source.skeleton,sourcePose,target.skeleton,adjusted);
            pose.morphWeights.assign(target.morphs.size(),0);
            for(std::size_t i=0;i<matches.size();++i)if(matches[i].target>=0&&sourceMorphs[i]<sourcePose.morphWeights.size())pose.morphWeights[matches[i].target]=sourcePose.morphWeights[sourceMorphs[i]];
            collector.Add(time,pose);
        }
        collector.Finish(sequence);report.samples=times.size();CopyDiagnostics(sequence,report);
        if(error)error->clear();if(output)*output=std::move(report);return sequence;
    }catch(const std::exception& e){if(error)*error=e.what();if(output)*output=std::move(report);return {};}
}
} // namespace Model
