#include "IWanna/Public/game_module.h"
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <set>

namespace IWanna {
void GameModule::LoadTileMap() {
    std::ifstream file(assets_/"tilemap.json");Json::Value root;Json::CharReaderBuilder reader;std::string error;
    if(!file || !Json::parseFromStream(reader,file,&root,&error))throw std::runtime_error("Invalid tilemap JSON: "+error);
    auto fail=[] {throw std::runtime_error("Invalid IWANNA_TILEMAP_1 data");};
    auto vec=[&](const Json::Value& value) {
        if(!value.isArray() || value.size()!=2 || !value[0].isNumeric() || !value[1].isNumeric())fail();
        glm::vec2 result(value[0].asFloat(),value[1].asFloat());
        if(!std::isfinite(result.x)||!std::isfinite(result.y))fail();return result;
    };
    if(root["format"]!="IWANNA_TILEMAP_1" || !root["width"].isInt() || !root["height"].isInt() || !root["tileSize"].isNumeric())fail();
    const int width=root["width"].asInt(),height=root["height"].asInt();const float size=root["tileSize"].asFloat();
    if(width<1||height<1||width>512||height>512||!std::isfinite(size)||size<=0)fail();
    const auto origin=vec(root["origin"]);const auto& layers=root["layers"];
    if(!layers.isArray()||layers.empty())fail();
    std::map<std::pair<int,int>,ECS::EntityID> cells;
    for(const auto& layer:layers) {
        const auto& rows=layer["cells"];if(!rows.isArray()||rows.size()!=height)fail();
        std::vector<std::string> grid;
        for(const auto& row:rows){if(!row.isString()||row.asString().size()!=width)fail();grid.push_back(row.asString());}
        auto filled=[&](int x,int y){return x>=0&&y>=0&&x<width&&y<height&&grid[y][x]!='0';};
        for(int y=0;y<height;++y)for(int x=0;x<width;++x) {
            char code=grid[y][x];if(code=='0')continue;
            if(code<'1'||code>'9')fail();
            const auto& definition=root["palette"][std::string(1,code)];
            if(!definition.isObject() || !definition["imagePattern"].isString() || !definition["solid"].isBool())fail();
            if(cells.contains({x,y}))throw std::runtime_error("Tile layers overlap at the same cell");
            int edges=(!filled(x,y-1)?1:0)|(!filled(x+1,y)?2:0)|(!filled(x,y+1)?4:0)|(!filled(x-1,y)?8:0);
            std::string image=definition["imagePattern"].asString();auto token=image.find("{edges:02}");
            if(token!=std::string::npos){std::ostringstream digits;digits<<std::setw(2)<<std::setfill('0')<<edges;image.replace(token,10,digits.str());}
            if(image.empty()||std::filesystem::path(image).filename().string()!=image)fail();
            auto id=scene_->CreateEntity(archetype_).GetID();entities_.push_back(id);cells[{x,y}]=id;
            auto& t=Get<Transform>(id);t.position=origin+glm::vec2(x+.5f,y+.5f)*size;t.spawn=t.position;t.size=glm::vec2(size);
            auto& s=Get<Sprite>(id);s.image=image;s.pixelArt=true;
            auto& b=Get<Behavior>(id);b.role=definition["solid"].asBool()?Role::Solid:Role::Decoration;b.name="tile_"+std::to_string(x)+"_"+std::to_string(y);
            auto& c=Get<Collider>(id);c.enabled=b.role==Role::Solid;c.points={{-1,-1},{1,-1},{1,1},{-1,1}};
            auto& cell=Get<TileCell>(id);cell.column=x;cell.row=y;cell.material=code-'0';
        }
    }
    for(const auto& marker:root["markers"]) {
        if(!marker["column"].isInt()||!marker["row"].isInt()||!marker["name"].isString()||!marker["target"].isString())fail();
        auto found=cells.find({marker["column"].asInt(),marker["row"].asInt()});if(found==cells.end())fail();
        auto& b=Get<Behavior>(found->second);b.name=marker["name"].asString();b.target=marker["target"].asString();b.triggerVelocity=vec(marker["velocity"]);
    }
    for(const auto& object:root["objects"]) {
        if(!object["sourceIndex"].isInt())fail();bool found=false;
        for(auto id:entities_)if(Get<TileCell>(id).sourceRecord==object["sourceIndex"].asInt()) {
            found=true;auto& t=Get<Transform>(id);auto& s=Get<Sprite>(id);
            if(object.isMember("position"))t.spawn=t.position=vec(object["position"]);
            if(object.isMember("size")){t.size=vec(object["size"]);if(t.size.x<=0||t.size.y<=0)fail();}
            if(object.isMember("image")){s.image=object["image"].asString();s.columns=s.rows=1;s.cellWidth=s.cellHeight=0;s.frames={0};}
            s.pixelArt=object.get("pixelArt",false).asBool();
            s.repeatX=object.get("repeatX",1).asInt();if(s.repeatX<1||s.repeatX>128)fail();
            if(s.image=="checkpoint_active.png")s.visible=false;
            break;
        }
        if(!found)throw std::runtime_error("Tilemap object sourceIndex not found");
    }
    checkpoint_=vec(root["spawn"]);Get<Transform>(player_).spawn=Get<Transform>(player_).position=checkpoint_;
    for(auto id:entities_)if(Get<Sprite>(id).image.ends_with(".jpg")){Get<Transform>(id).position={0,0};Get<Transform>(id).size={150,84.375f};}
}
}
