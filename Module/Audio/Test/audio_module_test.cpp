#include "Audio/Public/audio_module.h"
#include "Audio/Private/pcm_conversion.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <source_location>
#include <stdexcept>
#include <thread>
static void Check(bool v,const std::source_location& at=std::source_location::current()) {
    if(!v) throw std::runtime_error("audio test failed at line "+std::to_string(at.line()));
}
static void StreamingChecks() {
    auto tone=std::make_shared<Audio::Clip>(); tone->channels=2; tone->sampleRate=44100;
    for(size_t i=0;i<4410;++i) {
        float sample=.8f*std::sin(2*std::numbers::pi_v<float>*440*float(i)/44100);
        tone->samples.push_back(sample); tone->samples.push_back(sample*.5f);
    }
    Audio::Mixer whole,chunked;
    for(auto* mixer:{&whole,&chunked}) { mixer->Play(tone,1,true); mixer->Play(tone,1,true); }
    std::vector<float> expected(48000),actual(expected.size()); whole.Render(expected);
    // Exercise arbitrary callback lengths across both a source loop and rate
    // conversion boundaries; a backend can batch several completed buffers.
    const std::array<size_t,7> sizes={2,1024,4096,62,2048,128,358};
    size_t offset=0,step=0;
    while(offset<actual.size()) {
        size_t count=std::min(sizes[step++%sizes.size()],actual.size()-offset);
        chunked.Render(std::span(actual).subspan(offset,count)); offset+=count;
    }
    size_t nearCeiling=0;
    for(size_t i=0;i<expected.size();i+=2) {
        Check(expected[i]==actual[i] && expected[i+1]==actual[i+1]);
        Check(std::isfinite(actual[i]) && std::abs(actual[i])<=1);
        Check(std::abs(actual[i+1]-.5f*actual[i])<.000001f);
        if(i>4800 && std::abs(actual[i])>.999f) ++nearCeiling;
    }
    // A hard clip of this 1.6-peak tone saturates ~57% of its samples. The
    // limiter should retain an ordinary waveform after its initial attack.
    Check(nearCeiling<expected.size()/20);
    std::cout<<"overlapping 1.6-peak tone: "<<nearCeiling<<" near-full-scale samples in "<<expected.size()/2<<" frames\n";
    chunked.StopAll(); chunked.Play(tone); std::array<float,200> normal{}; chunked.Render(normal,44100);
    for(size_t i=0;i<normal.size();++i) Check(normal[i]==tone->samples[i]);
    using Audio::Detail::ToPcm16;
    Check(ToPcm16(0)==0 && ToPcm16(1)==32767 && ToPcm16(-1)==-32767);
    Check(ToPcm16(2)==32767 && ToPcm16(-2)==-32767);
    Check(ToPcm16(std::numeric_limits<float>::quiet_NaN())==0 && ToPcm16(std::numeric_limits<float>::infinity())==0);
    // Quantization must not introduce a block-edge jump or asymmetric DC bias.
    for(float sample:{-.75f,-.01f,.01f,.75f}) {
        Check(ToPcm16(-sample)==-ToPcm16(sample));
        Check(std::abs(float(ToPcm16(sample))/32767-sample)<=.5f/32767);
    }
}
static void DeviceChecks() {
    auto output=Audio::CreateNativeOutput();
    for(uint32_t rate:{48000u,44100u}) {
        std::atomic<size_t> frames{}; std::string error;
        Check(output->Open(rate,[&](std::span<float> pcm) {
            std::fill(pcm.begin(),pcm.end(),0.f); frames+=pcm.size()/2;
        },error));
        std::this_thread::sleep_for(std::chrono::milliseconds(220)); output->Close();
        const auto stopped=frames.load(); Check(stopped>=size_t(rate)/10);
        std::this_thread::sleep_for(std::chrono::milliseconds(30)); Check(frames==stopped);
        output->Close();
        std::cout<<"native silent device "<<rate<<"Hz: "<<stopped<<" frames, callbacks stopped after Close PASS\n";
    }
}
static void VoicePauseChecks() {
    auto clip=std::make_shared<Audio::Clip>(); clip->channels=1; clip->sampleRate=48000;
    clip->samples={.1f,.2f,.3f,.4f};
    auto bed=std::make_shared<Audio::Clip>(); bed->channels=1; bed->samples={.01f};
    Audio::Mixer mixer;
    auto voice=mixer.Play(clip), music=mixer.Play(bed,1,true);
    std::array<float,2> frame{};
    Check(mixer.ActiveVoices()==2 && mixer.IsPlaying(voice));
    mixer.Render(frame); Check(std::abs(frame[0]-.11f)<1e-6f);
    mixer.SetPaused(voice,true);
    for(int i=0;i<100;++i) { mixer.Render(frame); Check(frame[0]==.01f && frame[1]==.01f); }
    Check(mixer.IsPlaying(voice) && mixer.IsPlaying(music));
    mixer.SetPaused(voice,false);
    mixer.Render(frame); Check(std::abs(frame[0]-.21f)<1e-6f);
    mixer.Render(frame); Check(std::abs(frame[0]-.31f)<1e-6f);
    mixer.Render(frame); Check(std::abs(frame[0]-.41f)<1e-6f);
    Check(!mixer.IsPlaying(voice) && mixer.ActiveVoices()==1);
    mixer.SetPaused(voice,true); // stale voice IDs are harmless.
    mixer.SetPaused(music,true); mixer.Stop(music); mixer.Render(frame);
    Check(frame[0]==0 && frame[1]==0 && mixer.ActiveVoices()==0);
    std::cout<<"per-voice pause: cursor frozen, music continues, resume and stale IDs PASS\n";
}
int main(int argc,char** argv) {
    try {
    std::vector<uint8_t> wav={'R','I','F','F',40,0,0,0,'W','A','V','E','f','m','t',' ',16,0,0,0,
        1,0,1,0,0x80,0xbb,0,0,0,0x77,1,0,2,0,16,0,'d','a','t','a',4,0,0,0,0,0x40,0,0xc0};
    auto clip=Audio::Clip::DecodeWav(wav); Check(clip->samples.size()==2); Check(clip->samples[0]==.5f && clip->samples[1]==-.5f);
    Audio::Mixer mixer; std::array<float,8> out{};
    mixer.Play(clip); mixer.Render(out,96000); Check(out[0]==.5f && out[2]==0 && out[4]==-.5f);
    mixer.Render(out); for(auto f:out) Check(f==0);
    auto voice=mixer.Play(clip,1,true,1); mixer.Render(out); Check(out[0]==0 && out[1]==.5f && out[5]==.5f);
    mixer.Stop(voice); mixer.Render(out); for(auto f:out) Check(f==0);
    mixer.Play(clip,4,true); mixer.Play(clip,4,true); mixer.Render(out); Check(out[0]==1 && out[2]==-1);
    mixer.SetVolume(0); mixer.Render(out); for(auto f:out) Check(f==0);
    // Independently encoded boundary samples for every supported representation.
    auto decode=[&](int bits,int format,std::vector<uint8_t> payload) {
        auto bytes=wav;bytes.resize(44);bytes[20]=uint8_t(format);bytes[34]=uint8_t(bits);bytes[32]=uint8_t(bits/8);
        uint32_t byteRate=48000*(bits/8);for(int i=0;i<4;++i) bytes[28+i]=uint8_t(byteRate>>(8*i));
        bytes[40]=uint8_t(payload.size());bytes.insert(bytes.end(),payload.begin(),payload.end());
        if(payload.size()%2) bytes.push_back(0);bytes[4]=uint8_t(bytes.size()-8);
        return Audio::Clip::DecodeWav(bytes);
    };
    auto pcm8=decode(8,1,{0,128,255});Check(pcm8->samples[0]==-1 && pcm8->samples[1]==0 && pcm8->samples[2]==127.f/128);
    auto pcm24=decode(24,1,{0,0,128,0,0,64});Check(pcm24->samples[0]==-1 && pcm24->samples[1]==.5f);
    auto pcm32=decode(32,1,{0,0,0,128,0,0,0,64});Check(pcm32->samples[0]==-1 && pcm32->samples[1]==.5f);
    auto fp32=decode(32,3,{0,0,0,63,0,0,192,127});Check(fp32->samples[0]==.5f && fp32->samples[1]==0);
    auto stereo=std::make_shared<Audio::Clip>();stereo->samples={.25f,-.75f};
    mixer.StopAll();mixer.SetVolume(1);mixer.Play(stereo);mixer.Render(out);Check(out[0]==.25f && out[1]==-.75f && out[2]==0);
    for(size_t i=0;i<wav.size();++i) { bool threw=false; try { Audio::Clip::DecodeWav(std::span(wav).first(i)); } catch(const std::runtime_error&) {threw=true;} Check(threw); }
    wav[22]=3; bool threw=false; try { Audio::Clip::DecodeWav(wav); } catch(const std::runtime_error&) {threw=true;} Check(threw);
    StreamingChecks();
    VoicePauseChecks();
    for(int i=1;i<argc;++i) {
        const std::string arg=argv[i];
        if(arg=="--device-test") DeviceChecks();
        else if(arg=="--wav-dir" && i+1<argc) {
            for(const auto& item:std::filesystem::directory_iterator(argv[++i])) if(item.path().extension()==".wav") {
                auto asset=Audio::Clip::LoadWav(item.path()); Check(!asset->samples.empty());
                std::cout<<item.path().filename().string()<<": "<<asset->sampleRate<<"Hz, "<<asset->channels<<"ch decoded PASS\n";
            }
        } else throw std::runtime_error("unknown audio test argument");
    }
    std::cout<<"audio: WAV formats, stereo/resampling/loop, callback-block continuity, linked peak limiter, finite PCM conversion PASS\n";
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
