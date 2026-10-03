#pragma once

#include "Audio/Public/audio_module.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Brotato {
class GameModule;
enum class AudioCue : std::size_t { Wand, Gun, Burst, EnemyDeath, FireDeath, Material, Button, Music, Count };

// Main-thread presentation bridge. It owns only its own voices; the host owns
// the mixer/output and must keep them alive until after this bridge is destroyed.
class GameAudio final {
public:
    static constexpr std::size_t MaxGameVoices = 32;
    explicit GameAudio(Audio::Mixer& mixer) : mixer_(mixer) {}
    ~GameAudio() { Shutdown(); }
    GameAudio(const GameAudio&) = delete;
    GameAudio& operator=(const GameAudio&) = delete;
    bool Load(const std::filesystem::path& assetsRoot);
    void Shutdown();
    // Call once after advancing simulation, before extracting the render frame.
    // runSerial comes from SessionModule and disambiguates reused game addresses.
    void Sync(GameModule* game, std::uint64_t runSerial);
    void Click();
    bool IsLoaded() const { return loaded_; }
    const std::string& Error() const { return error_; }
    std::size_t GameVoiceCount() const;
    std::uint64_t PlayCount(AudioCue cue) const { return played_.at(static_cast<std::size_t>(cue)); }
    std::uint64_t RetiredVoiceCount() const { return retired_; }
    bool MusicPlaying() const { return mixer_.IsPlaying(music_); }
private:
    struct GameVoice { Audio::VoiceID id; AudioCue cue; };
    void ClearGameVoices();
    void EnsureMusic();
    void PlayGame(AudioCue cue, float gain, bool restart = false);
    Audio::VoiceID Play(AudioCue cue, float gain, bool loop = false);
    Audio::Mixer& mixer_;
    std::array<std::shared_ptr<const Audio::Clip>, static_cast<std::size_t>(AudioCue::Count)> clips_{};
    std::array<std::uint64_t, static_cast<std::size_t>(AudioCue::Count)> played_{};
    std::vector<GameVoice> voices_;
    Audio::VoiceID music_ = 0, button_ = 0;
    GameModule* game_ = nullptr; // identity only; never dereferenced after Sync
    std::uint64_t runSerial_ = 0, epoch_ = 0, retired_ = 0;
    bool loaded_ = false;
    std::string error_;
};
} // namespace Brotato
