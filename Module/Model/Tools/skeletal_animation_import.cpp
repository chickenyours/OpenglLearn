#include "Model/Public/vmd_animation.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace {
namespace fs = std::filesystem;
std::string Utf8(const fs::path& path) {
    const auto bytes = path.u8string(); return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}
void Usage() {
    std::puts("Animation import and retarget baker\n"
        "  skeletal_animation_import --target <Package.asset.json> --vmd <file-or-directory>\n"
        "      [--vmd <additional>] --output <new.anim.json> [--package-output <new-dir>]\n"
        "      [--fps 30] [--scale 1] [--name Taitui]\n"
        "  skeletal_animation_import --target <Package.asset.json> --source-package <Package.asset.json>\n"
        "      --sequence <name-or-index> --output <new.anim.json> [--package-output <new-dir>]\n"
        "      [--fps 30] [--scale 1] [--name Retargeted]\n"
        "  skeletal_animation_import --inspect <file.anim.json> --target <Package.asset.json>\n\n"
        "Directories are scanned recursively for VMD body and morph motions. Camera/light-only\n"
        "files are reported and skipped. Outputs bind to the target skeleton and play without\n"
        "the original VMD or source character. Existing nonempty outputs are never overwritten.");
}
double Number(const fs::path& argument, const char* label) {
    const auto text = Utf8(argument); std::size_t consumed = 0; const auto value = std::stod(text, &consumed);
    if (consumed != text.size() || !std::isfinite(value)) throw std::runtime_error(std::string("Invalid ") + label);
    return value;
}
void Summary(const Model::AnimationSequenceAsset& clip) {
    std::size_t t = 0, r = 0, s = 0, m = 0;
    for (const auto& track : clip.boneTracks) { t += track.translations.size(); r += track.rotations.size(); s += track.scales.size(); }
    for (const auto& track : clip.morphTracks) m += track.keys.size();
    std::printf("Animation: %s\nID: %s\nTarget skeleton: %s\nSource skeleton: %s\nSource: %s\n",
        clip.name.c_str(), clip.id.c_str(), clip.skeletonId.c_str(), clip.sourceSkeletonId.c_str(), clip.source.c_str());
    std::printf("Duration: %.6f s | %.3f fps | Baked: %s | Generated preview: %s\n",
        clip.duration, clip.framesPerSecond, clip.baked ? "yes" : "no", clip.generated ? "yes" : "no");
    std::printf("Bone tracks: %zu (T %zu / R %zu / S %zu keys) | Morph curves: %zu (%zu keys)\n",
        clip.boneTracks.size(), t, r, s, clip.morphTracks.size(), m);
    for (const auto& line : clip.diagnostics) std::printf("Diagnostic: %s\n", line.c_str());
}
void Report(const Model::AnimationBakeReport& report) {
    std::printf("Samples: %zu | Matched bones: %zu | Matched morphs: %zu | Unmatched bones: %zu | Unmatched morphs: %zu\n",
        report.samples, report.matchedBones, report.matchedMorphs, report.unmatchedBones, report.unmatchedMorphs);
    for (const auto& match : report.matches) {
        if (match.target >= 0) std::printf("  [%s] %s -> %s (%d, %.1f%%; %s)\n",
            match.morph ? "MORPH" : "BONE", match.sourceName.c_str(), match.targetName.c_str(), match.target,
            match.confidence * 100, match.method.c_str());
        else std::printf("  [UNMATCHED %s] %s (%s)\n", match.morph ? "MORPH" : "BONE", match.sourceName.c_str(), match.method.c_str());
    }
    for (const auto& warning : report.warnings) std::printf("Warning: %s\n", warning.c_str());
}
bool VmdExtension(const fs::path& path) {
    auto extension = Utf8(path.extension()); std::transform(extension.begin(), extension.end(), extension.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); }); return extension == ".vmd";
}
std::vector<fs::path> MotionFiles(const std::vector<fs::path>& inputs) {
    std::set<fs::path> files;
    for (const auto& input : inputs) {
        if (fs::is_directory(input)) {
            for (const auto& item : fs::recursive_directory_iterator(input))
                if (item.is_regular_file() && VmdExtension(item.path())) files.insert(fs::absolute(item.path()).lexically_normal());
        } else {
            if (!fs::is_regular_file(input) || !VmdExtension(input)) throw std::runtime_error("VMD input is missing or does not have a .vmd extension: " + Utf8(input));
            files.insert(fs::absolute(input).lexically_normal());
        }
    }
    if (files.empty()) throw std::runtime_error("No VMD files were found in the supplied paths.");
    return {files.begin(), files.end()};
}
const Model::AnimationSequenceAsset& SelectSequence(const Model::SkeletalAssetPackage& source, const std::string& name) {
    const Model::AnimationSequenceAsset* result = nullptr;
    for (const auto& clip : source.animations) if (clip.name == name) {
        if (result) throw std::runtime_error("Sequence name is ambiguous; select its zero-based index."); result = &clip;
    }
    if (result) return *result;
    if (!name.empty() && std::all_of(name.begin(), name.end(), [](unsigned char c) { return c >= '0' && c <= '9'; })) {
        const auto index = std::stoull(name); if (index < source.animations.size()) return source.animations[index];
    }
    throw std::runtime_error("Sequence was not found by name or zero-based index: " + name);
}
int Run(const std::vector<fs::path>& arguments) {
    fs::path targetPath, sourcePath, output, packageOutput, inspect;
    std::vector<fs::path> vmdInputs; std::string sequenceName, name;
    Model::AnimationBakeOptions options;
    for (std::size_t i = 1; i < arguments.size(); ++i) {
        const auto option = Utf8(arguments[i]);
        if (option == "--help" || option == "-h") { Usage(); return 0; }
        if (option != "--target" && option != "--source-package" && option != "--sequence" && option != "--vmd"
            && option != "--output" && option != "--package-output" && option != "--inspect" && option != "--fps"
            && option != "--scale" && option != "--name") throw std::runtime_error("Unknown option: " + option);
        if (++i == arguments.size()) throw std::runtime_error("Missing value for " + option);
        if (option == "--target") targetPath = arguments[i];
        else if (option == "--source-package") sourcePath = arguments[i];
        else if (option == "--sequence") sequenceName = Utf8(arguments[i]);
        else if (option == "--vmd") vmdInputs.push_back(arguments[i]);
        else if (option == "--output") output = arguments[i];
        else if (option == "--package-output") packageOutput = arguments[i];
        else if (option == "--inspect") inspect = arguments[i];
        else if (option == "--fps") options.framesPerSecond = Number(arguments[i], "sample frame rate");
        else if (option == "--scale") options.translationScale = Number(arguments[i], "translation scale");
        else name = Utf8(arguments[i]);
    }
    if (targetPath.empty()) { Usage(); return 2; }
    std::string error; const auto target = Model::LoadSkeletalAssetPackage(targetPath, &error);
    if (!target) throw std::runtime_error("Target package load failed: " + error);
    if (!inspect.empty()) {
        if (!vmdInputs.empty() || !sourcePath.empty() || !output.empty() || !packageOutput.empty()) throw std::runtime_error("--inspect cannot be combined with import options.");
        const auto clip = Model::LoadAnimationSequenceAsset(inspect, *target, &error);
        if (!clip) throw std::runtime_error("Animation load failed: " + error); Summary(*clip); return 0;
    }
    if (output.empty() || (vmdInputs.empty() == sourcePath.empty())) { Usage(); return 2; }
    if (!sourcePath.empty() && sequenceName.empty()) throw std::runtime_error("Generic retargeting requires --sequence <name-or-index>.");
    if (!vmdInputs.empty() && !sequenceName.empty()) throw std::runtime_error("--sequence selects a source-package clip and cannot be combined with --vmd.");
    if (options.framesPerSecond < 1 || options.framesPerSecond > 240 || options.translationScale <= 0 || options.translationScale > 1000000)
        throw std::runtime_error("Bake with 1 <= fps <= 240 and 0 < translation scale <= 1000000.");
    if (fs::exists(output) && (!fs::is_regular_file(output) || fs::file_size(output) != 0)) throw std::runtime_error("Animation output is nonempty; choose a new filename.");
    if (!packageOutput.empty() && fs::exists(packageOutput) && (!fs::is_directory(packageOutput) || !fs::is_empty(packageOutput))) throw std::runtime_error("Package output is nonempty; choose a new directory.");
    if (!packageOutput.empty()) {
        const auto relative = fs::absolute(output).lexically_normal().lexically_relative(fs::absolute(packageOutput).lexically_normal());
        if (!relative.empty() && *relative.begin() != "..") throw std::runtime_error("Place --output outside --package-output; the package writer creates its own Animations entries.");
    }
    Model::AnimationBakeReport report; std::optional<Model::AnimationSequenceAsset> baked;
    if (!vmdInputs.empty()) {
        std::vector<Model::VmdMotion> motions;
        for (const auto& path : MotionFiles(vmdInputs)) {
            auto motion = Model::LoadVmdMotion(path, &error);
            if (!motion) throw std::runtime_error("VMD load failed for " + Utf8(path) + ": " + error);
            std::printf("VMD %s: %zu bone tracks, %zu morph tracks, %.3f s, %u camera / %u light / %u shadow keys\n",
                Utf8(path).c_str(), motion->bones.size(), motion->morphs.size(), motion->duration, motion->cameraKeys, motion->lightKeys, motion->shadowKeys);
            for (const auto& warning : motion->warnings) std::printf("  Import warning: %s\n", warning.c_str());
            if (motion->bones.empty() && motion->morphs.empty()) { std::puts("  SKIPPED: no character bone or morph channels (camera/light/IK visibility only)."); continue; }
            motions.push_back(std::move(*motion));
        }
        if (motions.empty()) throw std::runtime_error("No character bone or expression channels were found; camera-only VMD cannot animate this model.");
        if (name.empty()) name = motions.front().name.empty() ? "ImportedVmd" : motions.front().name;
        auto merged = Model::MergeVmdMotions(motions, name, &error);
        if (!merged) throw std::runtime_error("VMD merge failed: " + error);
        baked = Model::BakeVmdAnimation(*merged, *target, options, name, &report, &error);
    } else {
        const auto source = Model::LoadSkeletalAssetPackage(sourcePath, &error);
        if (!source) throw std::runtime_error("Source package load failed: " + error);
        const auto& sequence = SelectSequence(*source, sequenceName);
        if (name.empty()) name = sequence.name + "_Retargeted";
        const auto mapping = Model::BuildRetargetMap(source->skeleton, source->ikRig, target->skeleton, target->ikRig);
        baked = Model::BakeRetargetedAnimation(*source, sequence, *target, mapping, options, name, &report, &error);
    }
    if (!baked) throw std::runtime_error("Animation bake failed: " + error);
    Summary(*baked); Report(report);
    if (!Model::SaveAnimationSequenceAsset(*baked, *target, output, &error)) throw std::runtime_error("Animation save failed: " + error);
    std::printf("Saved verified standalone animation: %s\n", Utf8(fs::absolute(output)).c_str());
    if (!packageOutput.empty()) {
        target->animations.push_back(std::move(*baked));
        if (!Model::SaveSkeletalAssetPackage(*target, packageOutput, &error)) throw std::runtime_error("Standalone animation was saved, but package export failed: " + error);
        std::printf("Saved verified package: %s\n", Utf8(fs::absolute(packageOutput) / "Package.asset.json").c_str());
    }
    return 0;
}
} // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8); std::vector<fs::path> arguments;
    for (int i = 0; i < argc; ++i) arguments.emplace_back(argv[i]);
    try { return Run(arguments); } catch (const std::exception& e) { std::fprintf(stderr, "Error: %s\n", e.what()); return 1; }
}
#else
int main(int argc, char** argv) {
    std::vector<fs::path> arguments; for (int i = 0; i < argc; ++i) arguments.emplace_back(fs::u8path(argv[i]));
    try { return Run(arguments); } catch (const std::exception& e) { std::fprintf(stderr, "Error: %s\n", e.what()); return 1; }
}
#endif
