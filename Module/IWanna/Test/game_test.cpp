#include "IWanna/Public/game_module.h"
#include "IWanna/Public/collision.h"
#include "Audio/Public/audio_module.h"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <set>
using namespace IWanna;
static void Check(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
static Sprite MaskSprite(int width,int height,std::initializer_list<glm::ivec2> pixels) {
    AlphaMask mask;mask.width=width;mask.height=height;mask.stride=(width+63)/64;
    mask.minX=width;mask.minY=height;mask.bits.resize(size_t(mask.stride)*height);
    for(auto p:pixels)mask.Set(p.x,p.y);
    Sprite sprite;sprite.masks=std::make_shared<std::vector<AlphaMask>>(1,mask);return sprite;
}
static void TestMasks() {
    auto ring=MaskSprite(3,3,{{0,0},{1,0},{2,0},{0,1},{2,1},{0,2},{1,2},{2,2}});
    auto pixel=MaskSprite(1,1,{{0,0}});Transform a,b;a.size={3,3};b.size={.5f,.5f};
    Check(!MaskOverlap(a,ring,b,pixel),"transparent hole must not collide");
    b.position={1,0};Check(MaskOverlap(a,ring,b,pixel),"opaque border collision");
    a.size={1,1};b.size={1,1};b.position={1,0};
    Check(!MaskOverlap(a,pixel,b,pixel),"touching texel edges have no area");
    b.position.x=.999f;Check(MaskOverlap(a,pixel,b,pixel),"subpixel overlap is detected");
    auto corner=MaskSprite(2,2,{{0,0}});a.size={2,2};b.size={.2f,.2f};b.position={-.5f,-.5f};
    Check(MaskOverlap(a,corner,b,pixel),"corner baseline");corner.flipX=true;
    Check(!MaskOverlap(a,corner,b,pixel),"horizontal flip changes mask");b.position.x=.5f;
    Check(MaskOverlap(a,corner,b,pixel),"horizontal flip destination");corner.flipY=true;b.position.y=.5f;
    Check(MaskOverlap(a,corner,b,pixel),"vertical flip destination");corner.flipX=corner.flipY=false;
    a.rotation=90;b.position={.5f,-.5f};Check(MaskOverlap(a,corner,b,pixel),"rotated mask");
    a.rotation=45;b.position={0,-.7f};Check(MaskOverlap(a,corner,b,pixel),"arbitrary angle mask");
    b.position={.65f,-1.3f};Check(!MaskOverlap(a,corner,b,pixel),"rotated AABB empty corner");
    a.rotation=0;a.size={4,2};b.position={-1,-.5f};Check(MaskOverlap(a,corner,b,pixel),"nonuniform scale");
    auto frames=std::make_shared<std::vector<AlphaMask>>(*corner.masks);
    frames->push_back(MaskSprite(2,2,{{1,1}}).masks->front());corner.masks=frames;corner.frames={0,1};
    corner.elapsed=.75f;Check(!MaskOverlap(a,corner,b,pixel),"animation switches collision frame");
    auto wide=MaskSprite(130,1,{{63,0},{64,0},{129,0}});const auto& bits=wide.masks->front();
    Check(bits.Any(64,0,65,1)&&bits.Any(129,0,130,1)&&!bits.Any(65,0,129,1),"bitset word boundaries");
}
static void TestPhysics() {
    GameModule game(IWANNA_ASSETS);Check(game.Startup(),game.Error().c_str());
    auto tuning=GameConfig::Load(std::filesystem::path(IWANNA_ASSETS)/"gameplay.json");
    Check(tuning.playerScale==glm::vec2(.75f,.75f),"recommended player scale is configured");
    Check(game.Get<Transform>(game.PlayerEntity()).size==tuning.ScaledPlayerSize(),"player render transform uses XY scale");
    Check(game.PlayerBody().size==tuning.ScaledPlayerSize()*tuning.character.sizeRatio,"player collision body scales with sprite");
    // Keep regression trajectories deterministic while allowing users to tune JSON.
    game.rules=Rules{};
    for(auto id:game.Entities())game.Get<Collider>(id).enabled=false;
    auto floor=game.Find("cao1"),player=game.PlayerEntity();
    auto& ft=game.Get<Transform>(floor);ft.position={0,10};ft.size={100,5};ft.rotation=0;
    game.Get<Sprite>(floor)=MaskSprite(1,1,{{0,0}});game.Get<Collider>(floor).enabled=true;
    game.Get<Behavior>(floor).target.clear();game.Get<Behavior>(floor).role=Role::Solid;
    auto& t=game.Get<Transform>(player);auto& v=game.Get<Motion>(player).velocity;t.position={0,4};v={0,0};
    for(int n=0;n<120;++n)game.FixedTick({});
    Check(game.Get<Player>(player).grounded,"mask floor landing");float ground=t.position.y;
    for(int n=0;n<120;++n){game.FixedTick({false,true});Check(std::abs(t.position.y-ground)<.02f && v.y>=0,"walking cannot generate lift");}
    Check(t.position.x>17.9f,"walking maintains configured speed");
    game.FixedTick({false,false,true,false,false,true});float apex=t.position.y;
    for(int n=0;n<180;++n){game.FixedTick({false,false,false,false,false,true});apex=std::min(apex,t.position.y);}
    const float heldHeight=ground-apex;
    Check(heldHeight>21.f && heldHeight<23.f,"held jump reaches configured full height");
    Check(game.Get<Player>(player).grounded,"jump returns to floor");
    game.FixedTick({false,false,true,false,false,true});float tapApex=t.position.y;
    for(int n=0;n<180;++n){game.FixedTick({});tapApex=std::min(tapApex,t.position.y);}
    Check(ground-tapApex>4.f && ground-tapApex<6.f && heldHeight>ground-tapApex+12.f,"releasing jump makes a shorter arc");
    Check(game.Get<Player>(player).grounded,"short jump returns to floor");
    game.FixedTick({false,false,true});Check(game.Get<Player>(player).jumps==1,"grounded first jump");
    game.FixedTick({false,false,true});Check(game.Get<Player>(player).jumps==2,"grounded takeoff retains one air jump");
    game.FixedTick({false,false,true});Check(game.Get<Player>(player).jumps==2,"grounded takeoff rejects third jump");
    for(int n=0;n<180;++n)game.FixedTick({});Check(game.Get<Player>(player).grounded&&game.Get<Player>(player).jumps==0,"landing replenishes two jumps");
    // Move the same solid overhead; an upward collision must cancel ascent.
    ft.position={t.position.x,ground-5};ft.size={100,1};
    game.FixedTick({false,false,true});bool stopped=false;
    for(int n=0;n<30;++n){game.FixedTick({});if(v.y==0)stopped=true;}
    Check(stopped,"ceiling stops upward motion");
    game.Get<Collider>(floor).enabled=false;t.position={0,-100};v={0,0};
    for(int n=0;n<100;++n)game.FixedTick({});Check(std::abs(v.y-65)<.001f,"terminal fall speed");
    // Reach the original map's first high platform with a timed double jump.
    GameModule route(IWANNA_ASSETS);Check(route.Startup(),route.Error().c_str());
    route.rules=Rules{};
    auto rp=route.PlayerEntity();route.Restart();
    for(int n=0;n<60;++n)route.FixedTick({});
    for(int n=0;n<45;++n)route.FixedTick({false,true});
    for(int n=0;n<180;++n) {
        auto pos=route.Get<Transform>(rp).position;
        route.FixedTick({false,pos.x < (pos.y < -1.6f ? -58.5f : -63.f),n==0 || n==66,false,false,n<30||(n>=66&&n<96)});
    }
    Check(route.GetState()==State::Playing && route.Get<Player>(rp).grounded && route.Get<Transform>(rp).position.y<0,"original high platform reachable");
}
static void TestCharacterContacts() {
    GameModule game(IWANNA_ASSETS);Check(game.Startup(),game.Error().c_str());game.rules=Rules{};
    std::vector<ECS::EntityID> tiles;
    for(auto id:game.Entities()) {
        game.Get<Collider>(id).enabled=false;
        if(game.Get<Behavior>(id).role==Role::Solid)tiles.push_back(id);
    }
    auto tile=[&](int index,glm::vec2 position,glm::vec2 size) {
        auto id=tiles.at(index);auto& transform=game.Get<Transform>(id);
        transform.position=position;transform.size=size;transform.rotation=0;
        game.Get<Sprite>(id)=RectangleMask();game.Get<Collider>(id).enabled=true;
        game.Get<Behavior>(id).target.clear();return id;
    };
    auto clear=[&] {for(auto id:tiles)game.Get<Collider>(id).enabled=false;};
    auto player=game.PlayerEntity();auto& t=game.Get<Transform>(player);auto& v=game.Get<Motion>(player).velocity;
    auto place=[&](glm::vec2 position) {t.position=position;v={0,0};game.Get<Player>(player)={};};
    auto body=game.PlayerBody();auto& sprite=game.Get<Sprite>(player);
    sprite.flipX=!sprite.flipX;sprite.elapsed=.4f;
    Check(game.PlayerBody().size==body.size && game.PlayerBody().position==body.position,"body ignores animation and facing");
    Check(body.size.x<t.size.x*.5f && body.size.y<t.size.y,"inner rectangle excludes cape and weapon");
    for(int i=0;i<13;++i)tile(i,{-30.f+5*i,10.f+(i%2?.045f:0.f)},{5.002f,5});
    place({-25,4});for(int n=0;n<100;++n)game.FixedTick({});const float floorY=t.position.y;
    for(int n=0;n<320;++n) {game.FixedTick({false,true});Check(std::abs(t.position.y-floorY)<.2f,"tile seams do not launch player");}
    Check(t.position.x>22,"cross raised and lowered floor seams to right");
    for(int n=0;n<320;++n)game.FixedTick({true,false});
    Check(t.position.x < -24,"cross raised and lowered floor seams to left");
    // Segmented walls have small horizontal mismatches like the imported map.
    // Slight initial overlap exercises recovery; pressing into the wall must
    // preserve falling motion and must not count as ground or restore jumps.
    for(int side:{-1,1})for(bool pressing:{false,true}) {
        clear();for(int i=0;i<8;++i)tile(i,{side*(3.f+(i%2?.04f:0.f)),-12.5f+5*i},{2,5.01f});
        const float half=game.PlayerBody().size.x*.5f;
        place({side*(2-half+.08f),-12});game.Get<Player>(player).jumps=2;
        for(int n=0;n<80;++n) {
            game.FixedTick({pressing && side<0,pressing && side>0});
            Check(!game.Get<Player>(player).grounded && game.Get<Player>(player).jumps==2,"wall does not become ground");
        }
        Check(t.position.y>7 && v.y>0,"slide down segmented wall while pressing into it");
        const float oldX=t.position.x;
        for(int n=0;n<10;++n)game.FixedTick({side>0,side<0});
        Check((t.position.x-oldX)*side < -1,"release wall by moving away");
    }
    clear();tile(0,{0,10},{30,5});tile(1,{3,0},{2,20});place({0,4});
    for(int n=0;n<100;++n)game.FixedTick({});const float groundedY=t.position.y;
    for(int n=0;n<180;++n)game.FixedTick({false,true});
    Check(std::abs(t.position.y-groundedY)<.04f && t.position.x<2,"step allowance cannot climb a wall");
    // Existing floor penetration is repaired without injecting an upward speed.
    t.position.y+=.1f;game.FixedTick({});
    Check(std::abs(t.position.y-groundedY)<.04f && v.y>=0,"shallow floor recovery");
    clear();tile(0,{0,10},{4,5});place({0,4});
    for(int n=0;n<100;++n)game.FixedTick({});
    for(int n=0;n<40;++n)game.FixedTick({false,true});
    Check(!game.Get<Player>(player).grounded && v.y>0 && t.position.y>groundedY+.5f,"walk off edge without hovering");
    game.FixedTick({false,false,true});Check(game.Get<Player>(player).jumps==2&&v.y<0,"ledge fall allows one air jump");
    game.FixedTick({false,false,true});Check(game.Get<Player>(player).jumps==2,"ledge fall rejects another jump");
    // Hazards still use their alpha mask, tested against the inner body only.
    clear();place({0,0});auto hazard=tile(0,{1.7f,0},{.2f,.2f});game.Get<Behavior>(hazard).role=Role::Hazard;
    game.FixedTick({});Check(game.GetState()==State::Playing,"cape area alone does not hit hazard");
    game.Get<Transform>(hazard).position=game.PlayerBody().position;
    game.FixedTick({});Check(game.GetState()==State::Dead,"inner body hits hazard mask");
}
int main() {
    try {
        TestMasks();TestPhysics();TestCharacterContacts();
        GameModule game(IWANNA_ASSETS);Check(game.Startup(),game.Error().c_str());
        size_t tileCount=0;std::set<std::pair<int,int>> occupied;
        for(auto id:game.Entities()) {
            const auto& cell=game.Get<TileCell>(id);if(!cell.material)continue;++tileCount;
            const auto& t=game.Get<Transform>(id);
            Check(occupied.emplace(cell.column,cell.row).second,"tile cells are unique");
            Check(t.size==glm::vec2(2.5f) && t.position==glm::vec2(-100+(cell.column+.5f)*2.5f,-45+(cell.row+.5f)*2.5f),"terrain lies exactly on grid");
            Check(game.Get<Sprite>(id).pixelArt,"terrain uses pixel sampling");
            Check(game.Get<Sprite>(id).repeatX==1 && game.Get<Sprite>(id).visible,"terrain cells render exactly once");
        }
        Check(tileCount==946 && game.Entities().size()==83+tileCount,"tilemap migration preserves non-terrain entities");
        for(auto id:game.Entities()) {
            const auto& c=game.Get<TileCell>(id);if(!c.material)continue;
            int edges=(!occupied.contains({c.column,c.row-1})?1:0)|(!occupied.contains({c.column+1,c.row})?2:0)|
                (!occupied.contains({c.column,c.row+1})?4:0)|(!occupied.contains({c.column-1,c.row})?8:0);
            const auto name="terrain_"+std::string(edges<10?"0":"")+std::to_string(edges)+".png";
            Check(game.Get<Sprite>(id).image==name,"autotile edges match neighbors");
        }
        int sounds=0;game.playSound=[&](const auto&){++sounds;};
        auto player=game.PlayerEntity();
        auto resetAir=[&] {game.Restart();game.Get<Transform>(player).position={0,-65};auto& p=game.Get<Player>(player);p.grounded=false;p.jumps=0;};
        resetAir();game.FixedTick({false,false,true});
        Check(game.Get<Player>(player).jumps==2,"falling player uses only air jump");
        float vy=game.Get<Motion>(player).velocity.y;game.FixedTick({false,false,true});
        Check(game.Get<Player>(player).jumps==2 && game.Get<Motion>(player).velocity.y>vy,"reject second jump after falling");
        resetAir();game.FixedTick({false,false,true,false,false,true});
        Check(game.Get<Player>(player).jumpHoldRemaining>0,"air jump opens hold window");
        game.FixedTick({});Check(game.Get<Player>(player).jumpHoldRemaining==0,"release closes hold window");
        game.FixedTick({false,false,true,false,false,true});
        Check(game.Get<Player>(player).jumps==2 && game.Get<Player>(player).jumpHoldRemaining==0,"falling player has no second air jump");
        resetAir();auto x=game.Get<Transform>(player).position.x;game.FixedTick({false,true});
        Check(game.Get<Transform>(player).position.x>x,"right movement");game.FixedTick({true,true});
        Check(game.Get<Motion>(player).velocity.x==0,"opposing input cancels");
        // Trigger all three independent apple behaviors and verify restart resets them.
        for(auto name:{"cao1","cao2","cao3"}) {auto id=game.Find(name);Check(id!=0,"trigger exists");game.Touch(id);}
        Check(game.Get<Motion>(game.Find("ciapple1")).velocity.x==-100,"horizontal apple");
        Check(game.Get<Motion>(game.Find("ciapple2")).velocity.y==50,"vertical apple");
        game.Touch(game.Find("tu1"));Check(!game.Get<Collider>(game.Find("tu2")).enabled,"vanish pair");
        auto checkpoint=game.Find("unkeep2");game.Touch(checkpoint);auto saved=game.Checkpoint();
        bool lit=false;
        for(auto id:game.Entities())if(game.Get<Sprite>(id).image=="checkpoint_active.png" && glm::distance(game.Get<Transform>(id).position,saved)<.1f)lit=game.Get<Sprite>(id).visible;
        Check(lit,"checkpoint reveals activated pixel art");
        game.Kill();game.Kill();Check(game.Deaths()==1 && game.GetState()==State::Dead,"death counted once");
        game.Restart();Check(game.Get<Transform>(player).position==saved,"checkpoint respawn");
        Check(game.Get<Motion>(game.Find("ciapple1")).velocity==glm::vec2(0),"apple reset");
        Check(game.Get<Collider>(game.Find("tu2")).enabled,"vanish reset");
        Check(!game.Get<Collider>(checkpoint).enabled,"checkpoint persists");
        game.Touch(game.Find("gg"));Check(game.GetState()==State::Won,"victory");
        bool corpse=false;for(auto id:game.Entities()) if(game.Get<Behavior>(id).role==Role::Corpse) corpse=game.Get<Sprite>(id).visible;
        Check(corpse,"victory displays death history");game.Restart();Check(game.Get<Collider>(game.Find("gg")).enabled,"goal restart");
        // Fixed-step accumulator preserves a short jump edge until a tick occurs.
        resetAir();game.Advance(.001,{false,false,true});Check(game.Get<Player>(player).jumps==0,"substep queued");
        game.Advance(.01,{});Check(game.Get<Player>(player).jumps==2,"queued air jump consumed");
        // A real map floor catches a high downward velocity; no tunneling.
        game.Restart();auto& t=game.Get<Transform>(player);t.position={-62.5f,22};game.Get<Motion>(player).velocity={0,150};
        for(int n=0;n<12;++n) game.FixedTick({});
        Check(game.GetState()==State::Playing && game.Get<Transform>(player).position.y<28,"floor collision");
        // Triangle SAT rejects empty corners that an AABB-only test would kill.
        std::vector<glm::vec2> triangle={{0,-1},{1,1},{-1,1}},corner={{.8f,-.9f},{1,-.9f},{1,-.7f},{.8f,-.7f}};glm::vec2 mtv;
        Check(!Overlap(triangle,corner,mtv),"spike empty corner");
        std::vector<glm::vec2> center={{-.1f,0},{.1f,0},{.1f,.2f},{-.1f,.2f}};
        Check(Overlap(triangle,center,mtv),"spike center hit");
        // Cache invalidation must preserve the uncached collision geometry.
        Transform cachedTransform;cachedTransform.position={3,4};cachedTransform.size={2,5};cachedTransform.rotation=90;
        Collider cachedCollider;cachedCollider.points=triangle;
        UpdatePolygon(cachedTransform,cachedCollider);Check(cachedCollider.world==WorldPolygon(cachedTransform,cachedCollider),"cached rotation geometry");
        cachedTransform.position.x+=7;cachedTransform.size.y=3;cachedCollider.points[0].x=.2f;
        UpdatePolygon(cachedTransform,cachedCollider);Check(cachedCollider.world==WorldPolygon(cachedTransform,cachedCollider),"transform and shape invalidation");
        // Cross an ECS chunk boundary; creation must refresh borrowed views.
        for(int n=0;n<70;++n) {game.Restart();game.Kill();}
        Check(game.Views().size()==game.Entities().size(),"view count after structural changes");
        for(const auto& view:game.Views()) Check(view.transform==&game.Get<Transform>(view.id) && view.collider==&game.Get<Collider>(view.id),"view address after growth");
        game.Restart();game.FixedTick({});
        for(const auto& entry:std::filesystem::directory_iterator(std::filesystem::path(IWANNA_ASSETS)/"audio")) {
            auto clip=Audio::Clip::LoadWav(entry.path());Check(!clip->samples.empty(),"original WAV decoded");
        }
        Check(sounds>5,"sound event dispatch");
        game.Shutdown();Check(game.Startup(),"module restart lifecycle");Check(game.Entities().size()==83+tileCount,"clean lifecycle");
        std::cout<<"IWanna: extraction, input, double jump, SAT, terrain, traps, checkpoint, death, victory, audio, lifecycle PASS\n";
    } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
