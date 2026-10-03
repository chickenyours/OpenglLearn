#include "IWanna/Public/editor_document.h"
#include "IWanna/Public/game_module.h"
#include "IWanna/Public/camera_math.h"
#include "IWanna/Public/playtest_launcher.h"
#include <cmath>
#include <iostream>
using namespace IWanna;
static void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static bool Near(float a,float b){return std::abs(a-b)<.02f;}
int main(){try{
    auto assets=std::filesystem::path(IWANNA_ASSETS),root=std::filesystem::current_path()/"camera_test_generated";
    int n=0;while(std::filesystem::exists(root))root=std::filesystem::current_path()/("camera_test_generated_"+std::to_string(++n));
    std::filesystem::create_directories(root);std::filesystem::copy(assets/"Workshop",root,std::filesystem::copy_options::recursive);
    EditorDocument doc;doc.Open(root/"world.json");
    for(int i=0;i<int(doc.world["rooms"].size());++i)if(doc.world["rooms"][i]["id"]=="movement"){doc.LoadRoom(i);break;}
    Check(doc.RoomId()=="movement"&&doc.DefaultSpawn()=="left","editor chooses room default spawn");
    auto args=PlaytestArguments("game.exe",assets,doc.worldPath,doc.RoomId(),"right",glm::vec2(12.5f,-3.25f));
    Check(args.size()==12&&args[6]=="movement"&&args[8]=="right"&&args[9]=="--position"&&args[10]=="12.5"&&args[11]=="-3.25","playtest launch targets selected room and point");
    Check(doc.CameraConfig()["mode"]=="fixed","default room camera is fixed");
    doc.SetCameraField("mode","follow");doc.SetCameraField("offsetX","5");doc.SetCameraField("zoom","2");doc.SetCameraField("followSpeed","0");doc.Save();
    auto room=RoomCatalog::Load(root/"world.json").rooms.at("movement");
    Check(room.camera.mode==Camera::Mode::FollowPlayer&&Near(room.camera.offset.x,5)&&Near(room.camera.zoom,2),"editor camera roundtrip");
    GameModule game(assets,{},root/"world.json");Check(game.Startup(),game.Error().c_str());
    auto id=game.PlayerEntity();auto player=game.Get<Transform>(id).position;
    Check(game.ActiveCamera().mode==Camera::Mode::FollowPlayer,"runtime uses room camera");
    auto expected=CameraClamp(player+glm::vec2(5,0),room.origin,room.extent,2);
    Check(Near(game.ActiveCamera().center.x,expected.x),"camera starts at player offset");
    auto& cam=game.Get<Camera>(id);cam.clampToRoom=false;game.Get<Transform>(id).position+=glm::vec2(10,0);game.UpdateCamera();
    Check(Near(cam.center.x,player.x+15),"instant follow tracks player");
    Check(Near(CameraProject(cam,game.Get<Transform>(id).position).x,-10),"world projection uses camera center and zoom");
    cam.mode=Camera::Mode::Fixed;cam.position={12,6};game.UpdateCamera();Check(Near(cam.center.x,12)&&Near(cam.center.y,6),"fixed camera stays on stage center");
    game.Shutdown();
    doc.SetCameraField("mode","fixed");doc.SetCameraField("x","9");doc.SetCameraField("y","4");doc.SetCameraField("clampToRoom","false");doc.Save();
    auto raw=Content::Read(doc.mapPath);raw["scriptLua"]="return {on_enter=function(ctx) ctx:camera_follow(3, -2, 1.5, 0) end}";Content::Write(doc.mapPath,raw);
    GameModule scripted(assets,{},root/"world.json");Check(scripted.Startup(),scripted.Error().c_str());
    const auto& active=scripted.ActiveCamera();Check(active.mode==Camera::Mode::FollowPlayer&&Near(active.offset.x,3)&&Near(active.offset.y,-2)&&Near(active.zoom,1.5f),"Lua overrides room camera");
    scripted.Shutdown();
    raw["scriptLua"]="return {on_enter=function(ctx) ctx:camera_fixed(8, 7, 1.25) end}";Content::Write(doc.mapPath,raw);
    GameModule fixed(assets,{},root/"world.json");Check(fixed.Startup(),fixed.Error().c_str());
    Check(fixed.ActiveCamera().mode==Camera::Mode::Fixed&&Near(fixed.ActiveCamera().center.x,8)&&Near(fixed.ActiveCamera().center.y,7),"Lua fixed camera command");
    std::cout<<"Camera room, editor, Lua and projection PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
