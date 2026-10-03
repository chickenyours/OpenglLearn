#include "PixelSandbox/Public/sandbox_renderer.h"
#include "Render/Private/rhi_device.h"
#include "Render/Public/Sprite/sprite_batch.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace PixelSandbox {
namespace {
constexpr float CW = 1280, CH = 800;
const glm::vec4 Background{.032f,.042f,.062f,1}, Panel{.052f,.069f,.094f,1};
const glm::vec4 Text{.85f,.90f,.97f,1}, Muted{.40f,.49f,.61f,1}, Accent{.38f,.89f,.75f,1};
struct Rect { float x,y,w,h; bool Contains(double px,double py) const { return px>=x&&py>=y&&px<x+w&&py<y+h; } };
struct Viewport { int x,y,w,h; };
Viewport Fit(int width,int height) {
    const double scale=std::min(width/double(CW),height/double(CH));
    const int w=std::max(1,int(CW*scale)),h=std::max(1,int(CH*scale));
    return {(width-w)/2,(height-h)/2,w,h};
}
Rect Grid(int columns,int rows) {
    if(columns<1||rows<1)return {};
    const float scale=std::min(928.f/columns,580.f/rows);
    return {24+(928-columns*scale)*.5f,122+(580-rows*scale)*.5f,columns*scale,rows*scale};
}
glm::vec4 Color(Material material) {
    const auto c=Info(material).color;
    return {float((c>>16)&255)/255,float((c>>8)&255)/255,float(c&255)/255,1};
}
std::string Number(double value,int decimals=1) {
    char result[64];std::snprintf(result,sizeof(result),"%.*f",decimals,value);return result;
}
std::string Count(std::size_t value) { return value>=10000?Number(value/1000.,0)+"K":std::to_string(value); }
struct PaletteEntry { Material material; Rect bounds; };
Rect CategoryBounds(int index) { return {996.f+float(index%3)*82,100.f+float(index/3)*27,76,23}; }
std::vector<PaletteEntry> Palette(PaletteGroup group,int page) {
    std::vector<PaletteEntry> entries;
    const auto materials=PaletteMaterials(group,page);
    for(std::size_t i=0;i<materials.size();++i)
        entries.push_back({materials[i],{996.f+float(i%2)*124,162.f+float(i/2)*34,116,29}});
    return entries;
}
struct Button { Rect bounds; const char* label; UiHit::Kind kind; int value=0; };
const std::array<Button,18> Buttons{{
    {{24,78,110,25},"1 FALLING",UiHit::Kind::Preset,int(Preset::Falling)},
    {{144,78,110,25},"2 VOLCANO",UiHit::Kind::Preset,int(Preset::Volcano)},
    {{264,78,110,25},"3 CIRCUIT",UiHit::Kind::Preset,int(Preset::Circuit)},
    {{384,78,110,25},"4 GARDEN",UiHit::Kind::Preset,int(Preset::Garden)},
    {{504,78,98,25},"5 ELEMENTS",UiHit::Kind::Preset,int(Preset::Elements)},
    {{612,78,88,25},"PAUSE",UiHit::Kind::Pause},
    {{710,78,66,25},"STEP",UiHit::Kind::Step},
    {{786,78,66,25},"CLEAR",UiHit::Kind::Clear},
    {{862,78,90,25},"HEAT VIEW",UiHit::Kind::HeatMap},
    {{996,590,44,26},"DRAW",UiHit::Kind::Tool,int(BrushTool::Paint)},
    {{1046,590,44,26},"ERASE",UiHit::Kind::Tool,int(BrushTool::Erase)},
    {{1096,590,44,26},"HEAT",UiHit::Kind::Tool,int(BrushTool::Heat)},
    {{1146,590,44,26},"COOL",UiHit::Kind::Tool,int(BrushTool::Cool)},
    {{1196,590,44,26},"AIR",UiHit::Kind::Tool,int(BrushTool::Pressure)},
    {{996,710,116,28},"F5 SAVE",UiHit::Kind::Save},
    {{1120,710,116,28},"F9 LOAD",UiHit::Kind::Load},
    {{996,514,62,25},"< PREV",UiHit::Kind::Page,-1},
    {{1174,514,62,25},"NEXT >",UiHit::Kind::Page,1}
}};
Render::VertexLayout Layout() { Render::VertexLayout result;result.typeSlots={Render::VertexFieldType::Vec2,Render::VertexFieldType::Vec2};return result; }
constexpr char VertexShader[]=R"GLSL(#version 450 core
layout(location=0) in vec2 position;
layout(location=1) in vec2 uv;
out vec2 vUV;
void main(){gl_Position=vec4(position.x/640.0-1.0,1.0-position.y/400.0,0,1);vUV=uv;}
)GLSL";
constexpr char FragmentShader[]=R"GLSL(#version 450 core
layout(binding=0) uniform sampler2D canvas;
in vec2 vUV;out vec4 color;
void main(){color=texture(canvas,vUV);}
)GLSL";
}

