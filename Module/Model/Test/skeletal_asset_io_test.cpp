#include "Model/Public/skeletal_asset.h"
#include <json/json.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace {
namespace fs = std::filesystem;
int failures = 0;
bool Expect(bool condition, const char* message) {
    std::printf("%-78s %s\n", message, condition ? "ok" : "FAIL"); failures += !condition; return condition;
}
Json::Value Read(const fs::path& path) {
    std::ifstream stream(path); Json::Value value; stream >> value; return value;
}
void Write(const fs::path& path, const Json::Value& value) {
    std::ofstream stream(path); Json::StreamWriterBuilder builder; stream << Json::writeString(builder, value);
}
Model::SkeletalAssetPackage Fixture() {
    using namespace Model;
    SkeletalAssetPackage asset; asset.id = "fixture-package"; asset.name = "PortableCharacter";
    asset.sourceFormat = "test"; asset.sourcePath = "source_that_does_not_exist/model.pmx";
    asset.sourceRigidBodies = 184; asset.sourceJoints = 172; asset.warnings = {"fixture warning"};
    asset.skeleton.id = "fixture-skeleton"; asset.skeleton.name = "Skeleton";
    SkeletonBone child; child.name = "Child"; child.englishName = "child"; child.parent = 1;
    child.restLocal.translation = {0, 1, 0}; child.inverseBind = glm::translate(glm::dmat4(1), glm::dvec3(0, -1, 0));
    child.sourceFlags = 0x1234; child.inheritParent = 1; child.inheritWeight = .25f; child.fixedAxis = {0, 0, 1};
    SkeletonBone root; root.name = "Root"; asset.skeleton.bones = {child, root};
    asset.geometry = std::make_shared<StaticModel>(); auto& geometry = *asset.geometry; geometry.sourcePath = asset.sourcePath;
    StaticMesh mesh; mesh.name = "Triangle";
    for (int i = 0; i < 3; ++i) {
        StaticVertex vertex; vertex.position = {1000000000000.125 + i, i == 2 ? 1 : 0, 0};
        vertex.uv = {i == 1 ? 1.f : 0.f, i == 2 ? 1.f : 0.f}; mesh.vertices.push_back(vertex); mesh.bounds.Include(vertex.position);
    }
    mesh.indices = {0, 1, 2}; geometry.meshes.push_back(mesh); geometry.bounds = mesh.bounds;
    geometry.instances.push_back({"MeshNode", 0, glm::dmat4(1)}); geometry.warnings = {"geometry warning"};
    PbrMaterial material; material.name = "Material"; material.textures[0] = 0; material.textures[7] = 1;
    material.baseColor = {.9f, .8f, .7f, .6f}; material.alphaMode = AlphaMode::Mask; material.twoSided = true;
    material.uvTransform = {2, 3, .1f, .2f}; geometry.materials.push_back(material);
    TextureImage texture; texture.name = "Albedo"; texture.width = 2; texture.height = 1;
    texture.hasAlpha = true; texture.rgba = {255, 10, 20, 255, 30, 40, 50, 128}; geometry.textures.push_back(texture);
    TextureImage hdr; hdr.name = "LinearEmission"; hdr.width = hdr.height = 1; hdr.linearColor = true;
    hdr.rgba32f = {5.25f, .1f, .2f, 1}; geometry.textures.push_back(hdr);
    MeshSkin skin; skin.sourceVertexIds = {8, 3, 12}; skin.boneFromMesh = {child.inverseBind, glm::dmat4(1)};
    for (int i = 0; i < 3; ++i) {
        VertexSkin v; v.joints = {0, 1, 0, 0}; v.weights = {.75f, .25f, 0, 0}; v.mode = SkinningMode::Sdef;
        v.sdefC = {1, 2, 3}; v.sdefR0 = {3, 2, 1}; v.sdefR1 = {.1f, .2f, .3f}; skin.vertices.push_back(v);
    }
    asset.skins.push_back(skin);
    MeshRegionInfo region; region.mesh = 0; region.kind = MeshRegionKind::Garment; region.confidence = .875f;
    region.authoredCloth = true; region.requiresReview = false; region.userOverride = true;
    region.sourceObject = "DressObject"; region.sourceMesh = "DressData"; region.sourceMaterial = "Silk";
    region.collections = {"Character", "衣装"}; region.vertexGroups = {"hips", "SkirtPins"};
    region.modifiers = {"ARMATURE", "CLOTH"}; region.evidence = {"Source Cloth modifier", "User confirmed garment"};
    region.clothPinGroup = "SkirtPins"; asset.meshRegions.push_back(region);
    MorphTarget morph; morph.name = "Smile"; morph.englishName = "smile"; morph.panel = 3;
    morph.deltas.push_back({0, 1, {.125, .25, .5}, {0, .1f, 0}}); asset.morphs.push_back(morph);
    MorphTarget empty; empty.name = "Natural"; asset.morphs.push_back(empty);
    AnimationSequenceAsset animation; animation.id = "fixture-animation"; animation.name = "GeneratedPreview";
    animation.skeletonId = asset.skeleton.id; animation.source = "procedural:test"; animation.generated = true;
    animation.duration = 1; animation.framesPerSecond = 30;
    BoneAnimationTrack track; track.bone = 0; track.translations = {{0, {0, 1, 0}}, {1, {0, 2, 0}}};
    track.rotations = {{0, glm::dquat(1, 0, 0, 0)}, {1, glm::angleAxis(.2, glm::dvec3(0, 0, 1))}};
    track.scales = {{0, {1, 1, 1}}}; animation.boneTracks.push_back(track);
    animation.morphTracks.push_back({0, {{0, 0}, {1, 1}}}); asset.animations.push_back(animation);
    asset.ikRig.id = "fixture-ikrig"; asset.ikRig.name = "IKRig"; asset.ikRig.skeletonId = asset.skeleton.id;
    asset.ikRig.humanoid.push_back({"hips", 1, .9f, "test"}); asset.ikRig.diagnostics = {"fixture diagnostic"};
    IkChain chain; chain.name = "Arm"; chain.role = "leftArm"; chain.root = 1; chain.effector = 0;
    chain.target = {.5, 1, 0}; chain.native = true; chain.iterations = 12; chain.links.push_back({1, true, {-1, -1, -1}, {1, 1, 1}});
    asset.ikRig.chains.push_back(chain); return asset;
}
Model::SkeletalAssetPackage FullInfluenceFixture() {
    auto asset = Fixture();
    for (std::uint32_t joint = 2; joint < 6; ++joint) {
        Model::SkeletonBone bone; bone.name = "Extra" + std::to_string(joint); bone.parent = 1;
        asset.skeleton.bones.push_back(bone);
    }
    auto& skin = asset.skins[0]; skin.boneFromMesh.resize(asset.skeleton.bones.size(), glm::dmat4(1));
    for (auto& vertex : skin.vertices) vertex.mode = Model::SkinningMode::Bdef4;
    skin.influenceOffsets = {0, 6, 11, 16};
    skin.influences = {{0, .1f}, {1, .15f}, {2, .2f}, {3, .05f}, {4, .25f}, {5, .25f}};
    for (int vertex = 1; vertex < 3; ++vertex)
        for (std::uint32_t joint = 0; joint < 5; ++joint) skin.influences.push_back({joint, .2f});
    return asset;
}
}

