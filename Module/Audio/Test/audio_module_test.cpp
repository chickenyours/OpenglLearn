#include "Audio/Public/audio_module.h"
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
static void Check(bool v) { if(!v) throw std::runtime_error("audio test failed"); }
int main() {
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
    std::cout<<"audio: PCM8/16/24/32, float32, malformed/truncated WAV, stereo, resampling, pan, loop, stop, clipping PASS\n";
}
