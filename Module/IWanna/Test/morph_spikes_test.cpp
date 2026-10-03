#include "IWanna/Public/game_module.h"
#include "IWanna/Public/content_format.h"
#include "IWanna/Public/transform_math.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>

using namespace IWanna;
namespace {
constexpr std::array<const char*,10> Variants{
    "vertical","horizontal","giant","squash_pop","cross","stutter",
    "turn_spear","windmill","diagonal_giant","staircase_combo"};
void Check(bool ok,const std::string& why) {if(!ok)throw std::runtime_error(why);}
void Tick(GameModule& game,int frames=1) {
    for(int i=0;i<frames;++i)game.FixedTick({});
    Check(game.World()->Error().empty(),game.World()->Error());
}
void Place(GameModule& game,glm::vec2 point) {
    const auto id=game.PlayerEntity();game.Get<Transform>(id).position=point;
    game.Get<Motion>(id).velocity={};game.Get<Player>(id)={};
}
bool Near(glm::vec2 a,glm::vec2 b,float epsilon=.015f) {return glm::length(a-b)<epsilon;}
bool Original(const Transform& a,const Transform& b) {
    return Near(a.position,b.position)&&Near(a.size,b.size)&&std::abs(a.rotation-b.rotation)<.01f;
}
std::vector<ECS::EntityID> Effects(GameModule& game) {
    std::vector<ECS::EntityID> result;
    for(const auto& view:game.Views())if(view.behavior->stableId.starts_with("morph:")) {
        Check(view.behavior->role==Role::Hazard,"morph task creates no warning or decorative cue");
        result.push_back(view.id);
    }
    return result;
}
ECS::EntityID Effect(GameModule& game) {
    const auto effects=Effects(game);Check(effects.size()==1,"exactly one ordinary-looking lethal spike per controller");
    return effects.front();
}
Json::Value Entity(const std::string& prefab,glm::vec2 position,glm::vec2 size={2.5f,2.5f}) {
    Json::Value result;result["kind"]="Entity";result["prefab"]=prefab;
    result["position"][0]=position.x;result["position"][1]=position.y;
    result["size"][0]=size.x;result["size"][1]=size.y;
    auto& p=result["properties"];p["detectionEnabled"]=true;
    p["detectionX"]=-8;p["detectionY"]=0;p["detectionW"]=2;p["detectionH"]=3;
    return result;
}
bool GainedMaskArea(const Transform& original,const Transform& changed,const Sprite& sprite) {
    // Probe three opaque interior portions of a triangular spike in its own
    // local frame, including the flanks that reveal horizontal expansion.
    for(const auto local:{glm::vec2{0,-.3f},glm::vec2{.3f,.3f},glm::vec2{-.3f,.3f}}) {
        Transform probe;probe.position=changed.position+RotateOffset(local*changed.size,changed.rotation);probe.size={.04f,.04f};
        if(MaskOverlap(probe,RectangleMask(),changed,sprite)&&!MaskOverlap(probe,RectangleMask(),original,sprite))return true;
    }
    return false;
}
}

