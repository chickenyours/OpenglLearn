#include "Model/Private/max_metadata.h"
#include <json/json.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <functional>

namespace {
using namespace Model;
using namespace Model::detail;
int failures=0;
void Expect(bool value,const char* message) {
    std::printf("  %s %s\n",value?"OK":"FAIL",message);failures+=!value;
}
void Write(const std::filesystem::path& path,const Json::Value& value) {
    std::ofstream out(path,std::ios::binary);Json::StreamWriterBuilder writer;out<<Json::writeString(writer,value);
}
SkeletalAssetPackage Fixture() {
    SkeletalAssetPackage asset;asset.geometry=std::make_shared<StaticModel>();asset.warnings={"Existing import diagnostic"};
    PbrMaterial material;material.name="Material";asset.geometry->materials.push_back(material);
    asset.geometry->meshes.resize(3);
    SkeletonBone a;a.name="Object001";SkeletonBone b;b.name="Object002";asset.skeleton.bones={a,b};
    return asset;
}
std::vector<SourceMeshSemantics> Semantics() {
    std::vector<SourceMeshSemantics> inputs(3);
    for(std::uint32_t i=0;i<inputs.size();++i) {
        auto& input=inputs[i];input.mesh=i;input.sourceObject=i<2?"Object001":"Object002";
        input.sourceMesh="Geometry"+std::to_string(i);input.sourceMaterial="Material";
        input.sourceObjectMaterialCount=i<2?2:1;input.collections={"Group001"};
    }
    return inputs;
}
Json::Value Document() {
    Json::Value value;value["assetType"]="MaxSceneMetadata";value["version"]=1;
    value["fbxSha256"]="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad";
    value["source"]="Source scene.max";value["objects"]=Json::Value(Json::arrayValue);
    for(unsigned i=0;i<2;++i) {
        Json::Value object;object["name"]=i==0?"Object001":"Object002";object["layer"]="Character";
        object["parent"]="Group001";object["class"]="Editable_Poly";
        object["materials"]=Json::Value(Json::arrayValue);object["materials"].append("Material");
        object["modifiers"]=Json::Value(Json::arrayValue);Json::Value modifier;
        modifier["name"]=i==0?"Normals":"Author cloth";modifier["type"]=i==0?"Normal":"Cloth";
        modifier["enabled"]=false;object["modifiers"].append(modifier);
        Json::Value classification;classification["kind"]=i==0?"garment":"accessory";
        classification["reviewed"]=i==0;classification["label"]=i==0?"Reviewed outer panels":"Unreviewed proposal";
        classification["evidence"]=Json::Value(Json::arrayValue);classification["evidence"].append("Visual source inspection");
        object["classification"]=classification;value["objects"].append(object);
    }
    return value;
}
void SidecarTests(const std::filesystem::path& path) {
    const std::vector<std::uint8_t> bytes={'a','b','c'};const auto inputs=Semantics();const auto good=Document();
    Write(path,good);auto asset=Fixture();ApplyMaxSceneMetadata(asset,inputs,path,bytes);
    Expect(asset.meshRegions.size()==3&&asset.meshRegions[0].sourceObject=="Object001"&&asset.meshRegions[1].sourceObject=="Object001",
        "Exact object provenance reaches every split material section");
    Expect(asset.meshRegions[0].kind==MeshRegionKind::Garment&&asset.meshRegions[1].kind==MeshRegionKind::Garment&&
        asset.meshRegions[0].userOverride&&!asset.meshRegions[0].requiresReview&&!asset.meshRegions[0].authoredCloth,
        "Reviewed visual garment annotations override classification without inventing authored Cloth");
    Expect(asset.meshRegions[2].kind==MeshRegionKind::Garment&&!asset.meshRegions[2].userOverride&&
        asset.meshRegions[2].requiresReview&&asset.meshRegions[2].authoredCloth,
        "Unreviewed proposal does not override automatic recognition and actual disabled Cloth remains visible");
    Expect(asset.meshRegions[0].collections.size()==3&&asset.meshRegions[0].modifiers[0].find("Normal")!=std::string::npos&&
        asset.warnings.size()==2&&asset.warnings.back().find("overrides 2")!=std::string::npos,
        "Source hierarchy, layer, actual modifier class and override counts remain inspectable");
    Expect(std::any_of(asset.meshRegions[0].evidence.begin(),asset.meshRegions[0].evidence.end(),[](const auto& text){
        return text.find("Reviewed outer panels")!=std::string::npos;}),"Reviewed labels and evidence survive in persistent mesh regions");

    const auto reject=[&](const char* message,const std::function<void(Json::Value&)>& mutate,
        const std::function<void(SkeletalAssetPackage&)>& changeAsset={}) {
        auto document=good;mutate(document);Write(path,document);auto target=Fixture();
        target.meshRegions=asset.meshRegions;const auto oldWarnings=target.warnings;
        if(changeAsset)changeAsset(target);bool failed=false;
        try{ApplyMaxSceneMetadata(target,inputs,path,bytes);}catch(const std::exception&){failed=true;}
        Expect(failed&&target.warnings==oldWarnings&&target.meshRegions.size()==3&&target.meshRegions[0].userOverride,
            message);
    };
    reject("Reject stale FBX hash atomically",[](auto& value){value["fbxSha256"]=std::string(64,'0');});
    reject("Reject malformed SHA-256",[](auto& value){value["fbxSha256"]=std::string(64,'g');});
    reject("Reject duplicate source object names",[](auto& value){const auto copy=value["objects"][0];value["objects"].append(copy);});
    reject("Reject unknown exported source object",[](auto& value){value["objects"][0]["name"]="Missing";});
    reject("Reject ambiguous exported FBX node names",[](auto&){},[](auto& value){value.skeleton.bones.push_back(value.skeleton.bones[0]);});
    reject("Reject unsupported sidecar version",[](auto& value){value["version"]=2;});
    reject("Reject unknown classification kind",[](auto& value){value["objects"][0]["classification"]["kind"]="skirt";});
    reject("Reject numeric reviewed flag",[](auto& value){value["objects"][0]["classification"]["reviewed"]=1;});
    reject("Reject oversized source string",[](auto& value){value["source"]=std::string(16385,'x');});
    reject("Reject oversized modifier list",[](auto& value){auto modifier=value["objects"][0]["modifiers"][0];
        for(unsigned i=1;i<4097;++i)value["objects"][0]["modifiers"].append(modifier);});
    reject("Reject unexpected sidecar fields",[](auto& value){value["guessAllCloth"]=true;});
    reject("Reject missing actual modifier type",[](auto& value){value["objects"][0]["modifiers"][0].removeMember("type");});
    reject("Reject null inside provenance name",[](auto& value){value["source"]=std::string("bad\0path",8);});
    Write(path,good);auto unknown=good;unknown["objects"][0]["classification"]["kind"]="unknown";Write(path,unknown);
    auto unclassified=Fixture();ApplyMaxSceneMetadata(unclassified,inputs,path,bytes);
    Expect(unclassified.meshRegions[0].kind==MeshRegionKind::Unknown&&!unclassified.meshRegions[0].userOverride&&
        unclassified.meshRegions[0].requiresReview,"Reviewed Unknown does not override automatic recognition or silently resolve review");
    {std::ofstream out(path);out<<"{\"assetType\":\"MaxSceneMetadata\",\"assetType\":\"MaxSceneMetadata\"}";}
    bool duplicateKeys=false;try{auto target=Fixture();ApplyMaxSceneMetadata(target,inputs,path,bytes);}catch(const std::exception&){duplicateKeys=true;}
    Expect(duplicateKeys,"Reject duplicate JSON keys");
    std::filesystem::remove(path);auto absent=Fixture();ApplyMaxSceneMetadata(absent,inputs,path,bytes);
    Expect(absent.meshRegions.size()==3&&absent.meshRegions[0].sourceObject=="Object001"&&
        !absent.meshRegions[0].userOverride,"Absent optional metadata preserves node semantics and automatic recognition");
    auto invalidInputs=inputs;invalidInputs[0].mesh=999;Write(path,good);bool bounds=false;
    try{auto target=Fixture();ApplyMaxSceneMetadata(target,invalidInputs,path,bytes);}catch(const std::exception&){bounds=true;}
    Expect(bounds,"Reject out-of-range imported section mapping");
    // Known vectors cover both padding branches and complete source blocks.
    for(const auto& [input,hash]:std::array<std::pair<std::string,std::string>,3>{{
        {"","e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
        {"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq","248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"},
        {std::string(64,'a'),"ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb"}}}) {
        auto document=good;document["fbxSha256"]=hash;Write(path,document);const std::vector<std::uint8_t> data(input.begin(),input.end());
        auto target=Fixture();ApplyMaxSceneMetadata(target,inputs,path,data);Expect(target.meshRegions[0].userOverride,"Known SHA-256 test vector accepted");
    }
}
void NativeFbxProvenanceTest(const std::filesystem::path& source) {
    auto sidecar=source;sidecar+=".max.json";
    const bool withoutMetadata=!std::filesystem::exists(sidecar);
    Expect(withoutMetadata,"Ordinary FBX fixture is isolated from all SHA-256 sidecar test vectors");
    if(!withoutMetadata)return;
    {
        std::ofstream out(source,std::ios::binary);
        out<<R"FBX(; FBX 7.4.0 project file
FBXHeaderExtension: { FBXHeaderVersion: 1003
    FBXVersion: 7400
    Creator: "Max metadata regression"
}
GlobalSettings: {
    Version: 1000
    Properties70: { P: "UpAxis", "int", "Integer", "",1
        P: "UpAxisSign", "int", "Integer", "",1
        P: "FrontAxis", "int", "Integer", "",2
        P: "FrontAxisSign", "int", "Integer", "",1
        P: "CoordAxis", "int", "Integer", "",0
        P: "CoordAxisSign", "int", "Integer", "",1
        P: "UnitScaleFactor", "double", "Number", "",1
    }
}
Definitions: {
    Version: 100
    Count: 4
    ObjectType: "Geometry" { Count: 1 }
    ObjectType: "Model" { Count: 2 }
    ObjectType: "Material" { Count: 1 }
}
Objects: {
    Geometry: 100, "Geometry::Geometry001", "Mesh" {
        GeometryVersion: 124
        Vertices: *9 { a: 0,0,0,1,0,0,0,1,0 }
        PolygonVertexIndex: *3 { a: 0,1,-3 }
        LayerElementMaterial: 0 {
            Version: 101
            Name: ""
            MappingInformationType: "AllSame"
            ReferenceInformationType: "IndexToDirect"
            Materials: *1 { a: 0 }
        }
        Layer: 0 {
            Version: 100
            LayerElement: { Type: "LayerElementMaterial"
                TypedIndex: 0
            }
        }
    }
    Model: 200, "Model::Object001", "Mesh" {
        Version: 232
        Properties70: { P: "Lcl Translation", "Lcl Translation", "", "A",1,2,3
            P: "Lcl Rotation", "Lcl Rotation", "", "A",0,0,0
            P: "Lcl Scaling", "Lcl Scaling", "", "A",1,1,1
        }
        Shading: T
        Culling: "CullingOff"
    }
    Model: 202, "Model::Group001", "Null" {
        Version: 232
        Properties70: { P: "Lcl Translation", "Lcl Translation", "", "A",10,20,30
            P: "Lcl Rotation", "Lcl Rotation", "", "A",0,0,0
            P: "Lcl Scaling", "Lcl Scaling", "", "A",1,1,1
        }
    }
    Material: 300, "Material::Material", "" {
        Version: 102
        ShadingModel: "phong"
        MultiLayer: 0
        Properties70: { P: "DiffuseColor", "Color", "", "A",0.8,0.5,0.25 }
    }
}
Connections: {
    C: "OO",100,200
    C: "OO",200,202
    C: "OO",202,0
    C: "OO",300,200
}
)FBX";
    }
    std::string error;auto asset=ImportSkeletalSource(source,{},&error);
    if(!asset){std::printf("FBX fixture diagnostic: %s\n",error.c_str());Expect(false,"Ordinary FBX fixture imports without Max metadata");return;}
    Expect(asset->meshRegions.size()==1&&asset->meshRegions[0].sourceObject=="Object001"&&
        std::find(asset->meshRegions[0].collections.begin(),asset->meshRegions[0].collections.end(),"Group001")!=asset->meshRegions[0].collections.end(),
        "Ordinary FBX without metadata retains exact object name and parent group");
    Expect(asset->geometry->TriangleCount()==1&&asset->skins.size()==1&&asset->skins[0].vertices.size()==3,
        "Non-skeletal high-model route uses rigid scene-node bindings without requiring authored bones");
}
} // namespace

int main() {
    const auto directory=std::filesystem::temp_directory_path()/("max-metadata-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory/"sidecar-vectors");
    std::filesystem::create_directories(directory/"ordinary-fbx");
    struct Cleanup {std::filesystem::path directory;~Cleanup(){std::error_code error;std::filesystem::remove_all(directory,error);}} cleanup{directory};
    try{SidecarTests(directory/"sidecar-vectors"/"scene.fbx.max.json");NativeFbxProvenanceTest(directory/"ordinary-fbx"/"scene.fbx");}
    catch(const std::exception& error){std::printf("Unexpected test failure: %s\n",error.what());++failures;}
    std::printf("Max metadata: %d failure(s)\n",failures);return failures?1:0;
}
