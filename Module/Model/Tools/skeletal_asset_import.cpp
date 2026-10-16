#include "Model/Public/skeletal_asset.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace {
std::string Utf8(const std::filesystem::path& path) {
    const auto bytes = path.u8string(); return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}
void Usage() {
    std::puts("Skeletal intermediate asset importer (format version 1)\n"
        "  skeletal_asset_import --source <model.pmx|fbx|gltf|blend|...> --output <new-directory> [--blender <blender.exe>] [--fit-profile <profile.json>]\n"
        "  skeletal_asset_import --inspect <Package.asset.json|package-directory> [--fit-profile <profile.json> --output <new-directory>]\n\n"
        "The package embeds geometry, skin weights, morphs, materials and decoded textures.\n"
        "Existing nonempty output directories are never overwritten. Generated preview\n"
        "sequences are explicitly tagged generated/procedural; they are not source motions.");
}
void Summary(const Model::SkeletalAssetPackage& asset) {
    std::size_t influences = 0, deltas = 0, imported = 0, generated = 0;
    for (const auto& skin : asset.skins) influences += skin.vertices.size();
    for (const auto& morph : asset.morphs) deltas += morph.deltas.size();
    for (const auto& sequence : asset.animations) { if (sequence.generated) ++generated; else ++imported; }
    std::printf("Asset: %s\nID: %s\nSource format: %s\n", asset.name.c_str(), asset.id.c_str(), asset.sourceFormat.c_str());
    std::printf("Meshes: %zu | Vertices: %llu | Triangles: %llu\n", asset.geometry->meshes.size(),
        static_cast<unsigned long long>(asset.geometry->VertexCount()), static_cast<unsigned long long>(asset.geometry->TriangleCount()));
    std::printf("Bones: %zu | Skin vertices: %zu | Morphs: %zu | Morph deltas: %zu\n", asset.skeleton.bones.size(), influences, asset.morphs.size(), deltas);
    std::size_t fullMeshes=0,fullWeights=0;std::uint32_t maximumInfluences=0;
    for(const auto& skin:asset.skins)if(!skin.influenceOffsets.empty()) {
        ++fullMeshes;fullWeights+=skin.influences.size();
        for(std::size_t vertex=0;vertex<skin.vertices.size();++vertex)maximumInfluences=std::max(maximumInfluences,skin.influenceOffsets[vertex+1]-skin.influenceOffsets[vertex]);
    }
    if(fullMeshes)std::printf("Complete skin meshes: %zu | Stored influence pairs: %zu | Maximum influences per vertex: %u\n",fullMeshes,fullWeights,maximumInfluences);
    std::printf("Materials: %zu | Embedded textures: %zu | IK chains: %zu\n", asset.geometry->materials.size(), asset.geometry->textures.size(), asset.ikRig.chains.size());
    std::printf("Mesh regions: %zu (classification does not enable cloth simulation)\n",asset.meshRegions.size());
    for(const auto& region:asset.meshRegions)std::printf("  Section %u: %s | confidence %.2f | object %s | material %s | authored Cloth %s | review %s\n",
        region.mesh,Model::MeshRegionKindName(region.kind),region.confidence,region.sourceObject.c_str(),
        region.sourceMaterial.c_str(),region.authoredCloth?"yes":"no",region.requiresReview?"yes":"no");
    std::printf("Animation sequences: %zu imported, %zu generated preview\n", imported, generated);
    for (const auto& sequence : asset.animations) std::printf("  [%s] %s (%.3fs, %.2f fps, %zu bone tracks, %zu morph tracks)\n",
        sequence.generated ? "GENERATED" : "IMPORTED", sequence.name.c_str(), sequence.duration, sequence.framesPerSecond,
        sequence.boneTracks.size(), sequence.morphTracks.size());
    for (const auto& warning : asset.warnings) std::printf("Warning: %s\n", warning.c_str());
    for (const auto& diagnostic : asset.ikRig.diagnostics) std::printf("IK: %s\n", diagnostic.c_str());
}
int Run(const std::vector<std::filesystem::path>& arguments) {
    std::filesystem::path source, output, inspect,fitProfile;
    Model::LoadOptions loadOptions;
    for (std::size_t i = 1; i < arguments.size(); ++i) {
        const auto option = Utf8(arguments[i]);
        if (option == "--help" || option == "-h") { Usage(); return 0; }
        if (option != "--source" && option != "--output" && option != "--inspect" && option != "--blender" && option!="--fit-profile") {
            std::fprintf(stderr, "Unknown option: %s\n", option.c_str()); Usage(); return 2;
        }
        if (++i == arguments.size()) { std::fprintf(stderr, "Missing value for %s\n", option.c_str()); return 2; }
        if (option == "--source") source = arguments[i];
        else if (option == "--output") output = arguments[i];
        else if(option == "--inspect") inspect = arguments[i];
        else if(option == "--fit-profile") fitProfile=arguments[i];
        else loadOptions.blenderExecutable=arguments[i];
    }
    std::string error;
    if (!inspect.empty()&&fitProfile.empty()) {
        if (!source.empty() || !output.empty()) { std::fputs("Use --fit-profile with --inspect when producing a fitted revision.\n", stderr); return 2; }
        const auto asset = Model::LoadSkeletalAssetPackage(inspect, &error);
        if (!asset) { std::fprintf(stderr, "Package load failed: %s\n", error.c_str()); return 1; }
        Summary(*asset); return 0;
    }
    if ((source.empty()==inspect.empty()) || output.empty()) { Usage(); return 2; }
    if (std::filesystem::exists(output) && (!std::filesystem::is_directory(output) || !std::filesystem::is_empty(output))) {
        std::fputs("Output is not an empty/new directory. Choose another output directory.\n", stderr); return 1;
    }
    std::printf("%s %s\n",inspect.empty()?"Importing":"Loading cached package", Utf8(inspect.empty()?source:inspect).c_str()); std::fflush(stdout);
    const auto asset = inspect.empty()?Model::ImportSkeletalSource(source, loadOptions, &error):Model::LoadSkeletalAssetPackage(inspect,&error);
    if (!asset) { std::fprintf(stderr, "Import failed: %s\n", error.c_str()); return 1; }
    if(!fitProfile.empty()) {
        if(!Model::ApplySkeletalFitProfile(*asset,fitProfile,&error)) {std::fprintf(stderr,"Fit profile failed: %s\n",error.c_str());return 1;}
        Model::AddGeneratedPreviewSequences(*asset);
    }
    Summary(*asset);
    if (!Model::SaveSkeletalAssetPackage(*asset, output, &error)) { std::fprintf(stderr, "Package save failed: %s\n", error.c_str()); return 1; }
    std::printf("Saved verified package: %s\n", Utf8(std::filesystem::absolute(output) / "Package.asset.json").c_str());
    return 0;
}
}

#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
    std::vector<std::filesystem::path> arguments; for (int i = 0; i < argc; ++i) arguments.emplace_back(argv[i]);
    try { return Run(arguments); } catch (const std::exception& e) { std::fprintf(stderr, "Error: %s\n", e.what()); return 1; }
}
#else
int main(int argc, char** argv) {
    std::vector<std::filesystem::path> arguments; for (int i = 0; i < argc; ++i) arguments.emplace_back(std::filesystem::u8path(argv[i]));
    try { return Run(arguments); } catch (const std::exception& e) { std::fprintf(stderr, "Error: %s\n", e.what()); return 1; }
}
#endif
