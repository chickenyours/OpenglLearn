#include "Model/Private/max_metadata.h"
#include <json/json.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace Model::detail {
namespace {
void Require(bool condition,const std::string& message) {
    if(!condition)throw std::runtime_error("Max scene metadata: "+message);
}
void Fields(const Json::Value& value,std::initializer_list<std::string_view> required,
    std::initializer_list<std::string_view> optional={}) {
    Require(value.isObject(),"expected an object");
    for(const auto field:required)Require(value.isMember(std::string(field)),"missing field '"+std::string(field)+"'");
    for(const auto& field:value.getMemberNames())Require(
        std::find(required.begin(),required.end(),field)!=required.end()||
        std::find(optional.begin(),optional.end(),field)!=optional.end(),"unexpected field '"+field+"'");
}
std::string String(const Json::Value& value,bool empty=true) {
    Require(value.isString(),"expected a string");const auto result=value.asString();
    Require(result.size()<=16384&&result.find('\0')==std::string::npos&&(empty||!result.empty()),"invalid or oversized string");
    return result;
}
std::vector<std::string> Strings(const Json::Value& value) {
    Require(value.isArray()&&value.size()<=4096,"invalid or oversized string array");
    std::vector<std::string> result;result.reserve(value.size());
    for(const auto& entry:value)result.push_back(String(entry));return result;
}
bool Boolean(const Json::Value& value) {
    Require(value.isBool(),"expected a boolean");return value.asBool();
}
std::string Sha256(std::span<const std::uint8_t> bytes) {
    // FIPS 180-4 SHA-256, without a platform crypto dependency or a second copy
    // of a potentially very large FBX. All arithmetic is explicitly uint32.
    constexpr std::array<std::uint32_t,64> constants={
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    std::array<std::uint32_t,8> state={0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    const auto block=[&](const std::uint8_t* input) {
        std::array<std::uint32_t,64> words{};
        for(unsigned i=0;i<16;++i)words[i]=(std::uint32_t(input[i*4])<<24)|(std::uint32_t(input[i*4+1])<<16)|
            (std::uint32_t(input[i*4+2])<<8)|input[i*4+3];
        for(unsigned i=16;i<64;++i) {
            const auto a=words[i-15],b=words[i-2];
            const auto s0=std::rotr(a,7)^std::rotr(a,18)^(a>>3),s1=std::rotr(b,17)^std::rotr(b,19)^(b>>10);
            words[i]=words[i-16]+s0+words[i-7]+s1;
        }
        auto a=state[0],b=state[1],c=state[2],d=state[3],e=state[4],f=state[5],g=state[6],h=state[7];
        for(unsigned i=0;i<64;++i) {
            const auto first=h+(std::rotr(e,6)^std::rotr(e,11)^std::rotr(e,25))+((e&f)^(~e&g))+constants[i]+words[i];
            const auto second=(std::rotr(a,2)^std::rotr(a,13)^std::rotr(a,22))+((a&b)^(a&c)^(b&c));
            h=g;g=f;f=e;e=d+first;d=c;c=b;b=a;a=first+second;
        }
        state[0]+=a;state[1]+=b;state[2]+=c;state[3]+=d;state[4]+=e;state[5]+=f;state[6]+=g;state[7]+=h;
    };
    const auto full=bytes.size()/64;
    for(std::size_t i=0;i<full;++i)block(bytes.data()+i*64);
    std::array<std::uint8_t,128> tail{};const auto count=bytes.size()%64;
    std::copy(bytes.begin()+full*64,bytes.end(),tail.begin());tail[count]=0x80;
    const std::size_t tailLength=count<56?64:128;const auto bits=std::uint64_t(bytes.size())*8;
    for(unsigned i=0;i<8;++i)tail[tailLength-1-i]=std::uint8_t(bits>>(i*8));
    block(tail.data());if(tailLength==128)block(tail.data()+64);
    std::ostringstream out;out<<std::hex<<std::setfill('0');for(auto word:state)out<<std::setw(8)<<word;return out.str();
}
MeshRegionKind Kind(const Json::Value& value) {
    const auto text=String(value,false);
    for(const auto kind:{MeshRegionKind::Unknown,MeshRegionKind::Body,MeshRegionKind::Hair,MeshRegionKind::Garment,MeshRegionKind::Accessory}) {
        std::string name=MeshRegionKindName(kind);std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return char(std::tolower(c));});
        if(text==name)return kind;
    }
    throw std::runtime_error("Max scene metadata: invalid classification kind '"+text+"'");
}
struct Classification {MeshRegionKind kind;bool reviewed;std::string label;std::vector<std::string> evidence;};
} // namespace

