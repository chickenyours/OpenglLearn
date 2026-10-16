#include "Model/Public/vmd_animation.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#elif __has_include(<iconv.h>)
#include <iconv.h>
#define MODEL_VMD_ICONV 1
#endif

namespace Model {
namespace {
constexpr std::size_t MaximumBytes=256u*1024u*1024u;
constexpr std::uint32_t MaximumKeys=2000000;
constexpr std::uint32_t MaximumFrame=30u*60u*60u*24u;
constexpr std::size_t MaximumTracks=100000;
std::string Utf8Path(const std::filesystem::path& path) {
    const auto value=path.u8string();return {reinterpret_cast<const char*>(value.data()),value.size()};
}
std::string DecodeName(std::string_view bytes) {
    const auto zero=bytes.find('\0');if(zero!=std::string_view::npos)bytes=bytes.substr(0,zero);
    if(bytes.empty())return {};
#if defined(_WIN32)
    const auto length=MultiByteToWideChar(932,MB_ERR_INVALID_CHARS,bytes.data(),static_cast<int>(bytes.size()),nullptr,0);
    if(!length)throw std::runtime_error("Invalid Shift-JIS/CP932 name in VMD");
    std::wstring wide(length,L'\0');MultiByteToWideChar(932,MB_ERR_INVALID_CHARS,bytes.data(),static_cast<int>(bytes.size()),wide.data(),length);
    const auto size=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wide.data(),length,nullptr,0,nullptr,nullptr);
    if(!size)throw std::runtime_error("Cannot convert VMD name to UTF-8");
    std::string result(size,'\0');WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,wide.data(),length,result.data(),size,nullptr,nullptr);return result;
#elif defined(MODEL_VMD_ICONV)
    auto converter=iconv_open("UTF-8","CP932");
    if(converter==reinterpret_cast<iconv_t>(-1))throw std::runtime_error("CP932 conversion is unavailable");
    std::string result(bytes.size()*4+4,'\0');char* input=const_cast<char*>(bytes.data());std::size_t inputLeft=bytes.size();
    char* output=result.data();std::size_t outputLeft=result.size();const auto status=iconv(converter,&input,&inputLeft,&output,&outputLeft);iconv_close(converter);
    if(status==static_cast<std::size_t>(-1)||inputLeft)throw std::runtime_error("Invalid Shift-JIS/CP932 name in VMD");
    result.resize(result.size()-outputLeft);return result;
#else
    if(std::any_of(bytes.begin(),bytes.end(),[](unsigned char c){return c>=128;}))
        throw std::runtime_error("CP932 conversion requires Windows or iconv");
    return std::string(bytes);
#endif
}
std::string HashId(std::string_view bytes) {
    std::uint64_t value=14695981039346656037ull;for(unsigned char c:bytes){value^=c;value*=1099511628211ull;}
    std::ostringstream out;out<<"vmd-"<<std::hex<<std::setw(16)<<std::setfill('0')<<value;return out.str();
}
class Reader {
public:
    explicit Reader(const std::vector<std::uint8_t>& bytes,std::size_t offset=0):bytes_(bytes),position_(offset){}
    std::size_t Left()const{return bytes_.size()-position_;}
    [[noreturn]] void Fail(const std::string& detail)const{throw std::runtime_error(detail+" at byte "+std::to_string(position_));}
    const std::uint8_t* Bytes(std::size_t count){if(count>Left())Fail("Truncated VMD record");const auto* p=bytes_.data()+position_;position_+=count;return p;}
    std::uint8_t U8(){return *Bytes(1);}
    std::uint32_t U32(){const auto* p=Bytes(4);return std::uint32_t(p[0])|(std::uint32_t(p[1])<<8)|(std::uint32_t(p[2])<<16)|(std::uint32_t(p[3])<<24);}
    double Float(){const auto value=std::bit_cast<float>(U32());if(!std::isfinite(value))Fail("Nonfinite VMD value");return value;}
    std::uint32_t Frame(){auto value=U32();if(value>MaximumFrame)Fail("VMD exceeds the 24-hour frame limit");return value;}
    std::string Name(std::size_t length){const auto* p=Bytes(length);return DecodeName({reinterpret_cast<const char*>(p),length});}
    std::uint32_t Count(std::size_t stride){const auto count=U32();if(count>MaximumKeys||count>Left()/stride)Fail("Invalid VMD section count");return count;}
    bool Optional(){if(!Left())return false;if(Left()<4)Fail("Truncated VMD section count");return true;}
    bool Boolean(){const auto value=U8();if(value>1)Fail("Invalid VMD boolean flag");return value!=0;}
private:
    const std::vector<std::uint8_t>& bytes_;std::size_t position_=0;
};
void UpdateDuration(VmdMotion& motion,std::uint32_t frame){motion.duration=std::max(motion.duration,frame/30.0);}
template<class Key> void NormalizeKeys(std::vector<Key>& keys) {
    std::stable_sort(keys.begin(),keys.end(),[](const Key& a,const Key& b){return a.frame<b.frame;});
    // Preserve the last authored value at each frame, including its arrival curve.
    std::size_t output=0;for(std::size_t i=0;i<keys.size();){std::size_t end=i+1;while(end<keys.size()&&keys[end].frame==keys[i].frame)++end;
        keys[output++]=std::move(keys[end-1]);i=end;}keys.resize(output);
}
VmdMotion Parse(const std::vector<std::uint8_t>& bytes,std::size_t modelNameLength) {
    Reader reader(bytes,30);VmdMotion motion;motion.modelName=reader.Name(modelNameLength);
    std::map<std::string,std::vector<VmdBoneKey>> bones;std::map<std::string,std::vector<VmdMorphKey>> morphs;
    const auto boneCount=reader.Count(111);bool physicsFlags=false;
    for(std::uint32_t i=0;i<boneCount;++i){const auto name=reader.Name(15);if(name.empty())reader.Fail("Empty VMD bone name");
        VmdBoneKey key;key.frame=reader.Frame();UpdateDuration(motion,key.frame);
        key.translation.x=reader.Float();key.translation.y=reader.Float();key.translation.z=-reader.Float();
        const auto x=reader.Float(),y=reader.Float(),z=reader.Float(),w=reader.Float();
        key.rotation=glm::dquat(w,-x,-y,z);const auto length=glm::length(key.rotation);
        if(length<1e-12||!std::isfinite(length))reader.Fail("Invalid VMD quaternion");key.rotation/=length;
        const auto* controls=reader.Bytes(64);
        // MMD can overwrite first-row Z/R x1 bytes with per-key physics flags.
        // Their undamaged copies are at 17/18 in the shifted second row.
        const bool overwritten=(controls[2]==0&&controls[3]==0)||(controls[2]==99&&controls[3]==15);
        if(overwritten&&(controls[2]!=controls[17]||controls[3]!=controls[18]))physicsFlags=true;
        for(std::size_t axis=0;axis<4;++axis){auto x1=controls[axis];if(overwritten&&axis>=2)x1=controls[axis+15];
            const auto y1=controls[axis+4],x2=controls[axis+8],y2=controls[axis+12];
            if(x1>127||y1>127||x2>127||y2>127)reader.Fail("VMD Bezier controls exceed 0..127");
            key.interpolation[axis]={x1/127.0,y1/127.0,x2/127.0,y2/127.0};}
        bones[name].push_back(key);if(bones.size()>MaximumTracks)reader.Fail("Too many VMD bone tracks");}
    for(auto& [name,keys]:bones){NormalizeKeys(keys);motion.bones.push_back({name,std::move(keys)});}
    if(physicsFlags)motion.warnings.push_back("Per-key VMD physics flags were detected; Bezier controls were recovered, but physics simulation is not imported.");
    if(!reader.Optional())return motion;
    const auto morphCount=reader.Count(23);
    for(std::uint32_t i=0;i<morphCount;++i){const auto name=reader.Name(15);if(name.empty())reader.Fail("Empty VMD morph name");VmdMorphKey key;
        key.frame=reader.Frame();key.weight=reader.Float();UpdateDuration(motion,key.frame);morphs[name].push_back(key);
        if(morphs.size()>MaximumTracks)reader.Fail("Too many VMD morph tracks");}
    for(auto& [name,keys]:morphs){NormalizeKeys(keys);motion.morphs.push_back({name,std::move(keys)});}
    if(!reader.Optional())return motion;
    motion.cameraKeys=reader.Count(61);
    for(std::uint32_t i=0;i<motion.cameraKeys;++i){UpdateDuration(motion,reader.Frame());for(int c=0;c<7;++c)reader.Float();reader.Bytes(24);
        const auto fov=reader.U32();if(fov>180)reader.Fail("Invalid VMD camera field of view");reader.Boolean();}
    if(!reader.Optional())return motion;
    motion.lightKeys=reader.Count(28);
    for(std::uint32_t i=0;i<motion.lightKeys;++i){UpdateDuration(motion,reader.Frame());for(int c=0;c<6;++c)reader.Float();}
    if(!reader.Optional())return motion;
    motion.shadowKeys=reader.Count(9);
    for(std::uint32_t i=0;i<motion.shadowKeys;++i){UpdateDuration(motion,reader.Frame());if(reader.U8()>2)reader.Fail("Invalid VMD shadow mode");reader.Float();}
    if(!reader.Optional())return motion;
    const auto ikCount=reader.Count(9);std::map<std::uint32_t,VmdIkKey> ik;
    for(std::uint32_t i=0;i<ikCount;++i){const auto frame=reader.Frame();UpdateDuration(motion,frame);const auto visible=reader.Boolean();const auto count=reader.Count(21);
        auto& key=ik[frame];key.frame=frame;key.visible=visible;std::map<std::string,bool> states;for(const auto& state:key.states)states[state.name]=state.enabled;
        for(std::uint32_t j=0;j<count;++j){const auto name=reader.Name(20);if(name.empty())reader.Fail("Empty VMD IK name");states[name]=reader.Boolean();}
        key.states.clear();for(const auto& [name,enabled]:states)key.states.push_back({name,enabled});}
    for(auto& [frame,key]:ik)motion.ikKeys.push_back(std::move(key));
    if(reader.Left())motion.warnings.push_back("Unrecognized trailing VMD extension: "+std::to_string(reader.Left())+" bytes were ignored.");
    return motion;
}
double Cubic(double a,double b,double t){const auto s=1-t;return 3*s*s*t*a+3*s*t*t*b+t*t*t;}
double Bezier(const VmdBezier& curve,double progress) {
    if(progress<=0)return 0;if(progress>=1)return 1;
    if(curve.x1==curve.y1&&curve.x2==curve.y2)return progress;
    // Bisection stays stable for flat endpoint tangents and quantized controls.
    double low=0,high=1;for(int i=0;i<36;++i){const auto middle=(low+high)*0.5;
        if(Cubic(curve.x1,curve.x2,middle)<progress)low=middle;else high=middle;}
    return Cubic(curve.y1,curve.y2,(low+high)*0.5);
}
double SampleFrame(double seconds){if(!std::isfinite(seconds))return seconds>0?double(MaximumFrame):0;return std::clamp(seconds*30.0,0.0,double(MaximumFrame));}
template<class Key> auto UpperKey(const std::vector<Key>& keys,double frame){return std::upper_bound(keys.begin(),keys.end(),frame,[](double value,const Key& key){return value<key.frame;});}
void Require(bool condition,const char* detail){if(!condition)throw std::runtime_error(detail);}
void ValidateKey(const VmdBoneKey& key) {
    Require(key.frame<=MaximumFrame,"VMD merge frame exceeds 24-hour limit");
    for(int c=0;c<3;++c)Require(std::isfinite(key.translation[c]),"VMD merge has nonfinite translation");
    const auto length=glm::length(key.rotation);Require(std::isfinite(length)&&length>1e-12,"VMD merge has invalid quaternion");
    for(const auto& curve:key.interpolation)for(double value:{curve.x1,curve.y1,curve.x2,curve.y2})Require(std::isfinite(value)&&value>=0&&value<=1,"VMD merge has invalid Bezier controls");
}
}

