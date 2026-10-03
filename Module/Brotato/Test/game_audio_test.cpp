#include "Brotato/Public/game_audio.h"
#include "Brotato/Public/game_module.h"
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using namespace Brotato;
void Check(bool condition,const char* message) { if(!condition) throw std::runtime_error(message); }
Config Quiet(WeaponKind kind=WeaponKind::Wand,bool armed=false) {
    Config config;
    config.spawning=false; config.armed=armed; config.enemySpeed=0; config.dropChance=0;
    config.initialWeapon=kind; config.waveSeconds=600;
    config.weapons[WeaponIndex(WeaponKind::Wand)].gravity=0;
    return config;
}
void Start(GameModule& game) { Check(game.Startup(),"game startup"); }
glm::vec2 Position(GameModule& game) { return game.Get<Transform>(game.PlayerEntity()).position; }
void Tick(GameModule& game,int count) { while(count-->0) game.FixedTick(); }
void KillWithProjectile(GameModule& game,glm::vec2 position) {
    game.SpawnEnemy(position,true);
    game.SpawnProjectile(position,{});
    game.FixedTick();
}
void ExactMusicAndButton(const std::filesystem::path& root,Audio::Mixer& mixer,GameAudio& audio) {
    Check(audio.IsLoaded() && audio.MusicPlaying(),"load starts looping background music");
    Audio::Mixer reference;
    reference.Play(Audio::Clip::LoadWav(root/"Audio/music.wav"),.594f,true);
    reference.Play(Audio::Clip::LoadWav(root/"Audio/button.wav"),.765f);
    audio.Click();
    std::array<float,10000> actual{},expected{};
    mixer.Render(actual); reference.Render(expected);
    Check(actual==expected,"music and UI use exact imported clips and source gains");
    audio.Click(); audio.Click();
    Check(mixer.ActiveVoices()==2,"button Play restarts one UI voice");
    mixer.Render(actual);
    Check(audio.PlayCount(AudioCue::Music)==1,"UI never restarts music");
}
void WeaponMapping(GameAudio& audio) {
    for(std::size_t index=0;index<WeaponCount;++index) {
        auto kind=static_cast<WeaponKind>(index);
        GameModule game(Quiet(kind,true)); Start(game);
        audio.Sync(&game,index+1);
        const auto wand=audio.PlayCount(AudioCue::Wand),gun=audio.PlayCount(AudioCue::Gun),burst=audio.PlayCount(AudioCue::Burst);
        game.SpawnEnemy(Position(game)+glm::vec2(3,0),true);
        game.FixedTick(); audio.Sync(&game,index+1);
        Check(audio.PlayCount(AudioCue::Wand)-wand==(kind==WeaponKind::Wand),"wand mapping");
        Check(audio.PlayCount(AudioCue::Gun)-gun==(kind==WeaponKind::Gun),"SMG mapping");
        Check(audio.PlayCount(AudioCue::Burst)-burst==(kind==WeaponKind::Burst),"shotgun mapping; other weapons have no firing clip");
        Check(game.DrainEvents().empty(),"bridge consumes each gameplay event once");
        audio.Sync(&game,index+1);
        Check(audio.PlayCount(AudioCue::Wand)-wand==(kind==WeaponKind::Wand),"repeated frame does not repeat events");
        audio.Sync(nullptr,0);
    }
}
void KillAndPickup(GameAudio& audio) {
    GameModule game(Quiet()); Start(game); audio.Sync(&game,20);
    auto death=audio.PlayCount(AudioCue::EnemyDeath),fire=audio.PlayCount(AudioCue::FireDeath),material=audio.PlayCount(AudioCue::Material);
    KillWithProjectile(game,Position(game)+glm::vec2(4,0)); audio.Sync(&game,20);
    Check(audio.PlayCount(AudioCue::EnemyDeath)==death+1 && audio.PlayCount(AudioCue::FireDeath)==fire,"ordinary lethal hit has one flesh sound");
    Tick(game,190); audio.Sync(&game,20);
    Check(audio.PlayCount(AudioCue::EnemyDeath)==death+1,"delayed corpse removal does not replay death sound");
    game.SpawnPickup(Position(game)); game.SpawnPickup(Position(game)); game.FixedTick(); audio.Sync(&game,20);
    Check(audio.PlayCount(AudioCue::Material)==material+2,"both pickups emit sound events");
    Check(audio.GameVoiceCount()==2,"shared pickup AudioSource restarts while independent enemy voice remains");
    audio.Sync(nullptr,0);
    GameModule torch(Quiet(WeaponKind::Torch,true)); Start(torch); audio.Sync(&torch,21);
    torch.SpawnEnemy(Position(torch)+glm::vec2(2,0),true);
    for(int i=0;i<30 && !torch.Stats().kills;++i) torch.FixedTick();
    Check(torch.Stats().kills==1,"torch setup killed enemy");
    audio.Sync(&torch,21);
    Check(audio.PlayCount(AudioCue::EnemyDeath)==death+2 && audio.PlayCount(AudioCue::FireDeath)==fire+1,"torch adds burn to flesh hit once");
    audio.Sync(nullptr,0);
}
void PauseDeathAndRevive(Audio::Mixer& mixer,GameAudio& audio) {
    auto config=Quiet(); config.initialHealth=config.contactDamage=10;
    GameModule game(config); Start(game); audio.Sync(&game,30);
    KillWithProjectile(game,Position(game)+glm::vec2(4,0)); audio.Sync(&game,30);
    Check(audio.GameVoiceCount()==1,"setup owns one game voice");
    game.SetPaused(true); audio.Sync(&game,30);
    std::array<float,4096> block{};
    for(int i=0;i<25;++i) mixer.Render(block);
    Check(audio.GameVoiceCount()==1 && audio.MusicPlaying(),"paused game sound retains cursor while long music keeps playing");
    auto button=audio.PlayCount(AudioCue::Button); audio.Click();
    Check(audio.PlayCount(AudioCue::Button)==button+1,"pause menu click is audible independently");
    game.SetPaused(false); audio.Sync(&game,30);
    for(int i=0;i<25;++i) mixer.Render(block);
    Check(audio.GameVoiceCount()==0,"resumed effect advances and expires");
    KillWithProjectile(game,Position(game)+glm::vec2(4,0)); audio.Sync(&game,30);
    game.SpawnEnemy(Position(game),true); game.FixedTick(); audio.Sync(&game,30);
    Check(game.GetState()==State::Dead && audio.GameVoiceCount()==0,"death clears all old game tails");
    game.Revive(); audio.Sync(&game,30);
    Check(audio.GameVoiceCount()==0 && audio.MusicPlaying(),"revive never resumes stale death-time sound");
    audio.Sync(nullptr,0);
}
void EpochRunAndHome(GameAudio& audio) {
    GameModule game(Quiet()); Start(game); audio.Sync(&game,40);
    KillWithProjectile(game,Position(game)+glm::vec2(4,0)); audio.Sync(&game,40);
    game.Restart(); audio.Sync(&game,40);
    Check(audio.GameVoiceCount()==0,"restart epoch clears game tails");
    KillWithProjectile(game,Position(game)+glm::vec2(4,0)); audio.Sync(&game,40);
    audio.Sync(&game,41);
    Check(audio.GameVoiceCount()==0,"new run serial clears even a reused address");
    KillWithProjectile(game,Position(game)+glm::vec2(4,0)); audio.Sync(&game,41);
    game.Shutdown(); audio.Sync(&game,41);
    Check(audio.GameVoiceCount()==0 && audio.MusicPlaying(),"shutdown keeps scene music but clears game voices");
    audio.Sync(nullptr,0); audio.Click();
    Check(audio.GameVoiceCount()==0,"home owns no old run voices");
    auto shortConfig=Quiet(); shortConfig.waveSeconds=.05f;
    GameModule wave(shortConfig); Start(wave); audio.Sync(&wave,42);
    KillWithProjectile(wave,Position(wave)+glm::vec2(4,0)); audio.Sync(&wave,42);
    Tick(wave,8); audio.Sync(&wave,42);
    Check(wave.GetState()==State::WaveComplete && audio.GameVoiceCount()==0,"wave completion clears tails");
    wave.NextWave(); audio.Sync(&wave,42);
    Check(audio.GameVoiceCount()==0,"next wave never replays old queue");
    audio.Sync(nullptr,0);
}
void BoundsAndOwnership(const std::filesystem::path& root,Audio::Mixer& mixer,GameAudio& audio) {
    auto foreign=mixer.Play(Audio::Clip::LoadWav(root/"Audio/material.wav"),.1f,true);
    GameModule game(Quiet()); Start(game); audio.Sync(&game,50);
    auto position=Position(game)+glm::vec2(4,0);
    for(int i=0;i<80;++i) { game.SpawnEnemy(position,true); game.SpawnProjectile(position,{}); }
    game.FixedTick(); audio.Sync(&game,50);
    Check(game.Stats().kills==80,"stress setup produces 80 independent lethal events");
    Check(audio.GameVoiceCount()==GameAudio::MaxGameVoices && audio.RetiredVoiceCount()>=48,"burst of deaths remains bounded to 32 voices");
    Check(audio.MusicPlaying() && mixer.IsPlaying(foreign),"game cap does not evict music or foreign module audio");
    audio.Shutdown();
    Check(mixer.ActiveVoices()==1 && mixer.IsPlaying(foreign),"bridge shutdown stops only owned voices");
    mixer.Stop(foreign);
    Check(!audio.Load(root/"missing") && !audio.Error().empty() && !audio.IsLoaded(),"missing assets fail atomically with diagnostic");
    audio.Click(); audio.Sync(&game,51);
    Check(mixer.ActiveVoices()==0,"unloaded bridge is safely silent");
}
}
int main(int argc,char** argv) {
    try {
        const std::filesystem::path root=argc>1?argv[1]:"Asset/Brotato";
        Audio::Mixer mixer; GameAudio audio(mixer);
        if(!audio.Load(root)) throw std::runtime_error(audio.Error());
        ExactMusicAndButton(root,mixer,audio); std::cout<<"[PASS] imported music and UI PCM/gains/restart\n";
        WeaponMapping(audio); std::cout<<"[PASS] six weapon mappings and event consumption\n";
        KillAndPickup(audio); std::cout<<"[PASS] enemy, torch, pickup source semantics\n";
        PauseDeathAndRevive(mixer,audio); std::cout<<"[PASS] pause cursor, UI, death and revive lifecycle\n";
        EpochRunAndHome(audio); std::cout<<"[PASS] restart, new serial, home and wave cleanup\n";
        BoundsAndOwnership(root,mixer,audio); std::cout<<"[PASS] voice cap, module ownership and missing assets\n";
        std::cout<<"Brotato audio: 6 offline groups passed (no subjective listening claim)\n";
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
