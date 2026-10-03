#include "IWanna/Public/game_module.h"
#include "IWanna/Public/sprite_renderer.h"
#include "Audio/Public/audio_module.h"
#include "ApplicationWindow/module.h"
#include "Render/module.h"
#include "IWanna/Public/frame_metrics.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>
#include <unordered_map>
#include <cstdio>
#include <cmath>
#include <optional>
#include <stb_image_write.h>

int main(int argc,char** argv) {
    bool smoke=false,smokeTour=false,mute=false;std::filesystem::path assets=std::filesystem::absolute(argv[0]).parent_path()/"IWanna";
    std::string capture,metricsPath,configPath,maskDirectory,worldPath,startRoom,startSpawn="left";int benchmark=0,smokeTicks=0;bool benchmarkVsync=false;
    std::optional<glm::vec2> startPosition;
    for(int i=1;i<argc;++i) {
        std::string arg=argv[i];if(arg=="--smoke-test") smoke=true;else if(arg=="--mute") mute=true;
        else if(arg=="--smoke-tour")smoke=smokeTour=true;
        else if(arg=="--smoke-ticks" && i+1<argc) {try{size_t n=0;std::string value=argv[++i];smokeTicks=std::stoi(value,&n);if(n!=value.size()||smokeTicks<0||smokeTicks>7200)throw std::invalid_argument("ticks");}catch(const std::exception&){std::cerr<<"Invalid --smoke-ticks (0..7200)\n";return 2;}}
        else if(arg=="--assets" && i+1<argc) assets=argv[++i];else if(arg=="--capture" && i+1<argc) capture=argv[++i];
        else if(arg=="--benchmark" && i+1<argc) benchmark=std::max(6,std::atoi(argv[++i]));
        else if(arg=="--metrics" && i+1<argc) metricsPath=argv[++i];
        else if(arg=="--benchmark-vsync") benchmarkVsync=true;
        else if(arg=="--config" && i+1<argc) configPath=argv[++i];
        else if(arg=="--world" && i+1<argc) worldPath=argv[++i];
        else if(arg=="--room" && i+1<argc) startRoom=argv[++i];
        else if(arg=="--spawn" && i+1<argc) startSpawn=argv[++i];
        else if(arg=="--position" && i+2<argc) {try{size_t nx=0,ny=0;std::string sx=argv[++i],sy=argv[++i];float x=std::stof(sx,&nx),y=std::stof(sy,&ny);if(nx!=sx.size()||ny!=sy.size()||!std::isfinite(x)||!std::isfinite(y))throw std::invalid_argument("position");startPosition=glm::vec2(x,y);}catch(const std::exception&){std::cerr<<"Invalid --position coordinates\n";return 2;}}
        else if(arg=="--export-masks" && i+1<argc) maskDirectory=argv[++i];
        else {std::cerr<<"Usage: iwanna_game/showcase [--assets path] [--config gameplay.json] [--world world.json] [--room id] [--spawn id] [--position x y] [--export-masks directory] [--mute] [--smoke-test] [--capture file.png] [--benchmark frames] [--metrics file.csv] [--benchmark-vsync]\n";return 2;}
    }
#ifdef IWANNA_SHOWCASE
    if(worldPath.empty())worldPath=(assets/"Workshop/world.json").string();
#endif
    IWanna::GameModule game(assets,configPath,worldPath);Audio::AudioModule audio;
    ApplicationWindow::ApplicationWindowModule windowModule;Render::RenderModule renderModule;
    std::unique_ptr<IWanna::SpriteRenderer> renderer;
    int result=0;
    try {
        if(!game.Startup()) throw std::runtime_error(game.Error());
        if(!startRoom.empty()) {
            if(!game.IsRoomGame())throw std::runtime_error("--room requires --world");
            game.World()->RequestRoom(startRoom,startSpawn);game.FixedTick({});
        }
        if(startPosition){
            if(!game.IsRoomGame())throw std::runtime_error("--position requires --world");
            const auto& room=game.World()->Current();
            if(startPosition->x<room.origin.x||startPosition->y<room.origin.y||startPosition->x>room.origin.x+room.extent.x||startPosition->y>room.origin.y+room.extent.y)throw std::runtime_error("--position is outside the room");
            auto& player=game.Get<IWanna::Transform>(game.PlayerEntity());player.position=*startPosition;player.spawn=*startPosition;
            game.UpdateCamera(true);
        }
        if(smokeTicks){
            if(!smoke)throw std::runtime_error("--smoke-ticks requires --smoke-test or --smoke-tour");
            // Advance real Lua/physics deterministically before a GPU capture.
            for(int tick=0;tick<smokeTicks;++tick)game.FixedTick({});
            if(game.World()&&!game.World()->Error().empty())throw std::runtime_error(game.World()->Error());
            game.UpdateCamera(true);
        }
        if(!maskDirectory.empty()){game.ExportMasks(maskDirectory);std::cout<<"Exported pixel collision masks to "<<maskDirectory<<'\n';return 0;}
        std::unordered_map<std::string,std::shared_ptr<const Audio::Clip>> sounds;
        for(const auto& entry:std::filesystem::directory_iterator(assets/"audio")) if(entry.path().extension()==".wav") sounds[entry.path().stem().string()]=Audio::Clip::LoadWav(entry.path());
        if(!mute && !audio.Startup()) std::cerr<<"Audio output unavailable: "<<audio.Error()<<"\n";
        game.playSound=[&](const std::string& name){if(audio.IsStarted() && sounds.contains(name)) audio.GetMixer().Play(sounds.at(name),.65f);};
        if(!windowModule.Startup()) throw std::runtime_error("ApplicationWindow startup failed");
        auto window=windowModule.GetCurrentWindow();auto* native=window->GetNativeWindow();
        const char* appTitle=game.IsRoomGame()?"I WANNA":"I WANNA BE THE KING";
        glfwSetWindowTitle(native,appTitle);
        glfwSetWindowSize(native,1920,1080);
        if(!renderModule.Startup()) throw std::runtime_error("Render startup failed");
        auto device=renderModule.GetRHIDevice();renderer=std::make_unique<IWanna::SpriteRenderer>(device);renderer->Initialize(assets,game.Views(),game.AnimationAssets());
        if(benchmark && !benchmarkVsync) device->async_ExecuteCode([]{glfwSwapInterval(0);});
        IWanna::FrameMetrics metrics;
        auto fpsStart=IWanna::Clock::now();int fpsFrames=0;
        auto start=std::chrono::steady_clock::now(),last=start;bool oldJump=false,oldRestart=false,oldShoot=false;int frames=0;bool framePending=false;
        std::string lastScriptError;
        while(!window->ShouldClose()) {
            window->PollEvents();device->returnSystem.DrainCallbacks();
            if(!renderer->Error().empty()) throw std::runtime_error(renderer->Error());
            if(glfwGetKey(native,GLFW_KEY_ESCAPE)==GLFW_PRESS) break;
            auto now=std::chrono::steady_clock::now();double elapsed=std::chrono::duration<double>(now-last).count();last=now;
            if(!renderer->Ready()) {if(now-start>std::chrono::seconds(15)) throw std::runtime_error("GPU resource creation timeout");device->returnSystem.WaitForCallbacks(std::chrono::milliseconds(8));continue;}
            if(benchmark && framePending) {device->returnSystem.WaitForCallbacks(std::chrono::milliseconds(8));continue;}
            if(benchmark && frames>=benchmark) {metrics.Save(metricsPath);break;}
            const auto measureStart=IWanna::Clock::now();
            bool focused=glfwGetWindowAttrib(native,GLFW_FOCUSED)!=0;
            auto down=[&](int key){return focused && glfwGetKey(native,key)==GLFW_PRESS;};
            bool jump=down(GLFW_KEY_J)||down(GLFW_KEY_SPACE),restart=down(GLFW_KEY_R);
            IWanna::Input input{down(GLFW_KEY_A)||down(GLFW_KEY_LEFT),down(GLFW_KEY_D)||down(GLFW_KEY_RIGHT),jump&&!oldJump,restart&&!oldRestart};
            input.jumpHeld=jump;input.any=input.left||input.right||input.jump||input.restart;oldJump=jump;oldRestart=restart;
            const bool shoot=down(GLFW_KEY_Z)||down(GLFW_KEY_K);
            input.shoot=shoot&&!oldShoot;input.shootHeld=shoot;input.any|=input.shoot;oldShoot=shoot;
            if(benchmark) {
                if(frames%60==0) game.Restart();
                IWanna::Input replay{false,true,frames%30==0,false};game.FixedTick(replay);replay.jump=false;game.FixedTick(replay);
            } else if(!smoke) game.Advance(elapsed,input);
            if(game.IsRoomGame() && game.World()->Error()!=lastScriptError) {
                lastScriptError=game.World()->Error();if(!lastScriptError.empty())std::cerr<<"Room script: "<<lastScriptError<<'\n';
            }
            const double simulationMs=IWanna::Milliseconds(IWanna::Clock::now()-measureStart);
            int width,height;glfwGetFramebufferSize(native,&width,&height);
            if(width<1 || height<1) {glfwWaitEventsTimeout(.05);continue;}
            if(framePending) {device->returnSystem.WaitForCallbacks(std::chrono::milliseconds(8));continue;}
            if(smokeTour && (frames==3||frames==6)) {
                if(!game.IsRoomGame())throw std::runtime_error("--smoke-tour requires room mode");
                game.World()->RequestRoom(frames==3?"traps":"gallery","left");game.FixedTick({});
                if(!game.World()->Error().empty())throw std::runtime_error(game.World()->Error());
            }
            Render::RHICommand::BeginFrame begin;begin.frameIndex=frames;begin.framebufferWidth=width;begin.framebufferHeight=height;begin.clearColor={.035f,.045f,.065f,1};
            auto frame=device->BeginFrame(begin);
            int vw=std::min(width,height*16/9),vh=vw*9/16;
            frame.SetViewport({(width-vw)/2,(height-vh)/2,uint32_t(vw),uint32_t(vh)});
            const auto& camera=game.ActiveCamera();
            renderer->Draw(frame,game.Extract(),true,&camera);
            if(game.IsRoomGame()) {
                const auto& room=game.World()->Current();
                for(const auto& label:room.labels)renderer->TextWorld(frame,label.text,label.position,label.scale,{.6f,.77f,.85f,1},camera);
                if(!game.World()->Message().empty())renderer->Text(frame,game.World()->Message(),{-69,38.8f},.28f,{.65f,1,.68f,1});
                if(!game.World()->Error().empty())renderer->Text(frame,"SCRIPT ERROR SEE CONSOLE",{-35,-10},.4f,{1,.3f,.3f,1});
            }
            if(!game.IsRoomGame())renderer->Text(frame,std::to_string(game.Deaths()),{57,30},.65f,{1,1,1,1});
            if(game.GetState()!=IWanna::State::Playing) {
                renderer->Rect(frame,{0,0},{76,16},{.015f,.025f,.035f,.92f});
                renderer->Text(frame,game.GetState()==IWanna::State::Won?"LEVEL CLEAR":"YOU DIED",{-26,-5},.65f,{1,.75f,.32f,1});
                renderer->Text(frame,"R TO RESPAWN",{-20,3},.45f,{.85f,.93f,1,1});
            }
            const bool final=smoke && frames==(smokeTour?8:2);
            renderer->Flush(frame);
            frame.End(!final || capture.empty());framePending=true;
            const auto commands=frame.GetCommandBuffer()->GetCommandCount();
            const double buildMs=IWanna::Milliseconds(IWanna::Clock::now()-measureStart)-simulationMs;
            renderer->Submit(frame.GetCommandBuffer(),[&,measureStart,simulationMs,buildMs,commands]{
                framePending=false;if(benchmark) metrics.samples.push_back({simulationMs,buildMs,device->GetFrameStats().executeMs,IWanna::Milliseconds(IWanna::Clock::now()-measureStart),commands});
                ++fpsFrames;auto fpsNow=IWanna::Clock::now();double seconds=std::chrono::duration<double>(fpsNow-fpsStart).count();
                if(seconds>=1 && !benchmark) {char title[180];std::snprintf(title,sizeof(title),"%s | %.0f FPS",appTitle,fpsFrames/seconds);glfwSetWindowTitle(native,title);fpsStart=fpsNow;fpsFrames=0;}
            });
            ++frames;
            if(final) {
                auto frameDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
                while(framePending && std::chrono::steady_clock::now()<frameDeadline) {device->returnSystem.DrainCallbacks();std::this_thread::sleep_for(std::chrono::milliseconds(2));}
                if(framePending) throw std::runtime_error("GPU frame timeout");
                bool done=false;int glError=0;bool saved=capture.empty();
                device->async_ExecuteCode([&]{
                    glFinish();glError=int(glGetError());
                    if(!capture.empty()) {std::vector<unsigned char> pixels(size_t(width)*height*4);glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());stbi_flip_vertically_on_write(1);saved=stbi_write_png(capture.c_str(),width,height,4,pixels.data(),width*4)!=0;}
                },[&]{done=true;});
                auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
                while(!done && std::chrono::steady_clock::now()<deadline) {device->returnSystem.DrainCallbacks();std::this_thread::sleep_for(std::chrono::milliseconds(2));}
                if(!done) device->StopAndRelease();
                if(!done || glError || !saved) throw std::runtime_error("GPU smoke/capture failed");
                std::cout<<"IWanna smoke PASS: "<<game.Entities().size()<<" ECS entities, "<<(smokeTour?9:3)<<" RHI frames\n";break;
            }
            device->returnSystem.WaitForCallbacks(std::chrono::milliseconds(8));
        }
        // Retire the last frame before destroying callbacks' captured host state.
        auto retireDeadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        while(framePending && std::chrono::steady_clock::now()<retireDeadline) {device->returnSystem.DrainCallbacks();std::this_thread::sleep_for(std::chrono::milliseconds(2));}
        if(framePending) {device->StopAndRelease();throw std::runtime_error("Render retirement timeout");}
        renderer->Shutdown();device->StopAndRelease();
        game.playSound={};
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';result=1;}
    // Render callbacks capture renderer: stop/join before destroying the adapter.
    renderModule.Shutdown();renderer.reset();windowModule.Shutdown();audio.Shutdown();game.Shutdown();
    return result;
}
