#include "Model/Private/garment_recognition.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <map>
#include <sstream>
#include <string_view>

namespace Model {
const char* MeshRegionKindName(MeshRegionKind kind) {
    switch(kind) {
        case MeshRegionKind::Body: return "Body";
        case MeshRegionKind::Hair: return "Hair";
        case MeshRegionKind::Garment: return "Garment";
        case MeshRegionKind::Accessory: return "Accessory";
        default: return "Unknown";
    }
}
namespace detail {
namespace {
constexpr std::array Kinds{MeshRegionKind::Unknown,MeshRegionKind::Body,
    MeshRegionKind::Hair,MeshRegionKind::Garment,MeshRegionKind::Accessory};
std::size_t Index(MeshRegionKind kind){return static_cast<std::size_t>(kind);}

std::string Words(std::string_view input) {
    std::string out;
    for(std::size_t i=0;i<input.size();++i) {
        const auto c=static_cast<unsigned char>(input[i]);
        if(c>=128){out.push_back(static_cast<char>(c));continue;}
        if(std::isupper(c)&&i>0&&static_cast<unsigned char>(input[i-1])<128&&
            std::islower(static_cast<unsigned char>(input[i-1]))) out.push_back(' ');
        if(std::isalpha(c))out.push_back(static_cast<char>(std::tolower(c)));
        else out.push_back(' ');
    }
    return out;
}
bool Term(const std::string& text,std::string_view term) {
    if(static_cast<unsigned char>(term.front())>=128)return text.find(term)!=std::string::npos;
    std::size_t p=0;
    while((p=text.find(term,p))!=std::string::npos) {
        const auto end=p+term.size();
        if((p==0||!std::isalpha(static_cast<unsigned char>(text[p-1])))&&
            (end==text.size()||!std::isalpha(static_cast<unsigned char>(text[end]))))return true;
        p=end;
    }
    return false;
}
bool Any(const std::string& words,std::initializer_list<std::string_view> terms) {
    return std::any_of(terms.begin(),terms.end(),[&](auto term){return Term(words,term);});
}
std::array<bool,5> NameKinds(std::string_view name) {
    const auto w=Words(name);std::array<bool,5> result{};
    result[Index(MeshRegionKind::Body)]=Any(w,{"body","skin","face","head","eye","eyes","eyewhite","eyelid","eyelids","iris","pupil","eyelash","eyelashes","eyebrow","eyebrows","mouth","teeth","tongue","nail","nails","nose","ear","ears","肌","皮肤","皮膚","身体","顔","目","口","歯","体","素体"});
    result[Index(MeshRegionKind::Hair)]=Any(w,{"hair","hairs","bang","bangs","ponytail","twintail","ahoge","髪","髮","头发","頭髪","前发","后发","发尾"});
    result[Index(MeshRegionKind::Garment)]=Any(w,{"cloth","clothes","clothing","costume","fabric","garment","skirt","dress","onepiece","underdress","collar","apron","blouse","sleeve","sleeves","cape","coat","jacket","uniform","shirt","pants","trousers","shorts","sock","socks","stocking","stockings","underwear","petticoat","maid","服","裙","衣","袖","围裙","圍裙","エプロン","スカート","ワンピース","ソックス"});
    result[Index(MeshRegionKind::Accessory)]=Any(w,{"accessory","accessories","ornament","ornaments","halo","mop","highheel","highheels","ring","ribbon","bow","button","buckle","jewel","jewelry","necklet","wristlet","shoe","shoes","boot","boots","glass","glasses","weapon","metal","armor","armour","饰品","飾品","装饰","裝飾","髪飾り","发饰","髮飾","蝴蝶领带","リボン","靴","眼镜","眼鏡","武器"});
    // A named hair ornament is a distinct accessory; its location in the
    // hairstyle is not evidence that it should join the hair simulation.
    if(Any(w,{"ornament","ornaments","髪飾り","发饰","髮飾"}))result[Index(MeshRegionKind::Hair)]=false;
    return result;
}
bool IsCloth(const SourceModifierSemantics& modifier){return Words(modifier.type)=="cloth";}
std::string Percent(double value) {
    std::ostringstream out;out<<std::fixed<<std::setprecision(1)<<value*100<<'%';return out.str();
}
} // namespace

std::vector<MeshRegionInfo> RecognizeGarmentSections(const SkeletalAssetPackage& source,
    std::span<const SourceMeshSemantics> semantics) {
    std::vector<MeshRegionInfo> reports;
    if(!source.geometry)return reports;
    // Duplicate/out-of-range metadata is not allowed to silently reassign a
    // section. Such a section falls back to geometry evidence and is reviewed.
    std::map<std::uint32_t,const SourceMeshSemantics*> byMesh;
    for(const auto& input:semantics)if(input.mesh<source.geometry->meshes.size()) {
        const auto [it,inserted]=byMesh.emplace(input.mesh,&input);if(!inserted)it->second=nullptr;
    }
    std::vector<std::array<bool,5>> boneKinds;boneKinds.reserve(source.skeleton.bones.size());
    for(const auto& bone:source.skeleton.bones) {
        const auto name=bone.name+" "+bone.englishName;auto kinds=NameKinds(name);
        // Ring proximal/intermediate/distal are anatomical finger channels,
        // even though a render material called ring is usually jewelry.
        if(Any(Words(name),{"proximal","intermediate","distal","finger","fingers"}))kinds[Index(MeshRegionKind::Accessory)]=false;
        boneKinds.push_back(kinds);
    }
    reports.reserve(source.geometry->meshes.size());
    for(std::uint32_t mi=0;mi<source.geometry->meshes.size();++mi) {
        const auto& mesh=source.geometry->meshes[mi];MeshRegionInfo info;info.mesh=mi;
        info.sourceMesh=mesh.name;
        if(mesh.materialIndex<source.geometry->materials.size())info.sourceMaterial=source.geometry->materials[mesh.materialIndex].name;
        const auto mapped=byMesh.find(mi);const auto* input=mapped==byMesh.end()?nullptr:mapped->second;
        if(input) {
            info.sourceObject=input->sourceObject;
            if(!input->sourceMesh.empty())info.sourceMesh=input->sourceMesh;
            if(!input->sourceMaterial.empty())info.sourceMaterial=input->sourceMaterial;
            info.collections=input->collections;info.vertexGroups=input->vertexGroups;
            for(const auto& modifier:input->modifiers) {
                std::string description=modifier.name+" ["+modifier.type+"]";
                if(!modifier.showViewport&&!modifier.showRender)description+=" (disabled)";
                else if(!modifier.showViewport)description+=" (render only)";
                else if(!modifier.showRender)description+=" (viewport only)";
                info.modifiers.push_back(std::move(description));
                if(IsCloth(modifier)) {
                    info.authoredCloth=true;
                    if(!modifier.clothPinGroup.empty())info.clothPinGroup=modifier.clothPinGroup;
                    info.evidence.push_back("Source object has authored Cloth modifier: "+modifier.name+
                        (modifier.showViewport||modifier.showRender?".":" (disabled in viewport and render)."));
                }
            }
        } else if(mapped!=byMesh.end())info.evidence.push_back("Conflicting source metadata mapping; object evidence ignored.");

        std::array<double,5> scores{};std::array<bool,5> sectionEvidence{};
        auto named=[&](const std::string& text,const char* label,double strength,bool section) {
            const auto detected=NameKinds(text);
            for(std::size_t k=1;k<scores.size();++k)if(detected[k]) {
                scores[k]=std::max(scores[k],strength);
                sectionEvidence[k]=sectionEvidence[k]||section;
                info.evidence.push_back(std::string(label)+" '"+text+"' suggests "+MeshRegionKindName(Kinds[k])+".");
            }
        };
        // A material section is more specific than its shared source object.
        // In particular a Body object can contain skirt, skin and hair slots.
        named(info.sourceMaterial,"Material",.94,true);
        // A Blender mesh datablock is shared by all material sections, whereas
        // an ordinary importer mesh name may identify a genuine section.
        const bool mixed=input&&input->sourceObjectMaterialCount>1;
        named(info.sourceMesh,"Mesh",mixed?.38:.76,!mixed);
        if(input) {
            named(info.sourceObject,"Object",mixed?.32:.72,false);
            for(const auto& collection:info.collections)named(collection,"Collection",.35,false);
            // Object-level group presence is retained for inspection, not
            // promoted to a per-section weight or classification.
            if(!info.vertexGroups.empty())info.evidence.push_back("Object vertex-group names are metadata only; section skin weights are evaluated separately.");
        }
        std::array<double,5> weights{};double total=0;
        if(mi<source.skins.size()&&source.skins[mi].vertices.size()==mesh.vertices.size()) {
            for(std::size_t vertex=0;vertex<source.skins[mi].vertices.size();++vertex)ForEachSkinInfluence(source.skins[mi],vertex,[&](std::uint32_t bone,float weight) {
                const double w=weight;
                if(!std::isfinite(w)||w<=0||bone>=boneKinds.size())return;
                total+=w;
                for(std::size_t k=1;k<weights.size();++k)if(boneKinds[bone][k])weights[k]+=w;
            });
        }
        if(total>0)for(const auto kind:{MeshRegionKind::Hair,MeshRegionKind::Garment,MeshRegionKind::Accessory}) {
            const auto k=Index(kind);const double fraction=weights[k]/total;
            if(fraction>=.10) {
                scores[k]=std::max(scores[k],std::min(.88,.55+.33*fraction));
                sectionEvidence[k]=true;
                info.evidence.push_back("Section skin weight on "+std::string(MeshRegionKindName(kind))+"-named bones: "+Percent(fraction)+".");
            }
        }
        // Naming a generic Head/Body bone does not establish visible skin:
        // clothes and hair are commonly weighted to those exact same bones.
        std::size_t best=0,runner=0;
        for(std::size_t k=1;k<scores.size();++k)if(scores[k]>scores[best]){runner=best;best=k;}else if(scores[k]>scores[runner])runner=k;
        const bool conflicting=best!=0&&scores[runner]>=.65&&scores[best]-scores[runner]<.20;
        const bool nonGarmentSection=sectionEvidence[Index(MeshRegionKind::Body)]||
            sectionEvidence[Index(MeshRegionKind::Hair)]||sectionEvidence[Index(MeshRegionKind::Accessory)];
        bool ambiguousAuthored=false;
        if(info.authoredCloth) {
            if(!nonGarmentSection&&(!mixed||sectionEvidence[Index(MeshRegionKind::Garment)])) {
                best=Index(MeshRegionKind::Garment);scores[best]=.99;
                info.evidence.push_back("Authored Cloth is compatible with this material section; simulation asset generation still requires explicit selection.");
            } else {
                ambiguousAuthored=true;
                info.evidence.push_back("Object-level Cloth does not identify a safe clothing mask for this material section; review before extraction or simulation.");
            }
        }
        if(best!=0&&scores[best]>=.55&&!conflicting) {
            info.kind=Kinds[best];info.confidence=static_cast<float>(scores[best]);
        } else {
            info.kind=MeshRegionKind::Unknown;info.confidence=static_cast<float>(scores[best]);
            info.evidence.push_back(conflicting?"Conflicting section evidence; no category was selected automatically.":"No sufficiently specific clothing/body/hair/accessory evidence was found.");
        }
        info.requiresReview=info.kind==MeshRegionKind::Unknown||info.kind==MeshRegionKind::Garment||ambiguousAuthored||info.confidence<.85f||mapped!=byMesh.end()&&!mapped->second;
        if(info.kind==MeshRegionKind::Garment)info.evidence.push_back("Garment candidate only: no cloth solver is enabled by this classification.");
        reports.push_back(std::move(info));
    }
    return reports;
}
} // namespace detail
} // namespace Model
