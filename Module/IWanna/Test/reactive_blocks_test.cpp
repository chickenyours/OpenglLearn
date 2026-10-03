#include "IWanna/Public/game_module.h"
#include "IWanna/Public/content_format.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
using namespace IWanna;
namespace {
void Check(bool ok,const std::string& why){if(!ok)throw std::runtime_error(why);}
void Tick(GameModule& game,int frames=1,Input input={}) {
    for(int i=0;i<frames;++i)game.FixedTick(input);
    Check(game.World()->Error().empty(),game.World()->Error());
}
void Place(GameModule& game,glm::vec2 point) {
    const auto id=game.PlayerEntity();game.Get<Transform>(id).position=point;
    game.Get<Motion>(id).velocity={};game.Get<Player>(id)={};
}
Json::Value Entity(const char* prefab,glm::vec2 position,glm::vec2 size) {
    Json::Value value;value["kind"]="Entity";value["prefab"]=prefab;
    value["position"][0]=position.x;value["position"][1]=position.y;
    value["size"][0]=size.x;value["size"][1]=size.y;value["properties"]=Json::Value(Json::objectValue);return value;
}
Json::Value Prefab(const char* event,bool visible) {
    Json::Value value;value["format"]="IWANNA_PREFAB_1";value["role"]="solid";
    value["image"]="terrain_moss_00.png";value["width"]=2.5;value["height"]=2.5;
    value["collide"]=true;value["visible"]=visible;value["event"]=event;
    value["detectionEnabled"]=true;return value;
}
}
int main(){try{
    const auto assets=std::filesystem::path(IWANNA_ASSETS);
    const auto root=std::filesystem::current_path()/"reactive_blocks_test_generated";
    std::filesystem::create_directories(root/"prefabs");std::filesystem::create_directories(root/"rooms");
    auto hidden=Prefab("reveal_hidden_wall",false);hidden["opacity"]=.65;
    const auto block=Prefab("break_trap_block",true);
    Content::Write(root/"prefabs/hidden_wall.prefab.json",hidden);
    Content::Write(root/"prefabs/trap_block.prefab.json",block);
    Json::Value room;room["format"]="IWANNA_ROOM_2";room["title"]="Reactive block fixture";room["hint"]="";
    room["grid"]["width"]=64;room["grid"]["height"]=20;room["grid"]["tileSize"]=2.5;
    room["grid"]["origin"][0]=-80;room["grid"]["origin"][1]=-25;room["palette"]["1"]="terrain_moss_00.png";
    for(int y=0;y<20;++y)for(int x=0;x<64;++x)room["tileLayers"]["Terrain"][y].append(y>=18?1:0);
    auto& entities=room["entities"];
    entities["start"]["kind"]="Spawn";entities["start"]["position"][0]=-60;entities["start"]["position"][1]=18.9;
    entities["wall"]=Entity("hidden_wall",{-20,16},{2.5,8});
    entities["top_block"]=Entity("trap_block",{-5,18.75},{2.5,2.5});
    entities["side_block"]=Entity("trap_block",{5,18.75},{2.5,2.5});
    entities["head_block"]=Entity("trap_block",{15,14},{2.5,2.5});
    entities["step_block"]=Entity("trap_block",{30,19.96},{2.5,.08});
    entities["trial_ceiling"]=Entity("hidden_wall",{29,17.99},{4,.12});
    entities["recovery_block"]=Entity("trap_block",{45,18.75},{2.5,2.5});
    entities["open_step_block"]=Entity("trap_block",{60,19.96},{2.5,.08});
    entities["opacity_probe"]=Entity("hidden_wall",{-70,-10},{2.5,2.5});
    entities["opacity_probe"]["properties"]["opacity"]=.4;
    // Fixed values make the motion assertions reproducible while the shipped
    // Lua module continues to choose a direction and support parameter ranges.
    for(const auto* id:{"top_block","side_block","head_block","step_block","recovery_block","open_step_block"}) {
        auto& p=entities[id]["properties"];p["duration"]=.5;p["popMin"]=6;p["popMax"]=6;
        p["driftMin"]=2;p["driftMax"]=2;p["gravity"]=45;p["step"]=1.0/60;
    }
    std::ifstream source(assets/"Examples/reactive_blocks.lua");std::ostringstream luaSource;luaSource<<source.rdbuf();
    Check(!luaSource.str().empty(),"canonical reactive block Lua source exists");
    room["scriptLua"]="local blocks=(function()\n"+luaSource.str()+R"(
end)()
blocks.on_enter=function(ctx) ctx:set_opacity('opacity_probe',0.25) end
return blocks
)";
    Json::Value world;world["format"]="IWANNA_WORLD_2";world["prefabDirectory"]="prefabs";world["roomDirectory"]="rooms";
    world["startRoom"]="arena";world["startSpawn"]="start";
    Content::Write(root/"world.json",world);Content::Write(root/"rooms/arena.room.json",room);
    auto other=room;other["entities"].removeMember("side_block");Content::Write(root/"rooms/other.room.json",other);
    const auto roundTrip=Content::FromTiled(Content::ToTiled(room));
    Check(roundTrip["entities"]["opacity_probe"]["properties"]["opacity"].asDouble()==.4,"instance opacity survives native/Tiled round trip");
    const auto catalog=RoomCatalog::Load(root/"world.json");
    Check(std::abs(catalog.prefabs.at("hidden_wall").sprite.opacity-.65f)<1e-6f,"prefab opacity loads");
    for(const auto& object:catalog.rooms.at("arena").objects)if(object.id=="opacity_probe")
        Check(std::abs(object.sprite.opacity-.4f)<1e-6f,"instance opacity overrides prefab");
    GameModule game(assets,{},root/"world.json");Check(game.Startup(),game.Error());
    std::vector<std::string> sounds;game.playSound=[&](const std::string& name){sounds.push_back(name);};
    auto soundCount=[&](const char* name){return std::count(sounds.begin(),sounds.end(),name);};
    auto reset=[&] {
        if(game.GetState()!=State::Playing){game.Restart();Tick(game);}
        game.World()->RequestRoom("arena","start");Tick(game);sounds.clear();
    };
    auto playerPosition=[&]{return game.Get<Transform>(game.PlayerEntity()).position;};
    auto broken=[&](const char* id){return game.Find(id)&&!game.Get<Collider>(game.Find(id)).enabled;};
    auto collideSide=[&](const char* id,glm::vec2 from) {
        Place(game,from);Input right;right.right=true;
        for(int i=0;i<70&&!broken(id);++i)Tick(game,1,right);
        Check(broken(id),std::string("real side contact triggers ")+id);
    };
    const auto opacityProbe=game.Find("opacity_probe");
    Check(game.Get<Sprite>(opacityProbe).opacity==.25f&&!game.Get<Sprite>(opacityProbe).visible&&game.Get<Collider>(opacityProbe).enabled,
        "Lua opacity is independent of collision and visibility");
    // An invisible solid is still a wall. Only the initial actual touch plays
    // the reveal sound; leaving and touching it again does not retrigger it.
    Place(game,{-24,18.9});Input right;right.right=true;Tick(game,50,right);
    const auto wall=game.Find("wall");
    Check(game.Get<Sprite>(wall).visible&&game.Get<Collider>(wall).enabled&&playerPosition().x< -21.6f,"hidden wall reveals while retaining solid collision");
    Check(std::abs(game.Get<Sprite>(wall).opacity-.65f)<1e-6f&&soundCount("Block Change")==1,"reveal preserves opacity and emits one sound");
    Place(game,{-26,18.9});Tick(game,1);Tick(game,80,right);Check(soundCount("Block Change")==1,"revealed wall ignores repeated contacts");
    reset();Place(game,{-5,15});
    for(int i=0;i<90&&!broken("top_block");++i)Tick(game);
    Check(broken("top_block")&&soundCount("Break")==1,"landing on a trap block starts one breakup task");
    reset();collideSide("side_block",{1,18.9});
    auto side=game.Find("side_block");const auto initial=game.Get<Transform>(side).position;
    Check(game.Get<Motion>(side).velocity.y== -6&&game.Get<Sprite>(side).opacity==1,"breakup begins with an upward pop and intact visual");
    Place(game,{-60,18.9});Tick(game,15);side=game.Find("side_block");
    Check(side&&game.Get<Transform>(side).position.y<initial.y-.25f&&std::abs(game.Get<Transform>(side).position.x-initial.x)>.1f,
        "disabled solid collider does not stop scripted debris motion");
    Check(game.Get<Sprite>(side).visible&&game.Get<Sprite>(side).opacity>0&&game.Get<Sprite>(side).opacity<.9f,"debris fades while remaining visible");
    Tick(game,24);side=game.Find("side_block");
    Check(side&&game.Get<Motion>(side).velocity.y>0&&game.Get<Sprite>(side).opacity<.5f,"Lua gravity turns the pop into a falling arc");
    Tick(game,26);Check(!game.Find("side_block")&&soundCount("Break")==1,"debris is removed after half a second without retriggering");
    reset();Place(game,{15,18.9});Tick(game,12);Input jump;jump.jump=jump.jumpHeld=true;Tick(game,1,jump);jump.jump=false;
    for(int i=0;i<60&&!broken("head_block");++i)Tick(game,1,jump);
    Check(broken("head_block"),"jumping into the underside of a block starts breakup");
    reset();collideSide("step_block",{26.5f,18.9f});
    Check(!game.Get<Sprite>(game.Find("trial_ceiling")).visible,"speculative step-up casts do not reveal a nearby ceiling");
    reset();collideSide("open_step_block",{56.5f,18.9f});
    Check(game.Get<Player>(game.PlayerEntity()).grounded,"a successful tiny step reports contact while keeping the player supported");
    reset();Place(game,{43.4f,18.9f});Tick(game);
    Check(broken("recovery_block"),"overlap recovery still reports the actual solid contact");
    reset();collideSide("side_block",{1,18.9});game.Kill();Tick(game);
    Check(!game.Find("side_block"),"death cancels and removes pending debris tasks");
    game.Restart();Tick(game);side=game.Find("side_block");
    Check(side&&game.Get<Collider>(side).enabled&&game.Get<Sprite>(side).opacity==1,"restart restores the authored intact block");
    collideSide("side_block",{1,18.9});game.World()->RequestRoom("other","start");Tick(game);Tick(game,90);
    Check(game.World()->CurrentId()=="other"&&!game.Find("side_block"),"old-room debris tasks cannot affect a replacement scene");
    reset();side=game.Find("side_block");Check(side&&game.Get<Sprite>(side).opacity==1&&game.Get<Collider>(side).enabled,"room return restores block state");
    Scripting::LuaModule lua;
    for(const auto* input:{"-0.01","1.01","0/0","math.huge","'0.5'","nil"}) {
        Check(lua.LoadSource(std::string("return {on_enter=function(ctx) ctx:set_opacity('x',")+input+") end}","invalid_opacity"),"opacity validation fixture loads");
        Check(!lua.Call({"on_enter","","",""})&&lua.TakeCommands().empty(),"invalid opacity is rejected before commands are committed");
    }
    for(const auto* input:{"0","0.5","1"}) {
        Check(lua.LoadSource(std::string("return {on_enter=function(ctx) ctx:set_opacity('x',")+input+") end}","valid_opacity"),"valid opacity fixture loads");
        Check(lua.Call({"on_enter","","",""}),lua.Error());const auto commands=lua.TakeCommands();
        Check(commands.size()==1&&commands.front().operation=="set_opacity","opacity endpoints and midpoint are accepted");
    }
    for(const auto invalid:{Json::Value(-.01),Json::Value(1.01),Json::Value("0.5"),Json::Value(true)}) {
        auto changed=hidden;changed["opacity"]=invalid;Content::Write(root/"prefabs/hidden_wall.prefab.json",changed);
        bool rejected=false;try{RoomCatalog::Load(root/"world.json");}catch(...){rejected=true;}
        Check(rejected,"invalid prefab opacity is rejected");
        Content::Write(root/"prefabs/hidden_wall.prefab.json",hidden);
        auto altered=room;altered["entities"]["wall"]["properties"]["opacity"]=invalid;Content::Write(root/"rooms/arena.room.json",altered);
        rejected=false;try{RoomCatalog::Load(root/"world.json");}catch(...){rejected=true;}
        Check(rejected,"invalid instance opacity is rejected");Content::Write(root/"rooms/arena.room.json",room);
    }
    std::cout<<"Reactive blocks PASS: reveal/solid contact, landing/head/side/recovery, probe isolation, Lua pop/fall/fade/cleanup, reset isolation, opacity validation\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