std::optional<VmdMotion> LoadVmdMotion(const std::filesystem::path& path,std::string* error) {
    try{
        if(error)error->clear();std::ifstream file(path,std::ios::binary|std::ios::ate);Require(bool(file),"Cannot open VMD file");const auto length=file.tellg();
        Require(length>=44&&length<=static_cast<std::streamoff>(MaximumBytes),"VMD file size is invalid or exceeds 256 MiB");
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));file.seekg(0);Require(bool(file.read(reinterpret_cast<char*>(bytes.data()),length)),"Cannot read complete VMD file");
        const auto* data=reinterpret_cast<const char*>(bytes.data());const auto headerEnd=std::find(data,data+30,'\0');const std::string_view header(data,headerEnd-data);
        const bool old=header=="Vocaloid Motion Data file"||header=="Vocaloid Motion Data 0001";
        Require(old||header=="Vocaloid Motion Data 0002","Invalid or unsupported VMD header");
        VmdMotion motion;if(old){try{motion=Parse(bytes,10);}catch(const std::exception&){motion=Parse(bytes,15);motion.warnings.push_back("Legacy VMD uses the 15-byte model name variant.");}}
        else motion=Parse(bytes,20);
        motion.id=HashId({data,bytes.size()});motion.name=Utf8Path(path.stem());motion.sourcePaths={path};
        if(motion.cameraKeys||motion.lightKeys||motion.shadowKeys)motion.warnings.push_back("Camera/light/shadow keys are counted and validated; they are not character animation tracks.");
        return motion;
    }catch(const std::exception& exception){if(error)*error=exception.what();return std::nullopt;}
}

