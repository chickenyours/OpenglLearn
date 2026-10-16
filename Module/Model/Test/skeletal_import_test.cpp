#include "Model/Public/skeletal_asset.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <stdexcept>

#ifndef MODEL_SOURCE_DIR
#define MODEL_SOURCE_DIR "."
#endif

namespace {
namespace fs = std::filesystem;
int failures = 0;
bool Expect(bool value,const char* message) {
    std::cout << (value ? "  OK   " : "  FAIL ") << message << '\n';
    failures += !value;
    return value;
}
bool Near(double a,double b,double epsilon = 1e-5) { return std::abs(a-b) <= epsilon; }
fs::path Path(std::u8string_view text) { return fs::path(text); }

struct Bytes {
    std::vector<std::uint8_t> data;
    template<class T> void Add(T value) {
        const auto start = data.size(); data.resize(start+sizeof(T));
        std::memcpy(data.data()+start,&value,sizeof(T));
    }
    void Float(float value) { Add(value); }
    void Vector(float x,float y,float z) { Float(x); Float(y); Float(z); }
    void Text(const std::string& value) {
        Add<std::int32_t>(static_cast<std::int32_t>(value.size()));
        data.insert(data.end(),value.begin(),value.end());
    }
    void Write(const fs::path& file) const {
        std::ofstream stream(file,std::ios::binary);
        stream.write(reinterpret_cast<const char*>(data.data()),static_cast<std::streamsize>(data.size()));
        if(!stream) throw std::runtime_error("Cannot write import fixture");
    }
};

void WritePmx(const fs::path& file,bool cyclic = false) {
    Bytes bytes;
    bytes.Add<std::uint32_t>(0x20584d50); bytes.Float(2);
    for(const auto value : {8,1,0,1,1,1,1,1,1}) bytes.Add<std::uint8_t>(value);
    bytes.Text("Synthetic"); bytes.Text("Synthetic"); bytes.Text(""); bytes.Text("");
    bytes.Add<std::int32_t>(3);
    for(int vertex = 0; vertex < 3; ++vertex) {
        bytes.Vector(vertex == 1 ? 1.f : 0.f,vertex == 2 ? 1.f : 0.f,0);
        bytes.Vector(0,0,-1); bytes.Float(vertex == 1 ? 1.f : 0.f); bytes.Float(vertex == 2 ? 1.f : 0.f);
        const auto mode = vertex == 0 ? 0 : vertex == 1 ? 3 : 4;
        bytes.Add<std::uint8_t>(mode);
        if(mode == 0) bytes.Add<std::int8_t>(0);
        if(mode == 3) {
            bytes.Add<std::int8_t>(0); bytes.Add<std::int8_t>(1); bytes.Float(.25f);
            bytes.Vector(.1f,.2f,.3f); bytes.Vector(.4f,.5f,.6f); bytes.Vector(.7f,.8f,.9f);
        }
        if(mode == 4) {
            for(const auto joint : {0,1,-1,-1}) bytes.Add<std::int8_t>(joint);
            for(const auto weight : {.5f,.5f,0.f,0.f}) bytes.Float(weight);
        }
        bytes.Float(1);
    }
    bytes.Add<std::int32_t>(3);
    for(const auto index : {0,1,2}) bytes.Add<std::uint8_t>(index);
    bytes.Add<std::int32_t>(0); // Texture table.
    bytes.Add<std::int32_t>(1); // Material.
    bytes.Text("White"); bytes.Text("White");
    for(int i = 0; i < 4; ++i) bytes.Float(1); // diffuse.
    bytes.Vector(0,0,0); bytes.Float(0); bytes.Vector(1,1,1);
    bytes.Add<std::uint8_t>(1); // double sided.
    for(int i = 0; i < 4; ++i) bytes.Float(0);
    bytes.Float(1);
    bytes.Add<std::int8_t>(-1); bytes.Add<std::int8_t>(-1);
    bytes.Add<std::uint8_t>(0); bytes.Add<std::uint8_t>(1); bytes.Add<std::uint8_t>(0);
    bytes.Text(""); bytes.Add<std::int32_t>(3);
    bytes.Add<std::int32_t>(2); // Bones.
    for(int bone = 0; bone < 2; ++bone) {
        bytes.Text(bone == 0 ? "Root" : "Tip"); bytes.Text(bone == 0 ? "Root" : "Tip");
        bytes.Vector(0,static_cast<float>(bone),0); bytes.Add<std::int8_t>(cyclic && bone == 0 ? 1 : bone-1);
        bytes.Add<std::int32_t>(0); bytes.Add<std::uint16_t>(0); bytes.Vector(0,1,0);
    }
    bytes.Add<std::int32_t>(1); // Morph.
    bytes.Text("Smile"); bytes.Text("Smile"); bytes.Add<std::uint8_t>(3); bytes.Add<std::uint8_t>(1);
    bytes.Add<std::int32_t>(1); bytes.Add<std::uint8_t>(2); bytes.Vector(.1f,.2f,.3f);
    for(int i = 0; i < 3; ++i) bytes.Add<std::int32_t>(0); // Display, bodies, joints.
    bytes.Write(file);
}

void TestPmx(const fs::path& directory) {
    const auto source = directory/"synthetic.pmx";
    WritePmx(source);
    std::string error;
    const auto asset = Model::ImportSkeletalSource(source,{},&error);
    if(!asset) std::cout << "    " << error << '\n';
    if(!Expect(asset != nullptr,"native PMX imports BDEF1, SDEF and QDEF")) return;
    Expect(asset->skeleton.bones.size() == 2 && asset->morphs.size() == 1,"source bone and morph counts are retained");
    const auto& skin = asset->skins.at(0);
    bool foundSdef = false,foundQdef = false,foundBdef = false;
    for(std::size_t i = 0; i < skin.vertices.size(); ++i) {
        const auto& vertex = skin.vertices[i];
        foundBdef |= vertex.mode == Model::SkinningMode::Bdef1;
        foundQdef |= vertex.mode == Model::SkinningMode::Qdef && Near(vertex.weights[0],.5);
        if(vertex.mode == Model::SkinningMode::Sdef)
            foundSdef = Near(vertex.weights[0],.25) && Near(vertex.sdefC.z,-.3) && Near(vertex.sdefR0.z,-.6) && Near(vertex.sdefR1.z,-.9);
    }
    Expect(foundSdef && foundQdef && foundBdef,"source deformation modes, weights and SDEF vectors survive conversion");
    Expect(asset->morphs[0].deltas.size() == 1 && Near(asset->morphs[0].deltas[0].position.z,-.3),"morph offsets use the same reflected Z coordinates as geometry");
    WritePmx(source,true);
    Expect(!Model::ImportSkeletalSource(source,{},&error) && error.find("Cycle") != std::string::npos,"cyclic native bone hierarchy is rejected before third-party import");
    WritePmx(source);
    auto bytes = fs::file_size(source);
    fs::resize_file(source,bytes-2);
    error.clear();
    Expect(!Model::ImportSkeletalSource(source,{},&error) && error.find("PMX") != std::string::npos,"truncated PMX fails with a bounded native-reader diagnostic");
}

void WriteGltf(const fs::path& directory) {
    Bytes bytes;
    std::vector<std::pair<std::size_t,std::size_t>> views;
    auto append = [&](const auto& values) {
        while(bytes.data.size()%4) bytes.Add<std::uint8_t>(0);
        const auto start = bytes.data.size();
        for(auto value : values) bytes.Add(value);
        views.emplace_back(start,bytes.data.size()-start);
    };
    // Exercise accessor byteStride as well as FLOAT -> ai_real conversion.
    append(std::array<float,12>{0,0,0,123,1,0,0,456,0,1,0,789});
    append(std::array<float,9>{0,0,1,0,0,1,0,0,1});
    append(std::array<std::uint16_t,3>{0,1,2});
    append(std::array<std::uint8_t,12>{0,0,0,0,1,0,0,0,1,0,0,0});
    append(std::array<float,12>{1,0,0,0,1,0,0,0,1,0,0,0});
    append(std::array<float,32>{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1,
                              1,0,0,0,0,1,0,0,0,0,1,0,0,-1,0,1});
    append(std::array<float,2>{0,1});
    append(std::array<float,6>{0,1,0,0,2,0});
    append(std::array<float,9>{0,0,0,0,0,0,.1f,0,0});
    append(std::array<float,2>{0,1});
    append(std::array<float,8>{0,0,0,1,0,0,.7071067811865475f,.7071067811865475f});
    append(std::array<float,6>{1,1,1,2,1,1});
    append(std::array<float,6>{0,0,1,0,0,1});
    append(std::array<float,12>{1,0,0,1,1,0,0,1,1,0,0,1});
    append(std::array<std::uint8_t,1>{2});
    append(std::array<float,3>{.1f,0,0});
    bytes.Write(directory/"skin.bin");
    std::ostringstream json;
    json << R"({"asset":{"version":"2.0"},"buffers":[{"uri":"skin.bin","byteLength":)" << bytes.data.size() << "}],\"bufferViews\":[";
    for(std::size_t i = 0; i < views.size(); ++i) {
        if(i) json << ',';
        json << "{\"buffer\":0,\"byteOffset\":" << views[i].first << ",\"byteLength\":" << views[i].second;
        if(i == 0) json << ",\"byteStride\":16";
        json << '}';
    }
    json << R"(],"accessors":[
{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[0,0,0],"max":[1,1,0]},
{"bufferView":1,"componentType":5126,"count":3,"type":"VEC3"},
{"bufferView":2,"componentType":5123,"count":3,"type":"SCALAR"},
{"bufferView":3,"componentType":5121,"count":3,"type":"VEC4"},
{"bufferView":4,"componentType":5126,"count":3,"type":"VEC4"},
{"bufferView":5,"componentType":5126,"count":2,"type":"MAT4"},
{"bufferView":6,"componentType":5126,"count":2,"type":"SCALAR","min":[0],"max":[1]},
{"bufferView":7,"componentType":5126,"count":2,"type":"VEC3"},
{"componentType":5126,"count":3,"type":"VEC3","sparse":{"count":1,"indices":{"bufferView":14,"componentType":5121},"values":{"bufferView":15}}},
{"bufferView":9,"componentType":5126,"count":2,"type":"SCALAR"},
{"bufferView":10,"componentType":5126,"count":2,"type":"VEC4"},
{"bufferView":11,"componentType":5126,"count":2,"type":"VEC3"},
{"bufferView":12,"componentType":5126,"count":3,"type":"VEC2"},
{"bufferView":13,"componentType":5126,"count":3,"type":"VEC4"}],
"nodes":[{"name":"SceneRoot","children":[1,3]},
{"name":"RootBone","children":[2]},
{"name":"TipBone","translation":[0,1,0]},
{"name":"TriangleNode","mesh":0,"skin":0,"translation":[7,11,13],"scale":[2,3,4]}],
"meshes":[{"name":"Triangle","extras":{"targetNames":["Smile"]},"weights":[0],"primitives":[{
"attributes":{"POSITION":0,"NORMAL":1,"JOINTS_0":3,"WEIGHTS_0":4,"TEXCOORD_0":12,"TANGENT":13},"indices":2,"targets":[{"POSITION":8}]}]}],
"skins":[{"joints":[1,2],"skeleton":1,"inverseBindMatrices":5}],
"animations":[{"name":"LiftAndSmile","samplers":[{"input":6,"output":7,"interpolation":"LINEAR"},{"input":6,"output":9,"interpolation":"LINEAR"}],
"channels":[{"sampler":0,"target":{"node":2,"path":"translation"}},{"sampler":1,"target":{"node":3,"path":"weights"}}]},
{"name":"TransformPrecision","samplers":[{"input":6,"output":7,"interpolation":"LINEAR"},{"input":6,"output":10,"interpolation":"LINEAR"},{"input":6,"output":11,"interpolation":"LINEAR"}],
"channels":[{"sampler":0,"target":{"node":2,"path":"translation"}},{"sampler":1,"target":{"node":2,"path":"rotation"}},{"sampler":2,"target":{"node":2,"path":"scale"}}]}],
"scenes":[{"nodes":[0]}],"scene":0})";
    std::ofstream stream(directory/"skin.gltf",std::ios::binary);
    stream << json.str();
}

