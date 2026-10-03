#include "IWanna/Public/sprite_renderer.h"
#include "IWanna/Public/camera_math.h"
#include "IWanna/Public/alpha_mask.h"
#include "IWanna/Public/stroke_font.h"
#include <stb_image.h>
#include <cmath>
#include <cstring>
#include <stdexcept>

namespace IWanna {
namespace {
struct Tile {std::string name;int frame,width,height,x=0,y=0;std::vector<unsigned char> pixels;};
constexpr int Border=8;
}
void SpriteRenderer::Initialize(const std::filesystem::path& assets,const std::vector<EntityView>& entities,const std::vector<Sprite>& animations) {
    static_assert(sizeof(Vertex)==9*sizeof(float),"RHI vertex layout must remain tightly packed");
    std::map<std::string,const Sprite*> definitions;
    auto registerSprite=[&](const Sprite& sprite) {
        if(sprite.columns<1||sprite.rows<1||sprite.columns>64||sprite.rows>64||sprite.columns*sprite.rows>1024||sprite.cellWidth<0||sprite.cellHeight<0||sprite.frames.empty()||sprite.frames.size()>1024)
            throw std::runtime_error("Invalid animation layout: "+sprite.image);
        for(int frame:sprite.frames)if(frame<0||frame>=sprite.columns*sprite.rows)throw std::runtime_error("Invalid animation frame: "+sprite.image);
        auto [found,inserted]=definitions.emplace(sprite.image,&sprite);
        const auto& previous=*found->second;
        if(!inserted&&(previous.columns!=sprite.columns||previous.rows!=sprite.rows||previous.cellWidth!=sprite.cellWidth||previous.cellHeight!=sprite.cellHeight))
            throw std::runtime_error("Conflicting animation layouts for image: "+sprite.image);
    };
    for(const auto& view:entities)registerSprite(*view.sprite);
    for(const auto& sprite:animations)registerSprite(sprite);
    std::vector<Tile> tiles;
    for(auto [name,s]:definitions) {
        int w,h,channels;auto path=assets/"images"/name;
        auto data=stbi_load(path.string().c_str(),&w,&h,&channels,4);
        if(!data) throw std::runtime_error("Image decode failed: "+path.string());
        std::unique_ptr<unsigned char,decltype(&stbi_image_free)> owner(data,stbi_image_free);
        int cw=s->cellWidth?s->cellWidth:w/s->columns,ch=s->cellHeight?s->cellHeight:h/s->rows;
        if(cw<1 || ch<1 || cw>w/s->columns || ch>h/s->rows) throw std::runtime_error("Invalid animation cells: "+name);
        textures_[name].frames.resize(s->columns*s->rows);
        for(int i=0;i<s->columns*s->rows;++i) {
            Tile tile{name,i,cw,ch};tile.pixels.resize(size_t(cw)*ch*4);
            for(int y=0;y<ch;++y) std::memcpy(tile.pixels.data()+size_t(y)*cw*4,data+((size_t(i/s->columns*ch+y)*w)+(i%s->columns*cw))*4,size_t(cw)*4);
            tiles.push_back(std::move(tile));
        }
    }
    textures_["white"].frames.resize(1);tiles.push_back({"white",0,1,1,0,0,{255,255,255,255}});
    textures_["font"].frames.resize(50);
    for(int i=0;i<50;++i) tiles.push_back({"font",i,48,64,0,0,GlyphSdf(i)});
    std::stable_sort(tiles.begin(),tiles.end(),[](const auto& a,const auto& b){return a.height>b.height;});
    int atlasWidth=2048,atlasHeight=0;
    for(;;) {
        int x=0,y=0,row=0;bool fits=true;
        for(auto& tile:tiles) {
            int w=tile.width+2*Border,h=tile.height+2*Border;if(w>atlasWidth) {fits=false;break;}
            if(x+w>atlasWidth) {x=0;y+=row;row=0;}
            tile.x=x+Border;tile.y=y+Border;x+=w;row=std::max(row,h);
        }
        atlasHeight=1;while(atlasHeight<y+row) atlasHeight*=2;
        if(fits && atlasHeight<=4096) break;
        atlasWidth*=2;if(atlasWidth>4096) throw std::runtime_error("Sprite atlas exceeds 4096; split the asset set");
    }
    std::vector<unsigned char> pixels(size_t(atlasWidth)*atlasHeight*4,0);
    for(const auto& tile:tiles) {
        for(int y=-Border;y<tile.height+Border;++y) for(int x=-Border;x<tile.width+Border;++x) {
            size_t src=(size_t(std::clamp(y,0,tile.height-1))*tile.width+std::clamp(x,0,tile.width-1))*4;
            size_t dst=(size_t(tile.y+y)*atlasWidth+tile.x+x)*4;std::memcpy(pixels.data()+dst,tile.pixels.data()+src,4);
        }
        textures_[tile.name].frames[tile.frame]={float(tile.x)/atlasWidth,float(tile.y)/atlasHeight,float(tile.width)/atlasWidth,float(tile.height)/atlasHeight};
    }
    Render::CreateRHITextureSpec texture;texture.width=atlasWidth;texture.height=atlasHeight;texture.data=pixels.data();
    texture.filterMode=Render::RHIFilterMode::Linear;texture.mipmapMode=Render::RHIMipmapMode::Linear;texture.addressMode=Render::RHIAddressMode::ClampToEdge;
    device_->async_CreateTexture(texture,[this](auto handle){atlas_=handle;});
    Vertex initial[3]{};
    Render::CreateMeshBufferDesc mesh;mesh.vertexLayout.typeSlots={Render::VertexFieldType::Vec2,Render::VertexFieldType::Vec2,Render::VertexFieldType::Vec4,Render::VertexFieldType::Float};
    mesh.vertexCount=3;mesh.vertexData=initial;mesh.vertexByteSize=sizeof(initial);mesh.usage=Render::BufferUsage::Stream;
    device_->async_CreateMeshBuffer(mesh,[this](auto h){mesh_=h;});
    const char* vs=R"(#version 450 core
layout(location=0) in vec2 position;layout(location=1) in vec2 texcoord;
layout(location=2) in vec4 color;layout(location=3) in float sdf;
out vec2 vUV;out vec4 vColor;flat out float vSdf;
void main(){gl_Position=vec4(position.x/75.0,-position.y/42.1875,0,1);vUV=texcoord;vColor=color;vSdf=sdf;})";
    const char* fs=R"(#version 450 core
layout(binding=0) uniform sampler2D atlas;
in vec2 vUV;in vec4 vColor;flat in float vSdf;out vec4 color;
void main(){
vec2 dx=dFdx(vUV)*textureSize(atlas,0),dy=dFdy(vUV)*textureSize(atlas,0);
float lod=clamp(0.5*log2(max(max(dot(dx,dx),dot(dy,dy)),1.0)),0.0,2.0);
vec4 texel=textureLod(atlas,vUV,vSdf>0.5?0.0:lod);
if(vSdf< -0.5)texel=texelFetch(atlas,ivec2(vUV*textureSize(atlas,0)),0);
if(vSdf>0.5){float edge=max(fwidth(texel.a),0.005);texel.a=smoothstep(0.5-edge,0.5+edge,texel.a);}
color=texel*vColor;})";
    device_->async_CreateShaderSource({Render::ShaderSourceType::Vertex,vs,std::strlen(vs)},[this,fs](auto vertex){
        device_->async_CreateShaderSource({Render::ShaderSourceType::Fragment,fs,std::strlen(fs)},[this,vertex](auto fragment){
            Render::CreateGraphicShaderDesc d{};d.vertexShaderSource=vertex;d.fragmentShaderSource=fragment;
            device_->async_CreateGraphicShader(d,[this](auto shader){
                Render::CreatePipelineDesc p;p.spec.shaderProgram=shader;p.spec.cullMode=Render::CullMode::None;
                p.spec.depthTest=false;p.spec.depthWrite=false;p.spec.blendEnable=true;p.spec.blendMode=Render::BlendMode::Alpha;
                device_->async_CreatePipeline(p,[this](auto h){pipeline_=h;});
            });
        });
    });
    vertices_.reserve(4096);indices_.reserve(6144);
}
bool SpriteRenderer::Ready() const {return mesh_.IsValid() && atlas_.IsValid() && pipeline_.IsValid();}
void SpriteRenderer::Quad(glm::vec2 pos,glm::vec2 size,float angle,glm::vec4 uv,glm::vec4 color,float sdf) {
    const float cs=std::cos(angle),sn=std::sin(angle);uint32_t first=uint32_t(vertices_.size());
    for(auto p:{glm::vec2(0,0),glm::vec2(1,0),glm::vec2(1,1),glm::vec2(0,1)}) {
        auto local=(p-.5f)*size;glm::vec2 world=pos+glm::vec2(cs*local.x-sn*local.y,sn*local.x+cs*local.y);
        vertices_.push_back({world,glm::vec2(uv)+p*glm::vec2(uv.z,uv.w),color,sdf});
    }
    for(uint32_t i:{0,1,2,0,2,3}) indices_.push_back(first+i);
}
void SpriteRenderer::Draw(Render::RHIFrameEncoder&,const std::vector<DrawSprite>& sprites,bool clear,const Camera* camera) {
    if(clear){vertices_.clear();indices_.clear();}
    for(const auto& draw:sprites) {
        auto t=camera?CameraProject(*camera,draw.transform):draw.transform;const auto& s=draw.sprite;
        float radius=.5f*glm::length(t.size);
        if(t.position.x+radius < -75 || t.position.x-radius>75 || t.position.y+radius < -42.1875f || t.position.y-radius>42.1875f) continue;
        int index=AnimationFrame(s);
        auto uv=textures_.at(s.image).frames.at(index);
        if(s.flipX) {uv.x+=uv.z;uv.z=-uv.z;}if(s.flipY) {uv.y+=uv.w;uv.w=-uv.w;}
        const float angle=t.rotation*.01745329252f;
        for(int repeat=0;repeat<s.repeatX;++repeat) {
            auto size=t.size;size.x/=s.repeatX;
            float offset=-t.size.x*.5f+(repeat+.5f)*size.x;
            auto position=t.position+glm::vec2(std::cos(angle),std::sin(angle))*offset;
            Quad(position,size,angle,uv,glm::vec4(1,1,1,std::clamp(s.opacity,0.f,1.f)),s.pixelArt?-1.f:0.f);
        }
    }
}
void SpriteRenderer::Rect(Render::RHIFrameEncoder&,glm::vec2 pos,glm::vec2 size,glm::vec4 color) {Quad(pos,size,0,textures_.at("white").frames[0],color);}
void SpriteRenderer::Line(Render::RHIFrameEncoder&,glm::vec2 a,glm::vec2 b,float thickness,glm::vec4 color){
    auto delta=b-a;float length=glm::length(delta);if(length<1e-5f)return;
    Quad((a+b)*.5f,{length,thickness},std::atan2(delta.y,delta.x),textures_.at("white").frames[0],color);
}
void SpriteRenderer::Text(Render::RHIFrameEncoder&,const std::string& text,glm::vec2 pos,float pixel,glm::vec4 color) {
    const float start=pos.x;
    for(char ch:text) {
        if(ch=='\n') {pos.x=start;pos.y+=9*pixel;continue;}
        if(ch>='a'&&ch<='z')ch=char(ch-'a'+'A');
        const std::string punctuation=".-_:/=+[]\",!?*";auto pi=punctuation.find(ch);
        int g=ch>='0'&&ch<='9'?ch-'0':ch>='A'&&ch<='Z'?ch-'A'+10:pi!=std::string::npos?36+int(pi):-1;
        if(g>=0) Quad(pos+glm::vec2(2,3)*pixel,glm::vec2(6,8)*pixel,0,textures_.at("font").frames[g],color,1);
        pos.x+=6*pixel;
    }
}
void SpriteRenderer::TextWorld(Render::RHIFrameEncoder& frame,const std::string& text,glm::vec2 pos,float pixel,glm::vec4 color,const Camera& camera) {
    Text(frame,text,CameraProject(camera,pos),pixel*camera.zoom,color);
}
void SpriteRenderer::Flush(Render::RHIFrameEncoder& frame) {
    frame.BindPipeline(pipeline_);frame.BindMesh(mesh_);frame.BindTexture(atlas_,0);
    Render::RHICommand::DrawIndexed draw;draw.indexCount=uint32_t(indices_.size());frame.DrawIndexed(draw);
}
void SpriteRenderer::Submit(ObjectWeakPtr<Render::RHIFrameCommandBuffer> frame,std::function<void()> completed) {
    Render::UpdateMeshBufferDesc update;update.vertexCount=uint32_t(vertices_.size());update.vertexData=vertices_.data();update.vertexByteSize=vertices_.size()*sizeof(Vertex);
    update.indexCount=uint32_t(indices_.size());update.indexData=indices_.data();update.indexByteSize=indices_.size()*sizeof(uint32_t);
    // The upload callback is an explicit resource -> frame dependency.
    device_->async_UpdateMeshBuffer(mesh_,update,[this,frame,completed=std::move(completed)](bool ok){
        if(!ok) {error_="Sprite batch upload failed";completed();return;}
        if(!device_->async_SubmitFrameCommands(frame,completed)) {error_="Sprite batch submission failed";completed();}
    });
}
void SpriteRenderer::Shutdown() {
    if(!device_) return;
    if(atlas_.IsValid()) device_->async_DeleteTexture(atlas_);
    if(mesh_.IsValid()) device_->async_DeleteVertexBuffer(mesh_);
    if(pipeline_.IsValid()) device_->async_DeletePipeline(pipeline_);
    textures_.clear();
}
}