std::optional<VmdMotion> MergeVmdMotions(const std::vector<VmdMotion>& motions,const std::string& name,std::string* error) {
    try{
        if(error)error->clear();Require(!motions.empty(),"No VMD sources to merge");VmdMotion result;result.name=name.empty()?motions.front().name:name;result.modelName=motions.front().modelName;
        std::map<std::string,std::map<std::uint32_t,VmdBoneKey>> bones;std::map<std::string,std::map<std::uint32_t,VmdMorphKey>> morphs;
        struct IkMerge{bool visible=true;std::map<std::string,bool> states;};std::map<std::uint32_t,IkMerge> ik;
        std::string hashInput;std::size_t totalKeys=0;std::set<std::string> warningSet;std::set<std::filesystem::path> sources;
        std::uint64_t cameras=0,lights=0,shadows=0;bool differentModel=false;
        for(const auto& motion:motions){Require(std::isfinite(motion.duration)&&motion.duration>=0&&motion.duration<=MaximumFrame/30.0,"Invalid VMD merge duration");
            result.duration=std::max(result.duration,motion.duration);differentModel|=!motion.modelName.empty()&&!result.modelName.empty()&&motion.modelName!=result.modelName;
            hashInput+=motion.id;hashInput.push_back('\0');for(const auto& path:motion.sourcePaths)if(sources.insert(path).second)result.sourcePaths.push_back(path);
            for(const auto& warning:motion.warnings)if(warningSet.insert(warning).second)result.warnings.push_back(warning);
            cameras+=motion.cameraKeys;lights+=motion.lightKeys;shadows+=motion.shadowKeys;
            for(const auto& track:motion.bones){Require(!track.name.empty(),"VMD merge has empty bone name");for(auto key:track.keys){ValidateKey(key);key.rotation=glm::normalize(key.rotation);bones[track.name][key.frame]=key;UpdateDuration(result,key.frame);Require(++totalKeys<=MaximumKeys,"Too many VMD merge keys");}}
            for(const auto& track:motion.morphs){Require(!track.name.empty(),"VMD merge has empty morph name");for(const auto& key:track.keys){Require(key.frame<=MaximumFrame&&std::isfinite(key.weight),"Invalid VMD merge morph key");morphs[track.name][key.frame]=key;UpdateDuration(result,key.frame);Require(++totalKeys<=MaximumKeys,"Too many VMD merge keys");}}
            for(const auto& key:motion.ikKeys){Require(key.frame<=MaximumFrame,"Invalid VMD merge IK frame");auto& output=ik[key.frame];output.visible=key.visible;
                for(const auto& state:key.states){Require(!state.name.empty(),"VMD merge has empty IK name");output.states[state.name]=state.enabled;Require(++totalKeys<=MaximumKeys,"Too many VMD merge keys");}UpdateDuration(result,key.frame);}
            Require(bones.size()<=MaximumTracks&&morphs.size()<=MaximumTracks,"Too many VMD merge tracks");}
        for(auto& [trackName,keys]:bones){VmdBoneTrack track;track.name=trackName;for(auto& [frame,key]:keys)track.keys.push_back(std::move(key));result.bones.push_back(std::move(track));}
        for(auto& [trackName,keys]:morphs){VmdMorphTrack track;track.name=trackName;for(auto& [frame,key]:keys)track.keys.push_back(std::move(key));result.morphs.push_back(std::move(track));}
        for(auto& [frame,key]:ik){VmdIkKey output;output.frame=frame;output.visible=key.visible;for(const auto& [stateName,enabled]:key.states)output.states.push_back({stateName,enabled});result.ikKeys.push_back(std::move(output));}
        Require(cameras<=MaximumKeys&&lights<=MaximumKeys&&shadows<=MaximumKeys,"Too many VMD merge scene keys");
        result.cameraKeys=static_cast<std::uint32_t>(cameras);result.lightKeys=static_cast<std::uint32_t>(lights);result.shadowKeys=static_cast<std::uint32_t>(shadows);
        result.id=HashId(hashInput);if(differentModel)result.warnings.push_back("Merged VMD sources declare different model names; VMD has no rest skeleton, so track names must be matched to the target rig.");
        return result;
    }catch(const std::exception& exception){if(error)*error=exception.what();return std::nullopt;}
}