void TestGltf(const fs::path& directory) {
    WriteGltf(directory);
    std::string error;
    auto asset = Model::ImportSkeletalSource(directory/"skin.gltf",{},&error);
    if(!asset) std::cout << "    " << error << '\n';
    if(!Expect(asset != nullptr,"glTF imports joint weights, inverse-bind matrices and embedded animation")) return;
    bool canonicalSkin=asset->geometry->instances.size()==1&&asset->skins.size()==1;
    if(canonicalSkin)for(int column=0;column<4;++column)for(int row=0;row<4;++row)
        canonicalSkin&=Near(asset->geometry->instances[0].transform[column][row],column==row?1.:0.)&&
            Near(asset->skins[0].bindTransform[column][row],column==row?1.:0.);
    const auto meshNode=std::find_if(asset->skeleton.bones.begin(),asset->skeleton.bones.end(),[](const auto& bone){return bone.name=="TriangleNode";});
    Expect(canonicalSkin&&meshNode!=asset->skeleton.bones.end()&&glm::length(meshNode->restLocal.translation-glm::dvec3(7,11,13))<1e-5,
        "glTF skinned mesh ignores its own translated/scaled node, while the original node hierarchy survives");
    const std::array<glm::dvec3,3> sourcePositions{{{0,0,0},{1,0,0},{0,1,0}}};
    const bool triangle = asset->geometry->meshes.size() == 1 && asset->geometry->meshes[0].vertices.size() == 3;
    bool positions = triangle, shading = triangle;
    if(triangle) for(std::size_t i = 0; i < sourcePositions.size(); ++i) {
        const auto& vertex = asset->geometry->meshes[0].vertices[i];
        positions &= glm::length(vertex.position-sourcePositions[i]) < 1e-5;
        shading &= glm::length(vertex.normal-glm::vec3(0,0,1)) < 1e-5f;
        shading &= glm::length(glm::vec3(vertex.tangent)-glm::vec3(1,0,0)) < 1e-5f;
        shading &= glm::length(vertex.uv-glm::vec2(sourcePositions[i])) < 1e-5f;
    }
    Expect(positions,"strided glTF FLOAT positions retain their numerical values in either Assimp precision mode");
    Expect(shading,"glTF FLOAT normals, tangents and two-component UVs retain their values");
    const auto bone = std::find_if(asset->skeleton.bones.begin(),asset->skeleton.bones.end(),[](const auto& value){return value.name=="TipBone";});
    Expect(bone != asset->skeleton.bones.end() && Near(bone->restLocal.translation.y,1),"generic skeleton retains the source local bind transform");
    const auto sequence = std::find_if(asset->animations.begin(),asset->animations.end(),[](const auto& value){return !value.generated;});
    if(!Expect(sequence != asset->animations.end(),"embedded sequence is distinguished from generated preview clips")) return;
    Expect(Near(sequence->duration,1) && !sequence->boneTracks.empty(),"source animation tick times are converted to seconds");
    Expect(asset->morphs.size() == 1 && !sequence->morphTracks.empty(),"glTF blend-shape target and weight animation channel are retained");
    Expect(asset->morphs.size() == 1 && asset->morphs[0].deltas.size() == 1 &&
        asset->morphs[0].deltas[0].vertex == 2 && glm::length(asset->morphs[0].deltas[0].position-glm::dvec3(.1,0,0)) < 1e-5,
        "sparse glTF FLOAT morph accessors retain their vertex index and numerical displacement");
    auto pose = Model::EvaluateSkeletalPose(*asset,&*sequence,1,false);
    Model::StaticModel skinned;
    Expect(Model::SkinSkeletalMesh(*asset,pose,skinned,&error),"imported bone and blend-shape animation evaluates through the shared runtime");
    bool lifted = false;
    if(!skinned.meshes.empty()) for(const auto& vertex : skinned.meshes[0].vertices) lifted |= vertex.position.y > 1.9;
    Expect(lifted,"animated bind offsets produce the expected one-unit joint translation");
    const std::array<glm::dvec3,3> liftedPositions{{{0,0,0},{1,1,0},{.1,2,0}}};
    bool exactLift = triangle && !skinned.meshes.empty() && skinned.meshes[0].vertices.size() == 3;
    if(exactLift) for(std::size_t i = 0; i < liftedPositions.size(); ++i)
        exactLift &= glm::length(skinned.meshes[0].vertices[i].position-liftedPositions[i]) < 1e-5;
    Expect(exactLift,"glTF bind offsets, joint weights and sparse morph combine into the exact expected final vertices");
    const auto transformSequence = std::find_if(asset->animations.begin(),asset->animations.end(),[](const auto& value){return value.name=="TransformPrecision";});
    bool transformed = transformSequence != asset->animations.end() && bone != asset->skeleton.bones.end();
    if(transformed) {
        const auto transformedPose = Model::EvaluateSkeletalPose(*asset,&*transformSequence,1,false);
        const auto& local = transformedPose.local[static_cast<std::size_t>(bone-asset->skeleton.bones.begin())];
        transformed = glm::length(local.translation-glm::dvec3(0,2,0)) < 1e-5 &&
            glm::length(local.scale-glm::dvec3(2,1,1)) < 1e-5 &&
            glm::length(local.rotation*glm::dvec3(1,0,0)-glm::dvec3(0,1,0)) < 1e-5;
        Model::StaticModel rotated;
        transformed &= Model::SkinSkeletalMesh(*asset,transformedPose,rotated,&error);
        const std::array<glm::dvec3,3> transformedPositions{{{0,0,0},{1,4,0},{0,2,0}}};
        transformed &= rotated.meshes.size() == 1 && rotated.meshes[0].vertices.size() == 3;
        if(transformed) for(std::size_t i = 0; i < transformedPositions.size(); ++i)
            transformed &= glm::length(rotated.meshes[0].vertices[i].position-transformedPositions[i]) < 1e-5;
    }
    Expect(transformed,"glTF FLOAT quaternion and scale keys preserve their component order and exact skinned result");
    if(!asset->warnings.empty()) for(const auto& warning : asset->warnings) std::cout << "    note: " << warning << '\n';
}

