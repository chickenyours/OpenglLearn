# Rikka 二次元风格渲染

查看器可使用 `Render::ToonRenderPipeline` 渲染已有静态模型。它复用 Model 模块生成的 Material 材质快照与基础色贴图，使用独立的分段明暗、染色阴影和轮廓线着色器。PBR 管线仍可在同一查看器中切换。

## 运行与截图

在项目根目录运行：

```powershell
.\Module\Model\RunRikkaToon.cmd
```

或直接启动：

```powershell
.\bin\model_viewer.exe --toon --model "Asset/Model/Rikka_Uniform"
```

按 **T** 在 Toon 与 PBR 间切换；按住鼠标左键或右键拖动旋转，滚轮缩放，**R** 重置相机，**Esc** 退出。命令行 `--toon` 选择初始显示模式。

生成 Toon 截图并执行实际模型渲染检查：

```powershell
.\bin\model_viewer.exe --toon --smoke-test --screenshot build/model_rikka_toon.png
```

`--smoke-test` 使用隐藏 OpenGL 窗口，仍需要可用图形设备。也可使用 `--frames N` 限定帧数，并用 `--model` 指向其他静态模型文件或只有一个候选模型的目录。构建依赖和支持的导入格式见 [README.md](README.md)。

## 参考图的视觉方向

默认效果以用户提供的角色参考图为方向：白色背景，明亮且清楚的原始贴图颜色，少量分段阴影，细暗色轮廓线，以及地面投影。Toon 使用基础色驱动主体颜色，避免高光和物理曝光把服装与发色冲淡。

参考图中的姿态、表情和镜头属于模型动画及美术设置。当前 Model 模块仍是静态网格导入，查看器显示导入姿态；这个渲染管线不会自动生成参考图中的动作、表情或骨骼蒙皮，也不会完整重现 MMD 的材质脚本。

## 构建与验证

在已配置的项目构建目录执行：

```powershell
cmake --build build --target model_viewer toon_pipeline_test
ctest --test-dir build -R "^toon_pipeline_test$" --output-on-failure
.\bin\model_viewer.exe --toon --smoke-test --screenshot build/model_rikka_toon.png
```

`toon_pipeline_test` 无需窗口或 GPU，检查设置的有效性、错误反馈以及未启动设备时的安全生命周期。实际着色器编译、材质贴图、描边、阴影与最终输出由查看器的 OpenGL 检查和截图验证。

## 材质与资源约定

Toon 直接接收 `ModelResource::AppendTo` 输出的 `RenderFrame`，无需导入或复制一份独立材质。使用相同的材质快照可在 PBR 与 Toon 间切换。模型基础色及透明度来自 Material 系统；镜头和模型应使用相同的渲染原点。

应用可通过 [toon_render_pipeline.h](../Render/Public/Pipeline/toon_render_pipeline.h) 使用管线：

```cpp
#include "Render/Public/Pipeline/toon_render_pipeline.h"

Render::ToonRenderPipeline toon(device);
if (!toon.Initialize() || !toon.Resize(width, height, 2048, 4)) {
    // 读取 toon.LastError()。
}

Render::ToonPipelineSettings settings;
settings.groundHeight = modelBottom;
settings.sceneCenter = modelCenter;
// 以相同的模型放置变换计算阴影中心、半径和地面高度。

Render::RenderFrame frame;
character->AppendTo(frame);
character->KeepAlive(encoder);
if (toon.Record(encoder, frame, camera, settings)) {
    const auto token = toon.RecordedFrameToken();
    // 结束 encoder 并可靠提交；提交完成后在应用线程 CompleteFrame(token)。
    // 若取消且未提交，则 Cancel encoder，再 DiscardFrame(token)。
}
```

常用设置如下。颜色除 `backgroundColor` 与 `outlineColor` 外均使用线性空间。

| 设置 | 用途 |
| --- | --- |
| `lightDirection`、`lightColor` | 指向光源的方向与光色；方向不能为零 |
| `bandThreshold`、`bandSoftness` | 明暗分段位置与边缘过渡宽度 |
| `shadowTint`、`bandStrength` | 阴影染色与分段明暗强度 |
| `normalWrap` | 扩大明亮区域，减轻小曲面上的暗部 |
| `highlightStrength`、`highlightThreshold` | 小范围风格化高光 |
| `outlineWidth`、`outlineColor` | 轮廓线宽度（屏幕像素）与颜色 |
| `sceneCenter`、`shadowRadius`、`shadowDistance` | 角色阴影相机的中心、覆盖范围与距离 |
| `shadowDepthBias`、`shadowNormalBias` | 调整阴影自遮挡伪影与贴合程度 |
| `groundHeight`、`groundExtent`、`groundShadowStrength` | 地面位置、范围与投影深浅 |

`outlinesEnabled`、`shadowsEnabled`、`groundEnabled`、`groundShadows` 可分别控制对应效果。`ValidateToonPipelineSettings(settings, &error)` 可在录制前检查参数。阴影范围应覆盖角色并尽量贴近角色尺寸；过大的范围会降低纹素密度。倒壳描边按屏幕像素扩张，默认使用多重采样减少轮廓与几何边缘的锯齿。

管线借用 `RHIDevice`。设备先启动，管线再初始化与调整尺寸，帧录制完成后按现有 RHI 流程提交。模型的网格与纹理需在帧执行完成前保持存活，可在提交前调用 `ModelResource::KeepAlive(encoder)`。最终先释放未提交帧与在途帧，再关闭管线及模型，最后关闭设备。调试纹理句柄仅供观察，Resize 或 Shutdown 后不应继续使用。

主体及投影目前统一使用双面绘制，以保留 PMX 发片、裙摆等薄片；轮廓线单独使用倒壳剔除。轮廓线依赖源模型的法线、拓扑和材质分组。硬法线、开放边界、分离的小面片与透明发片可能产生不连续或重叠线条；美术专用的轮廓法线、面部阴影纹理等仍可进一步改善效果。默认参数面向当前 Rikka 资源，其他单位、比例或贴图风格的模型可能需要调整描边与阴影设置。
