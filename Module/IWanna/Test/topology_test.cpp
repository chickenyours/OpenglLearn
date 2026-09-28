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
    // Crossing room edges is independent of any adjacency or room ordering.
    choose("movement");d.Boundary("right","gallery/left");d.Save();
    GameModule game(assets,{},root/"world.json");Check(game.Startup(),game.Error().c_str());auto player=game.PlayerEntity();game.Get<Transform>(player).position={75.2f,-5};game.Get<Motion>(player).velocity={};game.FixedTick({});Check(game.World()->CurrentId()=="gallery"&&game.GetState()==State::Playing,"right boundary transfer");game.FixedTick({});Check(game.World()->CurrentId()=="gallery","arrival does not bounce");
    game.Shutdown();
    for(const auto& edge:{"left","top","bottom"}){
        choose("movement");d.Boundary(edge,"gallery/left");d.Save();GameModule probe(assets,{},root/"world.json");Check(probe.Startup(),probe.Error().c_str());auto id=probe.PlayerEntity();glm::vec2 p=std::string(edge)=="left"?glm::vec2(-75.2f,-5):std::string(edge)=="top"?glm::vec2(0,-42.7f):glm::vec2(0,42.7f);
        probe.Get<Transform>(id).position=p;probe.Get<Motion>(id).velocity={};probe.FixedTick({});Check(probe.World()->CurrentId()=="gallery"&&probe.GetState()==State::Playing,"all four boundary directions transfer");
    }
    // Malformed externally pasted assets fail refresh without replacing the active document.
    auto previous=d.map;Content::Write(root/"rooms/bad.room.json",Json::Value("bad"));rejected=false;try{d.Refresh();}catch(...){rejected=true;}Check(rejected&&d.map==previous,"failed refresh retains document");
    std::cout<<"Room assets, copy, lasso, topology and runtime PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
