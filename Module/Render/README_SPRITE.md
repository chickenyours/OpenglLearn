# 通用 2D 精灵批次

`SpriteBatch2D` 使用一个图集和按提交顺序排列的网格绘制一帧的精灵、纯色图形和文字，不依赖游戏组件。Sprite 支持旋转／翻转，SpriteAffine 接收完整世界宽／高基向量，保留斜切与反射；SpriteRegion 使用选中帧内的归一化子区域。`sprite_nine_slice.h` 提供九宫格与平铺，`sprite_animation.h` 提供 Hermite 曲线和 Pose 采样。

## Line 与 Ring

这两个入口复用图集的白色区域和当前批次，不创建独立材质、GPU 资源或 draw call。它们按调用顺序与其他精灵合并，适合攻击瞄准、冲锋轨迹、范围效果等游戏显示。调用方传入复制的世界坐标和颜色，渲染模块不借用 ECS 指针。

```cpp
// 在 Ready 后成功 Begin 的当前帧内：
batch.Line(start, end, .06f, {1, .65f, .15f, 1});
batch.Ring(center, 1.15f, .06f, {1, .5f, .1f, .8f});
// 再 Flush 到活动 encoder，End 后 Submit。
```

- Line 的厚度是世界单位，沿线中心两侧各占一半。坐标、长度、厚度与颜色须有限；长度小于 1e-6 或厚度非正会拒绝。
- Ring 是闭合折线环，半径为折线中心半径，厚度为各段宽度。默认 32 段，可选 8..128 段；半径、厚度须正且厚度不大于半径。它不是圆形碰撞器或精确解析圆，不负责命中判定。
- 两者返回 bool，未 Ready、未 Begin、已经 Flush 或参数无效时拒绝。无需单独分配资源，所有图形沿用批次的初始化、在途提交与 Shutdown 生命周期。

## 线程与帧生命周期

调用在主线程进行，包括设备返回回调。顺序为 Initialize → 等待 Ready → Begin → 追加显示 → Flush → encoder.End → Submit；接受的提交完成后才复用。设备必须比批次活得更久，异步返回回调需持续处理。Submit 返回 false 表示未接受提交，调用方取消对应 encoder；成功接受的提交完成回调执行一次，包括失败情况。详细接口在 [sprite_batch.h](Public/Sprite/sprite_batch.h)。

## 验证

`sprite_batch_geometry_test` 验证原旋转、仿射、区域与九宫格几何。`sprite_batch_runtime_test` 检查上传依赖、在途帧排斥、失败取消、初始化／提交中销毁，以及 Line／Ring 的世界坐标、相机偏移、有限顶点、无效参数拒绝、单批次和资源释放。

```powershell
cmake --build build/brotato --target sprite_batch_geometry_test sprite_batch_runtime_test
ctest --test-dir build/brotato -R '^sprite_batch_(geometry|runtime)_test$' --output-on-failure
```

真实 OpenGL 图形由 Brotato 的 `--smoke-gameplay ranged/charge/elite/blast` 场景检查，各场景逐次运行并可捕获 PNG，命令见 [Brotato README](../Brotato/README.md)。这些场景检查攻击预警与精灵共用批次，不作为自动并行或性能提升的证明。
