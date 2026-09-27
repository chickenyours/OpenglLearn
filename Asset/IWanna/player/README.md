# 用户提供的角色素材

`source.png` 是用户提供的原图，保留原图署名文字：素材由 persafi 提取（原图署名为 “painstakingly ripped by persafi”）。这里只记录来源，不重新声明素材授权。

`tools/prepare_iwanna_player.py` 按 `crops.json` 中记录的区域提取 4 帧站立、5 帧跑步和 2 帧跳跃。使用边界洪水填充，仅将与裁切边界连通的白色背景转为透明，保留封闭的白色细节；不缩放、不插值、不生成新像素。各帧以相同身体锚点和脚底基线对齐，画布 128×96，并检查有效像素没有超出画布。

输出到 `images/player_*.png`，对应整张动画表蒙版在 `masks/player_*.png`。`generated_masks` 是按 alpha 阈值导出的逐帧蒙版。角色蒙版目前仅供素材检查：玩家实际使用 `gameplay.json` 中 `collision.playerBody` 定义的身体内矩形，地形与危险物继续使用像素蒙版。`manifest.json` 记录原图、裁切参数及输出角色图的 SHA-256。

仅重新制作素材时需要 Python + Pillow，运行游戏不依赖它们：

```powershell
python tools/prepare_iwanna_player.py
```
