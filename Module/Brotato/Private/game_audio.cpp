#include "Brotato/Public/game_audio.h"
#include "Brotato/Public/game_module.h"
#include <algorithm>
#include <exception>

namespace Brotato {
namespace {
constexpr std::array<const char*, static_cast<std::size_t>(AudioCue::Count)> Names = {
    "wand", "gun", "burst", "enemy_death", "fire_death", "material", "button", "music"
};
}
bool GameAudio::Load(const std::filesystem::path& root) {
    Shutdown();
    error_.clear();
    try {
        decltype(clips_) imported;
        for(std::size_t i=0;i<Names.size();++i)
            imported[i] = Audio::Clip::LoadWav(root / "Audio" / (std::string(Names[i])+".wav"));
        clips_ = std::move(imported);
        played_.fill(0);
        retired_ = 0;
        voices_.reserve(MaxGameVoices);
        loaded_ = true;
        EnsureMusic();
        return true;
    } catch(const std::exception& error) {
        error_ = error.what();
        return false;
    }
}
void GameAudio::Shutdown() {
    ClearGameVoices();
    mixer_.Stop(button_);
    mixer_.Stop(music_);
    music_ = button_ = 0;
    game_ = nullptr;
    epoch_ = runSerial_ = 0;
    loaded_ = false;
    clips_.fill({});
}
void GameAudio::ClearGameVoices() {
    for(const auto& voice:voices_) mixer_.Stop(voice.id);
    voices_.clear();
}
Audio::VoiceID GameAudio::Play(AudioCue cue,float gain,bool loop) {
    if(!loaded_) return 0;
    auto index = static_cast<std::size_t>(cue);
    auto id = mixer_.Play(clips_[index],gain,loop);
    if(id) ++played_[index];
    return id;
}
void GameAudio::EnsureMusic() {
    if(loaded_ && !mixer_.IsPlaying(music_)) music_ = Play(AudioCue::Music,.594f,true);
}
void GameAudio::PlayGame(AudioCue cue,float gain,bool restart) {
    if(!loaded_) return;
    std::erase_if(voices_,[&](const auto& voice) {
        if(restart && voice.cue==cue) { mixer_.Stop(voice.id); return true; }
        return !mixer_.IsPlaying(voice.id);
    });
    if(voices_.size()>=MaxGameVoices) {
        mixer_.Stop(voices_.front().id);
        voices_.erase(voices_.begin());
        ++retired_;
    }
    if(auto id = Play(cue,gain)) voices_.push_back({id,cue});
}
void GameAudio::Click() {
    mixer_.Stop(button_);
    button_ = Play(AudioCue::Button,.765f);
}
std::size_t GameAudio::GameVoiceCount() const {
    return std::count_if(voices_.begin(),voices_.end(),[&](const auto& voice){return mixer_.IsPlaying(voice.id);});
}
void GameAudio::Sync(GameModule* game,std::uint64_t runSerial) {
    EnsureMusic();
    if(!game || !game->IsStarted()) {
        ClearGameVoices();
        game_ = nullptr;
        epoch_ = runSerial_ = 0;
        return;
    }
    if(game!=game_ || runSerial!=runSerial_ || game->PresentationEpoch()!=epoch_) {
        ClearGameVoices();
        game_ = game;
        runSerial_ = runSerial;
        epoch_ = game->PresentationEpoch();
    }
    std::erase_if(voices_,[&](const auto& voice){return !mixer_.IsPlaying(voice.id);});
    const State state = game->GetState();
    auto events = game->DrainEvents();
    if(state==State::Dead || state==State::WaveComplete || state==State::Shop || state==State::Victory || state==State::Defeat) {
        ClearGameVoices();
        return;
    }
    const bool paused = state==State::Paused || state==State::LevelUp;
    for(const auto& voice:voices_) mixer_.SetPaused(voice.id,paused);
    if(paused) return;
    for(const auto& event:events) {
        switch(event.kind) {
        case GameEventKind::WeaponAttack:
            switch(event.weapon) {
            case WeaponKind::Wand: PlayGame(AudioCue::Wand,.732f); break;
            case WeaponKind::Gun: PlayGame(AudioCue::Gun,.504f,true); break;
            case WeaponKind::Burst: PlayGame(AudioCue::Burst,.504f,true); break;
            default: break; // Source torch/knife/laser have no firing audio binding.
            }
            break;
        case GameEventKind::EnemyKilled:
            PlayGame(AudioCue::EnemyDeath,event.weapon==WeaponKind::Torch?.3f:.618f);
            if(event.weapon==WeaponKind::Torch) PlayGame(AudioCue::FireDeath,1);
            break;
        case GameEventKind::MaterialCollected:
            PlayGame(AudioCue::Material,1,true);
            break;
        case GameEventKind::PlayerHurt:
            break; // No PlayerController hurt AudioSource in the source scene.
        }
    }
}
} // namespace Brotato
