#include "Model/Public/static_model.h"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>

namespace {
int failures=0;
void Expect(bool value,const char* text){std::cout<<(value?"OK ":"FAIL ")<<text<<'\n';failures+=!value;}
bool Near(float a,float b){return std::abs(a-b)<1e-5f;}
void Tga(const std::filesystem::path& path,const std::array<std::array<unsigned char,3>,2>& pixels) {
    std::ofstream out(path,std::ios::binary);std::array<unsigned char,18> header{};
    header[2]=2;header[12]=2;header[14]=1;header[16]=24;header[17]=0x20;
    out.write(reinterpret_cast<const char*>(header.data()),header.size());
    for(const auto& pixel:pixels){const std::array<unsigned char,3> bgr={pixel[2],pixel[1],pixel[0]};out.write(reinterpret_cast<const char*>(bgr.data()),3);}
}
void TgaAlpha(const std::filesystem::path& path) {
    std::ofstream out(path,std::ios::binary);std::array<unsigned char,18> header{};
    header[2]=2;header[12]=2;header[14]=1;header[16]=32;header[17]=0x28;
    out.write(reinterpret_cast<const char*>(header.data()),header.size());
    const std::array<unsigned char,8> bgra={60,40,20,128,120,100,80,255};
    out.write(reinterpret_cast<const char*>(bgra.data()),bgra.size());
}
void Png16(const std::filesystem::path& path) {
    // Two lossless RGB16 pixels: 1234/5678/9abc and def0/8000/0100.
    constexpr unsigned char bytes[]={137,80,78,71,13,10,26,10,0,0,0,13,73,72,68,82,0,0,0,2,0,0,0,1,16,2,0,0,0,43,208,52,158,0,0,0,21,73,68,65,84,120,156,99,16,50,9,171,152,181,231,222,135,6,6,70,6,0,32,143,4,186,230,112,113,169,0,0,0,0,73,69,78,68,174,66,96,130};
    std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(bytes),sizeof(bytes));
}
void Hdr(const std::filesystem::path& path) {
    std::ofstream out(path,std::ios::binary);out<<"#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 1 +X 2\n";
    const std::array<unsigned char,8> rgbe={128,64,32,130,64,128,32,129};
    out.write(reinterpret_cast<const char*>(rgbe.data()),rgbe.size());
}
std::filesystem::path Fbx(const std::filesystem::path& directory,const char* name,bool standardOpacity=false,
    int enabled=1,bool omitEnabled=false,int uv=0,bool physicalSignature=false,int roughnessInversion=1,bool omitInversion=false,
    const char* baseFile="base.tga",const char* maskFile="cutout.tga",bool sharedSources=false) {
    const auto path=directory/(std::string(name)+".fbx");std::ofstream out(path,std::ios::binary);
    out<<R"FBX(; FBX 7.4.0 project file
FBXHeaderExtension: {
    FBXHeaderVersion: 1003
    FBXVersion: 7400
}
Definitions: {
    Version: 100
    Count: )FBX"<<(sharedSources?12:6)<<R"FBX(
    ObjectType: "Geometry" { Count: 1 }
    ObjectType: "Model" { Count: 1 }
    ObjectType: "Material" { Count: )FBX"<<(sharedSources?2:1)<<R"FBX( }
    ObjectType: "Texture" { Count: )FBX"<<(sharedSources?8:3)<<R"FBX( }
}
Objects: {
    Geometry: 100, "Geometry::Panel", "Mesh" {
        GeometryVersion: 124
)FBX";
    if(sharedSources)out<<"        Vertices: *18 { a: 0,0,0,1,0,0,0,1,0,2,0,0,3,0,0,2,1,0 }\n"
        <<"        PolygonVertexIndex: *6 { a: 0,1,-3,3,4,-6 }\n";
    else out<<"        Vertices: *9 { a: 0,0,0,1,0,0,0,1,0 }\n        PolygonVertexIndex: *3 { a: 0,1,-3 }\n";
    out<<R"FBX(
        LayerElementUV: 0 {
            Version: 101
            Name: "UVMap"
            MappingInformationType: "ByPolygonVertex"
            ReferenceInformationType: "Direct"
)FBX";
    if(sharedSources)out<<"            UV: *12 { a: 0,0,1,0,0,1,0,0,1,0,0,1 }\n";
    else out<<"            UV: *6 { a: 0,0,1,0,0,1 }\n";
    out<<R"FBX(
        }
)FBX";
    if(uv!=0)out<<R"FBX(        LayerElementUV: 1 {
            Version: 101
            Name: "OtherUV"
            MappingInformationType: "ByPolygonVertex"
            ReferenceInformationType: "Direct"
            UV: *6 { a: 0,0,1,0,0,1 }
        }
)FBX";
    out<<R"FBX(        LayerElementMaterial: 0 {
            Version: 101
            Name: ""
)FBX";
    out<<"            MappingInformationType: \""<<(sharedSources?"ByPolygon":"AllSame")<<"\"\n";
    out<<R"FBX(
            ReferenceInformationType: "IndexToDirect"
)FBX";
    if(sharedSources)out<<"            Materials: *2 { a: 0,1 }\n";else out<<"            Materials: *1 { a: 0 }\n";
    out<<R"FBX(
        }
        Layer: 0 {
            Version: 100
            LayerElement: { Type: "LayerElementUV"
                TypedIndex: 0
            }
            LayerElement: { Type: "LayerElementMaterial"
                TypedIndex: 0
            }
        }
)FBX";
    if(uv!=0)out<<R"FBX(        Layer: 1 {
            Version: 100
            LayerElement: { Type: "LayerElementUV"
                TypedIndex: 1
            }
        }
)FBX";
    out<<R"FBX(    }
    Model: 200, "Model::Panel", "Mesh" {
        Version: 232
        Properties70: { P: "Lcl Translation", "Lcl Translation", "", "A",0,0,0
            P: "Lcl Rotation", "Lcl Rotation", "", "A",0,0,0
            P: "Lcl Scaling", "Lcl Scaling", "", "A",1,1,1
        }
    }
    Material: 300, "Material::MaxPhysical", "" {
        Version: 102
        ShadingModel: "phong"
        MultiLayer: 0
        Properties70: {
            P: "DiffuseColor", "Color", "", "A",0.8,0.6,0.4
            P: "DiffuseFactor", "Number", "", "A",1
            P: "Opacity", "Number", "", "A",1
)FBX";
    if(!omitEnabled)out<<"            P: \"3dsMax|Parameters|cutout_map_on\", \"bool\", \"\", \"\","<<enabled<<'\n';
    // Raw-looking fields alone must not change an ordinary FBX's standard
    // material. Only the exact exported Physical signature enables them.
    out<<"            P: \"3dsMax|Parameters|roughness\", \"Number\", \"\", \"\",0.7\n"
        <<"            P: \"3dsMax|Parameters|metalness\", \"Number\", \"\", \"\",0.2\n";
    if(!omitInversion)out<<"            P: \"3dsMax|Parameters|roughness_inv\", \"int\", \"\", \"\","<<roughnessInversion<<'\n';
    if(physicalSignature)out<<"            P: \"3dsMax|ORIGINAL_MTL\", \"KString\", \"\", \"\",\"PHYSICAL_MTL\"\n"
        <<"            P: \"ShininessExponent\", \"Number\", \"\", \"\",8\n";
    out<<"        }\n    }\n";
    if(sharedSources)out<<R"FBX(    Material: 301, "Material::PlainShared", "" {
        Version: 102
        ShadingModel: "phong"
        MultiLayer: 0
        Properties70: { P: "DiffuseColor", "Color", "", "A",1,1,1 }
    }
)FBX";
    for(unsigned texture=0;texture<(sharedSources?8u:3u);++texture) {
        const char* files[]={*baseFile?baseFile:"base.tga",maskFile,"opaque.tga","normal.tga","metal.tga","rough.tga","ao.tga","emission.tga"};
        out<<"    Texture: "<<400+texture<<", \"Texture::Map"<<texture<<"\", \"\" {\n"
            <<"        Type: \"TextureVideoClip\"\n        FileName: \""<<files[texture]<<"\"\n"
            <<"        RelativeFilename: \""<<files[texture]<<"\"\n        Properties70: {\n"
            <<"            P: \"Scaling\", \"Vector\", \"\", \"\",2,3,1\n"
            <<"            P: \"Translation\", \"Vector\", \"\", \"\",0.25,0.5,0\n"
            <<"            P: \"UVSet\", \"KString\", \"\", \"\",\""<<(texture==1&&uv?"OtherUV":"UVMap")<<"\"\n"
            <<"        }\n    }\n";
    }
    out<<R"FBX(}
