# AudioModule

独立、可注入设备后端的 C++20 音频处理模块，实现项目的 `IModule` 生命周期。
通用核心只使用 C++ 标准库；不依赖窗口或渲染模块，也不依赖音频中间件。

支持 RIFF/WAVE PCM 8/16/24/32-bit、IEEE float32、单/双声道。
按 RIFF chunk 长度读取，跳过未知块，拒绝截断、无效声道/采样率/帧对齐。
混音输出为交错 stereo float；线性插值重采样、循环、声像、单轨音量、主音量、停止及限幅均由算法实现。
叠加信号超过满幅时使用左右声道联动的峰值限制：立即降低增益，以 50ms 时间常数恢复，
避免多个爆炸等大音量音效直接硬削波。未过载时保持原始电平；算法按采样帧推进，不依赖设备回调块大小。
最多 64 个 voice，满额时淘汰最旧 voice；VoiceID 用于停止单个音轨。
`SetPaused(voice, true)` 只暂停该 voice 并冻结采样游标，恢复后接着播放，其他音乐/UI音轨继续；
暂停中的 voice 仍占容量。`IsPlaying(voice)` 包含暂停中的 voice，`ActiveVoices()` 返回当前占用数。
无效或已结束的 VoiceID 可安全查询、暂停和停止。宿主应给自己的效果设置更小上限，避免占满混音器挤掉背景音乐。
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
| Windows | WinMM waveOut，事件驱动、四个循环缓冲 | 系统 `winmm`，本机静音设备回调及重开/关闭验证 |
| macOS | AudioQueue | 系统 AudioToolbox framework，代码未在 Mac 实机验证 |
| Linux | OSS `/dev/dsp`，非阻塞写入 | 系统内核 OSS 接口；未实机验证，许多现代发行版默认没有此设备 |
| 其他/无设备 | `Startup()` 返回 false | 调用者可提供自己的 `Output`，错误信息在 `Error()` |

**Linux 默认后端不等同于 ALSA/PulseAudio/PipeWire 支持。** 没有这些开发库或运行库依赖；未提供 OSS 的 Linux 需要宿主实现 `Output` 或后续增加音频服务后端。
不会通过静默的空设备谎报播放成功。游戏会输出设备错误后继续运行，`--mute` 可显式禁用设备。
Windows/macOS 仅链接系统 SDK 库；新增第三方平台依赖前应另行确认。

Windows 在暂停状态下预填全部缓冲，再启动输出；`CALLBACK_EVENT` 唤醒设备线程并补齐所有已完成的缓冲，
不再使用 `sleep(2ms)` 轮询。每块为 1024 个立体声采样帧，48kHz 下总队列约 85ms，
为系统调度留出余量。设备线程使用 Windows 的较高线程优先级；关闭会唤醒线程、等待回调结束，
然后重置并释放设备。缓冲不能保证在无限期线程阻塞或设备故障时不断流。

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
还覆盖不同回调块长度下的逐样本连续性、限幅后左右声道比例、过载恢复与非有限值 PCM16 转换。
逐 voice 暂停测试验证采样游标冻结、背景音独立推进、准确恢复、自然结束和过期句柄。

可选检查（默认测试不需要音频设备）：

```text
bin/audio_module_test.exe --device-test --wav-dir D:/Games/MyIwana/audio
```

`--device-test` 只提交全零样本，检查 48kHz/44.1kHz 的真实设备回调、关闭后不再回调和重复打开；
`--wav-dir` 使用游戏同一个 WAV 解码器读取指定目录。静音设备测试不等于主观听音验证，
原始音效中已经存在的失真或压缩损伤也不会被混音限幅器还原。