struct SandboxRenderer::State {
    ObjectWeakPtr<Render::RHIDevice> device;
    Render::SpriteBatch2D hud;
    Render::RenderResourceHandle<Render::RHITextureSpec> texture;
    Render::RenderResourceHandle<Render::VertexBufferSpec> mesh;
    Render::RenderResourceHandle<Render::PipelineSpec> pipeline;
    Render::RenderResourceHandle<Render::ShaderProgramSpec> program;
    Render::RenderResourceHandle<Render::ShaderSourceSpec> vertex,fragment;
    int columns=0,rows=0;
    bool initialized=false,closed=false,busy=false,uploadedHeat=false;
    std::uint64_t completed=0,uploadedRevision=std::numeric_limits<std::uint64_t>::max();
    std::string error;
    explicit State(ObjectWeakPtr<Render::RHIDevice> value):device(value),hud(value){}
    ~State() {
        hud.Shutdown();
        if(!device||!device->IsRunning())return;
        if(pipeline.IsValid())device->async_DeletePipeline(pipeline);
        if(mesh.IsValid())device->async_DeleteVertexBuffer(mesh);
        if(texture.IsValid())device->async_DeleteTexture(texture);
        if(program.IsValid())device->async_DeleteShaderProgram(program);
        if(vertex.IsValid())device->async_DeleteShaderSource(vertex);
        if(fragment.IsValid())device->async_DeleteShaderSource(fragment);
    }
    bool Active()const{return !closed&&device&&device->IsRunning();}
    void Fail(const char* message){if(error.empty())error=message;}
    void Box(Rect r,glm::vec4 color){hud.Rect({r.x+r.w*.5f-CW*.5f,CH*.5f-r.y-r.h*.5f},{r.w,r.h},color);}
    void Label(std::string_view text,float x,float y,float pixel,glm::vec4 color=Text){hud.Text(text,{x-CW*.5f,CH*.5f-y},pixel,color);}
    void BuildHud(const SandboxModule& world,const ViewState& view) {
        hud.Begin(CW*.5f,CH*.5f);
        Box({978,24,278,752},Panel);
        Box({24,26,4,38},Accent);
        Label("PIXEL / SANDBOX",42,28,3.5f);
        Label("MATTER. HEAT. MOTION.",43,58,1.25f,Muted);
        Label(world.IsPaused()?"PAUSED":"SIMULATING",777,35,1.8f,world.IsPaused()?glm::vec4(1,.65f,.28f,1):Accent);
        Label(std::to_string(columns)+" X "+std::to_string(rows)+"    SEED "+std::to_string(world.Config().seed),602,58,1.2f,Muted);
        for(const auto& button:Buttons){
            const bool active=(button.kind==UiHit::Kind::Tool&&button.value==int(view.tool))||
                (button.kind==UiHit::Kind::HeatMap&&view.heatMap)||(button.kind==UiHit::Kind::Pause&&world.IsPaused());
            Box(button.bounds,active?glm::vec4(.12f,.29f,.28f,1):glm::vec4(.086f,.113f,.153f,1));
            const char* label=button.kind==UiHit::Kind::Pause&&world.IsPaused()?"RESUME":button.label;
            const float pixel=button.kind==UiHit::Kind::Tool?1.1f:1.25f;
            Label(label,button.bounds.x+(button.bounds.w-std::strlen(label)*6*pixel)*.5f,button.bounds.y+9,pixel,active?Accent:Text);
        }
        const auto bounds=Grid(columns,rows);
        const glm::vec4 border{.17f,.22f,.28f,1};
        Box({bounds.x-1,bounds.y-1,bounds.w+2,1},border);Box({bounds.x-1,bounds.y+bounds.h,bounds.w+2,1},border);
        Box({bounds.x-1,bounds.y,1,bounds.h},border);Box({bounds.x+bounds.w,bounds.y,1,bounds.h},border);
        Label("PARTICLE FIELD",24,109,1.05f,Muted);
        Label("TICK "+std::to_string(world.Stats().tick),795,109,1.05f,Muted);
        Label("MATERIAL LIBRARY",996,43,1.85f);
        Label("SELECT A MATERIAL, THEN DRAW",996,72,1.05f,Muted);
        for(int i=0;i<int(PaletteGroup::Count);++i){
            const auto tab=CategoryBounds(i);
            const bool active=int(view.paletteGroup)==i;
            Box(tab,active?glm::vec4(.12f,.29f,.28f,1):glm::vec4(.086f,.113f,.153f,1));
            Label(PaletteGroupNames[i],tab.x+8,tab.y+8,1.15f,active?Accent:Text);
        }
        for(const auto& entry:Palette(view.paletteGroup,view.palettePage)){
            const bool active=entry.material==view.selected;
            Box(entry.bounds,active?glm::vec4(.13f,.23f,.26f,1):glm::vec4(.071f,.090f,.122f,1));
            if(active)Box({entry.bounds.x,entry.bounds.y,2,entry.bounds.h},Accent);
            Box({entry.bounds.x+8,entry.bounds.y+8,8,8},Color(entry.material));
            Label(Info(entry.material).name,entry.bounds.x+23,entry.bounds.y+8,1.03f,active?Text:glm::vec4(.68f,.75f,.83f,1));
            const auto count=Count(world.Stats().counts[std::size_t(entry.material)]);
            Label(count,entry.bounds.x+entry.bounds.w-4-count.size()*5.1f,entry.bounds.y+20,.85f,Muted);
        }
        Label(std::to_string(view.palettePage+1)+" / "+std::to_string(PalettePages(view.paletteGroup)),1090,523,1.3f,Muted);
        Label(MaterialHint(view.selected),996,551,.95f,Muted);
        Label("BRUSH TOOLS",996,569,1.1f,Muted);
        Label("RADIUS "+std::to_string(view.radius)+" PX",996,633,1.3f,Text);
        Label(view.replace?"REPLACE ON":"EMPTY ONLY",1127,635,1.0f,view.replace?Accent:Muted);
        Label(std::string("SELECTED: ")+Info(view.selected).name,996,661,1.1f,Color(view.selected));
        Label("WHEEL SIZE / SHIFT REPLACE",996,683,1.05f,Muted);
        Label("LMB PAINT  RMB ERASE  MMB PICK",996,755,.95f,Muted);
        const auto& stats=world.Stats();
        Label(Count(stats.particles)+" PARTICLES",24,719,1.35f,Text);
        Label("ACTIVE "+std::to_string(stats.activeChunks)+" / "+std::to_string(stats.totalChunks)+" CHUNKS",250,719,1.2f,Muted);
        Label("STEP "+Number(stats.totalMs,2)+" MS",714,719,1.3f,Accent);
        std::string inspect="DRAW TO BUILD YOUR OWN WORLD";
        if(world.InBounds(view.hoverX,view.hoverY)){
            const auto& cell=world.At(view.hoverX,view.hoverY);
            inspect=std::string(Info(cell.material).name)+"  "+Number(cell.temperature,1)+" C  PRESSURE "+Number(world.PressureAt(view.hoverX,view.hoverY),2);
            if(cell.charge)inspect+="  CHARGE "+std::to_string(cell.charge);
            if(cell.material==Material::Clone)inspect+=std::string("  COPIES: ")+Info(Material(cell.flags>>8)).name;
            const float scale=bounds.w/columns,cx=bounds.x+(view.hoverX+.5f)*scale,cy=bounds.y+(view.hoverY+.5f)*scale;
            const float radius=(view.radius+.5f)*scale;
            for(int i=0;i<48;++i){
                const float a=i*6.2831853f/48,b=(i+1)*6.2831853f/48;
                const float x1=cx+std::cos(a)*radius,y1=cy+std::sin(a)*radius,x2=cx+std::cos(b)*radius,y2=cy+std::sin(b)*radius;
                if(!bounds.Contains(x1,y1)||!bounds.Contains(x2,y2))continue;
                hud.Rect({(x1+x2)*.5f-CW*.5f,CH*.5f-(y1+y2)*.5f},{std::hypot(x2-x1,y2-y1),.9f},{.9f,.94f,1,.8f},std::atan2(y1-y2,x2-x1));
            }
        }
        Label(view.status.empty()?inspect:view.status.substr(0,105),24,748,1.2f,view.status.empty()?Muted:Accent);
        Label("SPACE PAUSE   N STEP   C CLEAR   T HEAT VIEW   H/J HEAT/COOL   F5/F9 SAVE/LOAD",24,776,1.02f,Muted);
    }
};

