#include "IWanna/Public/editor_document.h"
#include "IWanna/Public/game_module.h"
#include <iostream>
using namespace IWanna;
void Check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int main(){try{
    auto source=std::filesystem::path(IWANNA_ASSETS);auto root=std::filesystem::current_path()/"editor_test_generated";
    // Painting and erasing native terrain updates adjoining edge art without
    // changing the underlying solid/non-solid cells.
    {
        EditorDocument terrain;
        terrain.map=Content::ToTiled(Content::Read(source/"Workshop/rooms/movement.room.json"));
        auto cell=[&](int x,int y){return terrain.map["layers"][0]["data"][y*terrain.map["width"].asInt()+x].asInt();};
        auto point=[&](int x,int y){return terrain.Origin()+glm::vec2(x+.5f,y+.5f)*terrain.Unit();};
        Check(cell(1,1)==0&&cell(2,1)==0,"terrain test cells clear");
        terrain.Paint(point(1,1),1);Check(cell(1,1)==16,"isolated terrain gets four exposed edges");
        terrain.Paint(point(2,1),1);Check(cell(1,1)==14&&cell(2,1)==8,"joined terrain updates both edges");
        terrain.Paint(point(2,1),0);Check(cell(1,1)==16&&cell(2,1)==0,"erasing restores exposed edge");
        auto gate=terrain.map["layers"][0];gate["name"]="ShotGate";for(auto& value:gate["data"])value=0;
        auto layers=terrain.map["layers"];terrain.map["layers"].clear();terrain.map["layers"].append(gate);for(const auto& layer:layers)terrain.map["layers"].append(layer);
        terrain.Paint(point(3,1),1);Check(terrain.map["layers"][0]["data"][terrain.map["width"].asInt()+3]==0,"terrain paint cannot accidentally modify alphabetically first gate layer");
        terrain.CycleTileLayer();Check(terrain.PaintLayer()=="ShotGate","tile layer can be selected");terrain.Paint(point(3,1),1);
        Check(terrain.map["layers"][0]["data"][terrain.map["width"].asInt()+3].asInt()!=0,"selected gate layer is editable");
    }
    std::filesystem::create_directories(root);std::filesystem::copy(source/"Showcase",root,std::filesystem::copy_options::recursive|std::filesystem::copy_options::overwrite_existing);
    {
        // Boundary edits operate on authored ranges, preserving other floors
        // and the default action through save, undo and subsequent edits.
        auto nativeRoot=root/"segmented";std::filesystem::create_directories(nativeRoot);
        std::filesystem::copy(source/"Workshop",nativeRoot,std::filesystem::copy_options::recursive|std::filesystem::copy_options::overwrite_existing);
        EditorDocument segmented;segmented.Open(nativeRoot/"world.json");
        for(int i=0;i<int(segmented.world["rooms"].size());++i)if(segmented.world["rooms"][i]["id"]=="movement")segmented.LoadRoom(i);
        segmented.Boundary("right","-");
        segmented.Boundary("right[-20,-5]","traps/left");segmented.Boundary("right[5,20]","gallery/left");
        auto pending=segmented.map;bool invalid=false;
        try{segmented.Boundary("right[-10,10]","gallery/left");}catch(...){invalid=true;}
        Check(invalid&&segmented.map==pending,"overlapping boundary edit preserves all prior ranges");
        invalid=false;try{segmented.Boundary("right[20,5]","death");}catch(...){invalid=true;}
        Check(invalid&&segmented.map==pending,"backwards boundary edit rejected without mutation");
        segmented.Boundary("right","death");Check(segmented.BoundaryRules()["right"]["segments"].size()==2,"editing default preserves floor exits");
        segmented.Boundary("right[-20,-5]","gallery/left@0,-10");segmented.Save();
        auto rules=segmented.catalog.rooms.at("movement").boundaries.at("right");
        Check(rules.segments.size()==2&&rules.segments[0].room=="gallery"&&rules.segments[0].position==glm::vec2(0,-10),"edited boundary segments survive runtime reload");
        segmented.Boundary("right[-20,-5]","-");Check(segmented.BoundaryRules()["right"]["segments"].size()==1,"deleting one boundary range retains other floor");
        segmented.Undo();Check(segmented.BoundaryRules()["right"]["segments"].size()==2,"boundary range deletion is undoable");
        segmented.Boundary("right","-");Check(!segmented.BoundaryRules().isMember("right"),"clearing full edge also clears its segments");
    }
    EditorDocument d;d.Open(root/"world.json");auto original=d.map;d.Begin();d.Paint({-73,-40},1);Check(d.Dirty(),"tile paint dirty");d.Undo();Check(d.map==original,"tile undo");d.Redo();Check(d.Dirty(),"tile redo");d.Undo();
    d.Place("apple",{-23.456f,4.321f});auto id=EditorDocument::Props(*d.Selected())["uid"].asString();Check(glm::length(d.ObjectTransform(*d.Selected()).position-glm::vec2(-23.456f,4.321f))<.0001f,"free coordinates preserved");
    d.EnableBox();d.SetField("detectionX","8");d.SetField("detectionW","2");d.SetField("detectionH","3");d.SetField("damage","7");d.SetField("event","editor_touch");d.SetField("x","-20.123");
    auto before=d.map;bool rejected=false;try{d.SetField("detectionH","-3");}catch(...){rejected=true;}Check(rejected&&before==d.map,"bad box edit rolled back");
    d.Save();Check(!d.Dirty(),"save clears dirty");Check(std::filesystem::exists(d.mapPath.string()+".editor.bak"),"backup written");
    auto catalog=RoomCatalog::Load(root/"world.json");const RoomObject* found=nullptr;for(const auto& o:catalog.rooms.at("movement").objects)if(o.id==id)found=&o;
    Check(found&&found->detectionBox&&found->detectionOffset.x==8&&found->detectionSize==glm::vec2(2,3),"runtime imports editor box");Check(found->properties["damage"].asInt()==7,"custom property roundtrip");
    // A custom instance value reaches Lua through the trigger event.
    {std::ofstream lua(root/"scripts/movement.lua");lua<<"return {on_trigger=function(ctx,e) if e.name=='editor_touch' then ctx:message(tostring(e.properties.damage)) end end}";}
    GameModule game(source,{},root/"world.json");Check(game.Startup(),game.Error().c_str());auto entity=game.Find(id);Check(entity!=0,"edited prefab created in ECS");
    auto& c=game.Get<Collider>(entity);Check(c.detectionBox&&c.detectionOffset.x==8,"ECS detection data");
    auto player=game.PlayerEntity();auto center=game.Get<Transform>(entity).position;
    game.Get<Transform>(player).position=center;game.Get<Motion>(player).velocity={};game.FixedTick({});Check(game.GetState()==State::Playing,"sprite outside offset box does not hit");
    game.Get<Transform>(player).position=center+glm::vec2(8,0);game.Get<Motion>(player).velocity={};game.FixedTick({});Check(game.GetState()==State::Dead,"offset box outside sprite actually hits");
    game.Restart();game.FixedTick({});entity=game.Find(id);player=game.PlayerEntity();game.Get<Behavior>(entity).role=Role::Trigger;
    game.Get<Transform>(player).position=center+glm::vec2(8,0);game.Get<Motion>(player).velocity={};game.FixedTick({});
    Check(game.World()->Message()=="7.0"||game.World()->Message()=="7","Lua receives custom instance properties");
    d.Erase();Check(d.Dirty(),"delete dirty");d.Undo();Check(d.map==before,"delete undo restores all fields");
    // A spawn referenced by another room cannot be deleted and saved silently.
    for(int i=0;i<int(d.Objects().size());++i)if(EditorDocument::Props(d.Objects()[i])["uid"]=="left")d.selected=i;
    auto disk=EditorDocument::Read(d.mapPath);d.Erase();rejected=false;try{d.Save();}catch(...){rejected=true;}
    Check(rejected&&EditorDocument::Read(d.mapPath)==disk,"invalid world save preserves disk");d.Undo();
    d.LoadRoom(1);Check(d.world["rooms"][d.roomIndex]["id"]=="traps","room switch");
    std::cout<<"Editor document and runtime integration PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