Connections: {
    C: "OO",100,200
    C: "OO",200,0
    C: "OO",300,200
)FBX";
    if(*baseFile)out<<"    C: \"OP\",400,300,\"3dsMax|Parameters|base_color_map\"\n";
    out<<"    C: \"OP\",401,300,\"3dsMax|Parameters|cutout_map\"\n";
    if(standardOpacity)out<<"    C: \"OP\",402,300,\"TransparentColor\"\n";
    if(sharedSources)out<<R"FBX(    C: "OO",301,200
    C: "OP",400,301,"3dsMax|Parameters|base_color_map"
    C: "OP",403,300,"NormalMap"
    C: "OP",404,300,"3dsMax|Parameters|metalness_map"
    C: "OP",405,300,"3dsMax|Parameters|roughness_map"
    C: "OP",406,300,"Maya|TEX_ao_map"
    C: "OP",407,300,"EmissiveColor"
)FBX";
    out<<"}\n";return path;
}
std::shared_ptr<const Model::StaticModel> Load(const std::filesystem::path& path,bool flip=true) {
    Model::LoadOptions options;options.flipUVs=flip;std::string error;const auto model=Model::StaticModelLoader::Load(path,options,&error);
    if(!model){std::cout<<"Import diagnostic: "<<error<<'\n';++failures;}return model;
}
const Model::PbrMaterial* Material(const Model::StaticModel& model) {
    const auto found=std::find_if(model.materials.begin(),model.materials.end(),[](const auto& material){return material.name=="MaxPhysical";});
    return found==model.materials.end()?nullptr:&*found;
}
float Sample(const Model::TextureImage& image,std::size_t channel) {
    return image.rgba.empty()?image.rgba32f.at(channel):image.rgba.at(channel)/255.f;
}
bool HasWarning(const Model::StaticModel& model,const char* text) {
    return std::any_of(model.warnings.begin(),model.warnings.end(),[&](const auto& warning){return warning.find(text)!=std::string::npos;});
}
}
int main() {
    const auto directory=std::filesystem::temp_directory_path()/("fbx-physical-material-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(directory);
    struct Cleanup {std::filesystem::path directory;~Cleanup(){std::error_code error;std::filesystem::remove_all(directory,error);}} cleanup{directory};
    try {
        Tga(directory/"base.tga",{{{20,40,60},{80,100,120}}});Tga(directory/"cutout.tga",{{{0,0,0},{255,255,255}}});
        Tga(directory/"opaque.tga",{{{255,255,255},{255,255,255}}});
        if(const auto model=Load(Fbx(directory,"cutout"))) {
            const auto* material=Material(*model);Expect(material&&material->alphaMode==Model::AlphaMode::Blend,"Physical raw cutout preserves continuous opacity in the blend material");
            if(material&&material->textures[0]>=0) {
                const auto& image=model->textures[material->textures[0]];
                Expect(Near(Sample(image,3),0)&&Near(Sample(image,7),1),"Black/white cutout survives as transparent/opaque albedo alpha");
                Expect(Near(Sample(image,0),20/255.f)&&Near(Sample(image,1),40/255.f)&&Near(Sample(image,4),80/255.f),"Ordinary Physical base-color RGB is preserved");
                Expect(Near(material->baseColor.r,.8f)&&Near(material->baseColor.g,.6f),"Existing standard base-color factors are not replaced");
                Expect(Near(material->roughness,.6f)&&Near(material->metallic,0),"Ordinary FBX standard factors ignore raw-looking Physical fields without the exact material signature");
                Expect(Near(material->uvTransform.x,2)&&Near(material->uvTransform.y,3)&&Near(material->uvTransform.z,.25f)&&Near(material->uvTransform.w,-.5f)&&
                    !HasWarning(*model,"different per-texture UV"),"Raw UV transform receives exactly the same FlipUVs conversion as standard base color");
            }else Expect(false,"Cutout material has combined albedo pixels");
            Expect(HasWarning(*model,"amount/channel/output"),"Uninterpreted Max output/amount semantics produce an explicit warning");
        }
        if(const auto model=Load(Fbx(directory,"standard-priority",true))) {
            const auto* material=Material(*model);Expect(material&&material->alphaMode==Model::AlphaMode::Opaque&&!HasWarning(*model,"Imported Max Physical cutout"),"Standard OPACITY takes precedence over raw Physical cutout");
        }
        if(const auto model=Load(Fbx(directory,"disabled",false,0))) {
            const auto* material=Material(*model);Expect(material&&material->alphaMode==Model::AlphaMode::Opaque,"Explicit disabled cutout flag is respected");
        }
        if(const auto model=Load(Fbx(directory,"missing-enable",false,1,true))) {
            const auto* material=Material(*model);Expect(material&&material->alphaMode==Model::AlphaMode::Blend&&HasWarning(*model,"no exported enable flag"),"Missing enable metadata uses connected continuous cutout and reports the limitation");
        }
        if(const auto model=Load(Fbx(directory,"unsupported-uv",false,1,false,1))) {
            const auto* material=Material(*model);Expect(material&&material->alphaMode==Model::AlphaMode::Opaque&&HasWarning(*model,"Only UV channel 0"),"Nonzero raw cutout UV channel is rejected explicitly");
        }
        if(const auto model=Load(Fbx(directory,"physical-glossiness",false,1,false,0,true,1))) {
            const auto* material=Material(*model);Expect(material&&Near(material->roughness,.3f)&&Near(material->metallic,.2f),
                "Signed Max Physical roughness 0.7 with inversion 1 becomes 0.3; authored metalness remains explicit");
        }
        if(const auto model=Load(Fbx(directory,"physical-roughness",false,1,false,0,true,0))) {
            const auto* material=Material(*model);Expect(material&&Near(material->roughness,.7f),"Signed Max Physical roughness 0.7 with inversion 0 remains 0.7");
        }
        if(const auto model=Load(Fbx(directory,"physical-missing-inversion",false,1,false,0,true,1,true))) {
            const auto* material=Material(*model);Expect(material&&Near(material->roughness,1-std::sqrt(8.f)/10)&&HasWarning(*model,"roughness_inv flag"),
                "Missing Physical roughness inversion flag preserves standard factor and reports uncertainty");
        }
        if(const auto model=Load(Fbx(directory,"physical-invalid-inversion",false,1,false,0,true,2))) {
            const auto* material=Material(*model);Expect(material&&Near(material->roughness,1-std::sqrt(8.f)/10)&&HasWarning(*model,"roughness_inv flag"),
                "Unknown Physical roughness inversion flag preserves standard factor and reports uncertainty");
        }
        Tga(directory/"cutout.tga",{{{128,128,128},{255,255,255}}});
        if(const auto model=Load(Fbx(directory,"byte-exact-midgray"))) {
            const auto* material=Material(*model);
            if(material&&material->textures[0]>=0) {
                const auto& image=model->textures[material->textures[0]];
                Expect(image.rgba32f.empty()&&image.rgba==std::vector<std::uint8_t>({20,40,60,128,80,100,120,255})&&
                    image.hasAlpha&&!image.linearColor&&material->alphaMode==Model::AlphaMode::Blend,
                    "Opaque RGB8 plus gray128/white255 mask preserves continuous blend opacity, exact RGB/alpha bytes and color flags in RGBA8");
            }else Expect(false,"Midgray cutout has combined texture");
        }
        if(const auto model=Load(Fbx(directory,"byte-exact-no-albedo",false,1,false,0,false,1,false,""))) {
            const auto* material=Material(*model);
            if(material&&material->textures[0]>=0) {
                const auto& image=model->textures[material->textures[0]];
                Expect(image.rgba32f.empty()&&image.rgba==std::vector<std::uint8_t>({255,255,255,128,255,255,255,255})&&
                    Near(material->uvTransform.x,2)&&Near(material->uvTransform.w,-.5f),
                    "Opacity-only source uses exact white RGBA8 and retains the mask UV transform");
            }else Expect(false,"Opacity-only cutout has combined texture");
        }
        TgaAlpha(directory/"base.tga");
        if(const auto model=Load(Fbx(directory,"partial-alpha-float"))) {
            const auto* material=Material(*model);
            if(material&&material->textures[0]>=0) {
                const auto& image=model->textures[material->textures[0]];const float expected=(128/255.f)*(128/255.f);
                Expect(image.rgba.empty()&&!image.rgba32f.empty()&&Near(Sample(image,3),expected)&&Near(Sample(image,0),20/255.f),
                    "Nonopaque 128 alpha multiplied by mask128 keeps its unquantized float32 product and RGB");
            }else Expect(false,"Partial-alpha cutout has combined texture");
        }
        Png16(directory/"base16.png");
        if(const auto model=Load(Fbx(directory,"sixteen-bit-base",false,1,false,0,false,1,false,"base16.png"))) {
            const auto* material=Material(*model);
            if(material&&material->textures[0]>=0) {
                const auto& image=model->textures[material->textures[0]];
                Expect(image.rgba.empty()&&!image.rgba32f.empty()&&Near(Sample(image,0),0x1234/65535.f)&&Near(Sample(image,3),128/255.f)&&!image.linearColor,
                    "RGB16 base source retains every 16-bit sample in float32 despite opaque source alpha");
            }else Expect(false,"RGB16 cutout has combined texture");
        }
        Hdr(directory/"base.hdr");
        if(const auto model=Load(Fbx(directory,"hdr-base",false,1,false,0,false,1,false,"base.hdr"))) {
            const auto* material=Material(*model);
            if(material&&material->textures[0]>=0) {
                const auto& image=model->textures[material->textures[0]];
                Expect(image.rgba.empty()&&!image.rgba32f.empty()&&Near(Sample(image,0),2)&&Near(Sample(image,3),128/255.f)&&image.linearColor,
                    "HDR base values above one and linear color flags survive opacity composition in float32");
            }else Expect(false,"HDR cutout has combined texture");
        }
        Tga(directory/"base.tga",{{{20,40,60},{80,100,120}}});Png16(directory/"mask16.png");
        if(const auto model=Load(Fbx(directory,"sixteen-bit-mask",false,1,false,0,false,1,false,"base.tga","mask16.png"))) {
            const auto* material=Material(*model);
            if(material&&material->textures[0]>=0) {
                const auto& image=model->textures[material->textures[0]];
                Expect(image.rgba.empty()&&!image.rgba32f.empty()&&Near(Sample(image,3),0x1234/65535.f)&&Near(Sample(image,7),0xdef0/65535.f),
                    "RGB16 mask retains unquantized opacity samples even with opaque RGB8 base");
            }else Expect(false,"RGB16 mask has combined texture");
        }
        Tga(directory/"normal.tga",{{{128,128,255},{128,128,255}}});
        for(const auto name:{"metal.tga","rough.tga","ao.tga","emission.tga"})
            Tga(directory/name,{{{64,128,192},{128,192,64}}});
        if(const auto model=Load(Fbx(directory,"shared-and-derived-slots",false,1,false,0,false,1,false,"base.tga","cutout.tga",true))) {
            const auto* derived=Material(*model);
            const auto plain=std::find_if(model->materials.begin(),model->materials.end(),[](const auto& material){return material.name=="PlainShared";});
            bool references=derived&&plain!=model->materials.end()&&model->meshes.size()==2;
            std::vector<bool> used(model->textures.size(),false);
            for(const auto& material:model->materials)for(const auto index:material.textures) {
                references&=index>=-1&&(index<0||static_cast<std::size_t>(index)<model->textures.size());
                if(index>=0&&static_cast<std::size_t>(index)<used.size())used[index]=true;
            }
            Expect(references&&std::all_of(used.begin(),used.end(),[](bool value){return value;}),
                "All eight slots remap to valid retained textures with no unreferenced decoded buffers");
            if(references) {
                bool slots=derived->textures[5]==-1&&derived->textures[6]==-1;
                for(const unsigned slot:{0,1,2,3,4,7})slots&=derived->textures[slot]>=0;
                Expect(slots&&plain->textures[0]>=0&&plain->textures[0]!=derived->textures[0],
                    "Compaction preserves six active PBR slots, inactive flow/distortion slots, and a second material's shared original base image");
                if(slots&&plain->textures[0]>=0) {
                    const auto& original=model->textures[plain->textures[0]];const auto& composed=model->textures[derived->textures[0]];
                    const auto& normal=model->textures[derived->textures[1]];
                    Expect(original.rgba==std::vector<std::uint8_t>({20,40,60,255,80,100,120,255})&&
                        composed.rgba==std::vector<std::uint8_t>({20,40,60,128,80,100,120,255})&&Near(Sample(normal,2),1),
                        "Shared original/albedo-with-cutout/normal pixels remain correctly associated after stable remapping");
                    Expect(Near(Sample(model->textures[derived->textures[2]],0),64/255.f)&&
                        Near(Sample(model->textures[derived->textures[3]],0),64/255.f)&&
                        Near(Sample(model->textures[derived->textures[4]],0),64/255.f)&&
                        model->textures[derived->textures[7]].linearColor,
                        "Metalness, roughness, AO scalar data and linear emission retain their individual PBR slots");
                }
            }
            Expect(HasWarning(*model,"unreferenced decoded"),"Runtime compaction reports released source images without changing exported source files");
        }
        Tga(directory/"cutout.tga",{{{64,64,64},{128,128,128}}});
        if(const auto model=Load(Fbx(directory,"low-coverage-continuous-cutout"))) {
            const auto* material=Material(*model);
            if(material&&material->textures[0]>=0) {
                const auto& image=model->textures[material->textures[0]];
                Expect(material->alphaMode==Model::AlphaMode::Blend&&Near(Sample(image,3),64/255.f)&&
                    Near(Sample(image,7),128/255.f)&&Sample(image,3)<material->alphaCutoff,
                    "Sparse Physical cutout keeps positive opacity below the mask cutoff and middle gray in the blend pipeline");
            }else Expect(false,"Low-coverage cutout has continuous opacity pixels");
        }
        Tga(directory/"opaque.tga",{{{64,64,64},{128,128,128}}});
        if(const auto model=Load(Fbx(directory,"standard-opacity-policy",true))) {
            const auto* material=Material(*model);
            Expect(material&&material->alphaMode==Model::AlphaMode::Mask&&!HasWarning(*model,"Imported Max Physical cutout"),
                "Existing standard OPACITY alpha-mask policy remains unchanged and precedes the Physical fallback");
        }
    }catch(const std::exception& error){std::cout<<"Unexpected FBX material test failure: "<<error.what()<<'\n';++failures;}
    std::cout<<"FBX Physical material: "<<failures<<" failure(s)\n";return failures?1:0;
}
