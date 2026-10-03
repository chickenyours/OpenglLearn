#include "IWanna/Public/project_generator.h"
#include <iostream>

int main(int argc,char** argv){
    if(argc<3||std::string(argv[1])!="new"){
        std::cerr<<"Usage: iwanna_project new <new-directory> [--assets <template-assets>]\n";return 2;
    }
    auto runtime=std::filesystem::absolute(argv[0]).parent_path();auto assets=runtime/"IWanna";
    for(int i=3;i<argc;++i){if(std::string(argv[i])=="--assets"&&i+1<argc)assets=argv[++i];else {std::cerr<<"Unknown option\n";return 2;}}
    try{std::cout<<IWanna::CreateBlankProject(argv[2],assets,runtime).string()<<'\n';return 0;}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
