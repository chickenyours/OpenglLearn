#include "Model/Public/vmd_animation.h"
#include <json/json.h>
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
    std::printf("%-83s %s\n", message, condition ? "ok" : "FAIL"); failures += !condition; return condition;
}
Json::Value Read(const fs::path& path) { std::ifstream input(path); Json::Value json; input >> json; return json; }
void Write(const fs::path& path, const Json::Value& json) { std::ofstream output(path); output << json; }
Model::SkeletalAssetPackage Target(bool geometry = false) {
    Model::SkeletalAssetPackage target; target.id = "io-target"; target.name = "Target";
    target.sourcePath = "a-nonexistent-source.pmx"; target.sourceFormat = "fixture";
    target.skeleton.id = "io-target-skeleton"; target.skeleton.name = "Skeleton";
    Model::SkeletonBone root; root.name = "Root"; Model::SkeletonBone child; child.name = "Head"; child.parent = 0;
    target.skeleton.bones = {root, child};
    Model::MorphTarget smile; smile.name = "Smile"; Model::MorphTarget blink; blink.name = "Blink";
    target.morphs = {smile, blink};
    if (geometry) {
        target.geometry = std::make_shared<Model::StaticModel>(); Model::StaticMesh mesh;
        mesh.name = "triangle"; mesh.vertices.resize(3); mesh.vertices[1].position.x = 1; mesh.vertices[2].position.y = 1;
        mesh.indices = {0, 1, 2}; target.geometry->meshes.push_back(mesh); target.geometry->materials.emplace_back();
        target.skins.resize(1); target.skins[0].vertices.resize(3);
        target.ikRig.id = "io-target-rig"; target.ikRig.name = "Rig"; target.ikRig.skeletonId = target.skeleton.id;
    }
    return target;
}
Model::AnimationSequenceAsset Clip(const Model::SkeletalAssetPackage& target) {
    Model::AnimationSequenceAsset clip; clip.id = "io-baked-animation"; clip.name = "BakedMotion";
    clip.skeletonId = target.skeleton.id; clip.sourceSkeletonId = "mmd-name-only";
    clip.source = "file-does-not-exist/body.vmd;face.vmd"; clip.baked = true; clip.duration = 2.5; clip.framesPerSecond = 30;
    clip.diagnostics = {"Matched 2 bones", "Unmatched source helper was skipped"};
    Model::BoneAnimationTrack root; root.bone = 0;
    root.translations = {{0, {.125, 0, 0}}, {1.25, {.25, 1, 0}}, {2.5, {.5, 0, 0}}};
    root.rotations = {{0, glm::dquat(1, 0, 0, 0)}, {2.5, glm::angleAxis(.75, glm::dvec3(0, 1, 0))}};
    root.scales = {{0, {1, 1, 1}}}; clip.boneTracks.push_back(root);
    clip.morphTracks.push_back({1, {{0, 0}, {1.25, .875}, {2.5, 0}}}); return clip;
}
} // namespace