SandboxRenderer::SandboxRenderer(ObjectWeakPtr<Render::RHIDevice> device):state_(std::make_shared<State>(device)){}
SandboxRenderer::~SandboxRenderer(){Shutdown();}
void SandboxRenderer::Initialize(int columns,int rows){
    auto s=state_;if(!s||s->initialized)return;s->initialized=true;s->columns=columns;s->rows=rows;
    if(!s->Active()||columns<1||rows<1){s->Fail("Invalid sandbox renderer device or dimensions");return;}
    if(!s->hud.Initialize({})){s->Fail("HUD initialization failed");return;}
    Render::CreateRHITextureSpec texture;texture.width=columns;texture.height=rows;texture.mipmaps=false;
    texture.filterMode=Render::RHIFilterMode::Nearest;texture.addressMode=Render::RHIAddressMode::ClampToEdge;
    s->device->async_CreateTexture(texture,[s](auto handle){s->texture=handle;if(!handle.IsValid())s->Fail("Sandbox texture creation failed");});
    struct Vertex{glm::vec2 position,uv;};
    const auto b=Grid(columns,rows);
    const Vertex vertices[]={{{b.x,b.y},{0,0}},{{b.x+b.w,b.y},{1,0}},{{b.x+b.w,b.y+b.h},{1,1}},{{b.x,b.y+b.h},{0,1}}};
    const std::uint32_t indices[]={0,1,2,0,2,3};
    Render::CreateMeshBufferDesc mesh;mesh.vertexLayout=Layout();mesh.vertexCount=4;mesh.vertexData=vertices;mesh.vertexByteSize=sizeof(vertices);
    mesh.indexCount=6;mesh.indexData=indices;mesh.indexByteSize=sizeof(indices);
    s->device->async_CreateMeshBuffer(mesh,[s](auto handle){s->mesh=handle;if(!handle.IsValid())s->Fail("Sandbox quad creation failed");});
    s->device->async_CreateShaderSource({Render::ShaderSourceType::Vertex,VertexShader,std::strlen(VertexShader)},[s](auto handle){
        s->vertex=handle;if(!handle.IsValid()){s->Fail("Sandbox vertex shader failed");return;}if(!s->Active())return;
        s->device->async_CreateShaderSource({Render::ShaderSourceType::Fragment,FragmentShader,std::strlen(FragmentShader)},[s](auto handle){
            s->fragment=handle;if(!handle.IsValid()){s->Fail("Sandbox fragment shader failed");return;}if(!s->Active())return;
            Render::CreateGraphicShaderDesc shader;shader.vertexShaderSource=s->vertex;shader.fragmentShaderSource=s->fragment;
            s->device->async_CreateGraphicShader(shader,[s](auto handle){
                s->program=handle;if(!handle.IsValid()){s->Fail("Sandbox shader link failed");return;}if(!s->Active())return;
                Render::CreatePipelineDesc pipeline;pipeline.spec.shaderProgram=handle;pipeline.spec.expectVertexLayout=Layout();
                pipeline.spec.cullMode=Render::CullMode::None;pipeline.spec.depthTest=false;pipeline.spec.depthWrite=false;
                s->device->async_CreatePipeline(pipeline,[s](auto handle){s->pipeline=handle;if(!handle.IsValid())s->Fail("Sandbox pipeline creation failed");});
            });
        });
    });
}
bool SandboxRenderer::Ready()const{return state_&&state_->Active()&&state_->error.empty()&&state_->hud.Ready()&&state_->texture.IsValid()&&state_->mesh.IsValid()&&state_->pipeline.IsValid();}
bool SandboxRenderer::Busy()const{return state_&&state_->busy;}
const std::string& SandboxRenderer::Error()const{static const std::string empty;if(!state_)return empty;return state_->error.empty()?state_->hud.Error():state_->error;}
std::uint64_t SandboxRenderer::CompletedFrames()const{return state_?state_->completed:0;}
bool SandboxRenderer::Draw(const SandboxModule& world,const ViewState& view,int width,int height,bool present){
    if(!Ready()||Busy()||width<1||height<1||!world.IsStarted())return false;
    auto s=state_;if(world.Config().width!=s->columns||world.Config().height!=s->rows){s->Fail("Sandbox dimensions changed after renderer initialization");return false;}
    s->BuildHud(world,view);s->busy=true;
    const auto revision=world.Revision();const bool heat=view.heatMap;
    auto submit=[s,width,height,present,revision,heat](bool success){
        if(!s->Active()){s->busy=false;return;}
        if(!success){s->Fail("Sandbox texture upload failed");s->busy=false;return;}
        s->uploadedRevision=revision;s->uploadedHeat=heat;
        Render::RHICommand::BeginFrame begin;begin.frameIndex=s->completed;begin.framebufferWidth=width;begin.framebufferHeight=height;begin.clearColor=Background;
        auto frame=s->device->BeginFrame(begin);const auto viewport=Fit(width,height);
        Render::RHICommand::DrawIndexed draw;draw.indexCount=6;
        const bool recorded=frame.KeepAlive(s)&&frame.SetViewport({viewport.x,viewport.y,std::uint32_t(viewport.w),std::uint32_t(viewport.h)})&&
            frame.BindPipeline(s->pipeline)&&frame.BindMesh(s->mesh)&&frame.BindTexture(s->texture,0)&&frame.DrawIndexed(draw)&&s->hud.Flush(frame)&&frame.End(present);
        if(!recorded||!s->hud.Submit(frame.GetCommandBuffer(),[s]{s->busy=false;++s->completed;})){
            frame.Cancel();s->busy=false;s->Fail("Sandbox frame submission failed");
        }
    };
    if(revision!=s->uploadedRevision||heat!=s->uploadedHeat){
        const auto pixels=world.Pixels(heat);
        Render::UpdateRHITextureDesc update;update.width=s->columns;update.height=s->rows;update.data=pixels.data();update.byteSize=pixels.size();
        s->device->async_UpdateTexture(s->texture,update,std::move(submit));
    }else submit(true);
    return true;
}
void SandboxRenderer::Shutdown(){if(state_){state_->closed=true;state_.reset();}}
UiHit SandboxRenderer::HitTest(double x,double y,int width,int height,int columns,int rows,PaletteGroup group,int page){
    if(width<1||height<1||columns<1||rows<1||!std::isfinite(x)||!std::isfinite(y))return {};
    const auto viewport=Fit(width,height);const int top=height-viewport.y-viewport.h;
    x=(x-viewport.x)*CW/viewport.w;y=(y-top)*CH/viewport.h;
    for(const auto& button:Buttons)if(button.bounds.Contains(x,y))return {button.kind,-1,-1,button.value};
    for(int i=0;i<int(PaletteGroup::Count);++i)if(CategoryBounds(i).Contains(x,y))return {UiHit::Kind::Category,-1,-1,i};
    for(const auto& entry:Palette(group,page))if(entry.bounds.Contains(x,y))return {UiHit::Kind::Material,-1,-1,int(entry.material)};
    const auto bounds=Grid(columns,rows);
    if(bounds.Contains(x,y))return {UiHit::Kind::Cell,int((x-bounds.x)*columns/bounds.w),int((y-bounds.y)*rows/bounds.h),0};
    return {};
}
}
