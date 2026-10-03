#pragma once
#include "module_base.h"
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <vector>

namespace Audio {
// Interleaved normalized PCM. Decoding and mixing have no OS dependency.
struct Clip {
    uint32_t sampleRate = 48000;
    uint16_t channels = 2;
    std::vector<float> samples;
    static std::shared_ptr<const Clip> LoadWav(const std::filesystem::path& path);
    static std::shared_ptr<const Clip> DecodeWav(std::span<const uint8_t> bytes);
};
using VoiceID = uint64_t;
class Mixer {
public:
    VoiceID Play(std::shared_ptr<const Clip> clip, float gain = 1, bool loop = false, float pan = 0);
    void Stop(VoiceID id);
    void StopAll();
    void SetVolume(float gain);
    void Render(std::span<float> stereo, uint32_t outputRate = 48000);
private:
    struct Voice { VoiceID id; std::shared_ptr<const Clip> clip; double cursor; float gain, pan; bool loop; };
    std::mutex mutex_;
    std::vector<Voice> voices_;
    VoiceID next_ = 1;
    float volume_ = 1;
    // Stereo-linked peak limiter: instant attenuation, gradual 50ms recovery.
    float limiterGain_ = 1;
};
// A host can supply any platform's output callback. Close must join all callbacks.
class Output {
public:
    using Pull = std::function<void(std::span<float>)>;
    virtual ~Output() = default;
    virtual bool Open(uint32_t sampleRate, Pull pull, std::string& error) = 0;
    virtual void Close() = 0;
};
std::unique_ptr<Output> CreateNativeOutput();
class AudioModule final : public IModule {
public:
    explicit AudioModule(std::unique_ptr<Output> output = CreateNativeOutput());
    ~AudioModule() override { Shutdown(); }
    const char* GetName() const noexcept override { return "AudioModule"; }
    bool Startup() override;
    void Shutdown() override;
    bool IsStarted() const noexcept override { return started_; }
    Mixer& GetMixer() { return mixer_; }
    const std::string& Error() const { return error_; }
private:
    Mixer mixer_;
    std::unique_ptr<Output> output_;
    std::string error_;
    bool started_ = false;
};
}