void ApplyMaxSceneMetadata(SkeletalAssetPackage& asset,std::span<const SourceMeshSemantics> nodeSemantics,
    const std::filesystem::path& metadataPath,std::span<const std::uint8_t> fbxBytes) {
    std::error_code ec;const bool exists=std::filesystem::exists(metadataPath,ec);
    Require(!ec,"cannot inspect the optional sidecar");
    if(!exists) {
        auto regions=RecognizeGarmentSections(asset,nodeSemantics);
        asset.meshRegions=std::move(regions);
        asset.warnings.push_back("FBX node provenance retained for "+std::to_string(nodeSemantics.size())+
            " material sections; no Max scene sidecar was supplied.");return;
    }
    const auto size=std::filesystem::file_size(metadataPath,ec);
    Require(!ec&&size<=16ull*1024*1024,"missing/non-file or oversized sidecar (limit 16 MiB)");
    std::ifstream stream(metadataPath,std::ios::binary);Require(bool(stream),"cannot open sidecar");
    Json::CharReaderBuilder reader;reader["collectComments"]=false;reader["rejectDupKeys"]=true;
    reader["allowSpecialFloats"]=false;reader["failIfExtra"]=true;reader["stackLimit"]=64;
    Json::Value document;std::string error;Require(Json::parseFromStream(reader,stream,&document,&error),"invalid JSON: "+error);
    Fields(document,{"assetType","version","fbxSha256","source","objects"});
    Require(document["assetType"].isString()&&document["assetType"].asString()=="MaxSceneMetadata"&&
        document["version"].isUInt()&&document["version"].asUInt()==1,"unsupported header/version");
    auto hash=String(document["fbxSha256"],false);Require(hash.size()==64,"fbxSha256 must contain 64 hexadecimal digits");
    for(auto& c:hash){Require(std::isxdigit(static_cast<unsigned char>(c)),"fbxSha256 must be hexadecimal");c=char(std::tolower(static_cast<unsigned char>(c)));}
    Require(hash==Sha256(fbxBytes),"FBX SHA-256 mismatch; regenerate the sidecar for this exact export");
    const auto source=String(document["source"],false);const auto& objects=document["objects"];
    Require(objects.isArray()&&objects.size()<=100000,"invalid or oversized objects array");
    Require(asset.geometry!=nullptr,"source geometry is absent");
    std::vector<SourceMeshSemantics> semantics(nodeSemantics.begin(),nodeSemantics.end());
    std::map<std::string,std::vector<std::size_t>> byObject;
    std::set<std::uint32_t> sections;
    for(std::size_t i=0;i<semantics.size();++i) {
        Require(semantics[i].mesh<asset.geometry->meshes.size()&&sections.insert(semantics[i].mesh).second,"invalid or duplicate imported section provenance");
        byObject[semantics[i].sourceObject].push_back(i);
    }
    std::map<std::string,std::size_t> nodeNames;for(const auto& bone:asset.skeleton.bones)++nodeNames[bone.name];
    std::set<std::string> names;std::map<std::uint32_t,Classification> classifications;
    std::map<std::uint32_t,std::string> objectClasses;std::size_t covered=0;
    for(const auto& object:objects) {
        Fields(object,{"name","layer","parent","class","materials","modifiers"},{"classification"});
        const auto name=String(object["name"],false);Require(names.insert(name).second,"duplicate source object '"+name+"'");
        const auto found=byObject.find(name);Require(found!=byObject.end(),"source object has no exact exported FBX section: '"+name+"'");
        Require(nodeNames[name]==1,"source object name is ambiguous in the FBX scene: '"+name+"'");
        const auto layer=String(object["layer"]),parent=String(object["parent"]),type=String(object["class"],false);
        const auto materials=Strings(object["materials"]);const auto& modifiers=object["modifiers"];
        Require(modifiers.isArray()&&modifiers.size()<=4096,"invalid or oversized modifiers array");
        std::vector<SourceModifierSemantics> parsedModifiers;
        for(const auto& modifier:modifiers) {
            Fields(modifier,{"name","type","enabled"});SourceModifierSemantics parsed;
            parsed.name=String(modifier["name"]);parsed.type=String(modifier["type"],false);
            parsed.showViewport=parsed.showRender=Boolean(modifier["enabled"]);parsedModifiers.push_back(std::move(parsed));
        }
        Classification classification{};const bool classified=object.isMember("classification");
        if(classified) {
            const auto& input=object["classification"];Fields(input,{"kind","reviewed","label","evidence"});
            classification.kind=Kind(input["kind"]);classification.reviewed=Boolean(input["reviewed"]);
            classification.label=String(input["label"],false);classification.evidence=Strings(input["evidence"]);
        }
        for(const auto index:found->second) {
            auto& input=semantics[index];
            if(!layer.empty())input.collections.push_back("Max layer: "+layer);
            if(!parent.empty())input.collections.push_back("Max parent: "+parent);
            input.modifiers=parsedModifiers;
            // Imported section material remains authoritative; this count is
            // only an object-scope ambiguity guard, never a material remap.
            input.sourceObjectMaterialCount=std::max(input.sourceObjectMaterialCount,std::max(1u,std::uint32_t(materials.size())));
            objectClasses.emplace(input.mesh,type);
            if(classified)classifications.emplace(input.mesh,classification);++covered;
        }
    }
    auto regions=RecognizeGarmentSections(asset,semantics);std::size_t overrides=0;
    for(auto& region:regions) {
        if(const auto sourceClass=objectClasses.find(region.mesh);sourceClass!=objectClasses.end())
            region.evidence.push_back("Max source object class: "+sourceClass->second+".");
        const auto found=classifications.find(region.mesh);if(found==classifications.end())continue;
        const auto& classification=found->second;
        region.evidence.push_back("Max visual classification label: "+classification.label+"; reviewed="+
            (classification.reviewed?"true":"false")+"; proposal="+MeshRegionKindName(classification.kind)+".");
        for(const auto& evidence:classification.evidence)region.evidence.push_back("Max review: "+evidence);
        if(classification.reviewed&&classification.kind!=MeshRegionKind::Unknown) {
            region.kind=classification.kind;region.confidence=1;region.userOverride=true;region.requiresReview=false;++overrides;
            region.evidence.push_back("Reviewed external Max annotation overrides automatic recognition; this does not author or enable cloth physics.");
        }else region.requiresReview=true;
    }
    std::vector<std::string> warnings=asset.warnings;
    warnings.push_back("Max scene metadata verified against FBX SHA-256 "+hash+"; source: "+source+
        "; exact objects "+std::to_string(names.size())+", covered sections "+std::to_string(covered)+
        "/"+std::to_string(regions.size())+", reviewed classification overrides "+std::to_string(overrides)+".");
    asset.meshRegions=std::move(regions);asset.warnings=std::move(warnings);
}
} // namespace Model::detail