int main() {try {
    const auto assets=std::filesystem::path(IWANNA_ASSETS);
    const auto root=std::filesystem::current_path()/"morph_spikes_test_generated";
    std::filesystem::create_directories(root/"prefabs");std::filesystem::create_directories(root/"rooms");
    for(const auto* variant:Variants) {
        const auto name="morph_spike_"+std::string(variant)+".prefab.json";
        Content::Write(root/"prefabs"/name,Content::Read(assets/"Workshop/prefabs"/name));
    }
    Content::Write(root/"prefabs/morph_spike_effect.prefab.json",Content::Read(assets/"Workshop/prefabs/morph_spike_effect.prefab.json"));
    std::ifstream input(assets/"Examples/morph_spikes.lua");std::ostringstream source;source<<input.rdbuf();
    Check(!source.str().empty(),"canonical morph spike Lua source exists");
    Json::Value room;room["format"]="IWANNA_ROOM_2";room["title"]="Unannounced morph spike fixture";room["hint"]="";
    room["grid"]["width"]=96;room["grid"]["height"]=20;room["grid"]["tileSize"]=2.5;
    room["grid"]["origin"][0]=-80;room["grid"]["origin"][1]=-25;room["palette"]["1"]="terrain_moss_00.png";
    for(int y=0;y<20;++y)for(int x=0;x<96;++x)room["tileLayers"]["Terrain"][y].append(y>=18?1:0);
    room["entities"]["start"]["kind"]="Spawn";room["entities"]["start"]["position"][0]=-60;room["entities"]["start"]["position"][1]=18.9;
    room["scriptLua"]=source.str();
    for(const auto* variant:Variants) {
        auto fixture=room;fixture["entities"]["trap"]=Entity("morph_spike_"+std::string(variant),{0,18.75});
        Content::Write(root/"rooms"/(std::string(variant)+".room.json"),fixture);
    }
    auto altered=room;altered["entities"]["trap"]=Entity("morph_spike_turn_spear",{10,8},{2,3});
    altered["entities"]["trap"]["rotation"]=90;
    Content::Write(root/"rooms/override.room.json",altered);
    Content::Write(root/"rooms/empty.room.json",room);
    auto budget=room;
    for(int i=0;i<12;++i) {
        const auto x=float(i*10);auto trap=Entity("morph_spike_vertical",{x,18.75});
        trap["properties"]["detectionX"]=-50-x;
        budget["entities"]["trap_"+std::to_string(i)]=trap;
    }
    Content::Write(root/"rooms/budget.room.json",budget);
    auto replacement=room;replacement["entities"]["trap"]=Entity("morph_spike_vertical",{0,18.75});
    replacement["scriptLua"]="local morph=(function()\n"+source.str()+R"(
end)()
local on_timer=morph.on_timer
morph.on_enter=function(ctx) ctx:after(.2,'fixture_replace','trap') end
morph.on_timer=function(ctx,event)
    if event.name=='fixture_replace' then
        ctx:destroy('trap')
        ctx:spawn('morph_spike_turn_spear','trap',10,8)
        ctx:set_size('trap',2,3)
        ctx:set_rotation('trap',90)
    else on_timer(ctx,event) end
end
return morph
)";
    Content::Write(root/"rooms/replacement.room.json",replacement);
    auto watchdog=room;
    for(int i=0;i<64;++i) {
        auto trap=Entity("morph_spike_windmill",{float(i*10),18.75});
        trap["properties"]["detectionX"]=(i<8?-50:-65)-i*10;
        watchdog["entities"]["trap_"+std::to_string(i)]=trap;
    }
    watchdog["scriptLua"]="local morph=(function()\n"+source.str()+R"(
end)()
local on_timer=morph.on_timer
morph.on_enter=function(ctx) ctx:after(.35,'fixture_remove','') end
morph.on_timer=function(ctx,event)
    if event.name=='fixture_remove' then
        for i=8,55 do ctx:destroy('trap_'..i) end
    else on_timer(ctx,event) end
