#include <assimp/Importer.hpp>
#include <assimp/config.h>
#include <assimp/material.h>
#include <assimp/scene.h>

#include <algorithm>
#include <cstring>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace {
template<class T> void Numbers(const aiMaterialProperty& property) {
    const auto count=property.mDataLength/sizeof(T);
    for(std::size_t i=0;i<std::min<std::size_t>(count,32);++i) {
        T value{};std::memcpy(&value,property.mData+i*sizeof(T),sizeof(T));if(i)std::cout<<',';std::cout<<value;
    }
    if(count>32)std::cout<<"... ("<<count<<" components)";
}
void Property(const aiMaterial& material,const aiMaterialProperty& property) {
    std::cout<<"  "<<property.mKey.C_Str()<<" [semantic="<<property.mSemantic<<", index="<<property.mIndex
        <<", encoding="<<property.mType<<", bytes="<<property.mDataLength<<"] = ";
    if(property.mType==aiPTI_String) {
        aiString value;
        if(aiGetMaterialString(&material,property.mKey.C_Str(),property.mSemantic,property.mIndex,&value)==aiReturn_SUCCESS)
            std::cout<<std::quoted(value.C_Str());else std::cout<<"<unreadable string>";
    }else if(property.mType==aiPTI_Float)Numbers<float>(property);
    else if(property.mType==aiPTI_Double)Numbers<double>(property);
    else if(property.mType==aiPTI_Integer)Numbers<std::int32_t>(property);
    else {
        for(unsigned i=0;i<std::min(32u,property.mDataLength);++i)
            std::cout<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(static_cast<unsigned char>(property.mData[i]));
        std::cout<<std::dec<<std::setfill(' ');if(property.mDataLength>32)std::cout<<"...";
    }
    std::cout<<'\n';
}
int Run(const std::filesystem::path& path) {
    std::ifstream stream(path,std::ios::binary|std::ios::ate);
    if(!stream)throw std::runtime_error("Cannot open source FBX");const auto length=stream.tellg();
    if(length<=0||std::uintmax_t(length)>std::numeric_limits<std::size_t>::max()||
        std::uintmax_t(length)>std::uintmax_t(std::numeric_limits<std::streamsize>::max()))
        throw std::runtime_error("Invalid/oversized source FBX");
    std::vector<char> bytes(static_cast<std::size_t>(length));stream.seekg(0);
    if(!stream.read(bytes.data(),static_cast<std::streamsize>(bytes.size())))throw std::runtime_error("Cannot read source FBX");
    Assimp::Importer importer;
    importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_ANIMATIONS,false);
    importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_CAMERAS,false);
    importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_LIGHTS,false);
    importer.SetPropertyBool(AI_CONFIG_IMPORT_FBX_READ_WEIGHTS,false);
    const auto* scene=importer.ReadFileFromMemory(bytes.data(),bytes.size(),0,"fbx");
    if(!scene)throw std::runtime_error(importer.GetErrorString());
    std::cout<<std::setprecision(12)<<"FBX material audit | materials="<<scene->mNumMaterials
        <<" embedded textures="<<scene->mNumTextures<<" meshes="<<scene->mNumMeshes<<" ai_real_bytes="<<sizeof(ai_real)<<'\n';
    for(unsigned i=0;i<scene->mNumMaterials;++i) {
        const auto& material=*scene->mMaterials[i];aiString name;material.Get(AI_MATKEY_NAME,name);
        std::cout<<"\nMATERIAL "<<i<<' '<<std::quoted(name.C_Str())<<" properties="<<material.mNumProperties<<'\n';
        for(unsigned type=0;type<=AI_TEXTURE_TYPE_MAX;++type) {
            const auto semantic=static_cast<aiTextureType>(type);const auto count=material.GetTextureCount(semantic);
            if(!count)continue;std::cout<<"  texturecount["<<type<<"]="<<count<<'\n';
            for(unsigned index=0;index<count;++index) {
                aiString file;unsigned uv=0;aiTextureMapping mapping=aiTextureMapping_UV;
                if(material.GetTexture(semantic,index,&file,&mapping,&uv)==aiReturn_SUCCESS)
                    std::cout<<"    texture["<<index<<"] file="<<std::quoted(file.C_Str())<<" mapping="<<mapping<<" uv="<<uv<<'\n';
            }
        }
        for(unsigned p=0;p<material.mNumProperties;++p)Property(material,*material.mProperties[p]);
    }
    return 0;
}
}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv) {
    SetConsoleOutputCP(CP_UTF8);
#else
int main(int argc,char** argv) {
#endif
    if(argc!=3||std::filesystem::path(argv[1])!="--source") {
        std::cerr<<"Usage: fbx_material_audit --source scene.fbx\n";return 2;
    }
    try{return Run(std::filesystem::path(argv[2]));}
    catch(const std::exception& error){std::cerr<<"Material audit failed: "<<error.what()<<'\n';return 1;}
}
