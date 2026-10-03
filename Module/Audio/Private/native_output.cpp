#include "Audio/Public/audio_module.h"
#include "pcm_conversion.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <thread>
#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#include <mmsystem.h>
#elif defined(__APPLE__)
#include <AudioToolbox/AudioToolbox.h>
#elif defined(__linux__)
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/soundcard.h>
#include <cerrno>
#endif
namespace Audio {
namespace {
#if defined(_WIN32)
class NativeOutput final : public Output {
    HWAVEOUT device_{}; std::thread worker_; std::atomic_bool stop_{false};
    HANDLE completed_{};
    // 85ms at the module's 48kHz rate: enough queue depth for a delayed game
    // thread without increasing the amount of work in each device callback.
    static constexpr size_t bufferCount=4, samplesPerBuffer=2048;
    std::array<WAVEHDR,bufferCount> headers_{};
    std::array<std::array<int16_t,samplesPerBuffer>,bufferCount> buffers_{};
    bool Fill(size_t index,Pull& pull,std::span<float> pcm) {
        std::fill(pcm.begin(),pcm.end(),0.f);
        pull(pcm);
        for(size_t j=0;j<pcm.size();++j) buffers_[index][j]=Detail::ToPcm16(pcm[j]);
        return waveOutWrite(device_,&headers_[index],sizeof(WAVEHDR))==MMSYSERR_NOERROR;
    }
public:
    ~NativeOutput() override { Close(); }
    bool Open(uint32_t rate,Pull pull,std::string& error) override {
        Close(); WAVEFORMATEX fmt{}; fmt.wFormatTag=WAVE_FORMAT_PCM; fmt.nChannels=2; fmt.nSamplesPerSec=rate;
        fmt.wBitsPerSample=16; fmt.nBlockAlign=4; fmt.nAvgBytesPerSec=rate*4;
        if(!pull || rate<1000 || rate>384000) { error="waveOut: invalid sample rate or callback"; return false; }
        completed_=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        if(!completed_) { error="waveOut: cannot create completion event"; return false; }
        if(waveOutOpen(&device_,WAVE_MAPPER,&fmt,reinterpret_cast<DWORD_PTR>(completed_),0,CALLBACK_EVENT)!=MMSYSERR_NOERROR) { device_=nullptr; error="waveOut: no available audio device"; Close(); return false; }
        // Queue a full preroll before starting playback; the first submitted
        // block must not start while later blocks are still being prepared.
        if(waveOutPause(device_)!=MMSYSERR_NOERROR) { error="waveOut: pause failed"; Close(); return false; }
        for(size_t i=0;i<bufferCount;++i) {
            auto& h=headers_[i]; h={}; h.lpData=reinterpret_cast<char*>(buffers_[i].data()); h.dwBufferLength=sizeof(buffers_[i]);
            if(waveOutPrepareHeader(device_,&h,sizeof(h))!=MMSYSERR_NOERROR) { error="waveOut: buffer preparation failed"; Close(); return false; }
        }
        std::array<float,samplesPerBuffer> preroll{};
        try {
            for(size_t i=0;i<bufferCount;++i) if(!Fill(i,pull,preroll)) {
                error="waveOut: initial buffer submission failed"; Close(); return false;
            }
        } catch(...) { error="waveOut: audio callback failed"; Close(); return false; }
        stop_=false;
        worker_=std::thread([this,pull=std::move(pull)]() mutable {
            SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_HIGHEST);
            std::array<float,samplesPerBuffer> pcm{};
            size_t next=0;
            while(!stop_) {
                // An auto-reset event can coalesce several completions. Drain
                // every completed block in submission order before sleeping.
                if(WaitForSingleObject(completed_,INFINITE)!=WAIT_OBJECT_0) { stop_=true; break; }
                while(!stop_ && (headers_[next].dwFlags&WHDR_DONE) && !(headers_[next].dwFlags&WHDR_INQUEUE)) {
                    try { if(!Fill(next,pull,pcm)) { stop_=true; break; } }
                    catch(...) { stop_=true; break; }
                    next=(next+1)%bufferCount;
                }
            }
        });
        if(waveOutRestart(device_)!=MMSYSERR_NOERROR) { error="waveOut: playback start failed"; Close(); return false; }
        return true;
    }
    void Close() override {
        stop_=true; if(completed_) SetEvent(completed_); if(worker_.joinable()) worker_.join();
        if(device_) { waveOutReset(device_); for(auto& h:headers_) if(h.dwFlags&WHDR_PREPARED) waveOutUnprepareHeader(device_,&h,sizeof(h)); waveOutClose(device_); device_=nullptr; }
        if(completed_) { CloseHandle(completed_); completed_=nullptr; }
    }
};
#elif defined(__APPLE__)
class NativeOutput final : public Output {
    AudioQueueRef queue_{}; Pull pull_;
    static void Fill(void* context,AudioQueueRef queue,AudioQueueBufferRef buffer) {
        auto& self=*static_cast<NativeOutput*>(context);
        self.pull_(std::span<float>(static_cast<float*>(buffer->mAudioData),1024));
        buffer->mAudioDataByteSize=1024*sizeof(float); AudioQueueEnqueueBuffer(queue,buffer,0,nullptr);
    }
public:
    ~NativeOutput() override { Close(); }
    bool Open(uint32_t rate,Pull pull,std::string& error) override {
        Close(); pull_=std::move(pull);
        AudioStreamBasicDescription fmt{}; fmt.mSampleRate=rate; fmt.mFormatID=kAudioFormatLinearPCM;
        fmt.mFormatFlags=kAudioFormatFlagsNativeFloatPacked; fmt.mBytesPerPacket=8; fmt.mFramesPerPacket=1;
        fmt.mBytesPerFrame=8; fmt.mChannelsPerFrame=2; fmt.mBitsPerChannel=32;
        if(AudioQueueNewOutput(&fmt,Fill,this,nullptr,nullptr,0,&queue_)) { error="AudioQueue: open failed"; Close(); return false; }
        for(int i=0;i<3;++i) { AudioQueueBufferRef buffer{}; if(AudioQueueAllocateBuffer(queue_,4096,&buffer)) { error="AudioQueue: allocation failed"; Close(); return false; } Fill(this,queue_,buffer); }
        if(AudioQueueStart(queue_,nullptr)) { error="AudioQueue: start failed"; Close(); return false; } return true;
    }
    void Close() override { if(queue_) { AudioQueueStop(queue_,true); AudioQueueDispose(queue_,true); queue_=nullptr; } }
};
#elif defined(__linux__)
// Linux kernel OSS interface: no ALSA/PulseAudio development-library dependency.
// Modern systems without /dev/dsp should inject an Output for their host API.
class NativeOutput final : public Output {
    int fd_=-1; std::thread worker_; std::atomic_bool stop_{false};
public:
    ~NativeOutput() override { Close(); }
    bool Open(uint32_t rate,Pull pull,std::string& error) override {
        Close(); fd_=open("/dev/dsp",O_WRONLY|O_NONBLOCK);
        int format=AFMT_S16_NE,channels=2,speed=int(rate);
        if(fd_<0 || ioctl(fd_,SNDCTL_DSP_SETFMT,&format)<0 || format!=AFMT_S16_NE ||
           ioctl(fd_,SNDCTL_DSP_CHANNELS,&channels)<0 || channels!=2 || ioctl(fd_,SNDCTL_DSP_SPEED,&speed)<0 || speed!=int(rate)) {
            error="OSS /dev/dsp stereo output unavailable; inject a host Output backend"; Close(); return false;
        }
        stop_=false; worker_=std::thread([this,pull=std::move(pull)] {
            std::array<float,1024> pcm{}; std::array<int16_t,1024> bytes{};
            while(!stop_) {
                pull(pcm); for(size_t i=0;i<pcm.size();++i) bytes[i]=Detail::ToPcm16(pcm[i]);
                size_t offset=0;
                while(offset<sizeof(bytes) && !stop_) {
                    auto n=write(fd_,reinterpret_cast<char*>(bytes.data())+offset,sizeof(bytes)-offset);
                    if(n>0) offset+=size_t(n); else if(n<0 && errno!=EAGAIN && errno!=EINTR) { stop_=true; break; }
                    else std::this_thread::sleep_for(std::chrono::milliseconds(2));
                }
            }
        }); return true;
    }
    void Close() override { stop_=true; if(worker_.joinable()) worker_.join(); if(fd_>=0) { close(fd_); fd_=-1; } }
};
#else
class NativeOutput final : public Output {
    bool Open(uint32_t,Pull,std::string& e) override { e="Native audio output unsupported; inject Output"; return false; }
    void Close() override {}
};
#endif
}
std::unique_ptr<Output> CreateNativeOutput() { return std::make_unique<NativeOutput>(); }
}
