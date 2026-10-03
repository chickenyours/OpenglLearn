#include "PixelSandbox/Public/sandbox_module.h"
#include "PixelSandbox/Public/sandbox_renderer.h"
#include "Render/module.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include <stb_image_write.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>

namespace {
using namespace PixelSandbox;
using Clock=std::chrono::steady_clock;
struct Options {
    SimulationConfig config;
    Preset preset=Preset::Falling;
    bool smoke=false,paused=false;
    std::filesystem::path capture,saveFile,loadFile;
};
void Usage(){
    std::cout<<"pixel_sandbox_demo [--width N] [--height N] [--seed N] [--paused]\n"
        "  [--preset empty|falling|volcano|circuit|garden|elements] [--save-file path] [--load path]\n"
        "  [--smoke-test] [--capture file.png|file.ppm]\n"
        "LMB paint / RMB erase / MMB pick / wheel brush / Shift replace\n"
        "Space pause / N step / C clear / 1..5 presets / T heat view / PgUp-PgDn palette\n"
        "B draw / E erase / H heat / J cool / P pressure / F5 save / F9 load / Esc exit\n";
}
std::uint32_t Number(std::string_view text){
    std::uint32_t number=0;const auto parsed=std::from_chars(text.data(),text.data()+text.size(),number);
    if(parsed.ec!=std::errc{}||parsed.ptr!=text.data()+text.size())throw std::invalid_argument("Invalid unsigned number");
    return number;
}
Options Parse(int argc,char**argv){
    Options options;
    options.saveFile=std::filesystem::absolute(argv[0]).parent_path()/"pixel_sandbox.save";
    for(int i=1;i<argc;++i){
        const std::string key=argv[i];
        auto value=[&]()->std::string{if(++i==argc)throw std::invalid_argument("Missing value for "+key);return argv[i];};
        if(key=="--smoke"||key=="--smoke-test")options.smoke=true;
        else if(key=="--paused")options.paused=true;
        else if(key=="--capture"){options.capture=value();options.smoke=true;}
        else if(key=="--save-file")options.saveFile=value();
        else if(key=="--load")options.loadFile=value();
        else if(key=="--seed")options.config.seed=Number(value());
        else if(key=="--width"||key=="--height"){
            const auto n=Number(value());if(n>1024)throw std::invalid_argument("Demo dimensions must not exceed 1024");
            if(key=="--width")options.config.width=int(n);else options.config.height=int(n);
        }else if(key=="--preset"){
            const auto name=value();
            if(name=="empty")options.preset=Preset::Empty;
            else if(name=="falling")options.preset=Preset::Falling;
            else if(name=="volcano")options.preset=Preset::Volcano;
            else if(name=="circuit")options.preset=Preset::Circuit;
            else if(name=="garden")options.preset=Preset::Garden;
            else if(name=="elements")options.preset=Preset::Elements;
            else throw std::invalid_argument("Unknown preset: "+name);
        }else throw std::invalid_argument("Unknown option: "+key);
    }
    return options;
}
void SaveCapture(const std::filesystem::path& path,const std::vector<std::uint8_t>& rgba,int width,int height){
    std::ofstream file(path,std::ios::binary);if(!file)throw std::runtime_error("Cannot open capture file");
    auto extension=path.extension().string();std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return char(std::tolower(c));});
    if(extension==".png"){
        std::vector<std::uint8_t> upright(rgba.size());
        for(int y=0;y<height;++y)std::copy_n(rgba.data()+std::size_t(height-1-y)*width*4,std::size_t(width)*4,upright.data()+std::size_t(y)*width*4);
        auto write=[](void* context,void* bytes,int size){static_cast<std::ofstream*>(context)->write(static_cast<char*>(bytes),size);};
        if(!stbi_write_png_to_func(write,&file,width,height,4,upright.data(),width*4))throw std::runtime_error("PNG encoding failed");
    }else{
        file<<"P6\n"<<width<<' '<<height<<"\n255\n";
        for(int y=height-1;y>=0;--y)for(int x=0;x<width;++x)
            file.write(reinterpret_cast<const char*>(rgba.data()+(std::size_t(y)*width+x)*4),3);
    }
    if(!file)throw std::runtime_error("Capture write failed");
}
struct MouseInput { double wheel=0; };
}

