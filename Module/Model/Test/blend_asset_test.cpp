#include "Model/Public/skeletal_asset.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <string>
#include <tuple>

#ifndef MODEL_SOURCE_DIR
#define MODEL_SOURCE_DIR "."
#endif

namespace {
using namespace Model;
namespace fs=std::filesystem;
int failures=0;
bool Expect(bool condition,const char* message) {
    std::printf("  %-94s %s\n",message,condition?"ok":"FAIL");failures+=!condition;return condition;
}
std::string Lower(std::string value) {
    std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c){return char(std::tolower(c));});return value;
}
std::string Text(const fs::path& path) {
    const auto text=path.u8string();return {text.begin(),text.end()};
}
bool Finite(glm::dvec3 value){return std::isfinite(value.x)&&std::isfinite(value.y)&&std::isfinite(value.z);}
bool FacialObject(std::string name){name=Lower(std::move(name));return name=="face"||name.starts_with("eye");}
double MatrixDifference(const glm::dmat4& a,const glm::dmat4& b){double result=0;for(int c=0;c<4;++c)for(int r=0;r<4;++r)result=std::max(result,std::abs(a[c][r]-b[c][r]));return result;}
bool AliceSource(const SkeletalAssetPackage& asset){
    const auto source=Lower(Text(asset.sourcePath.filename()));
    return Lower(asset.sourceFormat)=="blend"&&(source=="alice(1).blend"||source=="alice.blend");
}
void AliceRegressionChecks(const SkeletalAssetPackage& asset){
    std::puts("Alice source-pose and complete skinning regression:");
    struct ObjectSkin {std::size_t sections=0,fullSections=0,vertices=0,maxInfluences=0,controlWeights=0;std::set<std::string> weightedBones;};
    std::map<std::string,ObjectSkin> relevant;
    for(const auto* name:{"body","onepiece","shoes","underdress"})relevant.emplace(name,ObjectSkin{});
    std::set<std::uint32_t> relevantMeshes;bool identityBindings=true;
    for(const auto& region:asset.meshRegions){const auto found=relevant.find(region.sourceObject);if(found==relevant.end()||region.mesh>=asset.skins.size())continue;
        auto& stats=found->second;const auto& skin=asset.skins[region.mesh];++stats.sections;stats.fullSections+=!skin.influenceOffsets.empty();stats.vertices+=skin.vertices.size();relevantMeshes.insert(region.mesh);
        identityBindings&=MatrixDifference(skin.bindTransform,glm::dmat4(1))<1e-8;
        for(std::size_t vi=0;vi<skin.vertices.size();++vi){std::size_t count=0;
            ForEachSkinInfluence(skin,vi,[&](std::uint32_t bone,float weight){if(weight<=0||bone>=asset.skeleton.bones.size())return;++count;const auto& name=asset.skeleton.bones[bone].name;stats.weightedBones.insert(name);
                if(name.starts_with("Skirt")&&name.ends_with("_D_yure_skirt_"))++stats.controlWeights;});stats.maxInfluences=std::max(stats.maxInfluences,count);}
    }
    for(const auto& instance:asset.geometry->instances)if(relevantMeshes.contains(instance.meshIndex))identityBindings&=MatrixDifference(instance.transform,glm::dmat4(1))<1e-8;
    bool present=true;for(const auto& [name,stats]:relevant){present&=stats.sections>0&&stats.vertices>0;
        std::cout<<"Alice "<<name<<": sections="<<stats.sections<<" fullSections="<<stats.fullSections<<" vertices="<<stats.vertices<<" maxInfluences="<<stats.maxInfluences<<" weightedNonDeformControls="<<stats.controlWeights<<'\n';}
    Expect(present,"Alice body, outer dress, underskirt and both shoes retain nonempty distinct source-object sections");
    Expect(relevant["body"].maxInfluences>4&&relevant["onepiece"].maxInfluences>4&&relevant["underdress"].maxInfluences>4&&
        relevant["body"].fullSections>0&&relevant["onepiece"].fullSections>0&&relevant["underdress"].fullSections>0,
        "Alice body and both dress layers retain complete influence tables beyond the four-joint summary");
    Expect(relevant["onepiece"].controlWeights==0&&relevant["underdress"].controlWeights==0,
        "Alice non-deform Skirt##_D authoring control groups do not contribute to dress skinning");
    const auto& shoeBones=relevant["shoes"].weightedBones;
    Expect(shoeBones.contains("Bip01 L Foot")&&shoeBones.contains("Bip01 R Foot"),
        "Alice left and right high-heel sections remain bound to the corresponding original foot bones");
    Expect(identityBindings,"Alice skinned glTF body and garment instances avoid applying a second mesh-node transform");

    const auto source=std::find_if(asset.animations.begin(),asset.animations.end(),[](const auto& sequence){return sequence.name=="SourcePose"&&sequence.generated;});
    if(!Expect(source!=asset.animations.end(),"Repaired Alice package contains the generated authored SourcePose sequence"))return;
    bool constant=source->boneTracks.size()==asset.skeleton.bones.size();std::set<std::uint32_t> tracks;
    for(const auto& track:source->boneTracks)constant&=track.bone<asset.skeleton.bones.size()&&tracks.insert(track.bone).second&&track.translations.size()==1&&track.rotations.size()==1&&track.scales.size()==1&&
        track.translations.front().time==0&&track.rotations.front().time==0&&track.scales.front().time==0;
    for(const auto& track:source->morphTracks)constant&=track.keys.size()==1&&track.keys.front().time==0;
    Expect(constant,"Alice SourcePose stores one constant TRS per skeleton node and constant source expression defaults");
    const auto pose=EvaluateSkeletalPose(asset,&*source,0,false);const auto rest=EvaluateSkeletalPose(asset,nullptr,0,false);
    std::size_t changed=0;bool finite=true;for(std::size_t i=0;i<pose.local.size();++i){changed+=MatrixDifference(TransformMatrix(pose.local[i]),TransformMatrix(rest.local[i]))>1e-5;finite&=Finite(glm::dvec3(pose.global[i][3]));}
    Expect(finite&&changed>0,"Alice SourcePose retains authored bone changes rather than duplicating BindPose");
    const auto preview=std::find_if(asset.animations.begin(),asset.animations.end(),[](const auto& sequence){return sequence.name=="RigPreview"&&sequence.generated;});
    if(!Expect(preview!=asset.animations.end(),"Alice still provides an optional generated rig-inspection motion"))return;
    bool footFit=true;std::size_t feet=0;
    for(std::size_t bone=0;bone<asset.skeleton.bones.size();++bone)if(asset.skeleton.bones[bone].name=="Bip01 L Foot"||asset.skeleton.bones[bone].name=="Bip01 R Foot"){
        ++feet;for(double time:{0.,1.,2.75}){const auto inspection=EvaluateSkeletalPose(asset,&*preview,time,false);footFit&=MatrixDifference(inspection.global[bone],pose.global[bone])<1e-6;}}
    Expect(feet==2&&footFit,"Alice RigPreview preserves the complete source-pose world transforms of both high-heel foot bones");
    std::cout<<"Alice SourcePose: changed local transforms="<<changed<<" constant source tracks="<<source->boneTracks.size()<<'\n';
}
}

