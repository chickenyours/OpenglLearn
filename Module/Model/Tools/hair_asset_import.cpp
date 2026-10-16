#include "Model/Public/hair_asset.h"
#include <cstdio>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace {
namespace fs=std::filesystem;
std::string Utf8(const fs::path& p){const auto s=p.u8string();return {reinterpret_cast<const char*>(s.data()),s.size()};}
void Usage(){std::puts("Independent skin-bound hair secondary-motion asset generator\n"
    "  hair_asset_import --source <original.pmx|physics-package>\n"
    "      --target <body/Package.asset.json> --output <new.hair.json>\n"
    "      [--name Hair] [--max-probes 512] [--physics-hz 30|60]\n"
    "  hair_asset_import --inspect <existing.hair.json> --target <body-package>\n"
    "Source requires complete physical records (old count-only packages must be reimported).\n"
    "Output is independent, verified against target skin topology, and never overwritten.");}
void Summary(const Model::HairAsset& a){std::size_t bones=0,faces=0;for(const auto& c:a.chains)bones+=c.links.size();for(const auto& p:a.probes)faces+=p.triangleSample;
    std::printf("Hair: %s | %zu chains / %zu bones\nSkin: %zu material sections / %zu probes (%zu triangle samples)\nSource: %zu rigid bodies / %zu joints\nUnits/meter: %.10g\nBinding: %s\n",a.name.c_str(),a.chains.size(),bones,a.hairMeshes.size(),a.probes.size(),faces,a.sourceRigidBodies.size(),a.sourceJoints.size(),a.unitsPerMeter,a.bindingHash.c_str());
    std::printf("Contact: %.3f Hz / %u bone substeps / %.3f mm margin\n",1./a.settings.fixedTimeStep,a.settings.substeps,a.settings.thickness*1000);
    for(const auto& d:a.diagnostics)std::printf("  %s\n",d.c_str());}
int Run(const std::vector<fs::path>& args){fs::path source,target,output,inspect;Model::HairBuildOptions options;std::uint32_t physicsHz=60;
    for(std::size_t i=1;i<args.size();++i){const auto key=Utf8(args[i]);if(key=="--help"||key=="-h"){Usage();return 0;}if(key!="--source"&&key!="--target"&&key!="--output"&&key!="--inspect"&&key!="--name"&&key!="--max-probes"&&key!="--physics-hz")throw std::runtime_error("Unknown option: "+key);if(++i==args.size())throw std::runtime_error("Missing value for "+key);const auto value=Utf8(args[i]);
        if(key=="--source")source=args[i];else if(key=="--target")target=args[i];else if(key=="--output")output=args[i];else if(key=="--inspect")inspect=args[i];else if(key=="--name")options.name=value;else{std::size_t used=0;const auto n=std::stoull(value,&used);if(key=="--physics-hz"){if(value.empty()||value[0]=='-'||used!=value.size()||(n!=30&&n!=60))throw std::runtime_error("Hair physics rate must be 30 or 60 Hz.");physicsHz=static_cast<std::uint32_t>(n);}else{if(value.empty()||value[0]=='-'||used!=value.size()||n<64||n>512)throw std::runtime_error("Hair probe budget must be 64..512.");options.maxProbes=static_cast<std::size_t>(n);}}}
    if(target.empty()||(inspect.empty()&&(source.empty()||output.empty()))){Usage();return 2;}std::string error;
    const auto t=Model::LoadSkeletalAssetPackage(target,&error);if(!t)throw std::runtime_error("Target load failed: "+error);
    if(!inspect.empty()){if(!source.empty()||!output.empty())throw std::runtime_error("--inspect cannot be combined with source/output.");const auto a=Model::LoadHairAsset(inspect,*t,&error);if(!a)throw std::runtime_error(error);Summary(*a);return 0;}
    if(fs::exists(output))throw std::runtime_error("Hair output already exists; choose a new revision.");
    const auto p=(fs::is_directory(source)||source.extension()==".json")?Model::LoadSkeletalAssetPackage(source,&error):Model::ImportSkeletalSource(source,{},&error);if(!p)throw std::runtime_error("Source load failed: "+error);
    auto a=Model::BuildHairAsset(*p,*t,options,&error);if(!a)throw std::runtime_error("Hair generation failed: "+error);
    a->settings.fixedTimeStep=1./physicsHz;a->settings.substeps=120/physicsHz;
    a->diagnostics.push_back("Contact rate "+std::to_string(physicsHz)+" Hz; bone integration rate 120 Hz.");
    Summary(*a);if(!Model::SaveHairAsset(*a,*t,output,&error))throw std::runtime_error("Hair publish failed: "+error);
    std::printf("Saved verified independent hair asset: %s\n",Utf8(fs::absolute(output)).c_str());return 0;
}
}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv){SetConsoleOutputCP(CP_UTF8);std::vector<fs::path> a;for(int i=0;i<argc;++i)a.emplace_back(argv[i]);try{return Run(a);}catch(const std::exception& e){std::fprintf(stderr,"Error: %s\n",e.what());return 1;}}
#else
int main(int argc,char** argv){std::vector<fs::path> a;for(int i=0;i<argc;++i)a.emplace_back(fs::u8path(argv[i]));try{return Run(a);}catch(const std::exception& e){std::fprintf(stderr,"Error: %s\n",e.what());return 1;}}
#endif