int main() {
    const auto directory = fs::path(MODEL_SOURCE_DIR) / "build/animation_io_fixtures" /
        ("run-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        fs::create_directories(directory); auto target = Target(); auto clip = Clip(target); std::string error;
        const auto path = directory / "Baked.anim.json";
        Expect(Model::SaveAnimationSequenceAsset(clip, target, path, &error), "Export animation without geometry or any source motion file");
        auto loaded = Model::LoadAnimationSequenceAsset(path, target, &error);
        if (!Expect(bool(loaded), "Load standalone clip with only target skeleton and morph layout")) throw std::runtime_error(error);
        Expect(loaded->id == clip.id && loaded->baked && !loaded->generated && loaded->sourceSkeletonId == clip.sourceSkeletonId
            && loaded->diagnostics == clip.diagnostics, "Stable identity, baked/source metadata and mapping diagnostics roundtrip");
        Expect(loaded->boneTracks[0].translations[0].value.x == .125 && loaded->morphTracks[0].keys[1].value == .875
            && std::abs(glm::dot(loaded->boneTracks[0].rotations[1].value, clip.boneTracks[0].rotations[1].value)) > .99999999,
            "Double TRS values, quaternion order and morph weights roundtrip");
        const auto pose = Model::EvaluateSkeletalPose(target, &*loaded, 1.25, false);
        Expect(std::abs(pose.local[0].translation.y - 1) < 1e-12 && std::abs(pose.morphWeights[1] - .875) < 1e-12,
            "Reloaded standalone sequence evaluates without original VMD");
        Expect(!Model::SaveAnimationSequenceAsset(clip, target, path, &error) && Model::LoadAnimationSequenceAsset(path, target),
            "Nonempty animation export is never overwritten");
        const auto empty = directory / "Empty.anim.json"; { std::ofstream stream(empty); }
        Expect(Model::SaveAnimationSequenceAsset(clip, target, empty, &error), "An existing empty output file can be atomically published");
        const auto json = Read(path); auto changed = json;
        auto wrong = target; wrong.skeleton.id = "other-skeleton";
        Expect(!Model::LoadAnimationSequenceAsset(path, wrong, &error), "Reject a clip bound to another skeleton ID");
        wrong = target; std::swap(wrong.morphs[0], wrong.morphs[1]);
        Expect(!Model::LoadAnimationSequenceAsset(path, wrong, &error), "Reject reordered target morph layout even with identical skeleton ID");
        wrong = target; wrong.skeleton.bones[1].name = "DifferentHead";
        Expect(!Model::LoadAnimationSequenceAsset(path, wrong, &error), "Reject edited target bone layout with identical skeleton ID");
        wrong = target; wrong.skeleton.bones[1].restLocal.translation.y += 1;
        Expect(!Model::LoadAnimationSequenceAsset(path, wrong, &error), "Reject edited target rest pose with identical skeleton ID");
        wrong = target; wrong.skeleton.bones[0].parent = 1;
        Expect(!Model::SaveAnimationSequenceAsset(clip, wrong, directory / "InvalidTarget.anim.json", &error),
            "Reject invalid target hierarchy without needing mesh geometry");
        const auto malformed = directory / "Malformed.anim.json";
        const auto reject = [&](const char* message) { Write(malformed, changed); Expect(!Model::LoadAnimationSequenceAsset(malformed, target, &error), message); changed = json; };
        changed["version"] = 999; reject("Reject unsupported animation schema version");
        changed["id"] = ""; reject("Reject empty standalone asset ID");
        changed["name"] = ""; reject("Reject empty standalone sequence name");
        changed["generated"] = "false"; reject("Reject mistyped animation provenance flag");
        changed["boneTracks"][0]["bone"] = 2; reject("Reject out-of-range target bone index");
        changed["boneTracks"].append(changed["boneTracks"][0]); reject("Reject duplicate target bone tracks");
        changed["morphTracks"][0]["morph"] = 2; reject("Reject out-of-range target expression index");
        changed["morphTracks"].append(changed["morphTracks"][0]); reject("Reject duplicate target expression curves");
        changed["boneTracks"][0]["translations"][1]["time"] = -1; reject("Reject negative and unsorted bone key times");
        changed["morphTracks"][0]["keys"][1]["time"] = 4; reject("Reject expression key time outside duration");
        changed["boneTracks"][0]["rotations"][1]["time"] = 0; reject("Reject duplicate rotation timestamps");
        changed["boneTracks"][0]["rotations"][0]["value"] = Json::Value(Json::arrayValue);
        for (int i = 0; i < 4; ++i) changed["boneTracks"][0]["rotations"][0]["value"].append(0.0);
        reject("Reject zero-length rotation quaternion");
        for (int i = 0; i < 4; ++i) changed["boneTracks"][0]["rotations"][0]["value"][i] = 1e308;
        reject("Reject overflowed quaternion norm");
        changed["boneTracks"][0]["scales"][0]["value"][0] = 0; reject("Reject singular animated scale");
        changed["framesPerSecond"] = 0; reject("Reject zero sample rate");
        changed.removeMember("baked"); changed.removeMember("sourceSkeletonId"); changed.removeMember("diagnostics"); changed.removeMember("targetLayoutHash");
        Write(malformed, changed); loaded = Model::LoadAnimationSequenceAsset(malformed, target, &error);
        Expect(loaded && !loaded->baked && loaded->sourceSkeletonId.empty() && loaded->diagnostics.empty(), "Version 1 legacy animations remain readable with new optional fields absent");
        { std::ofstream stream(malformed); stream << "{\"id\":\"one\",\"id\":\"two\"}"; }
        Expect(!Model::LoadAnimationSequenceAsset(malformed, target, &error), "Reject duplicate JSON property names");
        auto invalid = clip; invalid.duration = std::numeric_limits<double>::infinity();
        Expect(!Model::SaveAnimationSequenceAsset(invalid, target, directory / "Invalid.anim.json", &error)
            && !fs::exists(directory / "Invalid.anim.json"), "Invalid animation never leaves a published file");
        auto package = Target(true); package.animations.push_back(clip);
        const auto packagePath = directory / "Package";
        Expect(Model::SaveSkeletalAssetPackage(package, packagePath, &error), "Portable package includes baked animation schema");
        auto reloaded = Model::LoadSkeletalAssetPackage(packagePath, &error);
        Expect(reloaded && reloaded->animations[0].baked && reloaded->animations[0].sourceSkeletonId == clip.sourceSkeletonId
            && reloaded->animations[0].diagnostics == clip.diagnostics, "Package animations retain baked/source/mapping diagnostics");
        package.animations.push_back(clip);
        Expect(!Model::ValidateSkeletalAsset(package, &error), "Package rejects duplicate animation asset IDs");
        auto many = clip; many.id = "many-keys"; many.duration = 100; many.boneTracks[0].translations.clear();
        for (int frame = 0; frame <= 3000; ++frame) many.boneTracks[0].translations.push_back({frame / 30.0, {frame / 1000.0, 0, 0}});
        const bool savedMany = Model::SaveAnimationSequenceAsset(many, target, directory / "ManyKeys.anim.json", &error);
        const auto loadedMany = Model::LoadAnimationSequenceAsset(directory / "ManyKeys.anim.json", target, &error);
        Expect(savedMany && loadedMany && loadedMany->boneTracks[0].translations.size() == 3001,
            "Dense resampled channels serialize and reload with all key times retained");
    } catch (const std::exception& e) { std::printf("Exception: %s\n", e.what()); ++failures; }
    std::printf("Animation asset IO failures: %d\n", failures); return failures ? 1 : 0;
}
