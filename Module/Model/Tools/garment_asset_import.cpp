#include "Model/Public/cloth_asset.h"
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace {
namespace fs=std::filesystem;
std::string Utf8(const fs::path& p) {const auto s=p.u8string();return {reinterpret_cast<const char*>(s.data()),s.size()};}
void Usage() {
    std::puts("Garment and XPBD cloth intermediate asset generator\n"
        "  garment_asset_import --source <Package.asset.json|package-dir|model.pmx> --output <new-dir>\n"
        "      [--name Skirt] [--pin-fraction 0.14] [--refinement 1] [--density 0.25]\n"
        "      [--material <zero-based-index>]...\n"
        "  garment_asset_import --inspect <Character.asset.json|character-dir>\n\n"
        "Writes Body, Garments/Skirt, Cloth and Character.asset.json as independent assets.\n"
        "Automatic skirt selection uses source skin weights and recognized skirt bones.\n"
        "--material explicitly restricts candidate source materials; use repeated options\n"
        "for skirts crossing multiple material sections. Existing assets are not overwritten.");
}
void Summary(const Model::ModularCharacterBundle& b) {
    std::size_t pinned=0;for(const auto& p:b.cloth.particles)pinned+=p.inverseMass==0;
    std::printf("Character: %s\nSkeleton: %s (%zu bones)\n",b.assembly.name.c_str(),b.assembly.skeletonId.c_str(),b.body->skeleton.bones.size());
    std::printf("Body: %llu vertices, %llu triangles, %zu material sections\n",
        static_cast<unsigned long long>(b.body->geometry->VertexCount()),static_cast<unsigned long long>(b.body->geometry->TriangleCount()),b.body->geometry->meshes.size());
    std::printf("Garment: %llu vertices, %llu triangles, %zu material sections\n",
        static_cast<unsigned long long>(b.garment->geometry->VertexCount()),static_cast<unsigned long long>(b.garment->geometry->TriangleCount()),b.garment->geometry->meshes.size());
    std::printf("Simulation: %zu particles (%zu pinned), %zu triangles, %zu stretch, %zu bend, %zu tethers\n",
        b.cloth.particles.size(),pinned,b.cloth.triangles.size(),b.cloth.stretch.size(),b.cloth.bend.size(),b.cloth.tethers.size());
    std::printf("Collision: %zu bone-driven capsules + %zu skin hulls | source units per meter: %.8g\nTopology: %s\n",b.cloth.capsules.size(),b.cloth.convexes.size(),b.cloth.unitsPerMeter,b.cloth.topologyHash.c_str());
    for(const auto& s:b.assembly.slots)std::printf("Slot: %s [%s] | %s\n",s.slot.c_str(),s.enabled?"enabled":"disabled",s.garmentId.c_str());
    for(const auto& d:b.diagnostics)std::printf("Character: %s\n",d.c_str());
    for(const auto& d:b.cloth.diagnostics)std::printf("Cloth: %s\n",d.c_str());
}
int Run(const std::vector<fs::path>& args) {
    fs::path source,output,inspect;Model::GarmentBuildOptions options;
    for(std::size_t i=1;i<args.size();++i) {
        const auto option=Utf8(args[i]);if(option=="--help"||option=="-h"){Usage();return 0;}
        if(option!="--source"&&option!="--output"&&option!="--inspect"&&option!="--name"&&option!="--pin-fraction"&&option!="--material"&&option!="--refinement"&&option!="--density")throw std::runtime_error("Unknown option: "+option);
        if(++i==args.size())throw std::runtime_error("Missing value for "+option);const auto value=Utf8(args[i]);
        if(option=="--source")source=args[i];else if(option=="--output")output=args[i];else if(option=="--inspect")inspect=args[i];else if(option=="--name")options.name=value;
        else if(option=="--pin-fraction") {std::size_t used=0;options.waistPinFraction=std::stod(value,&used);if(used!=value.size()||!std::isfinite(options.waistPinFraction)||options.waistPinFraction<=0||options.waistPinFraction>=1)throw std::runtime_error("Pin fraction must be a number strictly between 0 and 1.");}
        else if(option=="--refinement"){std::size_t used=0;const auto n=std::stoul(value,&used);if(used!=value.size()||n>2||value.empty()||value[0]=='-')throw std::runtime_error("Refinement must be 0, 1 or 2.");options.refinementLevels=std::uint32_t(n);}
        else if(option=="--density"){std::size_t used=0;options.surfaceDensity=std::stod(value,&used);if(used!=value.size()||!std::isfinite(options.surfaceDensity)||options.surfaceDensity<.01||options.surfaceDensity>5)throw std::runtime_error("Density must be 0.01..5 kg/m^2.");}
        else {std::size_t used=0;const auto n=std::stoull(value,&used);if(used!=value.size()||value.empty()||value[0]=='-'||n>std::numeric_limits<std::uint32_t>::max())throw std::runtime_error("Material index must be an unsigned 32-bit integer.");options.materialIndices.push_back(static_cast<std::uint32_t>(n));}
    }
    std::string error;if(!inspect.empty()) {
        if(!source.empty()||!output.empty())throw std::runtime_error("--inspect cannot be combined with source/output.");
        const auto b=Model::LoadModularCharacter(inspect,&error);if(!b)throw std::runtime_error(error);Summary(*b);return 0;
    }
    if(source.empty()||output.empty()){Usage();return 2;}
    if(fs::exists(output)&&(!fs::is_directory(output)||!fs::is_empty(output)))throw std::runtime_error("Output is not new/empty. Choose a new output directory.");
    std::shared_ptr<Model::SkeletalAssetPackage> package;
    if(fs::is_directory(source)||source.extension()==".json")package=Model::LoadSkeletalAssetPackage(source,&error);
    else package=Model::ImportSkeletalSource(source,{},&error);
    if(!package)throw std::runtime_error("Source load failed: "+error);
    const auto bundle=Model::BuildSkirtGarment(*package,options,&error);if(!bundle)throw std::runtime_error("Garment generation failed: "+error);Summary(*bundle);
    if(!Model::SaveModularCharacter(*bundle,output,&error))throw std::runtime_error("Asset publish failed: "+error);
    std::printf("Saved verified independent character: %s\n",Utf8(fs::absolute(output)/"Character.asset.json").c_str());return 0;
}
}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv){SetConsoleOutputCP(CP_UTF8);std::vector<fs::path> a;for(int i=0;i<argc;++i)a.emplace_back(argv[i]);try{return Run(a);}catch(const std::exception& e){std::fprintf(stderr,"Error: %s\n",e.what());return 1;}}
#else
int main(int argc,char** argv){std::vector<fs::path> a;for(int i=0;i<argc;++i)a.emplace_back(fs::u8path(argv[i]));try{return Run(a);}catch(const std::exception& e){std::fprintf(stderr,"Error: %s\n",e.what());return 1;}}
#endif
