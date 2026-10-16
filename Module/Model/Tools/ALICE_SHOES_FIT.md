# Alice 高跟鞋穿戴适配配置

`fit_alice_shoes.py` 复现已通过 GPU 近景检查的 **r6 身体适配**。它是本例 Alice 脚型及鞋楦的专用工具，不是通用自动穿鞋算法。它读取原生缓存及导入元数据，不运行 Blender，不修改源模型、原始几何、绑定矩阵、静止骨架或 `SourcePose`。

源资产的裸足与鞋不匹配，原袜体已经按高跟鞋脚型制作。因此正式配置**只对身体产生 21,150 个顶点差值，袜体和鞋子保持原样**。启用独立 `ShoesFit` 时应用适配形态；选择 `SourcePose` 即可还原忠实导入的原始外观。

## 重现配置

生成时需要 Python 和 NumPy；编辑器播放不需要这两个依赖。正式算法不再使用凸包拟合，因此不依赖 SciPy。生成器限制 BLAS 为单线程，拒绝覆盖已有输出文件。

```powershell
python Module/Model/Tools/fit_alice_shoes.py `
  --package Asset/Generated/AliceMaid_FitFix_20261016 `
  --metadata build/alice_fit_export_final/source.blend.json `
  --output build/AliceMaid.shoesfit.reproduced.json `
  --report build/alice_shoesfit.reproduced.report.json
```

`--package` 可以是目录或 `Package.asset.json`，也可传入正式发布缓存 `Asset/Generated/AliceMaid`；`--metadata` 使用该缓存对应的 `source.blend.json`。正式配置位于 `Asset/ImportProfiles/AliceMaid.shoesfit.json`。

生成器使用通过近景检查的固定参数，不重新运行优化器。已用 `AliceMaid_FitFix_20261016` 重现全部 21,150 项 `(mesh, vertex, expectedPosition, positionDelta, normalDelta)`，与冻结的 r6 配置逐项完全相同。

## 配置字段

| 字段 | 含义 |
| --- | --- |
| `assetType` / `version` | `SkeletalFitProfile` / `1` |
| `sourceAssetId` | 被适配包的精确资产 ID |
| `baseClip` | 保持不变的作者保存姿态 `SourcePose` |
| `name` | 派生穿戴序列 `ShoesFit` |
| `boneCorrections` | 本配置为空，不修改 Foot、Calf 或 Toe 骨骼变换 |
| `morphDeltas[].mesh` / `vertex` | 原生缓存中的网格分区及顶点索引 |
| `expectedPosition` | 原始双精度绑定空间顶点坐标，应用时逐点强校验 |
| `positionDelta` | 使用完整混合蒙皮矩阵逆变换得到的绑定空间位移 |
| `normalDelta` | 平滑变形 Jacobian 的逆转置得到的法线差值 |
| `evidence` | 源单位参数、真实鞋面射线余量、三角形及适配范围检查 |

基础顶点坐标不变，新增派生形态目标默认权重为 0；`ShoesFit` 序列将其设为 1。配置只匹配相同原生顶点布局，不能直接套到重新建模或改变拓扑的资产上。

## 正式变形与验收

身体先通过完整权重计算 `SourcePose` 世界位置，再进入对应 Foot 局部空间。Y 使用积分平滑坡道抬升足底：Y≤0.25 时位移为 0，Y≥0.40 时位移为 `-0.59 × (Y-0.325)`，中间以积分 smoothstep 连续连接。前掌在 Y `[0.30,0.60]`、Z `[-0.02,0.23]` 内平滑过渡至 `Z'=0.32+0.5Z`、`X'=0.8X±0.030`，左右镜像。

侧向及远端采用连续衰减，不使用 `norm(FootLocal)<1` 或脚部权重阈值硬切割，避免漏掉趾尖或在足背混合权重处产生跳变。左右变形域逐顶点检查无重叠；袜体差值为 0。

身体 174,512 个三角形中 0 个法线反转、0 个新增退化，最小面积比约 `0.205`；Jacobian 最小行列式约 `0.164`。[r6 鞋部近景](../../../build/alice_shoesfit_r6_shoes_preview.png) 已确认鞋底下的趾尖、白色足掌外露及适配引入的足背叠圈消失。

## 数学检查的范围与残余

报告使用实际 `highheel` 蓝鞋三角形沿 Foot 局部 Y 的交点，检查内鞋底上表面；`highheel1` 深色外底不作为容纳脚的空间。外壳凸包会包括内底下面的空区，不能以“在凸包内”代替真实鞋面检查。

| 身体区域 | 左 / 右样本数 | 低于实际内鞋底的点数 |
| --- | ---: | ---: |
| 中前掌 | 7,983 / 7,760 | 0 / 0 |
| 完整趾部，包括先前漏掉的趾尖 | 7,095 / 7,008 | 0 / 0 |

距离均为**源单位**，没有毫米换算；阈值为 `outsideToleranceSourceUnits=0.002`。

**仍保留的数学边界：** 后足及开放鞋口等广义 whole-foot 射线区域，左 120 点、右 121 点低于选出的内底交点，最大约 `0.0894` 源单位；中前掌上盖射线两侧各有 10 点超过选出的交点。绑带和开放鞋口也会产生上盖交点，因此这些统计不等同于全鞋闭腔穿透率，不能宣称所有数学残差均为零。正式验收包含实际 GPU 近景；未来任意动作、换鞋或改变拓扑后仍需重新验证。

31 对象忠实源表面对比继续使用未启用适配形态的 `SourcePose`，穿戴适配不会掩盖导入精度回归。
