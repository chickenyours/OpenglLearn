# Diana 发丝模拟重建

运行仓库根目录的 `StartPragmataHair.bat`。它使用 **Model 模块的 Skeletal Asset Editor 预览窗口**，载入提取的 Diana 头部和完整 LOD0 发丝。右侧选择 `GPU Strand Hair / 发丝模拟` 调参，参数后的 `(?)` 提供简短说明。右键旋转、中键平移、滚轮缩放沿用 Model 的操作。

```powershell
cmake --preset windows-release
cmake --build --preset windows-release --target skeletal_asset_editor hair_simulation_gpu_test strand_hair_asset_test vulkan_compute_test -j 1
./StartPragmataHair.bat --gravity low
./StartPragmataHair.bat --gravity zero
```

三档世界空间重力取自已提取的 Diana 控制配置：正常 `(0,-30,0)`、低重力 `(0,-1.6,0)`、无重力 `(0,0,0)`。这里“正常”是源配置的档位名称；可以在面板手动改为地球重力约 `-9.81`。切换档位保留运动状态；点击重置恢复原始发型。

## 数据与算法

`Asset/HairSimulation/diana_lod0.hairbin` 保存 120 条引导曲线、3,000 个引导节点、17,500 条渲染曲线、436,975 个渲染顶点和 419,475 段。每个渲染顶点保留原始三个引导曲线的节点索引、区间插值系数和归一化权重。所有位置统一为米、Y 向上。头部网格与发丝保持同一坐标系，Model 预览只对二者施加相同的显示矩阵。

提取的 `strandPhysics2UpdateCosseratRods.compute` DXIL 证明原方法使用位置、方向四元数、线速度、角速度，以及带累计乘子的伸长、弯曲约束。新模块按这些证据实现可读的 Cosserat 杆 / XPBD 重建：

1. 固定时间步累积，每个步分成子步；积分重力、空气阻力、线/角阻尼。
2. 固定每条曲线的发根位置和方向。
3. 迭代投影伸长/剪切向量约束与相对四元数弯曲/扭转约束；顺应性除以子步时长平方。
4. 按根到梢距离曲线恢复参考发型，处理头部碰撞和摩擦，更新线/角速度。
5. Vulkan 计算着色器通过原始三个引导绑定，将运动插值到全部渲染发丝。
6. 显式读回发丝数据交给 Render 的 StrandMesh，通过 RHI 三角形细带、双切线高光和加权透明合成接入 Model 的 PBR 预览；HDR 合成后再统一曝光与色调映射。

Render 的通用计算接口管理存储缓冲、描述符、运行时 GLSL 管线、批次派发、内存屏障、完成等待和读回。HairSimulation 不直接依赖 Vulkan 句柄。现有 Model 图形预览使用 OpenGL，因此当前存在跨 API 的同步读回/上传；面板报告该成本。

原始预设含 10 子步、6 迭代，以及半径、密度、模量、摩擦、阻尼和形变曲线等字段。其最终运行时覆盖和全部单位没有被证明，因此面板中的 XPBD 顺应性、风场、速度上限和渲染参数是**定义明确的重建控制量**，不冒充原游戏的逐参数还原。原始静止四元数未直接作为杆架使用；导出工具由实际曲线切线和平行输运生成重建杆架。

## 当前范围

已接入真实曲线及引导绑定，并实现 GPU 位置/方向约束与三个重力环境。碰撞使用提取的两段变半径胶囊，并通过真实头部骨骼静止矩阵变换到发丝坐标。头部保持静态，尚未实现角色动画驱动的头皮蒙皮、完整身体碰撞、发丝自碰撞、原游戏体积速度/密度网格、OIT 和密度光照。预览的细带切线光照便于观察模拟，不能据此认定恢复了游戏完整渲染效果。

当前默认 10 子步 / 6 迭代的有限迭代解仍有伸长残差：RTX 4060 上默认正常重力 180 步测试的最坏短段峰值约 21.46%，结束时最大约 15.56%，发根没有漂移、没有非有限状态或 GPU 恢复。面板显示当前最大段长误差；增加迭代或子步可以继续研究收敛。这是当前重建的精度限制，不能当作原游戏求解精度。

## 验证

```powershell
output/Release/bin/vulkan_compute_test.exe --validation
ctest --test-dir build/windows-release -R '^(strand_hair_asset_test|hair_simulation_gpu_test|model_strand_.*_gpu_test)$' --output-on-failure
output/Release/bin/skeletal_asset_editor.exe --strand-test --gravity normal --frames 120 --screenshot output/HairSimulation/normal.png
```

资源验证检查文件大小、范围、索引、归一化权重及损坏输入；GPU 验证检查实际 Vulkan 派发结果、三个重力响应、发根约束、有限状态、重置与固定步长；Model 冒烟验证检查实际预览像素随模拟变化。

源码：`Public/hair_simulation.h`（外部接口与参数说明）、`Shaders/guide_simulate.comp`（杆求解）、`Shaders/guide_interpolate.comp`（绑定插值）、`Model/Tools/strand_hair_preview.h`（现有预览窗口接入）。数据来源和提取校验记录见 `Asset/HairSimulation` 中的清单及导出脚本。
