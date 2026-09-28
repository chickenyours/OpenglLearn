#include "IWanna/Public/editor_document.h"
#include "IWanna/Public/sprite_renderer.h"
#include "ApplicationWindow/module.h"
#include "Render/module.h"
#include <iostream>
#include <chrono>
#include <thread>
#include <iomanip>
#include <stb_image_write.h>
using namespace IWanna;
namespace {
std::string Number(float v){std::ostringstream s;s<<std::fixed<<std::setprecision(3)<<v;return s.str();}
struct Editor {
    EditorDocument doc;
    int mode=0,tile=1,prefab=0,scroll=0,prefabScroll=0; // select / terrain / prefab / detection
    float zoom=.53f;glm::vec2 pan{-5,1},mouse{},last{},dragOffset{};
    bool magnetic=true,lasso=false,graph=false,reloadAtlas=false;int graphPage=0;glm::vec2 lassoStart{};
    bool left=false,right=false,middle=false,editing=false,replaceText=false,dragging=false,resizing=false,closeWarning=false;
    std::string key,buffer,status="READY",typed;double wheel=0;
    std::vector<std::string> prefabs;
    std::map<int,Sprite> tiles;
    std::map<int,bool> keys;
    std::vector<std::pair<std::string,std::string>> fields;
    bool Canvas()const{return mouse.x>-49&&mouse.x<38&&mouse.y>-30&&mouse.y<33;}
    glm::vec2 World(glm::vec2 p)const{return (p-pan)/zoom;}
    glm::vec2 Screen(glm::vec2 p)const{return p*zoom+pan;}
    bool Press(GLFWwindow* w,int k){bool down=glfwGetKey(w,k)==GLFW_PRESS;bool hit=down&&!keys[k];keys[k]=down;return hit;}
    void Open(const std::filesystem::path& p){doc.Open(p);for(const auto& n:doc.world["prefabs"].getMemberNames())prefabs.push_back(n);LoadTiles();}
    void LoadTiles(){tiles.clear();for(const auto& ref:doc.map["tilesets"]){auto set=ref.isMember("source")?EditorDocument::Read(doc.mapPath.parent_path()/ref["source"].asString()):ref;for(const auto& t:set["tiles"]){Sprite s;s.image=EditorDocument::Props(t)["runtimeImage"].asString();s.pixelArt=true;tiles[int(ref["firstgid"].asUInt()+t["id"].asUInt())]=s;}}}
    void Reload(){doc.Refresh();prefabs=doc.world["prefabs"].getMemberNames();prefab=std::clamp(prefab,0,std::max(0,int(prefabs.size())-1));LoadTiles();reloadAtlas=true;status="ROOM AND PREFAB FILES REFRESHED";}
    glm::vec2 Node(int i)const{if(doc.world["rooms"].size()<=3){static const glm::vec2 points[]={{-30,-19},{20,-2},{-30,17}};return points[i];}return {-34.f+(i%3)*28.f,-20.f+(i/3)*20.f};}
    void SwitchRoom(int i){if(doc.Dirty())throw std::runtime_error("Save before switching rooms");doc.LoadRoom(i);LoadTiles();reloadAtlas=true;scroll=0;graph=false;}
    void Edit(const std::string& name,const std::string& value){editing=true;replaceText=true;key=name;buffer=value;}
    void Commit(){
        if(key=="NEW ROOM"||key=="CLONE ROOM"){doc.NewRoom(buffer,key=="CLONE ROOM");LoadTiles();reloadAtlas=true;editing=false;status="ROOM ASSET CREATED";return;}
        if(key=="DELETE ROOM"){if(buffer!=doc.world["rooms"][doc.roomIndex]["id"].asString())throw std::runtime_error("Type the room ID to delete");doc.DeleteRoom();LoadTiles();reloadAtlas=true;editing=false;status="ROOM MOVED TO .TRASH";return;}
        if(key=="BOUNDARY LINK"){auto p=buffer.find('=');if(p==std::string::npos)throw std::runtime_error("Use right=room/spawn or right=-");doc.Boundary(buffer.substr(0,p),buffer.substr(p+1));editing=false;status="BOUNDARY LINK UPDATED - SAVE TO APPLY";return;}
        if(key=="NEW PROPERTY"){auto p=buffer.find('=');if(p==std::string::npos||p==0)throw std::runtime_error("Use name=value");doc.SetField(buffer.substr(0,p),buffer.substr(p+1));}else doc.SetField(key,buffer);editing=false;status="PROPERTY UPDATED";}
    void Fields(){fields.clear();auto* o=doc.Selected();if(!o)return;auto t=doc.ObjectTransform(*o);fields={{"x",Number(t.position.x)},{"y",Number(t.position.y)},{"width",Number(t.size.x)},{"height",Number(t.size.y)}};
        auto p=EditorDocument::Props(*o);auto effective=doc.Effective(*o);
        if(p.isMember("prefab")){if(!p.isMember("event"))p["event"]="";if(!p.isMember("visible"))p["visible"]=effective.get("visible",true);if(!p.isMember("collide"))p["collide"]=effective.get("collide",effective["role"]!="decoration");}
        for(const auto& n:p.getMemberNames()){auto v=p[n];fields.emplace_back(n,v.isString()?v.asString():v.asString());}
        scroll=std::clamp(scroll,0,std::max(0,int(fields.size())-13));
    }
    void Input(GLFWwindow* w){
        int ww,hh;glfwGetWindowSize(w,&ww,&hh);double mx,my;glfwGetCursorPos(w,&mx,&my);int vw=std::min(ww,hh*16/9),vh=vw*9/16;if(vw<=0||vh<=0)return;
        mouse={float((mx-(ww-vw)*.5)/vw*150-75),float((my-(hh-vh)*.5)/vh*84.375-42.1875)};
        bool l=glfwGetMouseButton(w,0)==GLFW_PRESS,r=glfwGetMouseButton(w,1)==GLFW_PRESS,m=glfwGetMouseButton(w,2)==GLFW_PRESS;
        bool click=l&&!left;bool ctrl=glfwGetKey(w,GLFW_KEY_LEFT_CONTROL)==GLFW_PRESS||glfwGetKey(w,GLFW_KEY_RIGHT_CONTROL)==GLFW_PRESS;
        bool enter=Press(w,GLFW_KEY_ENTER),esc=Press(w,GLFW_KEY_ESCAPE),back=Press(w,GLFW_KEY_BACKSPACE),save=Press(w,GLFW_KEY_S),undo=Press(w,GLFW_KEY_Z),redo=Press(w,GLFW_KEY_Y),del=Press(w,GLFW_KEY_DELETE);
        try{
            if(editing){if(!typed.empty()){if(replaceText)buffer.clear();replaceText=false;buffer+=typed;}if(back){if(replaceText)buffer.clear();else if(!buffer.empty())buffer.pop_back();replaceText=false;}if(enter)Commit();if(esc)editing=false;}
            else{
                if(ctrl&&save){doc.Save();status="SAVED";closeWarning=false;}
                for(int i=0;i<4;++i)if(Press(w,GLFW_KEY_1+i))mode=i;
                if(Press(w,GLFW_KEY_F5))Reload();
                if(Press(w,GLFW_KEY_F6))Edit("NEW ROOM","");
                if(Press(w,GLFW_KEY_F7))Edit("CLONE ROOM",doc.world["rooms"][doc.roomIndex]["id"].asString()+"_copy");
                if(Press(w,GLFW_KEY_F8))Edit("DELETE ROOM","");
                if(Press(w,GLFW_KEY_M))magnetic=!magnetic;
                if(Press(w,GLFW_KEY_TAB))graph=!graph;
                if(Press(w,GLFW_KEY_PAGE_DOWN))graphPage=std::min(graphPage+1,int(doc.world["rooms"].size()-1)/9);
                if(Press(w,GLFW_KEY_PAGE_UP))graphPage=std::max(0,graphPage-1);
                if(Press(w,GLFW_KEY_L)&&ctrl)Edit("BOUNDARY LINK","right=room/left");
                if(Press(w,GLFW_KEY_D)&&ctrl)doc.Duplicate({doc.Unit(),doc.Unit()});
                if(ctrl&&undo)doc.Undo();if(ctrl&&redo)doc.Redo();if(del)doc.Erase();
                if(esc){if(doc.Dirty()&&!closeWarning){closeWarning=true;status="UNSAVED - CTRL S TO SAVE OR ESC AGAIN TO DISCARD";}else glfwSetWindowShouldClose(w,1);}
                if(click&&mouse.y<-36){
                    if(mouse.x<-48){doc.Save();status="SAVED";}
                    else if(mouse.x<-32)doc.Undo();else if(mouse.x<-16)doc.Redo();
                    else if(mouse.x<20){graph=!graph;}
                    else {mode=3;doc.EnableBox();status="DRAG BOX TO MOVE / CORNER TO RESIZE";}
                }
                if(click&&mouse.y>=-36&&mouse.y<-31){
                    if(mouse.x<-56)Edit("NEW ROOM","");else if(mouse.x<-38)Edit("CLONE ROOM",doc.world["rooms"][doc.roomIndex]["id"].asString()+"_copy");
                    else if(mouse.x<-15)Edit("DELETE ROOM","");else if(mouse.x<8)Reload();else if(mouse.x<34)Edit("BOUNDARY LINK","right=room/left");else if(mouse.x<61)magnetic=!magnetic;else graph=!graph;
                }
                if(graph&&click&&Canvas()){for(int i=0;i<9&&graphPage*9+i<int(doc.world["rooms"].size());++i){auto d=glm::abs(mouse-Node(i));if(d.x<12&&d.y<5){SwitchRoom(graphPage*9+i);break;}}click=false;}
                if(click&&mouse.x<-50&&mouse.y>-29&&mouse.y<-17)mode=std::clamp(int((mouse.y+29)/3),0,3);
                if(click&&mouse.x<-50&&mouse.y>=-14&&mouse.y<6){int col=int((mouse.x+73)/5.2f),row=int((mouse.y+14)/5);int index=row*4+col;if(col>=0&&col<4&&index>=0&&index<int(tiles.size())){auto it=tiles.begin();std::advance(it,index);tile=it->first;mode=1;}}
                if(click&&mouse.x<-50&&mouse.y>=10){int i=int((mouse.y-10)/2.65f)+prefabScroll;if(i>=0&&i<int(prefabs.size())){prefab=i;mode=2;}}
                if(!graph&&click&&mouse.x>40&&mouse.y>=-23&&mouse.y<16){int i=int((mouse.y+23)/3)+scroll;if(i<int(fields.size()))Edit(fields[i].first,fields[i].second);}
                if(!graph&&click&&mouse.x>40&&mouse.y>=20&&mouse.y<23)Edit("NEW PROPERTY","");
                if(!graph&&click&&mouse.x>40&&mouse.y>=24&&mouse.y<27){doc.EnableBox();mode=3;}
                if(!graph&&click&&mouse.x>40&&mouse.y>=28&&mouse.y<31){doc.Erase();status="DELETED";}
                if(wheel){if(Canvas()){auto before=World(mouse);zoom=std::clamp(zoom*std::pow(1.15f,float(wheel)),.2f,4.f);pan=mouse-before*zoom;}else if(mouse.x<-50&&mouse.y>7)prefabScroll=std::clamp(prefabScroll-int(wheel),0,std::max(0,int(prefabs.size())-8));else if(mouse.x>40)scroll=std::clamp(scroll-int(wheel),0,std::max(0,int(fields.size())-13));}
                if(m&&middle)pan+=mouse-last;
                if(Canvas()&&!graph){
                    auto pos=World(mouse);
                    if(mode==1){if((l&&!left)||(r&&!right))doc.Begin();if(l||r){auto from=World(last);int count=std::max(1,int(glm::length(pos-from)/(doc.Unit()*.25f)));if((l&&!left)||(r&&!right))count=1;for(int n=1;n<=count;++n)doc.Paint(glm::mix(from,pos,float(n)/count),r?0:tile);}}
                    else if(click){
                        if(mode==2){doc.Place(prefabs.at(prefab),pos);doc.selection={doc.selected};if(magnetic&&glfwGetKey(w,GLFW_KEY_LEFT_ALT)!=GLFW_PRESS)doc.MoveSelection(doc.Snap(pos,.7f/zoom));mode=0;scroll=0;}
                        else if(mode==0){int hit=doc.Hit(pos);scroll=0;if(hit<0||glfwGetKey(w,GLFW_KEY_LEFT_SHIFT)==GLFW_PRESS){lasso=true;lassoStart=pos;}else{if(!doc.selection.contains(hit))doc.selection={hit};doc.selected=hit;if(ctrl)doc.Duplicate({0,0});if(auto* o=doc.Selected()){if(!ctrl)doc.Begin();auto t=doc.ObjectTransform(*o);dragOffset=t.position-pos;resizing=!ctrl&&doc.selection.size()==1&&glm::length(Screen(t.position+t.size*.5f)-mouse)<1.2f&&t.size.x>0&&t.size.y>0;dragging=true;}}}
                        else if(mode==3){if(auto* o=doc.Selected()){auto p=doc.Effective(*o);if(!p.get("detectionEnabled",false).asBool())doc.EnableBox();auto t=doc.Box(*o);auto d=glm::abs(pos-t.position);if(d.x<=t.size.x*.5f+1/zoom&&d.y<=t.size.y*.5f+1/zoom){doc.Begin();dragOffset=t.position-pos;resizing=glm::length(Screen(t.position+t.size*.5f)-mouse)<1.5f;dragging=true;}}}
                    }
                }
                if(l&&dragging){if(auto* o=doc.Selected()){auto pos=World(mouse);if(mode==3){auto t=doc.Box(*o);auto base=doc.ObjectTransform(*o);if(resizing){auto size=glm::max((pos-t.position)*2.f,glm::vec2(.1f));EditorDocument::Property(*o,"detectionW",size.x);EditorDocument::Property(*o,"detectionH",size.y);}else{auto d=pos+dragOffset-base.position;EditorDocument::Property(*o,"detectionX",d.x);EditorDocument::Property(*o,"detectionY",d.y);}}else if(resizing)doc.Resize(*o,(pos-doc.ObjectTransform(*o).position)*2.f);else doc.MoveSelection(magnetic&&glfwGetKey(w,GLFW_KEY_LEFT_ALT)!=GLFW_PRESS?doc.Snap(pos+dragOffset,.7f/zoom):pos+dragOffset);}}
                if(lasso){doc.SelectArea(lassoStart,World(mouse));if(!l)lasso=false;}
                if(!l)dragging=false;
            }
        }catch(const std::exception& e){status=e.what();}
        typed.clear();wheel=0;left=l;right=r;middle=m;last=mouse;
    }
    void Draw(SpriteRenderer& r,Render::RHIFrameEncoder& f){
        std::vector<DrawSprite> sprites;auto add=[&](Transform t,Sprite s){t.position=Screen(t.position);t.size*=zoom;s.visible=true;sprites.push_back({t,s});};
        int w=doc.map["width"].asInt();for(const auto& layer:doc.map["layers"])if(layer["type"]=="tilelayer")for(int i=0;i<int(layer["data"].size());++i){int gid=layer["data"][i].asInt();if(tiles.contains(gid)){Transform t;t.position=doc.Origin()+glm::vec2(i%w+.5f,i/w+.5f)*doc.Unit();t.size=glm::vec2(doc.Unit());add(t,tiles.at(gid));}}
        for(const auto& o:doc.Objects()){auto p=EditorDocument::Props(o);if(doc.catalog.prefabs.contains(p.get("prefab","").asString())){auto s=doc.catalog.prefabs.at(p["prefab"].asString()).sprite;if(doc.Effective(o).get("visible",true).asBool())add(doc.ObjectTransform(o),s);}}
        r.Draw(f,sprites);
        auto text=[&](std::string s,glm::vec2 p,float size=.23f,glm::vec4 color=glm::vec4(.8,.87,.94,1)){r.Text(f,s,p,size,color);};
        auto outline=[&](Transform t,glm::vec4 c){auto p=Screen(t.position),s=t.size*zoom;float k=.13f;r.Rect(f,p+glm::vec2(0,-s.y/2),{s.x,k},c);r.Rect(f,p+glm::vec2(0,s.y/2),{s.x,k},c);r.Rect(f,p+glm::vec2(-s.x/2,0),{k,s.y},c);r.Rect(f,p+glm::vec2(s.x/2,0),{k,s.y},c);};
        // Canvas geometry is covered by opaque chrome outside its viewport.
        if(zoom>.35f){for(int x=0;x<=doc.map["width"].asInt();++x){auto p=Screen(doc.Origin()+glm::vec2(x*doc.Unit(),0));r.Rect(f,{p.x,1},{.06f,64},{.25,.35,.43,.3});}for(int y=0;y<=doc.map["height"].asInt();++y){auto p=Screen(doc.Origin()+glm::vec2(0,y*doc.Unit()));r.Rect(f,{-5,p.y},{88,.06f},{.25,.35,.43,.3});}}
        for(const auto& o:doc.Objects()){auto p=doc.Effective(o);auto t=doc.ObjectTransform(o);if(p.get("detectionEnabled",false).asBool()||p["role"]=="trigger"||p["role"]=="exit")outline(doc.Box(o),{.25,1,.6,.8});if(t.size==glm::vec2(0)){r.Rect(f,Screen(t.position),{.7,.7},{.5,1,.6,1});text(EditorDocument::Props(o).get("uid",EditorDocument::Props(o).get("text","LABEL")).asString(),Screen(t.position)+glm::vec2(1,0),.18f);}}
        for(int i:doc.selection)outline(doc.ObjectTransform(doc.Objects()[i]),{.4,.8,1,1});
        if(lasso){Transform t;t.position=(lassoStart+World(mouse))*.5f;t.size=glm::abs(lassoStart-World(mouse));outline(t,{.4,.8,1,1});}
        if(auto* o=doc.Selected()){auto t=mode==3?doc.Box(*o):doc.ObjectTransform(*o);outline(t,{1,.75,.15,1});r.Rect(f,Screen(t.position+t.size*.5f),{1,1},{1,.75,.15,1});}
        r.Rect(f,{-62.5,0},{25,85},{.055,.075,.105,1});r.Rect(f,{57,0},{38,85},{.055,.075,.105,1});r.Rect(f,{0,-37},{150,11},{.075,.1,.14,1});r.Rect(f,{0,38},{150,10},{.075,.1,.14,1});
        text("SAVE",{-72,-40},.3f);text("UNDO",{-47,-40},.3f);text("REDO",{-31,-40},.3f);text("ROOM "+doc.world["rooms"][doc.roomIndex]["id"].asString(),{-15,-40},.27f);text("EDIT DETECTION",{23,-40},.27f);
        text("F6 NEW",{-73,-34.5f},.2f);text("F7 CLONE",{-55,-34.5f},.2f);text("F8 DELETE ROOM",{-37,-34.5f},.18f);text("F5 REFRESH",{-12,-34.5f},.2f);text("CTRL L LINK",{10,-34.5f},.2f);text(magnetic?"M SNAP ON":"M SNAP OFF",{35,-34.5f},.2f);text("TAB MAP",{62,-34.5f},.19f);
        const char* modes[]={"1 SELECT / MOVE","2 PAINT TILES","3 PLACE PREFAB","4 DETECTION BOX"};for(int i=0;i<4;++i){if(mode==i)r.Rect(f,{-62,-27.8f+i*3},{23,2.8},{.13,.26,.35,1});text(modes[i],{-73,-29.f+i*3},.22f);}
        text("TILES  RIGHT CLICK ERASES",{-73,-17},.15f);std::vector<DrawSprite> icons;int j=0;for(const auto& [gid,s]:tiles){Transform t;t.position={-70.5f+(j%4)*5.2f,-11.7f+(j/4)*5.f};t.size={3.8,3.8};if(gid==tile)r.Rect(f,t.position,{4.5,4.5},{.7,.5,.1,1});icons.push_back({t,s});++j;}r.Draw(f,icons,false);
        text("PREFABS",{-73,7},.24f);for(int i=prefabScroll;i<std::min(prefabScroll+8,int(prefabs.size()));++i){if(prefab==i)r.Rect(f,{-62,11+(i-prefabScroll)*2.65f},{23,2.5},{.13,.26,.35,1});text(prefabs[i].substr(0,15),{-72,10+(i-prefabScroll)*2.65f},.24f);}
        text("INSPECTOR",{41,-29},.35f);text("CLICK VALUE TO EDIT",{41,-25.8f},.18f);Fields();
        for(int i=scroll;i<std::min(int(fields.size()),scroll+13);++i){float y=-23+(i-scroll)*3.f;r.Rect(f,{57,y+1},{33,2.8},{.08,.12,.17,1});text(fields[i].first.substr(0,18),{41,y},.135f);text(fields[i].second.substr(0,14),{56,y},.2f);}
        text("+ ADD PROPERTY",{41,20},.25f);text("+ DETECTION BOX",{41,24},.25f);text("DELETE SELECTED",{41,28},.25f);
        text((doc.Dirty()?"* UNSAVED   ":"SAVED   ")+status.substr(0,84),{-73,34.5f},.21f);text("CTRL D COPY / CTRL DRAG COPY / SHIFT DRAG LASSO / ALT FREE MOVE / M SNAP / CTRL S SAVE",{-73,39},.19f);
        if(graph){
            r.Rect(f,{-5,1},{86,63},{.035,.055,.085,1});std::map<std::string,glm::vec2> positions;
            graphPage=std::clamp(graphPage,0,int(doc.world["rooms"].size()-1)/9);
            for(int i=0;i<9&&graphPage*9+i<int(doc.world["rooms"].size());++i)positions[doc.world["rooms"][graphPage*9+i]["id"].asString()]=Node(i);
            struct Link {std::string kind,source,room,spawn;};std::map<std::string,std::vector<Link>> links;
            auto current=doc.world["rooms"][doc.roomIndex]["id"].asString();
            for(const auto& [id,room]:doc.catalog.rooms){for(const auto& [edge,c]:room.connections)links[id].push_back({"EDGE",edge,c.room,c.spawn});for(const auto& o:room.objects)if(o.role==Role::Exit)links[id].push_back({"PORTAL",o.id,o.destinationRoom,o.destinationSpawn});}
            links[current].clear();for(const auto& edge:doc.map.get("_native",Json::Value(Json::objectValue))["connections"].getMemberNames()){auto c=doc.map["_native"]["connections"][edge];links[current].push_back({"EDGE",edge,c["room"]=="$self"?current:c["room"].asString(),c["spawn"].asString()});}
            for(const auto& o:doc.Objects()){auto p=doc.Effective(o);if(p["role"]=="exit")links[current].push_back({"PORTAL",p["uid"].asString(),p["destinationRoom"]=="$self"?current:p["destinationRoom"].asString(),p["destinationSpawn"].asString()});}
            auto segment=[&](glm::vec2 a,glm::vec2 b,glm::vec4 color){int n=std::max(1,int(glm::length(b-a)/.16f));for(int i=0;i<=n;++i)r.Rect(f,glm::mix(a,b,float(i)/n),{.2,.2},color);};
            auto line=[&](glm::vec2 a,glm::vec2 b,bool boundary){auto delta=b-a;if(glm::length(delta)<1)return;auto direction=glm::normalize(delta),side=glm::vec2(-direction.y,direction.x);float trim=std::min(12.3f/std::max(.001f,std::abs(direction.x)),5.3f/std::max(.001f,std::abs(direction.y)));a+=direction*trim+side*.45f;b-=direction*trim-side*.45f;auto color=boundary?glm::vec4(.35,.9,.55,1):glm::vec4(.3,.65,1,1);segment(a,b,color);segment(b,b-direction*1.4f+side*.75f,color);segment(b,b-direction*1.4f-side*.75f,color);};
            for(const auto& [id,p]:positions)for(const auto& link:links[id])if(positions.contains(link.room))line(p,positions.at(link.room),link.kind=="EDGE");
            for(const auto& [id,p]:positions){bool active=id==current;r.Rect(f,p,{24,10},active?glm::vec4(.15,.3,.4,1):glm::vec4(.08,.14,.2,1));text(id.substr(0,16),p+glm::vec2(-11,-3),.22f);text(std::to_string(links[id].size())+" OUTGOING LINKS",p+glm::vec2(-11,1),.16f);}
            r.Rect(f,{57,1},{38,64},{.055,.075,.105,1});text("ROOM CONNECTIONS",{41,-28},.25f);text(current.substr(0,18),{41,-24},.22f);int row=0;
            for(const auto& link:links[current]){if(row>=8)break;float y=-19+row*5.3f;text(link.kind+" "+link.source.substr(0,15),{41,y},.17f);text("TO "+link.room.substr(0,13)+" / "+link.spawn.substr(0,8),{41,y+2},.16f);++row;}
            text("BLUE PORTAL / GREEN EDGE",{41,25},.16f);text("CTRL L EDIT BOUNDARY",{41,29},.18f);
            text("TOPOLOGY / CLICK ROOM / PGUP PGDN PAGE "+std::to_string(graphPage+1),{-47,29},.18f);
        }
        if(editing){r.Rect(f,{0,0},{100,18},{.025,.05,.09,1});text("EDIT "+key,{-46,-6},.35f);text(buffer.substr(buffer.size()>90?buffer.size()-90:0)+"_",{-46,0},.24f);text("ENTER APPLY / ESC CANCEL   CASE IS PRESERVED",{-46,5},.2f);}
    }
};
}
int main(int argc,char** argv){
    auto assets=std::filesystem::absolute(argv[0]).parent_path()/"IWanna";std::filesystem::path world;bool smoke=false,smokeGraph=false;std::string capture;
    for(int i=1;i<argc;++i){std::string a=argv[i];if(a=="--assets"&&i+1<argc)assets=argv[++i];else if(a=="--world"&&i+1<argc)world=argv[++i];else if(a=="--smoke-test")smoke=true;else if(a=="--smoke-topology")smoke=smokeGraph=true;else if(a=="--capture"&&i+1<argc)capture=argv[++i];else return 2;}
    if(world.empty())world=assets/"Workshop/world.json";
    ApplicationWindow::ApplicationWindowModule windowModule;Render::RenderModule renderModule;std::unique_ptr<SpriteRenderer> renderer;int result=0;
    try{
        Editor editor;editor.Open(world);if(smoke){editor.doc.Place("trigger",{-20,5});editor.doc.EnableBox();editor.doc.SetField("detectionW","12");editor.doc.SetField("event","editor_demo");editor.mode=3;}editor.graph=smokeGraph;if(!windowModule.Startup())throw std::runtime_error("Window startup failed");auto window=windowModule.GetCurrentWindow();auto* native=window->GetNativeWindow();
        glfwSetWindowTitle(native,"IWanna Level Editor");glfwSetWindowSize(native,1920,1080);glfwSetWindowUserPointer(native,&editor);
        glfwSetCharCallback(native,[](GLFWwindow* w,unsigned c){auto& e=*static_cast<Editor*>(glfwGetWindowUserPointer(w));if(e.editing&&c>=32&&c<127)e.typed+=char(c);});
        glfwSetScrollCallback(native,[](GLFWwindow* w,double,double y){static_cast<Editor*>(glfwGetWindowUserPointer(w))->wheel+=y;});
        glfwSetWindowCloseCallback(native,[](GLFWwindow* w){auto& e=*static_cast<Editor*>(glfwGetWindowUserPointer(w));if(e.doc.Dirty()&&!e.closeWarning){glfwSetWindowShouldClose(w,0);e.closeWarning=true;e.status="UNSAVED - CTRL S TO SAVE OR CLOSE AGAIN TO DISCARD";}});
        if(!renderModule.Startup())throw std::runtime_error("Render startup failed");auto device=renderModule.GetRHIDevice();renderer=std::make_unique<SpriteRenderer>(device);auto sprites=editor.doc.catalog.Sprites();for(const auto& [_,s]:editor.tiles)sprites.push_back(s);renderer->Initialize(assets,{},sprites);
        bool pending=false;int frames=0;auto start=std::chrono::steady_clock::now();
        while(!window->ShouldClose()){
            window->PollEvents();device->returnSystem.DrainCallbacks();if(!renderer->Error().empty())throw std::runtime_error(renderer->Error());
            if(!renderer->Ready()||pending){if(!renderer->Ready()&&std::chrono::steady_clock::now()-start>std::chrono::seconds(20))throw std::runtime_error("Render timeout");device->returnSystem.WaitForCallbacks(std::chrono::milliseconds(8));continue;}
            editor.Input(native);if(editor.reloadAtlas){renderer->Shutdown();renderer=std::make_unique<SpriteRenderer>(device);auto refreshed=editor.doc.catalog.Sprites();for(const auto& [_,s]:editor.tiles)refreshed.push_back(s);renderer->Initialize(assets,{},refreshed);editor.reloadAtlas=false;start=std::chrono::steady_clock::now();continue;}int width,height;glfwGetFramebufferSize(native,&width,&height);if(width<1||height<1){glfwWaitEventsTimeout(.05);continue;}
            Render::RHICommand::BeginFrame begin;begin.frameIndex=frames;begin.framebufferWidth=width;begin.framebufferHeight=height;begin.clearColor={.025,.04,.065,1};auto frame=device->BeginFrame(begin);int vw=std::min(width,height*16/9),vh=vw*9/16;frame.SetViewport({(width-vw)/2,(height-vh)/2,uint32_t(vw),uint32_t(vh)});
            editor.Draw(*renderer,frame);renderer->Flush(frame);bool final=smoke&&frames==2;frame.End(!final||capture.empty());pending=true;renderer->Submit(frame.GetCommandBuffer(),[&]{pending=false;});++frames;
            if(final){while(pending){device->returnSystem.WaitForCallbacks(std::chrono::milliseconds(8));device->returnSystem.DrainCallbacks();}bool done=false,saved=true;int error=0;device->async_ExecuteCode([&]{glFinish();error=glGetError();if(!capture.empty()){std::vector<unsigned char> pixels(size_t(width)*height*4);glReadPixels(0,0,width,height,GL_RGBA,GL_UNSIGNED_BYTE,pixels.data());stbi_flip_vertically_on_write(1);saved=stbi_write_png(capture.c_str(),width,height,4,pixels.data(),width*4)!=0;}},[&]{done=true;});while(!done){device->returnSystem.WaitForCallbacks(std::chrono::milliseconds(8));device->returnSystem.DrainCallbacks();}if(error||!saved)throw std::runtime_error("Editor GPU smoke failed");std::cout<<"Editor GPU smoke PASS\n";break;}
        }
        while(pending){device->returnSystem.WaitForCallbacks(std::chrono::milliseconds(8));device->returnSystem.DrainCallbacks();}renderer->Shutdown();device->StopAndRelease();
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';result=1;}
    renderModule.Shutdown();renderer.reset();windowModule.Shutdown();return result;
}