void TestRealPmx() {
    std::string error;
    const auto rikka = Model::ImportSkeletalSource(fs::path(MODEL_SOURCE_DIR)/"Asset/Model/Rikka_Uniform/Rikka_Uniform.pmx",{},&error);
    if(!rikka) std::cout << "    " << error << '\n';
    if(Expect(rikka != nullptr,"bundled Rikka PMX imports without a GPU context")) {
        Expect(!rikka->skeleton.bones.empty() && rikka->skins.size() == rikka->geometry->meshes.size(),"bundled source has aligned geometry and skin buffers");
        const auto pose = Model::EvaluateSkeletalPose(*rikka,nullptr,0,false);
        Model::StaticModel output;
        const bool success = Model::SkinSkeletalMesh(*rikka,pose,output,&error);
        double drift = 0;
        if(success) for(std::size_t mesh = 0; mesh < output.meshes.size(); ++mesh)
            for(std::size_t vertex = 0; vertex < output.meshes[mesh].vertices.size(); ++vertex)
                drift = std::max(drift,glm::length(output.meshes[mesh].vertices[vertex].position-rikka->geometry->meshes[mesh].vertices[vertex].position));
        Expect(success && drift < 1e-5,"Rikka inverse-bind/rest-pose skinning reproduces the source mesh");
    }
    const auto mahiro = Path(u8"C:/Users/16620/Downloads/真寻酱（女仆装）_by_Sa（艾拉漫剪）_07df9bce2f9eeb31bc903e14f16e8582/真寻酱（女仆装）/真寻酱（女仆装）.pmx");
    if(!fs::exists(mahiro)) { std::cout << "  SKIP optional local Mahiro source is absent\n"; return; }
    const auto asset = Model::ImportSkeletalSource(mahiro,{},&error);
    if(!asset) std::cout << "    " << error << '\n';
    if(!Expect(asset != nullptr,"requested Unicode-path Mahiro PMX imports")) return;
    Expect(asset->skeleton.bones.size()==186 && asset->morphs.size()==57,"native PMX retains all 186 bones and 57 facial morph targets");
    const auto nativeIk = std::count_if(asset->ikRig.chains.begin(),asset->ikRig.chains.end(),[](const auto& chain){return chain.native;});
    Expect(nativeIk==4 && asset->sourceRigidBodies==184 && asset->sourceJoints==172,"native IK, rigid-body and joint source counts match the previous scan");
    std::set<std::uint32_t> sourceIds;
    std::uint64_t triangles = 0;
    for(std::size_t i = 0; i < asset->skins.size(); ++i) {
        sourceIds.insert(asset->skins[i].sourceVertexIds.begin(),asset->skins[i].sourceVertexIds.end());
        triangles += asset->geometry->meshes[i].indices.size()/3;
    }
    Expect(sourceIds.size()==26846 && triangles==38632,"source vertex IDs and every source triangle survive material mesh partitioning");
    Expect(std::all_of(asset->animations.begin(),asset->animations.end(),[](const auto& sequence){return sequence.generated;}),"PMX diagnostic clips are labeled generated rather than source animation");
}

} // namespace

int main() {
    const auto directory = fs::temp_directory_path()/ ("skeletal_import_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        fs::create_directories(directory);
        TestPmx(directory); TestGltf(directory); TestRealPmx();
    } catch(const std::exception& exception) {
        std::cerr << "Exception: " << exception.what() << '\n'; ++failures;
    }
    // Retain test fixtures on failure for diagnosis. No recursive cleanup of
    // computed paths is needed; this test writes a small fixed set of files.
    if(!failures) for(const auto& name : {"synthetic.pmx","skin.bin","skin.gltf"}) fs::remove(directory/name);
    if(!failures) fs::remove(directory);
    std::cout << failures << " import failures\n";
    return failures ? 1 : 0;
}