BoneTransform SampleVmdBone(const VmdBoneTrack& track,double seconds) {
    BoneTransform result;if(track.keys.empty())return result;const auto frame=SampleFrame(seconds);const auto upper=UpperKey(track.keys,frame);
    if(upper==track.keys.begin()){result.translation=upper->translation;result.rotation=upper->rotation;return result;}
    const auto& lower=*(upper-1);if(upper==track.keys.end()){result.translation=lower.translation;result.rotation=lower.rotation;return result;}
    const auto progress=(frame-lower.frame)/double(upper->frame-lower.frame);
    for(int axis=0;axis<3;++axis){const auto weight=Bezier(upper->interpolation[axis],progress);result.translation[axis]=lower.translation[axis]+(upper->translation[axis]-lower.translation[axis])*weight;}
    const auto rotationWeight=Bezier(upper->interpolation[3],progress);auto rotation=upper->rotation;
    if(glm::dot(lower.rotation,rotation)<0)rotation=-rotation;result.rotation=glm::normalize(glm::slerp(lower.rotation,rotation,rotationWeight));return result;
}
double SampleVmdMorph(const VmdMorphTrack& track,double seconds) {
    if(track.keys.empty())return 0;const auto frame=SampleFrame(seconds);const auto upper=UpperKey(track.keys,frame);
    if(upper==track.keys.begin())return upper->weight;const auto& lower=*(upper-1);if(upper==track.keys.end())return lower.weight;
    const auto progress=(frame-lower.frame)/double(upper->frame-lower.frame);return lower.weight+(upper->weight-lower.weight)*progress;
}
} // namespace Model
