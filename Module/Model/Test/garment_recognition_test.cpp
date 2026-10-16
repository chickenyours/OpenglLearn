#include "Model/Private/garment_recognition.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace {
using namespace Model;
using namespace Model::detail;
int failures=0;
bool Expect(bool condition,const char* message) {
    std::printf("  %-92s %s\n",message,condition?"ok":"FAIL");failures+=!condition;return condition;
}
bool Evidence(const MeshRegionInfo& info,const std::string& text) {
    return std::any_of(info.evidence.begin(),info.evidence.end(),[&](const auto& reason){return reason.find(text)!=std::string::npos;});
}
SkeletalAssetPackage Fixture(std::initializer_list<const char*> materialNames) {
    SkeletalAssetPackage source;source.geometry=std::make_shared<StaticModel>();
    for(const auto name:materialNames) {
        PbrMaterial material;material.name=name;source.geometry->materials.push_back(material);
        StaticMesh mesh;mesh.name="RenderSection";mesh.materialIndex=static_cast<std::uint32_t>(source.geometry->meshes.size());
        mesh.vertices.resize(3);mesh.indices={0,1,2};source.geometry->meshes.push_back(mesh);
        MeshSkin skin;skin.vertices.resize(3);source.skins.push_back(skin);
    }
    SkeletonBone head;head.name="Head";source.skeleton.bones.push_back(head);return source;
}
SourceMeshSemantics Input(std::uint32_t mesh,const char* object="Character",std::uint32_t materials=1) {
    SourceMeshSemantics input;input.mesh=mesh;input.sourceObject=object;input.sourceMesh="SharedMesh";
    input.sourceObjectMaterialCount=materials;return input;
}
void NameAndMixedObjectTests() {
    std::puts("Material-section clothing recognition:");
    auto source=Fixture({"Skin","Hair","Maid_Dress","Metal_Button","Material"});
    std::vector<SourceMeshSemantics> inputs;
    for(std::uint32_t mi=0;mi<source.geometry->meshes.size();++mi) {
        auto input=Input(mi,"Body",5);input.collections={"Entire character"};
        input.vertexGroups={"Head","SkirtFront","HairBack"};inputs.push_back(input);
    }
    const auto info=RecognizeGarmentSections(source,inputs);
    Expect(info.size()==5,"Every material section receives a report, including unknown sections");
    Expect(info[0].kind==MeshRegionKind::Body&&info[1].kind==MeshRegionKind::Hair&&
        info[2].kind==MeshRegionKind::Garment&&info[3].kind==MeshRegionKind::Accessory&&
        info[4].kind==MeshRegionKind::Unknown,"One shared Body object preserves skin/hair/garment/accessory/unknown distinctions");
    Expect(info[2].requiresReview&&!info[2].authoredCloth&&!info[2].userOverride,
        "Named clothing is a reviewable candidate without invented source physics or overrides");
    Expect(info[4].kind==MeshRegionKind::Unknown,"Object-level Skirt/Hair group presence does not classify a generic material section");
    Expect(info[2].sourceObject=="Body"&&info[2].sourceMesh=="SharedMesh"&&
        info[2].sourceMaterial=="Maid_Dress"&&info[2].vertexGroups.size()==3,
        "Reports retain source object, mesh, material and original group evidence");
    Expect(Evidence(info[2],"Material")&&Evidence(info[2],"no cloth solver"),
        "Garment evidence explains the classification and explicit simulation boundary");
    auto generic=Fixture({"Address","Material.001"});
    Expect(RecognizeGarmentSections(generic)[0].kind==MeshRegionKind::Unknown,
        "ASCII token matching does not mistake Address for dress");
    generic.geometry->materials[1].name="前髪";
    Expect(RecognizeGarmentSections(generic)[1].kind==MeshRegionKind::Hair,
        "Japanese hair naming is recognized without changing source encoding");
    generic.geometry->materials[0].name="M_女仆裙";
    Expect(RecognizeGarmentSections(generic)[0].kind==MeshRegionKind::Garment,
        "Chinese garment naming is recognized");
    const auto alice=RecognizeGarmentSections(Fixture({"onepiece","underdress_inner","apron1","stockings_R","collar","nose","ear","necklet","wristlet"}));
    Expect(alice[0].kind==MeshRegionKind::Garment&&alice[1].kind==MeshRegionKind::Garment&&
        alice[2].kind==MeshRegionKind::Garment&&alice[3].kind==MeshRegionKind::Garment&&alice[4].kind==MeshRegionKind::Garment&&
        alice[5].kind==MeshRegionKind::Body&&alice[6].kind==MeshRegionKind::Body&&alice[7].kind==MeshRegionKind::Accessory&&alice[8].kind==MeshRegionKind::Accessory,
        "Alice source aliases distinguish onepiece/underdress/collar, body features and accessories");
    const auto details=RecognizeGarmentSections(Fixture({"hair ornament","halo","mop","highheel","ring","eyewhite","eyelid","蝴蝶领带 阴影"}));
    Expect(details[0].kind==MeshRegionKind::Accessory&&details[1].kind==MeshRegionKind::Accessory&&
        details[2].kind==MeshRegionKind::Accessory&&details[3].kind==MeshRegionKind::Accessory&&details[4].kind==MeshRegionKind::Accessory&&
        details[5].kind==MeshRegionKind::Body&&details[6].kind==MeshRegionKind::Body&&details[7].kind==MeshRegionKind::Accessory,
        "Alice ornaments/props/jewelry stay accessories and eye surfaces remain body sections");
    auto mixedOrnament=Fixture({"Material"});auto ornamentInput=Input(0,"hair ornament",3);
    Expect(RecognizeGarmentSections(mixedOrnament,std::span(&ornamentInput,1))[0].kind==MeshRegionKind::Unknown,
        "A generic material in a mixed hair-ornament object still needs section review");
}
void AuthoredModifierTests() {
    std::puts("Authored Cloth and ambiguous object scope:");
    auto source=Fixture({"Skin","Hair","Dress","Material"});
    std::vector<SourceMeshSemantics> inputs;
    for(std::uint32_t mi=0;mi<4;++mi) {
        auto input=Input(mi,"Character",4);input.modifiers.push_back({"Cloth Setup","CLOTH",true,true,"WaistPin"});
        inputs.push_back(input);
    }
    const auto reports=RecognizeGarmentSections(source,inputs);
    Expect(reports[0].kind==MeshRegionKind::Body&&reports[1].kind==MeshRegionKind::Hair&&
        reports[2].kind==MeshRegionKind::Garment&&reports[3].kind==MeshRegionKind::Unknown,
        "Object Cloth modifier does not turn mixed skin/hair/unknown sections into clothing");
    Expect(std::all_of(reports.begin(),reports.end(),[](const auto& r){return r.authoredCloth&&r.requiresReview&&r.clothPinGroup=="WaistPin";}),
        "Authored modifier and pin group remain visible even for conflicting non-clothing sections");
    Expect(reports[2].confidence>=.99f&&Evidence(reports[3],"safe clothing mask"),
        "Authored clothing evidence is stronger while ambiguous masks explain their review requirement");
    auto plain=Fixture({"Material"});auto input=Input(0);input.modifiers.push_back({"Simulated panel","CLOTH",false,false,"Pinned"});
    auto result=RecognizeGarmentSections(plain,std::span(&input,1));
    Expect(result[0].kind==MeshRegionKind::Garment&&result[0].authoredCloth&&
        Evidence(result[0],"disabled")&&result[0].modifiers[0].find("disabled")!=std::string::npos,
        "A single generic section keeps authored Cloth evidence and its disabled state");
    input.modifiers={{"Cloth-looking subdivision name","SUBSURF",true,true,""}};
    result=RecognizeGarmentSections(plain,std::span(&input,1));
    Expect(!result[0].authoredCloth&&result[0].kind==MeshRegionKind::Unknown,
        "Modifier name alone cannot masquerade as Blender Cloth modifier type");
}
void SectionSkinEvidenceTests() {
    std::puts("Actual section skin weights and conservative conflicts:");
    auto source=Fixture({"Material","Material","Skin"});
    SkeletonBone skirt;skirt.name="SkirtFront";source.skeleton.bones.push_back(skirt);
    SkeletonBone hair;hair.name="HairBack";source.skeleton.bones.push_back(hair);
    for(auto& v:source.skins[0].vertices){v.joints={1,0,0,0};v.weights={.8f,.2f,0,0};}
    for(auto& v:source.skins[1].vertices){v.joints={2,0,0,0};v.weights={.7f,.3f,0,0};}
    for(auto& v:source.skins[2].vertices){v.joints={2,0,0,0};v.weights={1,0,0,0};}
    const auto before=source.skins[0].vertices[0].weights;
    const auto reports=RecognizeGarmentSections(source);
    Expect(reports[0].kind==MeshRegionKind::Garment&&reports[1].kind==MeshRegionKind::Hair,
        "Actual weighted skirt and hair bones can identify otherwise generic material sections");
    Expect(reports[2].kind==MeshRegionKind::Unknown&&reports[2].requiresReview&&Evidence(reports[2],"Conflicting"),
        "Strong Skin material versus Hair skinning conflict is exposed instead of silently selected");
    Expect(source.skins[0].vertices[0].weights==before&&source.geometry->meshes.size()==3,
        "Recognition does not change source skinning, mesh layout or material layout");
    auto headOnly=Fixture({"Material"});
    Expect(RecognizeGarmentSections(headOnly)[0].kind==MeshRegionKind::Unknown,
        "Head or Body bone weights alone do not imply visible skin or hair");
    headOnly.skeleton.bones[0].name="LeftRingProximal";
    Expect(RecognizeGarmentSections(headOnly)[0].kind==MeshRegionKind::Unknown,
        "Anatomical ring-finger bone weights do not imply jewelry geometry");
    source.skins[0].vertices[0].weights[0]=std::numeric_limits<float>::quiet_NaN();
    source.skins[0].vertices[1].joints[0]=999;
    const auto malformed=RecognizeGarmentSections(source);
    Expect(malformed.size()==3&&std::isfinite(malformed[0].confidence),
        "Malformed optional skin evidence is ignored without invalid indexing or non-finite confidence");
}
void MappingTests() {
    auto source=Fixture({"Material"});auto first=Input(0,"Skirt");auto second=Input(0,"Hair");
    const std::array inputs{first,second};const auto reports=RecognizeGarmentSections(source,inputs);
    Expect(reports[0].kind==MeshRegionKind::Unknown&&reports[0].requiresReview&&Evidence(reports[0],"Conflicting source"),
        "Duplicate section metadata cannot arbitrarily select a source object");
    SkeletalAssetPackage empty;Expect(RecognizeGarmentSections(empty).empty(),"Empty geometry yields an empty classification report");
    auto complete=Fixture({"Material"});
    for(const char* name:{"Joint1","Joint2","Joint3","Skirt"}){SkeletonBone bone;bone.name=name;complete.skeleton.bones.push_back(bone);}
    auto& skin=complete.skins[0];skin.influenceOffsets={0};
    for(std::size_t vertex=0;vertex<skin.vertices.size();++vertex){for(std::uint32_t bone=0;bone<5;++bone)skin.influences.push_back({bone,.2f});skin.influenceOffsets.push_back(std::uint32_t(skin.influences.size()));}
    Expect(RecognizeGarmentSections(complete)[0].kind==MeshRegionKind::Garment,
        "Clothing recognition includes a skirt influence beyond the four-weight compatibility summary");
}
} // namespace
int main() {
    NameAndMixedObjectTests();AuthoredModifierTests();SectionSkinEvidenceTests();MappingTests();
    std::printf("Garment recognition: %d failure(s)\n",failures);return failures?1:0;
}
