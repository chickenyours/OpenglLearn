# AudioModule

独立、可注入设备后端的 C++20 音频处理模块，实现项目的 `IModule` 生命周期。
通用核心只使用 C++ 标准库；不依赖窗口或渲染模块，也不依赖音频中间件。

支持 RIFF/WAVE PCM 8/16/24/32-bit、IEEE float32、单/双声道。
按 RIFF chunk 长度读取，跳过未知块，拒绝截断、无效声道/采样率/帧对齐。
混音输出为交错 stereo float；线性插值重采样、循环、声像、单轨音量、主音量、停止及限幅均由算法实现。
最多 64 个 voice，满额时淘汰最旧 voice；VoiceID 用于停止单个音轨。
Mixer 用互斥量保护主线程与设备线程之间的状态，不是无锁硬实时引擎。音频文件应在回调外加载。
Clip 发布后应视为不可变；使用 `shared_ptr<const Clip>` 保持播放期间的寿命。

```cpp
Audio::AudioModule audio;
if (audio.Startup()) {
    auto clip = Audio::Clip::LoadWav("jump.wav");
    auto voice = audio.GetMixer().Play(clip, 0.65f, false, 0.0f);
    // ... gameplay ...
    audio.GetMixer().Stop(voice);
}
audio.Shutdown();
```

## 平台边界

| 平台 | 默认设备后端 | 依赖/限制 |
|---|---|---|
| Windows | WinMM waveOut，三个循环缓冲 | 系统 `winmm`，本机编译并打开设备验证 |
| macOS | AudioQueue | 系统 AudioToolbox framework，代码未在 Mac 实机验证 |
| Linux | OSS `/dev/dsp`，非阻塞写入 | 系统内核 OSS 接口；未实机验证，许多现代发行版默认没有此设备 |
| 其他/无设备 | `Startup()` 返回 false | 调用者可提供自己的 `Output`，错误信息在 `Error()` |

**Linux 默认后端不等同于 ALSA/PulseAudio/PipeWire 支持。** 没有这些开发库或运行库依赖；未提供 OSS 的 Linux 需要宿主实现 `Output` 或后续增加音频服务后端。
不会通过静默的空设备谎报播放成功。游戏会输出设备错误后继续运行，`--mute` 可显式禁用设备。
Windows/macOS 仅链接系统 SDK 库；新增第三方平台依赖前应另行确认。

`Output::Open(rate, pull, error)` 接收从 Mixer 拉取 stereo float 的回调。`Close()` 必须停止并等待所有回调结束后返回，且可重复调用。
自定义后端注入：`AudioModule(std::unique_ptr<Output>)`。无需设备的离线处理可直接调用 `Mixer::Render(span, sampleRate)`。
不支持压缩 MP3/Ogg、WAVE_FORMAT_EXTENSIBLE、3D HRTF；原游戏的六个 WAV 均可直接解码。

## 单独构建

```text
cmake -S Module/Audio -B build/audio-standalone
cmake --build build/audio-standalone
ctest --test-dir build/audio-standalone --output-on-failure
```

使用所在平台的 C++20 编译器和系统 SDK。该独立入口不读取根项目的 Windows clang 工具链。
根目录 `build.bat` 也会构建本模块及 `audio_module_test`。
测试覆盖所有支持的 PCM 位宽、float 非有限值处理、截断文件、无效声道、立体声、重采样、循环、声像、音量和削波。