int main(int argc,char**argv){
    for(int i=1;i<argc;++i)if(std::string_view(argv[i])=="--help"||std::string_view(argv[i])=="-h"){Usage();return 0;}
    Options options;
    try{options=Parse(argc,argv);}catch(const std::exception& error){std::cerr<<error.what()<<'\n';Usage();return 2;}
    SandboxModule world(options.config);
    ApplicationWindow::ApplicationWindowModule windowModule;
    Render::RenderModule renderModule;
    std::unique_ptr<SandboxRenderer> renderer;
    int result=0;
    try{
        if(!world.Startup())throw std::runtime_error(world.Error());
        world.LoadPreset(options.preset);world.SetPaused(options.paused);
        if(!options.loadFile.empty()&&!world.Load(options.loadFile))throw std::runtime_error(world.Error());
        if(!glfwInit())throw std::runtime_error("GLFW initialization failed");
        glfwWindowHint(GLFW_VISIBLE,options.smoke?GLFW_FALSE:GLFW_TRUE);
        if(!windowModule.Startup())throw std::runtime_error("OpenGL 4.5 window creation failed");
        auto window=windowModule.GetCurrentWindow();auto* native=window->GetNativeWindow();
        glfwWindowHint(GLFW_VISIBLE,GLFW_TRUE);
        glfwSetWindowTitle(native,"Pixel Sandbox / Matter, Heat, Motion");
        glfwSetWindowSize(native,1280,800);glfwSetWindowSizeLimits(native,800,500,GLFW_DONT_CARE,GLFW_DONT_CARE);
        if(!renderModule.Startup())throw std::runtime_error("Render module startup failed");
        auto device=renderModule.GetRHIDevice();renderer=std::make_unique<SandboxRenderer>(device);
        renderer->Initialize(world.Config().width,world.Config().height);
        ViewState view;MouseInput mouse;
        if(options.preset==Preset::Elements){view.palettePage=1;view.selected=Material::Wax;}
        glfwSetWindowUserPointer(native,&mouse);
        glfwSetScrollCallback(native,[](GLFWwindow* w,double,double y){static_cast<MouseInput*>(glfwGetWindowUserPointer(w))->wheel+=y;});
        std::array<bool,GLFW_KEY_LAST+1> previous{};
        bool previousLeft=false,previousMiddle=false,stroking=false;
        int lastX=0,lastY=0,lastButton=0;
        auto last=Clock::now(),progress=last,statusUntil=last,started=last;
        std::uint64_t completed=0;
        constexpr std::uint64_t SmokeFrames=64;
        int width=1280,height=800;
        Usage();std::cout<<"Save file: "<<options.saveFile.string()<<'\n';
        auto status=[&](std::string text){view.status=std::move(text);statusUntil=Clock::now()+std::chrono::seconds(5);};
        auto preset=[&](Preset selected){
            world.LoadPreset(selected);
            if(selected==Preset::Elements){view.paletteGroup=PaletteGroup::All;view.palettePage=1;view.selected=Material::Wax;}
            status("PRESET LOADED - EXPERIMENT WITH MATERIALS AND HEAT");
        };
        auto save=[&]{status(world.Save(options.saveFile)?"SAVED "+options.saveFile.filename().string():"SAVE FAILED: "+world.Error());};
        auto load=[&]{status(world.Load(options.saveFile)?"LOADED "+options.saveFile.filename().string():"LOAD FAILED: "+world.Error());};
        while(!window->ShouldClose()){
            window->PollEvents();device->returnSystem.DrainCallbacks();
            if(!renderer->Error().empty())throw std::runtime_error(renderer->Error());
            const auto now=Clock::now();const double elapsed=std::chrono::duration<double>(now-last).count();last=now;
            if(now>statusUntil)view.status.clear();
            if(renderer->CompletedFrames()!=completed){completed=renderer->CompletedFrames();progress=now;}
            if(options.smoke&&completed>=SmokeFrames)break;
            if(now-progress>std::chrono::seconds(20))throw std::runtime_error("Sandbox rendering timeout");
            if(options.smoke&&now-started>std::chrono::seconds(90))throw std::runtime_error("Sandbox smoke test timeout");
            const bool focused=!options.smoke&&glfwGetWindowAttrib(native,GLFW_FOCUSED)!=0;
            auto down=[&](int key){return focused&&glfwGetKey(native,key)==GLFW_PRESS;};
            auto pressed=[&](int key){const bool value=down(key),edge=value&&!previous[key];previous[key]=value;return edge;};
            if(pressed(GLFW_KEY_ESCAPE))break;
            if(pressed(GLFW_KEY_SPACE))world.SetPaused(!world.IsPaused());
            bool singleStep=pressed(GLFW_KEY_N);
            if(singleStep){world.SetPaused(true);world.FixedTick();}
            if(pressed(GLFW_KEY_C)){world.Clear();status("CANVAS CLEARED");}
            if(pressed(GLFW_KEY_T))view.heatMap=!view.heatMap;
            if(pressed(GLFW_KEY_B))view.tool=BrushTool::Paint;
            if(pressed(GLFW_KEY_E))view.tool=BrushTool::Erase;
            if(pressed(GLFW_KEY_H))view.tool=BrushTool::Heat;
            if(pressed(GLFW_KEY_J))view.tool=BrushTool::Cool;
            if(pressed(GLFW_KEY_P))view.tool=BrushTool::Pressure;
            if(pressed(GLFW_KEY_1))preset(Preset::Falling);
            if(pressed(GLFW_KEY_2))preset(Preset::Volcano);
            if(pressed(GLFW_KEY_3))preset(Preset::Circuit);
            if(pressed(GLFW_KEY_4))preset(Preset::Garden);
            if(pressed(GLFW_KEY_5))preset(Preset::Elements);
            if(pressed(GLFW_KEY_PAGE_UP))view.palettePage=std::max(0,view.palettePage-1);
            if(pressed(GLFW_KEY_PAGE_DOWN))view.palettePage=std::min(PalettePages(view.paletteGroup)-1,view.palettePage+1);
            if(pressed(GLFW_KEY_F5))save();
            if(pressed(GLFW_KEY_F9))load();
            view.replace=down(GLFW_KEY_LEFT_SHIFT)||down(GLFW_KEY_RIGHT_SHIFT);
            if(focused){view.radius=std::clamp(view.radius+int(mouse.wheel),1,40);}
            mouse.wheel=0;
            if(pressed(GLFW_KEY_LEFT_BRACKET))view.radius=std::max(1,view.radius-1);
            if(pressed(GLFW_KEY_RIGHT_BRACKET))view.radius=std::min(40,view.radius+1);
            glfwGetFramebufferSize(native,&width,&height);
            if(width<1||height<1){progress=now;stroking=false;glfwWaitEventsTimeout(.05);continue;}
            view.hoverX=view.hoverY=-1;
            if(focused){
                int ww=0,wh=0;glfwGetWindowSize(native,&ww,&wh);double mx,my;glfwGetCursorPos(native,&mx,&my);
                const auto hit=SandboxRenderer::HitTest(mx*width/std::max(1,ww),my*height/std::max(1,wh),width,height,world.Config().width,world.Config().height,view.paletteGroup,view.palettePage);
                const bool left=glfwGetMouseButton(native,GLFW_MOUSE_BUTTON_LEFT)==GLFW_PRESS;
                const bool right=glfwGetMouseButton(native,GLFW_MOUSE_BUTTON_RIGHT)==GLFW_PRESS;
                const bool middle=glfwGetMouseButton(native,GLFW_MOUSE_BUTTON_MIDDLE)==GLFW_PRESS;
                if(left&&!previousLeft&&hit.kind!=UiHit::Kind::Cell){
                    switch(hit.kind){
                    case UiHit::Kind::Material:view.selected=Material(hit.value);view.tool=BrushTool::Paint;break;
                    case UiHit::Kind::Category:view.paletteGroup=PaletteGroup(hit.value);view.palettePage=0;break;
                    case UiHit::Kind::Page:view.palettePage=std::clamp(view.palettePage+hit.value,0,PalettePages(view.paletteGroup)-1);break;
                    case UiHit::Kind::Tool:view.tool=BrushTool(hit.value);break;
                    case UiHit::Kind::Pause:world.SetPaused(!world.IsPaused());break;
                    case UiHit::Kind::Step:world.SetPaused(true);world.FixedTick();singleStep=true;break;
                    case UiHit::Kind::Clear:world.Clear();status("CANVAS CLEARED");break;
                    case UiHit::Kind::HeatMap:view.heatMap=!view.heatMap;break;
                    case UiHit::Kind::Preset:preset(Preset(hit.value));break;
                    case UiHit::Kind::Save:save();break;
                    case UiHit::Kind::Load:load();break;
                    default:break;
                    }
                }
                if(hit.kind==UiHit::Kind::Cell){
                    view.hoverX=hit.x;view.hoverY=hit.y;
                    if(middle&&!previousMiddle){
                        view.selected=world.At(hit.x,hit.y).material;
                        view.tool=view.selected==Material::Empty?BrushTool::Erase:BrushTool::Paint;
                    }
                    if(left||right){
                        const int button=right?2:1;const auto tool=right?BrushTool::Erase:view.tool;
                        if(stroking&&button==lastButton)world.Stroke(lastX,lastY,hit.x,hit.y,view.radius,view.selected,tool,view.replace);
                        else world.Paint(hit.x,hit.y,view.radius,view.selected,tool,view.replace);
                        lastX=hit.x;lastY=hit.y;lastButton=button;stroking=true;
                    }else stroking=false;
                }else stroking=false;
                previousLeft=left;previousMiddle=middle;
                if(!singleStep)world.Advance(elapsed);
            }else{stroking=false;previousLeft=previousMiddle=false;}
            if(renderer->Ready()&&!renderer->Busy()){
                if(options.smoke){world.FixedTick();world.FixedTick();}
                const bool captureLast=!options.capture.empty()&&renderer->CompletedFrames()+1==SmokeFrames;
                renderer->Draw(world,view,width,height,!captureLast);
            }
            device->returnSystem.WaitForCallbacks(std::chrono::milliseconds(3));
        }
        glfwSetScrollCallback(native,nullptr);glfwSetWindowUserPointer(native,nullptr);
        const auto deadline=Clock::now()+std::chrono::seconds(10);
        while(renderer->Busy()&&Clock::now()<deadline){device->returnSystem.DrainCallbacks();device->returnSystem.WaitForCallbacks(std::chrono::milliseconds(3));}
        if(renderer->Busy())throw std::runtime_error("Frame retirement timeout");
        if(!renderer->Error().empty())throw std::runtime_error(renderer->Error());
        if(options.smoke){
            if(renderer->CompletedFrames()!=SmokeFrames)throw std::runtime_error("Smoke test interrupted");
            struct Readback{bool done=false;GLenum error=GL_NO_ERROR;std::vector<std::uint8_t> pixels;};
            auto capture=std::make_shared<Readback>();const bool read=!options.capture.empty();
            device->async_ExecuteCode([capture,width,height,read]{
                glFinish();if(read){capture->pixels.resize(std::size_t(width)*height*4);glReadBuffer(GL_BACK);glPixelStorei(GL_PACK_ALIGNMENT,1);
                    glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,capture->pixels.data());}capture->error=glGetError();
            },[capture]{capture->done=true;});
            const auto end=Clock::now()+std::chrono::seconds(10);
            while(!capture->done&&Clock::now()<end){device->returnSystem.DrainCallbacks();device->returnSystem.WaitForCallbacks(std::chrono::milliseconds(3));}
            if(!capture->done||capture->error!=GL_NO_ERROR)throw std::runtime_error("OpenGL smoke/readback failed");
            if(read)SaveCapture(options.capture,capture->pixels,width,height);
            std::cout<<"PixelSandbox smoke PASS: "<<renderer->CompletedFrames()<<" frames, "<<world.Stats().tick<<" ticks, "
                <<world.Stats().particles<<" particles, hash "<<world.StateHash()<<", GL_NO_ERROR\n";
        }
        renderer->Shutdown();
    }catch(const std::exception& error){std::cerr<<"PixelSandbox: "<<error.what()<<'\n';result=1;}
    // Join the worker while its OpenGL context/window and callback state live.
    renderModule.Shutdown();renderer.reset();windowModule.Shutdown();world.Shutdown();glfwTerminate();
    return result;
}