int main(int argc,char** argv) {
    if(argc>2){std::puts("Usage: blend_asset_test [Package.asset.json or package directory]");return 2;}
    const bool defaultAlice=argc==1;
    const auto path=defaultAlice?fs::path(MODEL_SOURCE_DIR)/"Asset/Generated/AliceMaid/Package.asset.json":fs::u8path(argv[1]);
    if(!fs::exists(path)) {
        std::cout<<"Blender cached asset fixture is missing: "<<Text(path)<<'\n';
        return defaultAlice?77:1;
    }
    try {
        std::string error;auto asset=LoadSkeletalAssetPackage(path,&error);
        if(!Expect(bool(asset),"Load generated Blender package directly, without running Blender")){std::cout<<error<<'\n';return 1;}
        if(!Expect(ValidateSkeletalAsset(*asset,&error),"Generated package passes geometry, skin, morph and provenance validation")){std::cout<<error<<'\n';return 1;}
        const auto& geometry=*asset->geometry;
        Expect(Lower(asset->sourceFormat)=="blend"&&Lower(Text(asset->sourcePath.extension()))==".blend",
            "Skeletal package retains the original Blender source format and path");
        Expect(asset->sourcePath.lexically_normal()==geometry.sourcePath.lexically_normal(),
            "Geometry provenance points to the original .blend file rather than a temporary GLB");
        std::set<std::uint32_t> reported,facialMeshes;std::array<std::size_t,5> categories{};
        bool sourceObjects=true;
        for(const auto& region:asset->meshRegions) {
            sourceObjects=sourceObjects&&!region.sourceObject.empty()&&region.mesh<geometry.meshes.size()&&reported.insert(region.mesh).second;
            const auto kind=static_cast<std::size_t>(region.kind);if(kind<categories.size())++categories[kind];
            if(FacialObject(region.sourceObject))facialMeshes.insert(region.mesh);
        }
        Expect(sourceObjects&&reported.size()==geometry.meshes.size(),
            "Every render section has one exact nonempty source-object provenance report");
        Expect(categories[std::size_t(MeshRegionKind::Body)]>0&&categories[std::size_t(MeshRegionKind::Hair)]>0&&categories[std::size_t(MeshRegionKind::Garment)]>0,
            "Body, hair and garment sections remain independently classified");
        std::set<std::string> boneNames;bool uniqueNames=true;
        for(const auto& bone:asset->skeleton.bones)if(bone.name.empty()||!boneNames.insert(bone.name).second){uniqueNames=false;std::cout<<"Duplicate or empty skeleton node name: "<<bone.name<<'\n';}
        Expect(uniqueNames,"Export node aliases preserve a unique skeleton-node name for every imported node");
        std::uint32_t maximumWidth=0,maximumHeight=0;
        for(const auto& texture:geometry.textures){maximumWidth=std::max(maximumWidth,texture.width);maximumHeight=std::max(maximumHeight,texture.height);}
        std::cout<<"Asset: "<<asset->name<<" | sections="<<geometry.meshes.size()<<" vertices="<<geometry.VertexCount()<<" triangles="<<geometry.TriangleCount()
            <<" bones="<<asset->skeleton.bones.size()<<" morphs="<<asset->morphs.size()<<" textures="<<geometry.textures.size()<<'\n';
        std::cout<<"Regions: body="<<categories[1]<<" hair="<<categories[2]<<" garment="<<categories[3]<<" accessory="<<categories[4]<<" unknown="<<categories[0]<<'\n';
        std::cout<<"Largest texture dimensions: width="<<maximumWidth<<" height="<<maximumHeight<<'\n';
        const bool aliceFixture=defaultAlice||AliceSource(*asset);
        if(aliceFixture){Expect(maximumWidth>=6144,"The Alice fixture retains its source texture resolution of at least 6144 pixels");AliceRegressionChecks(*asset);}

        auto pose=EvaluateSkeletalPose(*asset,nullptr,0,false);StaticModel skinned;
        if(!Expect(SkinSkeletalMesh(*asset,pose,skinned,&error),"Original bind/rest pose skins successfully through the normal runtime")){std::cout<<error<<'\n';return 1;}
        double maximumDrift=0,maximumWorldDrift=0;bool finite=true;
        std::vector<double> sectionDrift(geometry.meshes.size(),0),sectionWorldDrift(geometry.meshes.size(),0);
        std::vector<std::size_t> worstVertices(geometry.meshes.size(),0);std::size_t worstMesh=0;
        for(std::size_t mi=0;mi<geometry.meshes.size();++mi)for(std::size_t vi=0;vi<geometry.meshes[mi].vertices.size();++vi) {
            const auto source=geometry.meshes[mi].vertices[vi].position,current=skinned.meshes[mi].vertices[vi].position;
            const auto delta=current-source;finite=finite&&Finite(current)&&Finite(delta);
            const double localDrift=glm::length(delta),worldDrift=glm::length(glm::dvec3(asset->skins[mi].bindTransform*glm::dvec4(delta,0)));
            if(localDrift>sectionDrift[mi]){sectionDrift[mi]=localDrift;worstVertices[mi]=vi;}
            sectionWorldDrift[mi]=std::max(sectionWorldDrift[mi],worldDrift);
            if(localDrift>maximumDrift){maximumDrift=localDrift;worstMesh=mi;}
            maximumWorldDrift=std::max(maximumWorldDrift,worldDrift);
        }
        const auto extent=geometry.bounds.Extent();const double tolerance=1e-4*std::max({1.,extent.x,extent.y,extent.z});
        std::cout<<std::setprecision(12)<<"Rest skin maximum bind drift="<<maximumDrift<<" source units; model-space="<<maximumWorldDrift<<"; tolerance="<<tolerance<<'\n';
        for(std::size_t mi=0;mi<geometry.meshes.size();++mi)if(sectionDrift[mi]>tolerance||sectionWorldDrift[mi]>tolerance) {
            const auto source=std::find_if(asset->meshRegions.begin(),asset->meshRegions.end(),[&](const auto& region){return region.mesh==mi;});
            const auto vi=worstVertices[mi];const auto delta=skinned.meshes[mi].vertices[vi].position-geometry.meshes[mi].vertices[vi].position;
            std::cout<<"Rest drift section="<<mi<<" mesh='"<<geometry.meshes[mi].name<<"' object='"<<(source==asset->meshRegions.end()?"(unmapped)":source->sourceObject)
                <<"' vertex="<<vi<<" local="<<sectionDrift[mi]<<" world="<<sectionWorldDrift[mi]<<" delta=("<<delta.x<<','<<delta.y<<','<<delta.z<<")\n";
        }
        if(maximumDrift>tolerance) {
            const auto vi=worstVertices[worstMesh];const auto& skin=asset->skins[worstMesh];
            const auto position=geometry.meshes[worstMesh].vertices[vi].position;
            std::cout<<"Worst rest vertex section="<<worstMesh<<" vertex="<<vi<<" source=("<<position.x<<','<<position.y<<','<<position.z<<")\n";
            ForEachSkinInfluence(skin,vi,[&](std::uint32_t joint,float weight){if(weight>0&&joint<pose.global.size()) {
                const auto offset=joint<skin.boneFromMesh.size()?skin.boneFromMesh[joint]:asset->skeleton.bones[joint].inverseBind*skin.bindTransform;
                const auto matrix=glm::inverse(skin.bindTransform)*pose.global[joint]*offset;double identityError=0;
                for(int column=0;column<4;++column)for(int row=0;row<4;++row)identityError=std::max(identityError,std::abs(matrix[column][row]-(column==row?1.:0.)));
                const auto individual=glm::dvec3(matrix*glm::dvec4(position,1))-position;
                std::cout<<"  weighted bone="<<joint<<" '"<<asset->skeleton.bones[joint].name<<"' weight="<<weight
                    <<" rest matrix identity error="<<identityError<<" translation=("<<matrix[3].x<<','<<matrix[3].y<<','<<matrix[3].z
                    <<") point delta=("<<individual.x<<','<<individual.y<<','<<individual.z<<")\n";
            }});
        }
        Expect(finite&&maximumDrift<=tolerance&&maximumWorldDrift<=tolerance,
            "Rest skinning preserves imported bind geometry within the scaled precision tolerance");

        std::size_t selected=asset->morphs.size();double largestDeltaSquared=0;
        for(std::size_t morph=0;morph<asset->morphs.size();++morph)for(const auto& delta:asset->morphs[morph].deltas)if(facialMeshes.contains(delta.mesh)&&Finite(delta.position)) {
            const double length=glm::dot(delta.position,delta.position);
            if(length>largestDeltaSquared){largestDeltaSquared=length;selected=morph;}
        }
        if(Expect(selected<asset->morphs.size()&&largestDeltaSquared>1e-16,
            "At least one nonzero face/eye source shape key survives conversion")) {
            // Keep just the affected facial positions rather than duplicate all
            // render geometry or any high-resolution image data for this check.
            std::vector<std::tuple<std::uint32_t,std::uint32_t,glm::dvec3>> baseline;
            std::set<std::pair<std::uint32_t,std::uint32_t>> visited;
            for(const auto& delta:asset->morphs[selected].deltas)if(facialMeshes.contains(delta.mesh)&&
                delta.mesh<skinned.meshes.size()&&delta.vertex<skinned.meshes[delta.mesh].vertices.size()&&visited.emplace(delta.mesh,delta.vertex).second)
                baseline.emplace_back(delta.mesh,delta.vertex,skinned.meshes[delta.mesh].vertices[delta.vertex].position);
            std::fill(pose.morphWeights.begin(),pose.morphWeights.end(),0.f);pose.morphWeights[selected]=1;
            if(Expect(SkinSkeletalMesh(*asset,pose,skinned,&error),"A facial shape key at weight one skins through the same runtime")) {
                double changed=0;bool morphFinite=true;
                for(const auto& [mesh,vertex,before]:baseline){const auto after=skinned.meshes[mesh].vertices[vertex].position;morphFinite=morphFinite&&Finite(after);changed=std::max(changed,glm::length(after-before));}
                std::cout<<"Facial morph: "<<asset->morphs[selected].name<<" | observed maximum vertex change="<<changed<<" source units on "<<baseline.size()<<" affected facial vertices\n";
                Expect(morphFinite&&changed>1e-8,"The imported facial shape key changes actual rendered vertex positions");
            }else std::cout<<error<<'\n';
        }
        std::printf("Blender cached asset: %d failure(s).\n",failures);return failures?1:0;
    } catch(const std::exception& exception){std::cerr<<"Blender cached asset test failed: "<<exception.what()<<'\n';return 1;}
}
