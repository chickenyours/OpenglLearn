#include "IWanna/Public/editor_document.h"
#include "IWanna/Public/game_module.h"
#include <iostream>
using namespace IWanna;
static void Check(bool x,const char* why){if(!x)throw std::runtime_error(why);}
int main(){try{
    auto assets=std::filesystem::path(IWANNA_ASSETS);auto root=std::filesystem::current_path()/"topology_test_generated";
    // Each run uses a new directory; no removal of user-created content.
    int n=0;while(std::filesystem::exists(root))root=std::filesystem::current_path()/("topology_test_generated_"+std::to_string(++n));
    std::filesystem::create_directories(root);std::filesystem::copy(assets/"Workshop",root,std::filesystem::copy_options::recursive);
    EditorDocument d;d.Open(root/"world.json");Check(d.nativeWorld&&d.NativeRoom(),"native assets open");
    auto choose=[&](const std::string& name){for(int i=0;i<int(d.world["rooms"].size());++i)if(d.world["rooms"][i]["id"]==name){d.LoadRoom(i);return;}throw std::runtime_error("missing room "+name);};
    choose("movement");auto count=d.world["rooms"].size();std::filesystem::copy_file(root/"rooms/movement.room.json",root/"rooms/movement_copy.room.json");d.Refresh();Check(d.world["rooms"].size()==count+1,"filesystem copy discovered");choose("movement_copy");
    std::filesystem::copy_file(root/"prefabs/apple.prefab.json",root/"prefabs/apple_copy.prefab.json");d.Refresh();Check(d.catalog.prefabs.contains("apple_copy"),"prefab file copy discovered");
    std::u8string unicodeName=u8"movement - \u526f\u672c";std::string unicodeId(unicodeName.begin(),unicodeName.end());
    std::filesystem::copy_file(root/"rooms/movement.room.json",root/"rooms"/Content::Path(unicodeId+".room.json"));d.Refresh();choose(unicodeId);d.Place("apple",{0,-20});d.Save();Check(d.catalog.rooms.contains(unicodeId),"UTF8 file copy loads and saves");choose("movement_copy");
    auto raw=Content::Read(d.mapPath);raw["metadata"]["agent_note"]="preserve me";Content::Write(d.mapPath,raw);d.Refresh();
    d.Place("apple",{1.234f,-10.5f});d.selection={d.selected};auto original=EditorDocument::Props(*d.Selected())["uid"].asString();d.SetField("speed","18");d.EnableBox();d.Duplicate({2.5f,0});auto clone=EditorDocument::Props(*d.Selected())["uid"].asString();Check(original!=clone,"clone ID unique");Check(EditorDocument::Props(*d.Selected())["speed"].asInt()==18&&d.Box(*d.Selected()).size==d.ObjectTransform(*d.Selected()).size,"clone preserves instance data and box");
    d.Undo();Check(d.Hit({3.734f,-10.5f})<0,"duplicate undo");d.Redo();
    d.SelectArea({-5,-13},{8,-8});Check(d.selection.size()==2,"lasso selects both");auto before=d.Objects().size();d.Duplicate({10,0});Check(d.Objects().size()==before+2&&d.selection.size()==2,"group duplication");
    auto pos=d.ObjectTransform(*d.Selected()).position;auto snapped=d.Snap({pos.x,10.04f},.2f);Check(std::abs(snapped.y-10.04f)<.21f,"snap threshold bounded");
    d.Save();Check(Content::Read(d.mapPath)["metadata"]["agent_note"]=="preserve me","unknown room metadata preserved");
    d.Boundary("right","traps/left");d.Save();Check(RoomCatalog::Load(root/"world.json").rooms.at("movement_copy").connections.at("right").room=="traps","boundary link roundtrip");
    d.NewRoom("empty_room",false);Check(d.Objects().size()==1&&d.world["rooms"][d.roomIndex]["id"]=="empty_room","new blank room");d.NewRoom("empty_clone",true);Check(d.Objects().size()==1,"room prefab cloning");d.DeleteRoom();Check(!std::filesystem::exists(root/"rooms/empty_clone.room.json")&&std::filesystem::exists(root/"rooms/.trash/empty_clone.room.json"),"delete uses recoverable trash");
    choose("traps");bool rejected=false;try{d.DeleteRoom();}catch(...){rejected=true;}Check(rejected,"inbound room link prevents deletion");
    choose("movement_copy");d.Boundary("right","-");d.Save();Check(d.catalog.rooms.at("movement_copy").connections.empty(),"remove boundary");
    // Prefab rotation is a structural transform, preserved through editor save and usable by Lua.
    choose("movement");for(int i=0;i<int(d.Objects().size());++i)if(EditorDocument::Props(d.Objects()[i])["uid"]=="spike_13"){d.selected=i;break;}
    Check(d.Selected()!=nullptr,"find spike for rotation");d.SetField("rotation","90");d.Save();
    auto rotated=RoomCatalog::Load(root/"world.json");bool sawRotation=false;for(const auto& o:rotated.rooms.at("movement").objects)if(o.id=="spike_13")sawRotation=o.transform.rotation==90;
    Check(sawRotation,"editor prefab rotation roundtrip");
    auto spikeTemplate=Content::Read(root/"prefabs/spike.prefab.json");spikeTemplate["rotation"]=30;Content::Write(root/"prefabs/spike.prefab.json",spikeTemplate);
    rotated=RoomCatalog::Load(root/"world.json");bool inherited=false;for(const auto& o:rotated.rooms.at("movement").objects)if(o.id=="spike_15")inherited=o.transform.rotation==30;
    Check(inherited,"instance inherits prefab rotation");
    d.map["_native"]["scriptLua"]="return { on_enter=function(ctx) ctx:set_rotation('spike_13',180) end }";d.Save();
    GameModule rotationGame(assets,{},root/"world.json");Check(rotationGame.Startup(),rotationGame.Error().c_str());
    Check(rotationGame.Find("spike_13")&&rotationGame.Get<Transform>(rotationGame.Find("spike_13")).rotation==180,"Lua live prefab rotation");rotationGame.Shutdown();
    d.map["_native"]["scriptLua"]="return { on_enter=function(ctx) ctx:set_player_scale(0.5,1.25) end, on_timer=function(ctx) ctx:set_player_size(2.25,3.75) end }";d.Save();
    GameModule scaledGame(assets,{},root/"world.json");Check(scaledGame.Startup(),scaledGame.Error().c_str());
    auto scaledId=scaledGame.PlayerEntity();auto scaled=scaledGame.Get<Transform>(scaledId);
    Check(std::abs(scaled.size.x-2.4f)<.001f&&std::abs(scaled.size.y-4.5f)<.001f,"Lua sets independent player XY scale");
    auto initial=scaledGame.World()->Current().spawns.at("left");
    auto base=GameConfig::Load(assets/"gameplay.json");const float footRatio=base.character.offsetRatio.y+base.character.sizeRatio.y*.5f;
    Check(std::abs(scaled.position.y+scaled.size.y*footRatio-(initial.y+base.ScaledPlayerSize().y*footRatio))<.001f,"live player resize keeps collision feet anchored");
    Check(std::abs(scaledGame.PlayerBody().size.x-scaled.size.x*base.character.sizeRatio.x)<.001f,"live player scale updates collision");
    // The second API accepts absolute world units and runs through the same command queue.
    d.map["_native"]["scriptLua"]="return { on_enter=function(ctx) ctx:after(0, 'size') end, on_timer=function(ctx) ctx:set_player_size(2.25,3.75) end }";d.Save();
    scaledGame.Shutdown();GameModule sizedGame(assets,{},root/"world.json");Check(sizedGame.Startup(),sizedGame.Error().c_str());
    sizedGame.FixedTick({});auto size=sizedGame.Get<Transform>(sizedGame.PlayerEntity()).size;
    Check(std::abs(size.x-2.25f)<.001f&&std::abs(size.y-3.75f)<.001f,"Lua sets absolute player XY size");sizedGame.Shutdown();
    // A wider body beside a wall must be rejected without moving or resizing
    // the player, rather than leaving the physics solver stuck inside the wall.
    d.map["_native"]["scriptLua"]="return { on_enter=function(ctx) ctx:after(0, 'grow') end, on_timer=function(ctx) ctx:set_player_scale(1, 0.75) end }";
    d.Place("platform",{initial.x+1.85f,initial.y+base.ScaledPlayerSize().y*base.character.offsetRatio.y});
    d.selection.clear();d.EnableBox();d.Save();
    GameModule blockedScale(assets,{},root/"world.json");Check(blockedScale.Startup(),blockedScale.Error().c_str());
    auto oldSize=blockedScale.Get<Transform>(blockedScale.PlayerEntity()).size;
    blockedScale.FixedTick({});
    Check(blockedScale.World()->Error().find("intersect solid")!=std::string::npos,"unsafe player growth is rejected");
    Check(blockedScale.Get<Transform>(blockedScale.PlayerEntity()).size==oldSize,"failed growth preserves player dimensions");
    blockedScale.Shutdown();d.Erase();d.map["_native"]["scriptLua"]="return {}";d.Save();
    d.map["_native"]["scriptLua"]="return { on_enter=function(ctx) ctx:set_player_scale(0, 1) end }";d.Save();
    GameModule invalidScale(assets,{},root/"world.json");Check(invalidScale.Startup(),invalidScale.Error().c_str());
    Check(invalidScale.World()->Error().find("Invalid player scale")!=std::string::npos,"invalid Lua scale reports room error");
    Check(invalidScale.Get<Transform>(invalidScale.PlayerEntity()).size==base.ScaledPlayerSize(),"invalid Lua scale preserves default size");
    invalidScale.Shutdown();d.map["_native"]["scriptLua"]="return {}";d.Save();
    // Crossing room edges is independent of any adjacency or room ordering.
    choose("movement");d.Boundary("right","gallery/left@0,-10");d.Save();
    GameModule game(assets,{},root/"world.json");Check(game.Startup(),game.Error().c_str());auto player=game.PlayerEntity();game.Get<Transform>(player).position={75.2f,-5};game.Get<Motion>(player).velocity={};game.FixedTick({});Check(game.World()->CurrentId()=="gallery"&&game.GetState()==State::Playing,"right boundary transfer");game.FixedTick({});Check(game.World()->CurrentId()=="gallery","arrival does not bounce");
    Check(std::abs(game.Get<Transform>(game.PlayerEntity()).position.x)<1&&std::abs(game.Get<Transform>(game.PlayerEntity()).position.y+10)<1,"boundary arrival uses explicit position");
    game.Shutdown();
    for(const auto& edge:{"left","top","bottom"}){
        choose("movement");d.Boundary(edge,"gallery/left");d.Save();GameModule probe(assets,{},root/"world.json");Check(probe.Startup(),probe.Error().c_str());auto id=probe.PlayerEntity();glm::vec2 p=std::string(edge)=="left"?glm::vec2(-75.2f,-5):std::string(edge)=="top"?glm::vec2(0,-42.7f):glm::vec2(0,42.7f);
        probe.Get<Transform>(id).position=p;probe.Get<Motion>(id).velocity={};probe.FixedTick({});Check(probe.World()->CurrentId()=="gallery"&&probe.GetState()==State::Playing,"all four boundary directions transfer");
    }
    choose("movement");d.Boundary("left","death");d.Save();GameModule fatal(assets,{},root/"world.json");Check(fatal.Startup(),fatal.Error().c_str());fatal.Get<Transform>(fatal.PlayerEntity()).position={-75.2f,-5};fatal.FixedTick({});Check(fatal.GetState()==State::Dead,"explicit death boundary");fatal.Shutdown();
    choose("movement");d.Boundary("top","ignore");d.Save();GameModule ignored(assets,{},root/"world.json");Check(ignored.Startup(),ignored.Error().c_str());ignored.Get<Transform>(ignored.PlayerEntity()).position={0,-42.7f};ignored.Get<Motion>(ignored.PlayerEntity()).velocity={};ignored.FixedTick({});Check(ignored.GetState()==State::Playing&&ignored.World()->CurrentId()=="movement","ignored boundary");ignored.Shutdown();
    choose("movement");d.Boundary("bottom","-");d.Save();GameModule defaultDeath(assets,{},root/"world.json");Check(defaultDeath.Startup(),defaultDeath.Error().c_str());defaultDeath.Get<Transform>(defaultDeath.PlayerEntity()).position={0,42.7f};defaultDeath.Get<Motion>(defaultDeath.PlayerEntity()).velocity={};defaultDeath.FixedTick({});Check(defaultDeath.GetState()==State::Dead,"unconfigured boundary defaults to death");defaultDeath.Shutdown();
    // A single edge can serve separate floors, while every unmatched portion
    // keeps its explicit default action. Segments use world coordinates.
    d.NewRoom("segment_target",false);choose("movement");
    Json::Value split(Json::objectValue);split["action"]="death";
    auto segment=[](float low,float high,const std::string& room){Json::Value s;s["range"][0]=low;s["range"][1]=high;s["action"]="transfer";s["room"]=room;s["spawn"]="left";return s;};
    split["segments"].append(segment(-25,-10,"gallery"));split["segments"].append(segment(0,20,"segment_target"));
    auto& boundaryData=d.map["_native"]["boundaries"];boundaryData=Json::Value(Json::objectValue);boundaryData["right"]=split;
    d.map["_native"]["connections"]=Json::Value(Json::objectValue);d.Save();
    auto splitCatalog=RoomCatalog::Load(root/"world.json");const auto& splitRoom=splitCatalog.rooms.at("movement");
    Check(splitRoom.boundaries.at("right").segments.size()==2&&splitRoom.connections.at("right[0]").room=="gallery"&&splitRoom.connections.at("right[1]").room=="segment_target","all segmented transfer targets appear in topology");
    Check(Content::Read(d.mapPath)["boundaries"]["right"]==split,"editor save preserves segmented boundary rules");
    choose("segment_target");rejected=false;try{d.DeleteRoom();}catch(...){rejected=true;}Check(rejected,"segment-only inbound link prevents deletion");choose("movement");
    auto probeBoundary=[&](glm::vec2 position,const std::string& expectedRoom,State expectedState,const char* why){
        GameModule probe(assets,{},root/"world.json");Check(probe.Startup(),probe.Error().c_str());
        probe.Get<Transform>(probe.PlayerEntity()).position=position;
        probe.World()->CrossBoundary(position);probe.World()->EndTick(0);
        Check(probe.World()->Error().empty(),probe.World()->Error().c_str());
        Check(probe.World()->CurrentId()==expectedRoom&&probe.GetState()==expectedState,why);probe.Shutdown();
    };
    probeBoundary({75.2f,-20},"gallery",State::Playing,"upper floor uses upper segment destination");
    probeBoundary({75.2f,10},"segment_target",State::Playing,"lower floor uses lower segment destination");
    probeBoundary({75.2f,-25},"gallery",State::Playing,"segment minimum is inclusive");
    probeBoundary({75.2f,-10},"movement",State::Dead,"segment maximum is exclusive");
    probeBoundary({75.2f,20},"movement",State::Dead,"lower segment maximum restores edge default");
    for(glm::vec2 point:{glm::vec2(75.2f,-30),glm::vec2(75.2f,-5),glm::vec2(75.2f,30),glm::vec2(-75.2f,10),glm::vec2(0,-42.7f),glm::vec2(0,42.7f)})
        probeBoundary(point,"movement",State::Dead,"unconfigured edge or segment gap causes death");
    probeBoundary({75,10},"movement",State::Playing,"touching edge without crossing preserves center-based boundary semantics");
    // The same generic rule also uses world X for horizontal edges, supports
    // $self and explicit arrival positions, and allows ignore segments.
    auto self=segment(20,25,"$self");self.removeMember("spawn");self["position"][0]=0;self["position"][1]=-10;
    d.map["_native"]["boundaries"]["right"]["segments"].append(self);
    auto top=segment(-30,10,"gallery");d.map["_native"]["boundaries"]["top"]["action"]="death";d.map["_native"]["boundaries"]["top"]["segments"].append(top);
    Json::Value ignore;ignore["range"][0]=-5;ignore["range"][1]=5;ignore["action"]="ignore";
    d.map["_native"]["boundaries"]["left"]["action"]="death";d.map["_native"]["boundaries"]["left"]["segments"].append(ignore);d.Save();
    Check(d.catalog.rooms.at("movement").connections.at("right[2]").room=="movement","self segment resolves to owning room");
    probeBoundary({75.2f,20},"movement",State::Playing,"touching adjacent ranges chooses the next half-open segment");
    probeBoundary({-20,-42.7f},"gallery",State::Playing,"horizontal segments match world X");
    probeBoundary({10,-42.7f},"movement",State::Dead,"horizontal segment maximum is exclusive");
    probeBoundary({-75.2f,0},"movement",State::Playing,"ignore segment overrides death default");
    // Invalid files must fail loading rather than silently changing routes.
    const auto validSplit=Content::Read(d.mapPath);
    auto rejectBoundary=[&](Json::Value rule,const char* why){auto bad=validSplit;bad["boundaries"]["right"]=rule;Content::Write(d.mapPath,bad);bool failed=false;try{RoomCatalog::Load(root/"world.json");}catch(...){failed=true;}Content::Write(d.mapPath,validSplit);Check(failed,why);};
    auto bad=split;bad["segments"]=Json::Value(Json::objectValue);rejectBoundary(bad,"segments must be an array");
    bad=split;bad["segments"][0].removeMember("range");rejectBoundary(bad,"segment range is required");
    bad=split;bad["segments"][0]["range"][0]="-25";rejectBoundary(bad,"range values must be numbers");
    bad=split;bad["segments"][0]["range"][1]=-25;rejectBoundary(bad,"empty range rejected");
    bad=split;bad["segments"][0]["range"][0]=1e100;rejectBoundary(bad,"non-finite runtime coordinates rejected");
    bad=split;bad["segments"][0]["range"][0]=-43;rejectBoundary(bad,"out-of-edge range rejected");
    bad=split;bad["segments"][1]["range"][0]=-15;rejectBoundary(bad,"overlapping ranges rejected");
    bad=split;bad["segments"][0]["segments"]=Json::Value(Json::arrayValue);rejectBoundary(bad,"nested segments rejected");
    bad=split;bad["segments"][0]["room"]="missing_room";rejectBoundary(bad,"unknown segment destination rejected");
    bad=split;bad["segments"][0]["spawn"]="missing_spawn";rejectBoundary(bad,"unknown segment spawn rejected");
    bad=split;bad["segments"][0]["position"][0]=10000;bad["segments"][0]["position"][1]=0;rejectBoundary(bad,"out-of-room segment arrival rejected");
    bad=split;bad["segments"][0]["action"]="death";rejectBoundary(bad,"death segment cannot carry a transfer target");
    d.Refresh();
    // Malformed externally pasted assets fail refresh without replacing the active document.
    auto previous=d.map;Content::Write(root/"rooms/bad.room.json",Json::Value("bad"));rejected=false;try{d.Refresh();}catch(...){rejected=true;}Check(rejected&&d.map==previous,"failed refresh retains document");
    std::cout<<"Room assets, copy, lasso, topology and runtime PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
