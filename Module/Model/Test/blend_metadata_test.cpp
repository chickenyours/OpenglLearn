#include "Model/Private/blend_metadata.h"
#include <json/json.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace {
Json::Value Rows(const glm::dmat4& matrix) {
    Json::Value rows(Json::arrayValue);
    for(unsigned r=0;r<4;++r) {
        Json::Value row(Json::arrayValue);
        for(unsigned c=0;c<4;++c)row.append(matrix[c][r]);
        rows.append(row);
    }
    return rows;
}
bool Near(const glm::dmat4& a,const glm::dmat4& b) {
    for(unsigned c=0;c<4;++c)for(unsigned r=0;r<4;++r)if(std::abs(a[c][r]-b[c][r])>1e-8)return false;
    return true;
}
void Write(const std::filesystem::path& path,const Json::Value& document) {
    std::ofstream stream(path,std::ios::binary);Json::StreamWriterBuilder builder;
    stream<<Json::writeString(builder,document);
}
void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
Json::Value ReadJson(std::istream& stream) {
    Json::CharReaderBuilder builder;builder["rejectDupKeys"]=true;builder["allowSpecialFloats"]=false;
    builder["failIfExtra"]=true;Json::Value document;std::string error;
    if(!Json::parseFromStream(builder,stream,&document,&error))throw std::runtime_error("Invalid metadata JSON: "+error);
    return document;
}
Json::Value GlbJson(const std::filesystem::path& path) {
    std::ifstream stream(path,std::ios::binary);Require(bool(stream),"Cannot open lightweight GLB scene metadata");
    std::array<unsigned char,20> header{};stream.read(reinterpret_cast<char*>(header.data()),header.size());
    Require(bool(stream),"Truncated GLB header");
    const auto u32=[&](unsigned offset){return std::uint32_t(header[offset])|(std::uint32_t(header[offset+1])<<8)|
        (std::uint32_t(header[offset+2])<<16)|(std::uint32_t(header[offset+3])<<24);};
    const auto count=u32(12);
    Require(u32(0)==0x46546c67&&u32(4)==2&&u32(16)==0x4e4f534a&&count>0&&count<=64ull*1024*1024&&
        std::uint64_t(count)+20<=u32(8),"Invalid or oversized GLB JSON metadata chunk");
    std::string json(count,'\0');stream.read(json.data(),static_cast<std::streamsize>(json.size()));
    Require(bool(stream),"Truncated GLB JSON chunk");std::istringstream source(json);return ReadJson(source);
}
double Component(const Json::Value& array,unsigned index) {
    Require(array.isArray()&&index<array.size()&&array[index].isNumeric(),"Invalid GLB node TRS component");
    const double value=array[index].asDouble();Require(std::isfinite(value),"Non-finite GLB node TRS component");return value;
}
Model::BoneTransform GlbTransform(const Json::Value& node) {
    Model::BoneTransform transform;
    if(node.isMember("matrix")) {
        Require(node["matrix"].size()==16,"Invalid GLB node matrix size");glm::dmat4 matrix(1);
        for(unsigned c=0;c<4;++c)for(unsigned r=0;r<4;++r)matrix[c][r]=Component(node["matrix"],c*4+r);
        glm::dvec3 skew;glm::dvec4 perspective;
        Require(glm::decompose(matrix,transform.scale,transform.rotation,transform.translation,skew,perspective),
            "Cannot decompose GLB scene-node rest metadata");
    }else {
        for(unsigned axis=0;axis<3;++axis) {
            if(node.isMember("translation"))transform.translation[axis]=Component(node["translation"],axis);
            if(node.isMember("scale"))transform.scale[axis]=Component(node["scale"],axis);
        }
        if(node.isMember("rotation"))transform.rotation=glm::dquat(Component(node["rotation"],3),
            Component(node["rotation"],0),Component(node["rotation"],1),Component(node["rotation"],2));
    }
    Require(std::isfinite(glm::dot(transform.rotation,transform.rotation))&&glm::dot(transform.rotation,transform.rotation)>1e-16,
        "Invalid GLB scene-node rest quaternion");
    transform.rotation=glm::normalize(transform.rotation);return transform;
}
int MetadataCommand(const std::filesystem::path& metadataPath,const std::filesystem::path& glbPath) {
    try {
        using namespace Model;const auto glb=GlbJson(glbPath);const auto& nodes=glb["nodes"];
        Require(nodes.isArray()&&!nodes.empty()&&nodes.size()<=100000,"Invalid GLB scene-node metadata count");
        SkeletalAssetPackage asset;asset.id="source-pose-metadata-check";asset.skeleton.id="metadata-skeleton";
        asset.sourcePath=metadataPath;asset.geometry=std::make_shared<StaticModel>();asset.skeleton.bones.resize(nodes.size());
        for(Json::ArrayIndex i=0;i<nodes.size();++i) {
            auto& bone=asset.skeleton.bones[i];bone.name=nodes[i].get("name","Node_"+std::to_string(i)).asString();
            bone.restLocal=GlbTransform(nodes[i]);
            if(nodes[i].isMember("children")) {
                Require(nodes[i]["children"].isArray()&&nodes[i]["children"].size()<=nodes.size(),"Invalid GLB node children");
                for(const auto& child:nodes[i]["children"]) {
                    Require(child.isUInt()&&child.asUInt()<nodes.size(),"GLB scene child index is out of range");
                    auto& parent=asset.skeleton.bones[child.asUInt()].parent;
                    Require(parent==-1,"GLB scene node has multiple parents");parent=static_cast<std::int32_t>(i);
                }
            }
        }
        detail::ImportBlendSourcePose(asset,metadataPath);
        Require(asset.animations.size()==1,"Metadata produced no captured SourcePose");
        const auto& sequence=asset.animations[0];
        std::printf("SourcePose metadata accepted: %zu nodes, %zu bone tracks, %.3f FPS.\n",
            asset.skeleton.bones.size(),sequence.boneTracks.size(),sequence.framesPerSecond);
        std::puts("Only GLB JSON scene nodes were read; mesh buffers and texture pixels were not loaded.");
        for(const auto& diagnostic:sequence.diagnostics)std::printf("  %s\n",diagnostic.c_str());
        return 0;
    }catch(const std::exception& error){std::fprintf(stderr,"SourcePose metadata rejected: %s\n",error.what());return 1;}
}
}