end
return morph
)";
    Content::Write(root/"rooms/watchdog.room.json",watchdog);
    Json::Value world;world["format"]="IWANNA_WORLD_2";world["prefabDirectory"]="prefabs";world["roomDirectory"]="rooms";
    world["startRoom"]="vertical";world["startSpawn"]="start";Content::Write(root/"world.json",world);
    GameModule game(assets,{},root/"world.json");Check(game.Startup(),game.Error());
    std::vector<std::string> sounds;game.playSound=[&](const std::string& sound){sounds.push_back(sound);};
    auto countSounds=[&] {return std::count(sounds.begin(),sounds.end(),"Block Change");};
    auto enter=[&](const std::string& id) {
        if(game.GetState()!=State::Playing){game.Restart();Tick(game);}
        game.World()->RequestRoom(id,"start");Tick(game);sounds.clear();
    };
    auto arm=[&] {
        Tick(game); // Observe the previous leave before producing a fresh enter.
        const auto id=game.Find("trap");const auto& t=game.Get<Transform>(id);const auto& c=game.Get<Collider>(id);
        Place(game,t.position+RotateOffset(c.detectionOffset,t.rotation));Tick(game);
        Check(game.GetState()==State::Playing,"the proximity detector itself cannot kill the player");
        Place(game,{-60,18.9});
    };
    for(const auto* variant:Variants) {
        enter(variant);const auto baseline=game.Get<Transform>(Effect(game));
        const auto controller=game.Find("trap");
        Check(Near(baseline.size,{2.5f,2.5f})&&Near(baseline.position,{0,18.75f})&&baseline.rotation==0,
            std::string(variant)+": authored ordinary spike appearance is preserved before activation");
        Check(!game.Get<Sprite>(controller).visible&&game.Get<Behavior>(controller).role==Role::Trigger,
            "the controller is hidden and uses a nonlethal trigger role");
        const auto spike=Effect(game);const auto& sprite=game.Get<Sprite>(spike);
        Check(sprite.visible&&sprite.image=="demo_spike.png"&&sprite.opacity==1&&sprite.masks&&game.Get<Collider>(spike).enabled,
            "idle spike is visible, opaque and has a live image alpha mask");
        Tick(game,12);Check(countSounds()==0&&Original(baseline,game.Get<Transform>(Effect(game))),"idle state has no sound, motion or advance warning");
        arm();Check(countSounds()==1,"trigger starts a transformation and Block Change immediately");
        const auto firstCount=countSounds();arm();
        Check(countSounds()==firstCount,"repeated entry cannot restart or duplicate an active transformation");
        glm::vec2 largest=baseline.size,smallest=baseline.size;float largestAngle=0;bool expandedMask=false;
        int lastSoundFrame=0;auto previousSounds=countSounds();
        for(int i=1;i<=480;++i) {
            Tick(game);const auto effect=Effect(game);const auto& t=game.Get<Transform>(effect);
            const auto& s=game.Get<Sprite>(effect);
            Check(game.Get<Collider>(effect).enabled&&s.visible&&s.opacity==1,"all transformation phases retain lethal visible collision without blinking");
            Check(std::isfinite(t.position.x)&&std::isfinite(t.position.y)&&t.size.x>0&&t.size.y>0,"compound transformations remain finite and nondegenerate");
            largest=glm::max(largest,t.size);smallest=glm::min(smallest,t.size);largestAngle=std::max(largestAngle,std::abs(t.rotation));
            if(!expandedMask)expandedMask=GainedMaskArea(baseline,t,s);
            const auto currentSounds=countSounds();
            if(currentSounds!=previousSounds) {
                Check(currentSounds==previousSounds+1,"one sound is emitted for a phase transition");
                Check(i-lastSoundFrame>=3,"Block Change is not played on every interpolation tick");
                lastSoundFrame=i;previousSounds=currentSounds;
            }
        }
        Check(expandedMask,std::string(variant)+": changing size/rotation expands the actual hazardous mask");
        Check(Original(baseline,game.Get<Transform>(Effect(game))),std::string(variant)+": full sequence restores exact authored size, pivot position and rotation");
        Check(Near(game.Get<Motion>(Effect(game)).velocity,{}),"restored spike has no residual drift");
        const std::string name=variant;
        const auto expectedSounds=name=="staircase_combo"?5:(name=="windmill"||name=="stutter")?4:
            (name=="squash_pop"||name=="cross"||name=="turn_spear")?3:2;
        Check(countSounds()==expectedSounds,std::string(variant)+": every authored transformation phase, including recovery, has exactly one Block Change sound");
        if(name=="vertical")Check(largest.y>baseline.size.y*2&&largest.x<baseline.size.x*1.1f,"vertical variant stretches vertically");
        if(name=="horizontal")Check(largest.x>baseline.size.x*2&&largest.y<baseline.size.y*1.1f,"horizontal variant stretches horizontally");
        if(name=="giant")Check(largest.x>baseline.size.x*2&&largest.y>baseline.size.y*2,"giant variant enlarges both axes");
        if(name=="squash_pop")Check(smallest.y<baseline.size.y*.8f&&largest.y>baseline.size.y*2,"squash/pop uses a deceptive contraction followed by extension");
        if(name=="turn_spear"||name=="windmill"||name=="diagonal_giant"||name=="staircase_combo")
            Check(largestAngle>30,std::string(variant)+": sequence includes rotation as well as scaling");
        if(name=="cross")Check(largest.x>baseline.size.x*2&&largest.y>baseline.size.y*2,"cross performs both horizontal and vertical attacks");
        const auto completeCount=countSounds();arm();Check(countSounds()==completeCount+1,"leaving and entering after cooldown starts a fresh cycle");
        Tick(game,480);Check(Original(baseline,game.Get<Transform>(Effect(game))),"repeated cycles do not accumulate pivot drift or extra entities");
    }
    // Idle appearance is deceptive but the actual spike remains a normal lethal
    // hazard before its proximity sensor has ever fired.
    enter("vertical");Place(game,{0,18.75});Tick(game);
    Check(game.GetState()==State::Dead,"ordinary idle spike kills through the real game collision system");
    Check(Effects(game).empty(),"death removes all managed spike hazards");
    game.Restart();Tick(game,12);Check(Effects(game).size()==1,"respawn reconstructs exactly one ordinary spike");
    // A point safely above the original spike becomes lethal during elongation.
    enter("vertical");arm();
    for(int i=0;i<120&&game.Get<Transform>(Effect(game)).size.y<8;++i)Tick(game);
    const auto grown=game.Get<Transform>(Effect(game));Check(grown.size.y>=8,"vertical spike reaches its dangerous extended phase");
    Place(game,grown.position+RotateOffset({0,-grown.size.y*.2f},grown.rotation));Tick(game);
    Check(game.GetState()==State::Dead&&Effects(game).empty(),"transformed alpha mask harms the player and death cancels animation tasks");
    game.Restart();Tick(game,480);Check(Effects(game).size()==1&&Near(game.Get<Transform>(Effect(game)).size,{2.5f,2.5f}),"old timers cannot modify the respawned idle spike");
    enter("override");const auto overridden=game.Get<Transform>(Effect(game));
    Check(Near(overridden.position,{10,8})&&Near(overridden.size,{2,3})&&std::abs(overridden.rotation-90)<.01f,"instance dimensions and rotation initialize the child hazard");
    arm();Tick(game,480);Check(Original(overridden,game.Get<Transform>(Effect(game))),"a rotated non-square instance restores its own authored transform");
    arm();Tick(game,8);game.World()->RequestRoom("empty","start");Tick(game,480);
    Check(Effects(game).empty()&&game.GetState()==State::Playing,"room transitions discard all old spike tasks and managed hazards");
    enter("replacement");const auto retiredName=game.Get<Behavior>(Effect(game)).stableId;
    arm();Tick(game,30);const auto replaced=game.Get<Transform>(Effect(game));
    Check(!game.Find(retiredName)&&Near(replaced.position,{10,8})&&Near(replaced.size,{2,3})&&std::abs(replaced.rotation-90)<.01f,
        "destroy/spawn reusing a controller ID cancels the old task and initializes the new prefab's instance transform");
    const auto replacementSounds=countSounds();arm();Tick(game,480);
    Check(countSounds()==replacementSounds+3&&Original(replaced,game.Get<Transform>(Effect(game))),
        "a reused controller ID runs the replacement variant rather than stale prior behavior");
    enter("budget");Check(Effects(game).size()==12,"idle hazards do not consume active animation slots");
    Place(game,{-50,18.9});Tick(game);Place(game,{-60,18.9});
    Check(countSounds()==8,"simultaneous triggers respect the eight-active-task limit");
    size_t mostChanged=0;
    for(int i=0;i<480;++i) {
        Tick(game);const auto effects=Effects(game);Check(effects.size()==12,"concurrent cycles retain one hazard per controller without temporary growth");
        size_t changed=0;for(auto id:effects)if(!Near(game.Get<Transform>(id).size,{2.5f,2.5f}))++changed;
        mostChanged=std::max(mostChanged,changed);Check(changed<=8,"animation budget limits concurrently transformed spikes");
    }
    Check(mostChanged==8,"all eight accepted simultaneous attacks actually animate");
    for(auto id:Effects(game))Check(Near(game.Get<Transform>(id).size,{2.5f,2.5f})&&Near(game.Get<Motion>(id).velocity,{}),"budget stress recovers to stationary ordinary spikes");
    Place(game,{-50,18.9});Tick(game);Place(game,{-60,18.9});game.Kill();Tick(game);
    Check(Effects(game).empty(),"death clears active and idle hazards without orphaned children");
    enter("watchdog");Check(Effects(game).size()==64,"the documented maximum of 64 ordinary hazards initializes safely");
    Tick(game,46); // Eight attacks reach a phase boundary close to the .5 s watch.
    for(int i=56;i<64;++i)game.Get<Transform>(game.Find("trap_"+std::to_string(i))).position.y-=2;
    Place(game,{-50,18.9});Tick(game);Place(game,{-60,18.9});Tick(game,480);
    Check(Effects(game).size()==16,"bounded watchdog work eventually removes 48 orphaned hazards while preserving live controllers");
    for(auto id:Effects(game))Check(Near(game.Get<Transform>(id).size,{2.5f,2.5f}),"concurrent watchdog cleanup and compound phase changes recover without exhausting the command queue");
    for(const auto* invalid:{"unknown_variant","negative_scale","nonfinite_size","oversized_shape","excessive_speed"}) {
        Scripting::LuaModule lua;Check(lua.LoadSource(source.str(),"morph_validation"),lua.Error());
        lua.queryPosition=[](const std::string&)->std::optional<Scripting::LuaModule::Position>{return Scripting::LuaModule::Position{0,0};};
        Scripting::Event event{"on_entity_spawn","morph_spike","trap","spawn"};
        event.properties["variant"]=std::string("vertical");event.properties["sizeX"]=2.5;event.properties["sizeY"]=2.5;
        const std::string bad=invalid;
        if(bad=="unknown_variant")event.properties["variant"]=std::string("not_a_pattern");
        if(bad=="negative_scale")event.properties["scaleX"]=-1.0;
        if(bad=="nonfinite_size")event.properties["sizeY"]=std::numeric_limits<double>::infinity();
        if(bad=="oversized_shape"){event.properties["sizeY"]=20.0;event.properties["scaleY"]=4.0;}
        if(bad=="excessive_speed"){event.properties["sizeY"]=20.0;event.properties["speedScale"]=3.0;}
        Check(!lua.Call(event)&&lua.TakeCommands().empty(),"invalid morph configuration fails before any world mutation");
    }
    std::cout<<"Morph spikes PASS: ten ordinary idle hazards, immediate no-warning activation, phase sounds, transformed masks, compound recovery, instance overrides, repeat/dedup, eight-task budget and death/room cleanup\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