int main() {
    const auto directory = fs::path(MODEL_SOURCE_DIR) / "build/skeletal_io_fixtures" /
        ("run-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        auto asset = Fixture(); std::string error;
        Expect(Model::ValidateSkeletalAsset(asset, &error), "Fixture accepts non-topological hierarchy and empty default morph");
        Expect(Model::SaveSkeletalAssetPackage(asset, directory, &error), "Save complete candidate and atomically publish package");
        if (!error.empty()) std::printf("  %s\n", error.c_str());
        {
            // Recorded before the bounded-memory IO change, using the v1
            // in-memory writer. Keep existing payload bytes and offsets stable.
            const auto legacyBytes = Read(directory / "SkeletalMeshes/model.skmesh.json");
            Expect(legacyBytes["payloadBytes"].asUInt64() == 487 &&
                legacyBytes["payloadHashFNV1a64"].asString() == "8b855054feb50f51" &&
                legacyBytes["meshes"][0]["vertices"]["offset"].asUInt64() == 8 &&
                legacyBytes["meshes"][0]["skin"]["offset"].asUInt64() == 200,
                "Streamed writer preserves the historical v1 payload checksum, byte count and ranges");
        }
        auto loaded = Model::LoadSkeletalAssetPackage(directory, &error);
        if (!Expect(bool(loaded), "Load package without the source model or external textures")) throw std::runtime_error(error);
        Expect(loaded->id == asset.id && loaded->skeleton.id == asset.skeleton.id && loaded->ikRig.id == asset.ikRig.id, "Stable asset IDs roundtrip");
        Expect(loaded->geometry->meshes[0].vertices[0].position.x == 1000000000000.125, "Double geometry positions retain precision beyond float");
        Expect(loaded->geometry->meshes[0].indices == asset.geometry->meshes[0].indices && loaded->skins[0].sourceVertexIds == asset.skins[0].sourceVertexIds, "Triangle and source vertex indices roundtrip");
        Expect(loaded->skins[0].vertices[0].mode == Model::SkinningMode::Sdef && loaded->skins[0].vertices[0].sdefC == glm::vec3(1, 2, 3)
            && loaded->skins[0].boneFromMesh.size() == 2, "Skinning mode, SDEF data and per-mesh offsets roundtrip");
        Expect(loaded->geometry->textures[0].rgba == asset.geometry->textures[0].rgba && loaded->geometry->textures[1].rgba32f == asset.geometry->textures[1].rgba32f, "RGBA8 and HDR float decoded textures roundtrip");
        Expect(loaded->skeleton.bones[0].sourceFlags == 0x1234 && loaded->skeleton.bones[0].inheritParent == 1
            && loaded->skeleton.bones[0].inheritWeight == .25f, "Source bone flags, axes and inheritance metadata roundtrip");
        Expect(loaded->morphs.size() == 2 && loaded->morphs[1].deltas.empty() && loaded->morphs[0].deltas[0].position.z == .5, "Sparse morph deltas and zero-delta default expression roundtrip");
        Expect(loaded->animations[0].generated && loaded->animations[0].source == "procedural:test" && loaded->animations[0].boneTracks[0].rotations.size() == 2
            && loaded->animations[0].morphTracks[0].keys[1].value == 1, "Animation channels and explicit generated provenance roundtrip");
        Expect(loaded->ikRig.chains[0].native && loaded->ikRig.chains[0].links[0].limited && loaded->ikRig.humanoid[0].confidence == .9f
            && loaded->sourceRigidBodies == 184 && loaded->sourceJoints == 172, "IK settings, mapping confidence and physics counts roundtrip");
        Expect(loaded->meshRegions.size() == 1 && loaded->meshRegions[0].mesh == 0
            && loaded->meshRegions[0].kind == Model::MeshRegionKind::Garment && loaded->meshRegions[0].confidence == .875f
            && loaded->meshRegions[0].authoredCloth && !loaded->meshRegions[0].requiresReview && loaded->meshRegions[0].userOverride,
            "Garment classification, confidence, Cloth evidence and manual override roundtrip");
        if (!loaded->meshRegions.empty()) {
            const auto& actual = loaded->meshRegions[0]; const auto& original = asset.meshRegions[0];
            Expect(actual.sourceObject == original.sourceObject && actual.sourceMesh == original.sourceMesh
                && actual.sourceMaterial == original.sourceMaterial && actual.clothPinGroup == original.clothPinGroup
                && actual.collections == original.collections && actual.vertexGroups == original.vertexGroups
                && actual.modifiers == original.modifiers && actual.evidence == original.evidence,
                "Mesh source provenance, UTF-8 collections and cloth pin group roundtrip");
        }
        {
            auto full = FullInfluenceFixture(); const auto fullDirectory = directory / "FullInfluences";
            Expect(Model::ValidateSkeletalAsset(full, &error), "Accept normalized skinning with up to six source influences per vertex");
            const bool saved = Model::SaveSkeletalAssetPackage(full, fullDirectory, &error);
            Expect(saved, "Save optional full skin influence data without reducing to four weights");
            if (!saved) std::printf("  %s\n", error.c_str());
            auto fullLoaded = saved ? Model::LoadSkeletalAssetPackage(fullDirectory, &error) : nullptr;
            bool preserved = fullLoaded && fullLoaded->skins[0].influenceOffsets == full.skins[0].influenceOffsets
                && fullLoaded->skins[0].influences.size() == full.skins[0].influences.size();
            if (preserved) {
                for (std::size_t i = 0; i < full.skins[0].influences.size(); ++i) {
                    const auto& actual = fullLoaded->skins[0].influences[i]; const auto& expected = full.skins[0].influences[i];
                    preserved = preserved && actual.joint == expected.joint && actual.weight == expected.weight;
                }
            }
            Expect(preserved, "Every full influence joint and weight survives package roundtrip exactly");
            Expect(fullLoaded && fullLoaded->skins[0].vertices[0].joints == full.skins[0].vertices[0].joints
                && fullLoaded->skins[0].vertices[0].weights == full.skins[0].vertices[0].weights,
                "Full influence data preserves the legacy four-weight summary alongside it");
            Expect(loaded->skins[0].influenceOffsets.empty() && loaded->skins[0].influences.empty(),
                "Legacy packages without optional full influences continue to load");
            if (saved) {
                const auto fullMeshPath = fullDirectory / "SkeletalMeshes/model.skmesh.json"; const auto fullMesh = Read(fullMeshPath);
                Expect(fullMesh["payloadBytes"].asUInt64() == 631 &&
                    fullMesh["payloadHashFNV1a64"].asString() == "a351aca4848b1d01",
                    "Optional full-influence payload remains byte-compatible with the historical v1 writer");
                auto invalidFull = fullMesh; invalidFull["meshes"][0].removeMember("fullInfluences"); Write(fullMeshPath, invalidFull);
                Expect(!Model::LoadSkeletalAssetPackage(fullDirectory, &error), "Reject unpaired optional full influence ranges");
                invalidFull = fullMesh; invalidFull["meshes"][0]["fullInfluenceOffsets"]["count"] = 3; Write(fullMeshPath, invalidFull);
                Expect(!Model::LoadSkeletalAssetPackage(fullDirectory, &error), "Reject full offsets range with wrong vertex count");
                invalidFull = fullMesh; invalidFull["meshes"][0]["fullInfluences"]["stride"] = 4; Write(fullMeshPath, invalidFull);
                Expect(!Model::LoadSkeletalAssetPackage(fullDirectory, &error), "Reject full influence range with incorrect scalar layout");
                invalidFull = fullMesh; invalidFull["meshes"][0]["fullInfluences"]["offset"] = Json::UInt64(std::numeric_limits<std::uint64_t>::max()); Write(fullMeshPath, invalidFull);
                Expect(!Model::LoadSkeletalAssetPackage(fullDirectory, &error), "Reject overflowing full influence payload offset");
                invalidFull = fullMesh; invalidFull["meshes"][0]["fullInfluences"]["offset"] = fullMesh["meshes"][0]["skin"]["offset"]; Write(fullMeshPath, invalidFull);
                Expect(!Model::LoadSkeletalAssetPackage(fullDirectory, &error), "Reject full influence payload overlapping fixed skin data");
                invalidFull = fullMesh; invalidFull["meshes"][0]["fullInfluences"]["count"] = Json::UInt64(100000001); Write(fullMeshPath, invalidFull);
                Expect(!Model::LoadSkeletalAssetPackage(fullDirectory, &error), "Reject full influence count above the format element budget");
                Write(fullMeshPath, fullMesh);
                auto withoutFull = FullInfluenceFixture(); withoutFull.skins[0].influenceOffsets.clear(); withoutFull.skins[0].influences.clear();
                const auto withoutFullDirectory = directory / "LegacyFullFixture";
                const bool legacySaved = Model::SaveSkeletalAssetPackage(withoutFull, withoutFullDirectory, &error);
                Expect(legacySaved && !Read(withoutFullDirectory / "SkeletalMeshes/model.skmesh.json")["meshes"][0].isMember("fullInfluences"),
                    "Writer omits optional full skin fields for four-influence legacy data");
                Expect(legacySaved && fullMesh["payloadHashFNV1a64"] != Read(withoutFullDirectory / "SkeletalMeshes/model.skmesh.json")["payloadHashFNV1a64"],
                    "Full skin influences participate in the ordinary payload checksum");
            }
            auto invalid = FullInfluenceFixture(); invalid.skins[0].influences.clear();
            Expect(!Model::ValidateSkeletalAsset(invalid, &error), "Reject full offsets without influence records before saving");
            invalid = FullInfluenceFixture(); invalid.skins[0].influenceOffsets.clear();
            Expect(!Model::ValidateSkeletalAsset(invalid, &error), "Reject full influence records without offsets before saving");
            invalid = FullInfluenceFixture(); invalid.skins[0].influenceOffsets.pop_back();
            Expect(!Model::ValidateSkeletalAsset(invalid, &error), "Reject full offsets with missing vertex terminator");
            invalid = FullInfluenceFixture(); invalid.skins[0].influenceOffsets.front() = 1;
            Expect(!Model::ValidateSkeletalAsset(invalid, &error), "Reject full offsets that skip the first influence");
            invalid = FullInfluenceFixture(); invalid.skins[0].influenceOffsets.back() = 15;
            Expect(!Model::ValidateSkeletalAsset(invalid, &error), "Reject full offsets that leave influence records unreferenced");
            invalid = FullInfluenceFixture(); invalid.skins[0].influenceOffsets[1] = 0;
            Expect(!Model::ValidateSkeletalAsset(invalid, &error), "Reject a vertex with an empty full influence interval");
            invalid = FullInfluenceFixture(); invalid.skins[0].influenceOffsets[2] = 5;
            Expect(!Model::ValidateSkeletalAsset(invalid, &error), "Reject decreasing full influence offsets");
            invalid = FullInfluenceFixture(); invalid.skins[0].influenceOffsets[1] = 17;
            Expect(!Model::ValidateSkeletalAsset(invalid, &error), "Reject full influence offset beyond the record array");
            invalid = FullInfluenceFixture(); invalid.skins[0].influences[0].joint = 6;
            Expect(!Model::ValidateSkeletalAsset(invalid, &error), "Reject full influence joints outside the skeleton");
            invalid = FullInfluenceFixture(); invalid.skins[0].influences[0].joint = 1;
            Expect(!Model::ValidateSkeletalAsset(invalid, &error), "Reject duplicate full influence joints on a vertex");
            invalid = FullInfluenceFixture(); invalid.skins[0].influences[0].weight = 0;
            Expect(!Model::ValidateSkeletalAsset(invalid, &error), "Reject zero full influence weights");
            invalid = FullInfluenceFixture(); invalid.skins[0].influences[0].weight = -.1f;
            Expect(!Model::ValidateSkeletalAsset(invalid, &error), "Reject negative full influence weights");
            invalid = FullInfluenceFixture(); invalid.skins[0].influences[0].weight = std::numeric_limits<float>::quiet_NaN();
            Expect(!Model::ValidateSkeletalAsset(invalid, &error), "Reject non-finite full influence weights");
            invalid = FullInfluenceFixture(); invalid.skins[0].influences[0].weight = .5f;
            Expect(!Model::ValidateSkeletalAsset(invalid, &error), "Reject full skin influence weights that do not sum to one");
            Expect(!Model::SaveSkeletalAssetPackage(invalid, directory / "InvalidFullInfluences", &error),
                "Malformed full influence assets are refused before package publication");
        }
        Expect(!Model::SaveSkeletalAssetPackage(asset, directory, &error) && fs::exists(directory / "Package.asset.json"), "Refuse to overwrite an existing nonempty asset package");
        const auto manifestPath = directory / "Package.asset.json"; const auto manifest = Read(manifestPath);
        auto changed = manifest; changed.removeMember("meshRegions"); Write(manifestPath, changed);
        const auto legacy = Model::LoadSkeletalAssetPackage(directory, &error);
        Expect(legacy && legacy->meshRegions.empty(), "Legacy v1 package without mesh regions remains compatible"); Write(manifestPath, manifest);
        changed = manifest; changed["meshRegions"] = Json::Value(Json::arrayValue); Write(manifestPath, changed);
        const auto emptyRegions = Model::LoadSkeletalAssetPackage(directory, &error);
        Expect(emptyRegions && emptyRegions->meshRegions.empty(), "Explicit empty mesh region array remains compatible"); Write(manifestPath, manifest);
        changed = manifest; changed["meshRegions"][0]["mesh"] = 1; Write(manifestPath, changed);
        Expect(!Model::LoadSkeletalAssetPackage(directory, &error), "Reject mesh region references outside geometry"); Write(manifestPath, manifest);
        changed = manifest; changed["meshRegions"].append(manifest["meshRegions"][0]); Write(manifestPath, changed);
        Expect(!Model::LoadSkeletalAssetPackage(directory, &error), "Reject duplicate region records for a mesh"); Write(manifestPath, manifest);
        changed = manifest; changed["meshRegions"][0]["kind"] = "clothMaybe"; Write(manifestPath, changed);
        Expect(!Model::LoadSkeletalAssetPackage(directory, &error), "Reject unknown mesh region kind instead of assuming garment"); Write(manifestPath, manifest);
        changed = manifest; changed["meshRegions"][0]["kind"] = 3; Write(manifestPath, changed);
        Expect(!Model::LoadSkeletalAssetPackage(directory, &error), "Reject numeric mesh region kinds in JSON"); Write(manifestPath, manifest);
        changed = manifest; changed["meshRegions"][0]["confidence"] = 1.00000001; Write(manifestPath, changed);
        Expect(!Model::LoadSkeletalAssetPackage(directory, &error), "Reject confidence above one before narrowing to float"); Write(manifestPath, manifest);
        changed = manifest; changed["meshRegions"][0]["confidence"] = -0.1; Write(manifestPath, changed);
        Expect(!Model::LoadSkeletalAssetPackage(directory, &error), "Reject negative mesh region confidence"); Write(manifestPath, manifest);
        changed = manifest; changed["meshRegions"][0]["userOverride"] = 1; Write(manifestPath, changed);
        Expect(!Model::LoadSkeletalAssetPackage(directory, &error), "Require explicit booleans for mesh region override state"); Write(manifestPath, manifest);
        changed = manifest; changed["meshRegions"][0]["vertexGroups"][0] = 42; Write(manifestPath, changed);
        Expect(!Model::LoadSkeletalAssetPackage(directory, &error), "Reject malformed mesh region provenance lists"); Write(manifestPath, manifest);
        changed = manifest; changed["meshRegions"][0]["sourceObject"] = std::string(16385, 'x'); Write(manifestPath, changed);
        Expect(!Model::LoadSkeletalAssetPackage(directory, &error), "Reject oversized mesh region metadata before retaining it"); Write(manifestPath, manifest);
        auto overrideAsset = Fixture(); overrideAsset.meshRegions[0].kind = Model::MeshRegionKind::Accessory;
        overrideAsset.meshRegions[0].evidence.push_back("Manual reclassification");
        const auto overrideDirectory = directory / "MetadataOverride";
        Expect(Model::SaveSkeletalAssetPackage(overrideAsset, overrideDirectory, &error), "Save manual reclassification as a new package revision");
        const auto overrideLoaded = Model::LoadSkeletalAssetPackage(overrideDirectory, &error);
        Expect(overrideLoaded && !overrideLoaded->meshRegions.empty() && overrideLoaded->meshRegions[0].kind == Model::MeshRegionKind::Accessory
            && overrideLoaded->meshRegions[0].userOverride, "Reload preserves manual classification over import evidence");
        Expect(Read(directory / "SkeletalMeshes/model.skmesh.json") == Read(overrideDirectory / "SkeletalMeshes/model.skmesh.json")
            && Read(directory / "Skeletons/model.skeleton.json") == Read(overrideDirectory / "Skeletons/model.skeleton.json"),
            "Classification changes leave geometry checksum and skeleton identity unchanged");
        {
            // Exercise fixed-size buffers with double positions and 69-byte
            // skin records spanning block boundaries, then both texture paths.
            auto bufferedAsset = Fixture();
            auto& mesh = bufferedAsset.geometry->meshes[0]; auto& skin = bufferedAsset.skins[0];
            mesh.vertices.resize(8193, mesh.vertices.front());
            skin.vertices.resize(mesh.vertices.size(), skin.vertices.front());
            skin.sourceVertexIds.resize(mesh.vertices.size(), 8);
            auto& rgba = bufferedAsset.geometry->textures[0]; rgba.width = rgba.height = 257;
            rgba.rgba.resize(std::size_t(rgba.width) * rgba.height * 4);
            for (std::size_t i = 0; i < rgba.rgba.size(); ++i) rgba.rgba[i] = static_cast<std::uint8_t>((i * 37 + i / 257) % 256);
            auto& hdr = bufferedAsset.geometry->textures[1]; hdr.width = 17; hdr.height = 1025;
            hdr.rgba32f.resize(std::size_t(hdr.width) * hdr.height * 4);
            for (std::size_t i = 0; i < hdr.rgba32f.size(); ++i) hdr.rgba32f[i] = static_cast<float>(i % 4096) * .03125f;
            const auto bufferedDirectory = directory / "BufferBoundaries";
            const bool saved = Model::SaveSkeletalAssetPackage(bufferedAsset, bufferedDirectory, &error);
            Expect(saved, "Save and verify scalar, RGBA8 and float payloads across multiple IO buffer boundaries");
            if (!saved) std::printf("  %s\n", error.c_str());
            if (saved) {
                auto actual = Model::LoadSkeletalAssetPackage(bufferedDirectory, &error);
                bool preserved = actual && actual->geometry->meshes[0].vertices.size() == mesh.vertices.size() &&
                    actual->skins[0].sourceVertexIds == skin.sourceVertexIds &&
                    actual->geometry->textures[0].rgba == rgba.rgba && actual->geometry->textures[1].rgba32f == hdr.rgba32f;
                if (actual) for (std::size_t i = 0; i < mesh.vertices.size(); ++i)
                    preserved = preserved && actual->geometry->meshes[0].vertices[i].position == mesh.vertices[i].position &&
                        actual->skins[0].vertices[i].sdefR1 == skin.vertices[i].sdefR1;
                Expect(preserved, "Every cross-buffer double position, skin scalar and texture channel roundtrips exactly");
                const auto path = bufferedDirectory / "SkeletalMeshes/model.skmesh.json";
                auto json = Read(path); const auto original = json;
                const auto first = json["textures"][0]; json["textures"][0] = json["textures"][1]; json["textures"][1] = first;
                for (auto& material : json["materials"]) for (auto& texture : material["textures"])
                    if (texture.asInt() >= 0) texture = 1 - texture.asInt();
                Write(path, json); actual = Model::LoadSkeletalAssetPackage(bufferedDirectory, &error);
                Expect(actual && actual->geometry->textures[0].rgba32f == hdr.rgba32f &&
                    actual->geometry->textures[1].rgba == rgba.rgba,
                    "Buffered reader supports non-monotonic, non-overlapping payload ranges without a full-file copy");
                Write(path, original);
                const auto binary = bufferedDirectory / "SkeletalMeshes/model.skmesh.bin";
                const auto length = fs::file_size(binary); fs::resize_file(binary, length - 1);
                Expect(!Model::LoadSkeletalAssetPackage(bufferedDirectory, &error),
                    "Streamed reader rejects a truncated multi-buffer payload before decoding");
            }
        }
        {
            // Alice's source uses 6144² atlases. RGBA channel count exceeds
            // the ordinary 100-million mesh-element cap while its byte span
            // remains valid; retain full resolution through candidate reload.
            auto atlasAsset = Fixture(); auto& atlas = atlasAsset.geometry->textures[0];
            atlas.width = atlas.height = 6144; atlas.name = "FullResolution6144Atlas";
            atlas.rgba.assign(std::size_t(atlas.width) * atlas.height * 4, 255);
            atlas.rgba.front() = 17; atlas.rgba[atlas.rgba.size() / 2] = 91; atlas.rgba.back() = 113;
            const auto atlasDirectory = directory / "LargeAtlas";
            const bool atlasSaved = Model::SaveSkeletalAssetPackage(atlasAsset, atlasDirectory, &error);
            Expect(atlasSaved, "Save and verify full-resolution 6144 squared atlas above mesh element cap");
            if (!atlasSaved) std::printf("  %s\n", error.c_str());
            if (atlasSaved) {
                auto atlasLoaded = Model::LoadSkeletalAssetPackage(atlasDirectory, &error);
                Expect(atlasLoaded && atlasLoaded->geometry->textures[0].width == 6144
                    && atlasLoaded->geometry->textures[0].height == 6144
                    && atlasLoaded->geometry->textures[0].rgba == atlas.rgba,
                    "Large atlas dimensions and every decoded channel roundtrip without downsampling");
                atlasLoaded.reset();
                const auto atlasMeshPath = atlasDirectory / "SkeletalMeshes/model.skmesh.json";
                const auto atlasMesh = Read(atlasMeshPath); auto invalidAtlas = atlasMesh;
                invalidAtlas["textures"][0]["pixels"]["count"] = Json::UInt64(atlas.rgba.size() + 4);
                Write(atlasMeshPath, invalidAtlas);
                Expect(!Model::LoadSkeletalAssetPackage(atlasDirectory, &error), "Large atlas still rejects channel count beyond its declared dimensions");
                invalidAtlas = atlasMesh; invalidAtlas["textures"][0]["width"] = 32768;
                invalidAtlas["textures"][0]["height"] = 32768;
                invalidAtlas["textures"][0]["pixels"]["count"] = Json::UInt64(32768ull * 32768 * 4);
                Write(atlasMeshPath, invalidAtlas);
                Expect(!Model::LoadSkeletalAssetPackage(atlasDirectory, &error), "Oversized texture byte budget is rejected before pixel allocation");
                Write(atlasMeshPath, atlasMesh);
            }
            // Keep diagnostic JSON, but do not accumulate a 144 MiB binary on
            // every fixture run. This file is generated solely by this test.
            std::error_code cleanup;
            fs::remove(atlasDirectory / "SkeletalMeshes/model.skmesh.bin", cleanup);
        }
        changed = manifest; changed["version"] = 999; Write(manifestPath, changed);
        Expect(!Model::LoadSkeletalAssetPackage(directory, &error), "Reject unknown package format version"); Write(manifestPath, manifest);
        changed = manifest; changed["skeleton"] = "../outside.json"; Write(manifestPath, changed);
        Expect(!Model::LoadSkeletalAssetPackage(directory, &error), "Reject asset references outside the package"); Write(manifestPath, manifest);
#ifdef _WIN32
        changed = manifest; changed["skeleton"] = ".. /outside.json"; Write(manifestPath, changed);
        Expect(!Model::LoadSkeletalAssetPackage(directory, &error) && error.find("regular Windows filenames") != std::string::npos,
            "Reject trailing-space parent aliases before Windows normalizes them"); Write(manifestPath, manifest);
#endif
        changed = manifest; changed["animations"].append(manifest["animations"][0]); Write(manifestPath, changed);
        Expect(!Model::LoadSkeletalAssetPackage(directory, &error), "Reject duplicate animation references before repeated loading"); Write(manifestPath, manifest);
        const auto duplicateSequence = Read(directory / fs::path(manifest["animations"][0].asString()));
        Write(directory / "Animations/duplicate.anim.json", duplicateSequence);
        changed = manifest; changed["animations"].append("Animations/duplicate.anim.json"); Write(manifestPath, changed);
        Expect(!Model::LoadSkeletalAssetPackage(directory, &error), "Reject distinct animation files with duplicate asset IDs"); Write(manifestPath, manifest);
        const auto skeletonPath = directory / "Skeletons/model.skeleton.json"; const auto skeleton = Read(skeletonPath);
        changed = skeleton; changed["bones"][1]["parent"] = 0; Write(skeletonPath, changed);
        Expect(!Model::LoadSkeletalAssetPackage(directory, &error), "Reject cyclic skeleton parents before pose evaluation"); Write(skeletonPath, skeleton);
        const auto meshPath = directory / "SkeletalMeshes/model.skmesh.json"; const auto mesh = Read(meshPath);
        changed = mesh; changed["meshes"][0]["vertices"]["offset"] = Json::UInt64(std::numeric_limits<std::uint64_t>::max()); Write(meshPath, changed);
        Expect(!Model::LoadSkeletalAssetPackage(directory, &error), "Reject overflowing binary offset before allocation/read"); Write(meshPath, mesh);
        changed = mesh; changed["meshes"][0]["vertices"]["offset"] = Json::UInt64(0); Write(meshPath, changed);
        Expect(!Model::LoadSkeletalAssetPackage(directory, &error), "Reject binary ranges that overlap the payload signature"); Write(meshPath, mesh);
        changed = mesh; changed["meshes"].append(mesh["meshes"][0]); Write(meshPath, changed);
        Expect(!Model::LoadSkeletalAssetPackage(directory, &error), "Reject reused binary ranges instead of repeated mesh allocations"); Write(meshPath, mesh);
        changed = mesh; changed["textures"][0]["width"] = 30000; Write(meshPath, changed);
        Expect(!Model::LoadSkeletalAssetPackage(directory, &error), "Reject texture dimensions that disagree with decoded payload"); Write(meshPath, mesh);
        const auto binaryPath = directory / "SkeletalMeshes/model.skmesh.bin";
        { std::fstream stream(binaryPath, std::ios::in | std::ios::out | std::ios::binary); stream.seekg(-1, std::ios::end); char value; stream.read(&value, 1); value ^= 1; stream.seekp(-1, std::ios::end); stream.write(&value, 1); }
        Expect(!Model::LoadSkeletalAssetPackage(directory, &error), "Reject damaged payload checksum");
        asset.skins[0].vertices[0].joints[0] = 20;
        Expect(!Model::ValidateSkeletalAsset(asset, &error), "Reject weighted joint indices beyond skeleton range");
        asset = Fixture(); asset.skins[0].vertices[0].weights = {0, 0, 0, 0};
        Expect(!Model::ValidateSkeletalAsset(asset, &error), "Reject zero-sum skin weights");
        asset = Fixture(); asset.skeleton.bones[1].parent = 100;
        Expect(!Model::ValidateSkeletalAsset(asset, &error), "Reject out-of-range parent on forward-referenced bone");
        asset = Fixture(); asset.animations[0].boneTracks[0].translations[1].time = -1;
        Expect(!Model::ValidateSkeletalAsset(asset, &error), "Reject unsorted/out-of-range animation keys");
        asset = Fixture(); asset.meshRegions[0].mesh = 1;
        Expect(!Model::ValidateSkeletalAsset(asset, &error), "Validate rejects out-of-range region mesh before saving");
        asset = Fixture(); asset.meshRegions.push_back(asset.meshRegions[0]);
        Expect(!Model::ValidateSkeletalAsset(asset, &error), "Validate rejects duplicate mesh region annotations");
        asset = Fixture(); asset.meshRegions[0].kind = static_cast<Model::MeshRegionKind>(255);
        Expect(!Model::ValidateSkeletalAsset(asset, &error), "Validate rejects invalid in-memory region kind");
        asset = Fixture(); asset.meshRegions[0].confidence = std::numeric_limits<float>::quiet_NaN();
        Expect(!Model::ValidateSkeletalAsset(asset, &error), "Validate rejects non-finite mesh region confidence");
        asset = Fixture(); asset.meshRegions[0].confidence = 1.01f;
        Expect(!Model::ValidateSkeletalAsset(asset, &error), "Validate rejects in-memory confidence beyond one");
        asset = Fixture(); asset.meshRegions[0].evidence.resize(4097);
        Expect(!Model::ValidateSkeletalAsset(asset, &error), "Validate bounds mesh region evidence count");
        asset = Fixture(); asset.meshRegions[0].sourceMesh = std::string("bad\0name", 8);
        Expect(!Model::ValidateSkeletalAsset(asset, &error), "Validate rejects embedded NUL source metadata");
    } catch (const std::exception& e) { std::printf("Exception: %s\n", e.what()); ++failures; }
    // Keep this small fixture in the workspace for diagnosis; no recursive delete.
    std::printf("Fixture retained: %s\n", directory.string().c_str());
    std::printf("%d failures\n", failures); return failures ? 1 : 0;
}