int main(int argc,char** argv) {
    if(argc==4&&std::string(argv[1])=="--source-pose-metadata")
        return MetadataCommand(std::filesystem::u8path(argv[2]),std::filesystem::u8path(argv[3]));
    if(argc!=1){std::fprintf(stderr,"Usage: blend_metadata_test [--source-pose-metadata source.blend.json scene.glb]\n");return 2;}
    using namespace Model;
    int failures=0;
    auto expect=[&](bool value,const char* message){std::printf("%s %s\n",value?"OK":"FAIL",message);failures+=!value;};
    const auto path=std::filesystem::temp_directory_path()/("blend-metadata-"+
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+".json");
    struct Cleanup { std::filesystem::path path;~Cleanup(){std::error_code ec;std::filesystem::remove(path,ec);} } cleanup{path};
    {
        std::ofstream out(path,std::ios::binary);
        out<<R"({"assetType":"BlenderSourceMetadata","version":1,"warnings":["Material approximation"],"objects":[
          {"name":"Character","type":"MESH","dataName":"SharedSource","collections":["cloth"],
           "materials":[{"slot":0,"name":"Skin","vertexGroups":[{"name":"Head","weightedVertexCount":3}]},
                        {"slot":1,"name":"Dress","vertexGroups":[{"name":"WaistPin","weightedVertexCount":2}]}],
           "modifiers":[{"name":"Fabric","type":"CLOTH","showViewport":false,"showRender":true,"settings":{"vertex_group_mass":"WaistPin"}}],
           "export":{"nodes":[{"nodeName":"Exact Node","meshName":"codexMesh_0","primitives":[
              {"primitiveIndex":0,"materialName":"Skin","sourceMaterialSlots":[0]},
              {"primitiveIndex":1,"materialName":"Dress","sourceMaterialSlots":[1]}]}]}}]})";
    }
    SkeletalAssetPackage source;source.geometry=std::make_shared<StaticModel>();
    for(const char* name:{"Skin","Dress","NotExported"}) {
        const auto index=static_cast<std::uint32_t>(source.geometry->materials.size());
        PbrMaterial material;material.name=name;source.geometry->materials.push_back(material);
        StaticMesh mesh;mesh.name="codexMesh_0-"+std::to_string(index);mesh.materialIndex=index;source.geometry->meshes.push_back(mesh);
        source.geometry->instances.push_back({"Exact Node",index,glm::dmat4(1)});
    }
    try {
        const auto inputs=detail::ReadBlendMeshSemantics(source,path);
        expect(inputs.size()==2,"Only exact exported node and material pairs obtain source provenance");
        expect(inputs.size()==2&&inputs[0].mesh==0&&inputs[1].mesh==1&&inputs[1].sourceObject=="Character"&&
            inputs[1].sourceMesh=="SharedSource"&&inputs[1].sourceObjectMaterialCount==2,"A mixed object keeps distinct source material sections");
        expect(inputs.size()==2&&inputs[0].vertexGroups==std::vector<std::string>{"Head"}&&
            inputs[1].vertexGroups==std::vector<std::string>{"WaistPin"},"Per-section weighted groups never leak across material sections");
        expect(inputs.size()==2&&inputs[1].modifiers.size()==1&&inputs[1].modifiers[0].clothPinGroup=="WaistPin"&&
            !inputs[1].modifiers[0].showViewport&&inputs[1].modifiers[0].showRender,"Authored disabled Cloth and pin metadata survive the bridge");
        expect(source.warnings.size()==2,"Approximation and missing provenance produce explicit diagnostics");
        const auto reports=detail::RecognizeGarmentSections(source,inputs);
        expect(reports.size()==3&&reports[0].kind==MeshRegionKind::Body&&reports[1].kind==MeshRegionKind::Garment&&
            reports[2].kind==MeshRegionKind::Unknown,"Mixed source Cloth does not classify skin or unmatched sections as garments");

        detail::ImportBlendSourcePose(source,path);
        expect(source.animations.empty(),"Legacy metadata without a captured source pose remains unchanged");
        SkeletalAssetPackage poseAsset;poseAsset.id="pose-fixture";poseAsset.skeleton.id="pose-skeleton";
        poseAsset.sourcePath="source/character.blend";poseAsset.geometry=std::make_shared<StaticModel>();
        // Deliberately place child nodes before their parents. Captured bones
        // and ordinary scene nodes must share one consistent evaluated frame.
        SkeletonBone tip;tip.name="Tip";tip.parent=3;tip.restLocal.translation={0,1,0};
        SkeletonBone scene;scene.name="Scene";scene.restLocal.translation={10,0,0};
        SkeletonBone meshNode;meshNode.name="MeshNode";meshNode.parent=0;meshNode.restLocal.translation={0,0,2};
        SkeletonBone hip;hip.name="Hip";hip.parent=1;hip.restLocal.translation={0,2,0};
        SkeletonBone follower;follower.name="Follower";follower.parent=3;follower.restLocal.translation={1,0,0};
        poseAsset.skeleton.bones={tip,scene,meshNode,hip,follower};
        const auto originalRest=GlobalBoneTransforms(poseAsset.skeleton,RestPose(poseAsset.skeleton));
        MeshSkin skin;
        for(std::size_t i=0;i<originalRest.size();++i) {
            poseAsset.skeleton.bones[i].inverseBind=glm::inverse(originalRest[i]);
            skin.boneFromMesh.push_back(poseAsset.skeleton.bones[i].inverseBind);
        }
        skin.bindTransform=glm::translate(glm::dmat4(1),glm::dvec3(1,2,3));poseAsset.skins.push_back(skin);
        for(unsigned mesh=0;mesh<2;++mesh) {
            poseAsset.geometry->meshes.emplace_back();MeshRegionInfo region;region.mesh=mesh;
            region.sourceObject=mesh==0?"DressObject":"FaceObject";poseAsset.meshRegions.push_back(region);
            MorphTarget morph;morph.name="Smile";morph.deltas.push_back({mesh,0,{0,.1,0},{0,0,0}});
            poseAsset.morphs.push_back(morph);
        }
        const auto before=poseAsset;
        BoneTransform hipPose;hipPose.translation={11,4,0};
        hipPose.rotation=glm::angleAxis(.5,glm::dvec3(0,0,1));hipPose.scale={1.5,1.5,1.5};
        const auto hipGlobal=TransformMatrix(hipPose);
        BoneTransform tipPose;tipPose.translation={0,3,0};
        tipPose.rotation=glm::angleAxis(-.2,glm::dvec3(1,0,0));tipPose.scale={.75,1.25,1};
        const auto tipGlobal=hipGlobal*TransformMatrix(tipPose);
        Json::Value document;document["assetType"]="BlenderSourceMetadata";document["version"]=1;
        document["objects"]=Json::Value(Json::arrayValue);document["scene"]["framesPerSecond"]=24;
        auto& captured=document["sourcePose"];captured["name"]="SourcePose";captured["frame"]=42;
        captured["framesPerSecond"]=48;captured["boneGlobals"]=Json::Value(Json::arrayValue);
        Json::Value hipEntry;hipEntry["name"]="Hip";hipEntry["matrix"]=Rows(hipGlobal);captured["boneGlobals"].append(hipEntry);
        Json::Value tipEntry;tipEntry["name"]="Tip";tipEntry["matrix"]=Rows(tipGlobal);captured["boneGlobals"].append(tipEntry);
        Json::Value dressMorph;dressMorph["sourceObject"]="DressObject";dressMorph["name"]="Smile";dressMorph["value"]=.7;
        Json::Value faceMorph;faceMorph["sourceObject"]="FaceObject";faceMorph["name"]="Smile";faceMorph["value"]=.2;
        captured["morphDefaults"].append(dressMorph);captured["morphDefaults"].append(faceMorph);
        Write(path,document);detail::ImportBlendSourcePose(poseAsset,path);
        expect(poseAsset.animations.size()==1&&poseAsset.animations[0].name=="SourcePose"&&
            poseAsset.animations[0].id=="pose-fixture/animations/SourcePose"&&poseAsset.animations[0].generated&&
            poseAsset.animations[0].duration==1&&poseAsset.animations[0].framesPerSecond==48&&
            poseAsset.animations[0].source.find("frame=42")!=std::string::npos,
            "Captured source pose becomes an identified constant generated preview with source frame and FPS");
        if(poseAsset.animations.empty())throw std::runtime_error("Missing generated source pose fixture");
        const auto& animation=poseAsset.animations[0];bool constant=animation.boneTracks.size()==5;
        for(const auto& track:animation.boneTracks)constant=constant&&track.translations.size()==1&&
            track.rotations.size()==1&&track.scales.size()==1&&track.translations[0].time==0&&
            track.rotations[0].time==0&&track.scales[0].time==0;
        expect(constant,"Every captured bone and propagated scene node has a single TRS key at zero");
        const auto pose=EvaluateSkeletalPose(poseAsset,&animation,.75,false);
        expect(Near(pose.global[3],hipGlobal)&&Near(pose.global[0],tipGlobal),
            "Source row matrices reproduce captured world transforms through forward-parent hierarchy");
        expect(Near(pose.global[1],originalRest[1])&&
            Near(pose.global[2],tipGlobal*TransformMatrix(meshNode.restLocal))&&
            Near(pose.global[4],hipGlobal*TransformMatrix(follower.restLocal)),
            "Uncaptured scene and mesh nodes propagate original local transforms in the source pose");
        const auto latePose=EvaluateSkeletalPose(poseAsset,&animation,100,false);
        expect(Near(latePose.global[0],pose.global[0])&&Near(latePose.global[3],pose.global[3]),
            "Captured pose remains constant across preview playback and endpoint clamping");
        expect(pose.morphWeights.size()==2&&std::abs(pose.morphWeights[0]-.7f)<1e-6&&
            std::abs(pose.morphWeights[1]-.2f)<1e-6,"Source morph defaults distinguish identical names on separate objects");
        bool bindsUnchanged=Near(poseAsset.skins[0].bindTransform,before.skins[0].bindTransform);
        const auto afterRest=GlobalBoneTransforms(poseAsset.skeleton,RestPose(poseAsset.skeleton));
        for(std::size_t i=0;i<originalRest.size();++i)bindsUnchanged=bindsUnchanged&&Near(originalRest[i],afterRest[i])&&
            Near(poseAsset.skeleton.bones[i].inverseBind,before.skeleton.bones[i].inverseBind)&&
            Near(poseAsset.skins[0].boneFromMesh[i],before.skins[0].boneFromMesh[i]);
        expect(bindsUnchanged,"Source pose capture preserves original rest hierarchy, mesh frame and all inverse binds");
        detail::ImportBlendSourcePose(poseAsset,path);
        expect(poseAsset.animations.size()==1,"Repeated capture replaces its generated sequence without duplicate IDs");
        auto sceneFPS=document;sceneFPS["sourcePose"].removeMember("framesPerSecond");Write(path,sceneFPS);
        auto fpsAsset=before;detail::ImportBlendSourcePose(fpsAsset,path);
        expect(fpsAsset.animations.size()==1&&fpsAsset.animations[0].framesPerSecond==24,
            "Source pose frame rate falls back to scene metadata");
        {
            // Actual Alice Bip01 R Thigh parent-local matrix. Independent
            // float32 world/parent matrices leave a Gram-Schmidt residual of
            // about 1.1e-5; nearest polar TRS stays below the original 1e-5 cap.
            constexpr double sourceRows[4][4]={
                {-.9410979537903708,.021316880995123924,.3374662363779948,-.35407761024360296},
                {.02353703067077193,-.9914611897737408,.1282635905209689,.00019065680347907232},
                {.33732913557848043,.1286502396076386,.9325653788563559,-.0031715478911918393},
                {0,0,0,1}};
            glm::dmat4 measured(1);for(unsigned r=0;r<4;++r)for(unsigned c=0;c<4;++c)measured[c][r]=sourceRows[r][c];
            auto precisionCase=[&](const glm::dmat4& matrix,const char* message) {
                SkeletalAssetPackage candidate;candidate.id="precision-fixture";candidate.skeleton.id="precision-skeleton";
                candidate.geometry=std::make_shared<StaticModel>();SkeletonBone bone;bone.name="Bip01 R Thigh";candidate.skeleton.bones={bone};
                auto precisionDocument=document;auto& precisionSource=precisionDocument["sourcePose"];
                precisionSource["boneGlobals"]=Json::Value(Json::arrayValue);precisionSource["morphDefaults"]=Json::Value(Json::arrayValue);
                Json::Value entry;entry["name"]=bone.name;entry["matrix"]=Rows(matrix);precisionSource["boneGlobals"].append(entry);
                Write(path,precisionDocument);detail::ImportBlendSourcePose(candidate,path);
                Require(candidate.animations.size()==1,"Missing precision fixture SourcePose");
                const auto& clip=candidate.animations[0];const auto result=EvaluateSkeletalPose(candidate,&clip,0,false);
                double error=0,magnitude=0;
                for(unsigned c=0;c<3;++c)for(unsigned r=0;r<3;++r){error=std::max(error,std::abs(result.global[0][c][r]-matrix[c][r]));magnitude=std::max(magnitude,std::abs(matrix[c][r]));}
                bool diagnosed=false;for(const auto& diagnostic:clip.diagnostics)if(diagnostic.find("bounded polar TRS")!=std::string::npos&&diagnostic.find(bone.name)!=std::string::npos)diagnosed=true;
                expect(error/magnitude<=1e-5&&diagnosed&&
                    glm::determinant(glm::dmat3(result.global[0]))*glm::determinant(glm::dmat3(matrix))>0,message);
            };
            precisionCase(measured,"Measured Alice near-TRS source precision fits within the unchanged reconstruction tolerance");
            auto reflected=measured;reflected[0]=-reflected[0];
            precisionCase(reflected,"Bounded polar fitting retains reflections through signed scale and a proper rotation");
            auto boundary=glm::dmat4(1);boundary[1][0]=1.5e-5;
            precisionCase(boundary,"Small boundary precision residual uses polar fitting rather than increasing the TRS error cap");
        }
        auto rejected=[&](const Json::Value& invalid,const char* message) {
            auto candidate=before;Write(path,invalid);bool failed=false;
            try{detail::ImportBlendSourcePose(candidate,path);}catch(const std::exception&){failed=true;}
            expect(failed&&candidate.animations.empty(),message);
        };
        auto invalid=document;invalid["sourcePose"]["boneGlobals"][0]["matrix"][0][0]="invalid";
        rejected(invalid,"Reject nonnumeric source pose matrix components before creating a preview");
        invalid=document;invalid["sourcePose"]["boneGlobals"][0]["matrix"][0].resize(3);
        rejected(invalid,"Reject source pose matrices with malformed dimensions");
        invalid=document;invalid["sourcePose"]["boneGlobals"][0]["matrix"][3][0]=.1;
        rejected(invalid,"Reject perspective source pose matrices");
        invalid=document;for(unsigned row=0;row<3;++row)invalid["sourcePose"]["boneGlobals"][0]["matrix"][row][0]=0;
        rejected(invalid,"Reject singular source pose matrices");
        invalid=document;invalid["sourcePose"]["boneGlobals"][0]["matrix"][0][1]=
            invalid["sourcePose"]["boneGlobals"][0]["matrix"][0][1].asDouble()+.5;
        rejected(invalid,"Reject source pose shear that bone TRS cannot reproduce");
        invalid["sourcePose"]["boneGlobals"][0]["matrix"][0][3]=1e12;
        rejected(invalid,"Large world translations never relax the linear source-pose shear tolerance");
        invalid=document;invalid["sourcePose"]["boneGlobals"].append(invalid["sourcePose"]["boneGlobals"][0]);
        rejected(invalid,"Reject duplicate captured source bone names");
        invalid=document;invalid["sourcePose"]["boneGlobals"][0]["name"]="Missing1";
        invalid["sourcePose"]["boneGlobals"][1]["name"]="Missing2";
        rejected(invalid,"Reject a captured pose whose bones cannot map to the imported skeleton");
        invalid=document;invalid["sourcePose"]["morphDefaults"][0]["value"]="wrong";
        rejected(invalid,"Reject malformed morph defaults atomically with no partial source pose sequence");
        invalid=document;invalid["sourcePose"]["framesPerSecond"]=0;
        rejected(invalid,"Reject nonpositive source pose frame rates");
        Write(path,document);auto cyclic=before;cyclic.skeleton.bones[1].parent=0;bool rejectedCycle=false;
        try{detail::ImportBlendSourcePose(cyclic,path);}catch(const std::exception&){rejectedCycle=true;}
        expect(rejectedCycle&&cyclic.animations.empty(),"Reject cyclic source pose hierarchy before publishing animation tracks");
        auto staticDocument=document;staticDocument["sourcePose"]["boneGlobals"]=Json::Value(Json::arrayValue);
        staticDocument["sourcePose"]["morphDefaults"]=Json::Value(Json::arrayValue);Write(path,staticDocument);
        auto staticAsset=before;detail::ImportBlendSourcePose(staticAsset,path);
        expect(staticAsset.animations.empty(),"Static Blender models without bones or morph defaults need no generated source pose");
    }catch(const std::exception& error){std::printf("FAIL %s\n",error.what());++failures;}
    return failures?1:0;
}
