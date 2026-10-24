# Diana 资源离线导出

运行时只读取仓库内 `.hairbin` 与 `.glb`，不依赖原游戏、E/F 盘研究目录、Python、Blender 或第三方格式读取器。下列 Python 脚本仅用于再次导出和离线校验，需要 Python 3.10+ 与 NumPy。

从仓库根目录运行：

```powershell
python -B Module/HairSimulation/Tools/export_diana.py --research-root <已有pragmata研究目录>
python -B Module/HairSimulation/Tools/validate_diana.py
```

导出器只支持已核对的 STRD v31、零次级 Guide、SBD 251121870 与 MESH 251121828。不会尝试猜测其他版本。检查源文件头、每段长度、Guide 和显示发丝索引、三角形索引、有限浮点与实际头皮绑定残差后写出资源；源文件始终只读。

`validate_diana.py` 不需要研究目录。它核对完整导出 SHA256、全部发丝拓扑、Guide 四元数与切线、弧长、前三千显示点的原生 Guide 插值恒等性、GLB 三角形索引，以及 17,500 个头皮根绑定相对实际头部网格的残差。C++ `Test/asset_test.cpp` 另外测试实际 runtime loader 对截断、溢出计数、错误文件头和 GPU 索引越界的拒绝行为。

## 格式

`DHAIR001`：little endian，32-bit IEEE754 float，单位米，Y 向上。所有数组连续保存，无额外对齐填充。

| 段 | 元素数 | 内容 |
| --- | ---: | --- |
| header | 56 bytes | 8-byte magic，6 个 uint32（version、guides、nodes、strands、renderVertices、segments），bounds min/max 六个 float |
| guideOffsets | guides+1 | uint32，最后一个为 nodes |
| guideRest | nodes | float4 xyz + 从根累计弧长 |
| guideRestOrientation | nodes | float4 xyzw 四元数 |
| strandOffsets | strands+1 | uint32，最后一个为 renderVertices |
| renderRest | renderVertices | float4 xyz + 半径（米） |
| renderBindings | renderVertices | uint4 guideNodes，float4 weights，float4 lerps；第 4 lane 保留为零 |
| segments | segments | uint32，每个连续线段的起点索引，终点为起点+1 |

Guide binding 节点和 lerp 来自原资源，weights 从原半精度值归一化到 float32。三个 Guide 节点各表示一段 `[node,node+1]`。静止四元数由真实曲线切线与平行移动框架重建，`rotate(q,+Z)` 为向后一个节点的切线；其自由扭转不是对原程序初始化公式的断言。

`DBIND001`：8-byte magic、uint32 version/count，然后每个根为 uint3 头皮三角形顶点索引和 float3 重心权重。原始 SBD 的 `(u,v)` 对应 `(1-u-v,u,v)`，已通过全部实际根与头部顶点交叉验证。

## 来源与第三方格式参考

全部游戏资源 SHA256、原生相对路径、版本、实际参数和重建标记保存在 `Asset/HairSimulation/diana_metadata.json`。原始 GPUS 预设、组件和控制器有独立 JSON 证据。头部 GLB 使用真实静止位置/法线/三角形和中性预览材质，不含原始纹理、面部形变或角色骨骼动画。

STRD/SBD 布局与既有本地 REasy 研究输出交叉核对（REasy 研究中固定提交 `ffcecb48830a1bc9f4ce8710ab88a98641366eb8`，MIT）。该脚本独立读取已核对的缓冲结构，没有捆绑 REasy 程序。

Pragmata retail MESH 字段布局另与社区维护的 [REME `file_re_mesh.py`](https://github.com/TrueShadow01/REME-RE-Engine-Blender-Addon/blob/main/modules/mesh/file_re_mesh.py) 交叉核对。作者标记为 NSA Cloud，并注明 AsteriskAmpersand 与 AlphaZomega 的格式研究。REME 仓库包含 [GPL 许可证](https://github.com/TrueShadow01/REME-RE-Engine-Blender-Addon/blob/main/LICENSE.GPL)。本次仅检查字段定义、独立实现最小静态格式解析；未捆绑、导入或运行该第三方源文件。检查时该参考文件 SHA256 为 `3f280440e0845988adf3967afc56719e0112069761a842598b3402d09f295853`。
