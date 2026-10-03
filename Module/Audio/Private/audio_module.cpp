#include "Audio/Public/audio_module.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <fstream>
#include <stdexcept>

namespace Audio {
std::shared_ptr<const Clip> Clip::LoadWav(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("Cannot open WAV: " + path.string());
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(file)), {});
    return DecodeWav(bytes);
}
std::shared_ptr<const Clip> Clip::DecodeWav(std::span<const uint8_t> b) {
    auto fail = [] { throw std::runtime_error("Invalid or unsupported RIFF/WAVE (PCM 8/16/24/32 or float32, mono/stereo required)"); };
    auto u16 = [&](size_t p) -> uint16_t { if(p+2>b.size()) fail(); return b[p] | uint16_t(b[p+1])<<8; };
    auto u32 = [&](size_t p) -> uint32_t { return u16(p) | uint32_t(u16(p+2))<<16; };
    auto tag = [&](size_t p, const char* s) { return p+4<=b.size() && std::equal(b.begin()+p,b.begin()+p+4,s); };
    if(b.size()<12 || !tag(0,"RIFF") || !tag(8,"WAVE")) fail();
    const uint64_t end = uint64_t(u32(4))+8;
    if(end>b.size() || end<12) fail();
    uint16_t format=0, channels=0, bits=0, align=0; uint32_t rate=0;
    std::span<const uint8_t> data;
    for(size_t p=12; p+8<=end;) {
        const size_t n=u32(p+4), start=p+8;
        if(n>end-start) fail();
        if(tag(p,"fmt ")) {
            if(n<16) fail();
            format=u16(start); channels=u16(start+2); rate=u32(start+4); align=u16(start+12); bits=u16(start+14);
        } else if(tag(p,"data")) data=b.subspan(start,n);
        p=start+n+(n&1);
    }
    if((channels!=1 && channels!=2) || rate<1000 || rate>384000 ||
       !((format==1 && (bits==8 || bits==16 || bits==24 || bits==32)) || (format==3 && bits==32)) ||
       align!=channels*(bits/8) || data.empty() || data.size()%align) fail();
    auto clip=std::make_shared<Clip>(); clip->sampleRate=rate; clip->channels=channels;
    for(size_t p=0; p<data.size(); p+=bits/8) {
        uint32_t raw=0; for(unsigned j=0;j<bits/8;++j) raw|=uint32_t(data[p+j])<<(j*8);
        float value;
        if(format==3) value=std::bit_cast<float>(raw);
        else if(bits==8) value=(int(raw)-128)/128.f;
        else { int64_t signedValue=raw; if(raw & (uint32_t(1)<<(bits-1))) signedValue-=int64_t(1)<<bits; value=float(double(signedValue)/double(int64_t(1)<<(bits-1))); }
        clip->samples.push_back(std::isfinite(value)?std::clamp(value,-1.f,1.f):0.f);
    }
    return clip;
}
VoiceID Mixer::Play(std::shared_ptr<const Clip> clip,float gain,bool loop,float pan) {
    if(!clip || clip->samples.empty() || (clip->channels!=1 && clip->channels!=2) || !clip->sampleRate || clip->samples.size()%clip->channels || !std::isfinite(gain) || !std::isfinite(pan)) return 0;
    std::lock_guard lock(mutex_);
    if(voices_.size()>=64) voices_.erase(voices_.begin());
    const auto id=next_++; voices_.push_back({id,std::move(clip),0,std::clamp(gain,0.f,4.f),std::clamp(pan,-1.f,1.f),loop}); return id;
}
void Mixer::Stop(VoiceID id) { std::lock_guard lock(mutex_); std::erase_if(voices_,[=](const auto& v){return v.id==id;}); }
void Mixer::StopAll() { std::lock_guard lock(mutex_); voices_.clear(); limiterGain_=1; }
void Mixer::SetPaused(VoiceID id,bool paused) {
    std::lock_guard lock(mutex_);
    for(auto& voice:voices_) if(voice.id==id) { voice.paused=paused; break; }
}
bool Mixer::IsPlaying(VoiceID id) const {
    std::lock_guard lock(mutex_);
    return std::any_of(voices_.begin(),voices_.end(),[=](const auto& voice){return voice.id==id;});
}
std::size_t Mixer::ActiveVoices() const { std::lock_guard lock(mutex_); return voices_.size(); }
void Mixer::SetVolume(float v) { std::lock_guard lock(mutex_); volume_=std::isfinite(v)?std::clamp(v,0.f,1.f):0; }
void Mixer::Render(std::span<float> out,uint32_t rate) {
    std::lock_guard lock(mutex_); std::fill(out.begin(),out.end(),0);
    if(!rate) return;
    for(auto& v:voices_) {
        if(v.paused) continue;
        const auto& c=*v.clip; const size_t frames=c.samples.size()/c.channels;
        for(size_t i=0;i+1<out.size();i+=2) {
            if(v.cursor>=frames) { if(!v.loop) break; v.cursor=std::fmod(v.cursor,double(frames)); }
            size_t a=size_t(v.cursor), b=a+1; if(b>=frames) b=v.loop?0:a;
            float t=float(v.cursor-a);
            for(size_t ch=0;ch<2;++ch) {
                const size_t sc=ch%c.channels;
                float s=c.samples[a*c.channels+sc]*(1-t)+c.samples[b*c.channels+sc]*t;
                if(!std::isfinite(s)) s=0;
                else s=std::clamp(s,-1.f,1.f);
                out[i+ch]+=s*v.gain*(ch==0?1-std::max(v.pan,0.f):1+std::min(v.pan,0.f));
            }
            v.cursor+=double(c.sampleRate)/rate;
        }
    }
    std::erase_if(voices_,[](const auto& v){return !v.loop && v.cursor>=v.clip->samples.size()/v.clip->channels;});
    // Apply one gain to both channels. Hard clipping each sample used to turn
    // overlapping loud effects into sustained square-wave-like distortion.
    // Keep normal-level audio unchanged; recover smoothly after a loud peak.
    // The envelope advances per frame, so output does not depend on block size.
    const float recovery=1.f-std::exp(-1.f/(.05f*rate));
    for(size_t i=0;i+1<out.size();i+=2) {
        const float left=out[i]*volume_, right=out[i+1]*volume_;
        const float peak=std::max(std::abs(left),std::abs(right));
        const float ceiling=peak>1.f?1.f/peak:1.f;
        limiterGain_=std::min(ceiling,limiterGain_+(1.f-limiterGain_)*recovery);
        out[i]=std::clamp(left*limiterGain_,-1.f,1.f);
        out[i+1]=std::clamp(right*limiterGain_,-1.f,1.f);
    }
}
AudioModule::AudioModule(std::unique_ptr<Output> output):output_(std::move(output)){}
bool AudioModule::Startup() {
    if(started_) return true; error_.clear();
    if(!output_) { error_="No audio output backend"; return false; }
    started_=output_->Open(48000,[this](std::span<float> b){mixer_.Render(b);},error_);
    return started_;
}
void AudioModule::Shutdown() { if(output_) output_->Close(); started_=false; mixer_.StopAll(); }
}
