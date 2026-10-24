#include "Model/Public/character_material_profile.h"
#include "Render/Public/Material/material_runtime.h"
#include <json/json.h>
#include <cmath>
#include <bit>
#include <cstring>
#include <fstream>
#include <set>
#include <stdexcept>
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace Model {
namespace {
void require(bool value,const std::string& message){if(!value)throw std::runtime_error(message);}
std::string utf8(const std::filesystem::path& p){auto s=p.u8string();return {s.begin(),s.end()};}
std::filesystem::path pathOf(const std::string& s){return std::filesystem::path(std::u8string(s.begin(),s.end()));}
float number(const Json::Value& value,const char* name,float lo,float hi){
    require(value.isNumeric(),std::string("Material profile expects number: ")+name);
    const double v=value.asDouble();require(std::isfinite(v)&&v>=lo&&v<=hi,std::string("Material profile value out of range: ")+name);return float(v);
}
glm::vec4 vector(const Json::Value& value,const char* name,unsigned count){
    require(value.isArray()&&value.size()==count,std::string("Material profile vector size: ")+name);
    glm::vec4 result(0);for(unsigned i=0;i<count;++i)result[i]=number(value[i],name,-10000,10000);return result;
}
Render::Material::ParameterValue parameter(const Json::Value& v,const std::string& name){
    if(v.isBool())return v.asBool();
    if(v.isNumeric())return number(v,name.c_str(),-10000,10000);
    require(v.isArray()&&v.size()>=2&&v.size()<=4,"Unsupported material parameter: "+name);
    const auto q=vector(v,name.c_str(),v.size());
    switch(v.size()){case 2:return glm::vec2(q);case 3:return glm::vec3(q);case 4:return q;default:throw std::runtime_error("Unsupported material vector: "+name);}
}
TextureImage decode(const std::filesystem::path& path,bool linear){
    std::ifstream file(path,std::ios::binary|std::ios::ate);require(bool(file),"Cannot open material texture: "+utf8(path));
    const auto size=file.tellg();require(size>0&&size<256*1024*1024,"Invalid material texture size");
    std::vector<unsigned char> data(static_cast<std::size_t>(size));file.seekg(0);file.read(reinterpret_cast<char*>(data.data()),size);
    int w=0,h=0,c=0;require(stbi_info_from_memory(data.data(),int(data.size()),&w,&h,&c)&&w>0&&h>0&&w<=16384&&h<=16384,"Invalid material texture dimensions: "+utf8(path));
    TextureImage image;image.name=utf8(path);image.width=w;image.height=h;image.linearColor=linear;image.hasAlpha=c==2||c==4;
    if(stbi_is_16_bit_from_memory(data.data(),int(data.size()))){
        std::unique_ptr<stbi_us,decltype(&stbi_image_free)> pixels(stbi_load_16_from_memory(data.data(),int(data.size()),&w,&h,&c,4),stbi_image_free);
        require(bool(pixels),"Cannot decode material texture: "+utf8(path));image.rgba32f.resize(std::size_t(w)*h*4);
        for(std::size_t i=0;i<image.rgba32f.size();++i)image.rgba32f[i]=float(pixels.get()[i])/65535.f;
    }else{
        std::unique_ptr<stbi_uc,decltype(&stbi_image_free)> pixels(stbi_load_from_memory(data.data(),int(data.size()),&w,&h,&c,4),stbi_image_free);
        require(bool(pixels),"Cannot decode material texture: "+utf8(path));image.rgba.assign(pixels.get(),pixels.get()+std::size_t(w)*h*4);
    }return image;
}
const std::map<std::string,std::size_t> slots{{"albedo",0},{"normal",1},{"metallic",2},{"roughness",3},{"ao",4},{"distortion",5},{"flow",6},{"emissive",7}};
}
std::shared_ptr<const Render::StrandTextureTable> LoadStrandTextureTable(const std::filesystem::path& path,std::string* error){
    try{
        require(std::endian::native==std::endian::little,"Strand tables require a little-endian host");
        std::ifstream file(path,std::ios::binary|std::ios::ate);require(bool(file),"Cannot open strand table: "+utf8(path));
        const auto bytes=file.tellg();require(bytes>=24&&bytes<=64*1024*1024+24,"Invalid strand table size");file.seekg(0);
        char magic[8]{};std::uint32_t header[4]{};file.read(magic,8);file.read(reinterpret_cast<char*>(header),16);
        require(bool(file)&&std::memcmp(magic,"DHSLUT01",8)==0&&header[0]==1&&header[3]==4,"Unsupported strand table header");
        const std::uint64_t count=std::uint64_t(header[1])*header[2];
        require(header[1]>0&&header[2]>0&&header[1]<=4096&&header[2]<=4096&&count<=4*1024*1024&&std::uint64_t(bytes)==24+count*16,"Invalid strand table dimensions or payload size");
        auto table=std::make_shared<Render::StrandTextureTable>();table->width=header[1];table->height=header[2];table->texels.resize(std::size_t(count));
        file.read(reinterpret_cast<char*>(table->texels.data()),std::streamsize(count*16));require(bool(file),"Truncated strand table");
        for(const auto& texel:table->texels)for(unsigned c=0;c<4;++c)require(std::isfinite(texel[c]),"Non-finite strand table texel");
        if(error)error->clear();return table;
    }catch(const std::exception& e){if(error)*error=e.what();return {};}
}
bool LoadStrandColorCoordinates(const std::filesystem::path& path,std::size_t expectedVertices,std::size_t expectedStrands,
    std::vector<Render::StrandColorCoordinate>& output,std::string* error){
    try{
        require(std::endian::native==std::endian::little,"Strand appearance requires a little-endian host");
        std::ifstream file(path,std::ios::binary|std::ios::ate);require(bool(file),"Cannot open strand appearance: "+utf8(path));
        const auto bytes=file.tellg();require(bytes>=24&&bytes<=20*1024*1024+24,"Invalid strand appearance size");file.seekg(0);
        char magic[8]{};std::uint32_t header[4]{};file.read(magic,8);file.read(reinterpret_cast<char*>(header),16);
        require(bool(file)&&std::memcmp(magic,"DHCOL001",8)==0&&header[0]==1&&header[3]==2,"Unsupported strand appearance header");
        require(header[1]==expectedVertices&&header[2]==expectedStrands&&header[1]>0&&header[2]>0&&header[2]<=header[1]&&std::uint64_t(bytes)==24+2ull*header[1],"Strand appearance topology or payload mismatch");
        std::vector<std::uint8_t> source(std::size_t(header[1])*2);file.read(reinterpret_cast<char*>(source.data()),std::streamsize(source.size()));require(bool(file),"Truncated strand appearance");
        std::vector<Render::StrandColorCoordinate> coordinates(header[1]);
        for(std::size_t i=0;i<coordinates.size();++i)coordinates[i].gradientRandom=glm::vec2(source[2*i],source[2*i+1])/255.f;
        output=std::move(coordinates);if(error)error->clear();return true;
    }catch(const std::exception& e){if(error)*error=e.what();return false;}
}
std::optional<CharacterMaterialProfile> CharacterMaterialProfile::Load(const std::filesystem::path& path,std::string* error){
    try{
        std::ifstream file(path,std::ios::binary);require(bool(file),"Cannot open character material profile: "+utf8(path));
        Json::CharReaderBuilder reader;Json::Value root;std::string reason;require(Json::parseFromStream(reader,file,&root,&reason),"Invalid character material profile: "+reason);
        require(root["schema"].asString()=="model.character-materials.v1","Unsupported character material profile schema");
        require(root["materials"].isArray()&&!root["materials"].empty(),"Character material profile has no materials");
        CharacterMaterialProfile p;p.path_=std::filesystem::absolute(path);std::set<std::string> names;
        for(const auto& item:root["materials"]){
            CharacterMaterialEntry e;e.name=item["name"].asString();e.category=item.get("category","surface").asString();
            require(!e.name.empty()&&names.insert(e.name).second,"Duplicate/empty character material name: "+e.name);
            if(item.isMember("baseColor"))e.baseColor=vector(item["baseColor"],"baseColor",4);
            if(item.isMember("emissive"))e.emissive=glm::vec3(vector(item["emissive"],"emissive",3));
            const auto scalar=[&](const char* name,std::optional<float>& dest,float lo,float hi){if(item.isMember(name))dest=number(item[name],name,lo,hi);};
            scalar("metallic",e.metallic,0,1);scalar("roughness",e.roughness,.01f,1);scalar("ao",e.ao,0,1);scalar("normalScale",e.normalScale,0,8);scalar("alphaCutoff",e.alphaCutoff,0,1);
            if(item.isMember("twoSided")){require(item["twoSided"].isBool(),"twoSided must be bool");e.twoSided=item["twoSided"].asBool();}
            if(item.isMember("alphaMode")){const auto a=item["alphaMode"].asString();require(a=="opaque"||a=="mask"||a=="blend","Invalid alphaMode");e.alphaMode=a=="opaque"?AlphaMode::Opaque:a=="mask"?AlphaMode::Mask:AlphaMode::Blend;}
            const auto& textures=item["textures"];require(textures.isNull()||textures.isObject(),"Material textures must be object");
            for(const auto& name:textures.getMemberNames()){
                require(slots.contains(name)&&textures[name].isString(),"Unknown material texture slot: "+name);
                const auto relative=pathOf(textures[name].asString());require(!relative.empty()&&relative.is_relative(),"Material texture must be a relative asset path");
                for(const auto& component:relative)require(component!="..","Material texture escapes asset directory");
                e.textures.emplace(name,p.path_.parent_path()/relative);
            }
            const auto& params=item["parameters"];require(params.isNull()||params.isObject(),"Material parameters must be object");
            for(const auto& name:params.getMemberNames())e.parameters.emplace(name,parameter(params[name],name));
            p.entries_.push_back(std::move(e));
        }
        require(root["strands"].isNull()||root["strands"].isObject(),"Strand parameters must be an object");
        if(root["strands"].isObject())for(const auto& name:root["strands"].getMemberNames())if(root["strands"][name].isBool()||root["strands"][name].isNumeric()||root["strands"][name].isArray())p.strands_.emplace(name,parameter(root["strands"][name],name));
        for(const char* flag:{"useNativeMomentLut","volumeHardwareFiltering","strandMotionVectors","volumePreserveNearField","sourceSpatialIbl","sourceSegmentShading","sourceCurveGeometry","volumeExactIntegration","volumeProxyBlockers","volumeStableOpaqueCache","volumePointIbl","sourceDSVolume"})
            if(root["strands"].isMember(flag))require(root["strands"][flag].isBool(),std::string("Strand flag must be boolean: ")+flag);
        if(root["strands"].isMember("nativeMomentParameters")){
            const auto key=vector(root["strands"]["nativeMomentParameters"],"nativeMomentParameters",4);
            require(key.x>=1&&key.x<=3&&key.y>=0&&key.y<=1&&key.z>=.02f&&key.z<=1&&std::abs(key.w)<=1,
                "Native strand table key expects IOR, Fresnel F0, azimuthal roughness and cuticle tilt");
        }
        const auto& strandAssets=root["strandAssets"];require(strandAssets.isNull()||strandAssets.isObject(),"Strand assets must be an object");
        for(const auto& name:strandAssets.getMemberNames()){
            require((name=="gradientTable"||name=="colorCoordinates"||name=="nttTable"||name=="nativeMomentTable")&&strandAssets[name].isString(),"Unsupported strand asset: "+name);
            const auto relative=pathOf(strandAssets[name].asString());require(!relative.empty()&&relative.is_relative(),"Strand asset must be a relative path");
            for(const auto& component:relative)require(component!="..","Strand asset escapes profile directory");
            p.strandAssets_.emplace(name,p.path_.parent_path()/relative);
        }
        if(error)error->clear();return p;
    }catch(const std::exception& e){if(error)*error=e.what();return {};}
}
std::shared_ptr<const StaticModel> CharacterMaterialProfile::Prepare(std::shared_ptr<const StaticModel> original,CharacterMaterialReport* output,std::string* error)const{
    try{
        require(bool(original),"Character material profile needs model geometry");auto model=std::make_shared<StaticModel>(*original);CharacterMaterialReport report;
        std::map<std::pair<std::filesystem::path,bool>,std::int32_t> cache;std::set<std::uint32_t> matched,used;
        for(const auto& m:model->meshes)used.insert(m.materialIndex);
        for(const auto& e:entries_){bool found=false;
            for(std::uint32_t i=0;i<model->materials.size();++i){auto& m=model->materials[i];if(m.name!=e.name)continue;found=true;matched.insert(i);++report.matchedMaterials;++report.categories[e.category];
                if(e.baseColor)m.baseColor=*e.baseColor;if(e.emissive)m.emissive=*e.emissive;if(e.metallic)m.metallic=*e.metallic;if(e.roughness)m.roughness=*e.roughness;if(e.ao)m.ao=*e.ao;if(e.normalScale)m.normalScale=*e.normalScale;if(e.twoSided)m.twoSided=*e.twoSided;if(e.alphaMode)m.alphaMode=*e.alphaMode;if(e.alphaCutoff)m.alphaCutoff=*e.alphaCutoff;
                for(const auto& [slot,path]:e.textures){const bool linear=slot!="albedo";auto [it,added]=cache.emplace(std::make_pair(path,linear),-1);if(added){it->second=std::int32_t(model->textures.size());model->textures.push_back(decode(path,linear));++report.decodedTextures;}m.textures[slots.at(slot)]=it->second;++report.textureBindings;}
            }if(!found)report.unmatchedEntries.push_back(e.name);
        }
        require(!matched.empty(),"Character profile matched zero material names");
        for(auto i:used)require(matched.contains(i),"Missing character profile material: "+model->materials.at(i).name);
        if(output)*output=std::move(report);if(error)error->clear();return model;
    }catch(const std::exception& e){if(error)*error=e.what();return {};}
}
bool CharacterMaterialProfile::Apply(ModelResource& resource,std::string* error)const{
    for(const auto& e:entries_)for(std::size_t i=0;i<resource.Source().materials.size();++i)if(resource.Source().materials[i].name==e.name)
        for(const auto& [name,value]:e.parameters){std::string reason;if(!resource.Materials().SetParameter(resource.MaterialHandles().at(i),name,value,&reason)){if(error)*error="Character material "+e.name+" / "+name+": "+reason;return false;}}
    if(error)error->clear();return true;
}
} // namespace Model
