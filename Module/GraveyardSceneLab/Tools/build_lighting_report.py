"""Package real GPU captures, an accessible Chinese report, and current validation."""
from pathlib import Path
from PIL import Image
import re, json, hashlib, html

MOD=Path(__file__).resolve().parents[1]
ROOT=MOD.parents[1]
REPORT=MOD/'Report'
CAP=MOD/'Captures/lighting-v2'
OUT=REPORT/'images/lighting-v2'
OUT.mkdir(parents=True,exist_ok=True)
for p in CAP.glob('*.png'):
    Image.open(p).convert('RGB').save(OUT/(p.stem+'.webp'),lossless=True)

# Preserve the initial report and its original figures as a clearly labelled baseline.
foundation=REPORT/'foundation.html'
if not foundation.exists():
    old=(REPORT/'index.html').read_text(encoding='utf-8')
    old=old.replace('<main>','<main><div class="callout"><p>第一轮基础报告（保留原始效果图）。当前光影实现与验证请查看 <a href="index.html">第二轮光影改进报告</a>；下文光影章节描述的是初版。</p></div>',1)
    foundation.write_text(old,encoding='utf-8')
css=re.search(r'<style>(.*?)</style>',foundation.read_text(encoding='utf-8'),re.S).group(1)
css+='\n.hero h1{max-width:1050px}.trio img{aspect-ratio:16/9;object-fit:cover}.ref img{image-rendering:pixelated}.smallfig img{max-height:420px;object-fit:contain}.compare img{image-rendering:auto}.metric{font-size:32px;color:var(--gold);line-height:1.2}.gallery{display:grid;grid-template-columns:repeat(4,1fr);gap:10px}.gallery img{width:100%}@media(max-width:800px){.gallery{grid-template-columns:1fr 1fr}}\n'

def fig(name,caption,cls=''):
    return f'<figure class="{cls}"><img loading="lazy" src="images/lighting-v2/{name}.webp" alt="{html.escape(caption)}"><figcaption>{caption}</figcaption></figure>'

def compare(a,b,left,right,caption,baseline=False):
    ap=f'images/{a}.webp' if baseline else f'images/lighting-v2/{a}.webp'
    return f'''<figure><div class="compare"><img loading="lazy" src="{ap}" alt="{left}"><img loading="lazy" class="after" src="images/lighting-v2/{b}.webp" alt="{right}"><div class="divider"></div><span class="label left">{left}</span><span class="label right">{right}</span><input type="range" min="0" max="100" value="50" aria-label="{left}与{right}对比位置"></div><figcaption>{caption} <span class="hint">左右拖动分界线。</span></figcaption></figure>'''

gpu=json.loads((MOD/'Evidence/gpu-validation.json').read_text())
perf=json.loads((MOD/'Evidence/performance-validation.json').read_text())
fps=perf.get('mean_fps',0)
mean=perf.get('mean_frame_ms',0)
changed=gpu.get('visual_toggle_changed_pixels',{})
day='https://images.gog-statics.com/a71ef27e8516d191b892f7fda604f020ce35d56ce1a9b9fc55881515d69b1307_product_card_v2_mobile_slider_639.jpg'
night='https://images.gog-statics.com/67ceea9d4b735a562833a0aab3beb9c11cccef36a0453e702d304c2d8c207366_product_card_v2_mobile_slider_639.jpg'
indoor='https://images.gog-statics.com/1ff16d30516884c99e2d8adf181a11779650baa1602fad8e3d330afa00fb8303_product_card_v2_mobile_slider_639.jpg'
doc=f'''<!doctype html>
<html lang="zh-CN"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Graveyard Keeper 2 · 第二轮光影改进</title><style>{css}</style></head><body>
<header><nav><b>SCENE STUDY / LIGHTING 02</b><a href="#reference">官方画面对照</a><a href="#model">受光</a><a href="#shadow">太阳阴影</a><a href="#contact">接触阴影</a><a href="#cloud">云影</a><a href="#night">昼夜灯光</a><a href="#run">运行</a><a href="#evidence">证据</a></nav></header><main>
<div class="hero"><div class="eyebrow">Graveyard Keeper 2 · lighting revision · 2026-10-20</div><h1>从平面明暗，走向有空间的光影<br><small>官方截图对照 · 原着色器复核 · OpenGL 实际画面</small></h1>
<p class="lead">树冠有朝向，岩壁能挡住光，夜间有蓝色月光和暖色灯池。第二轮把这些关系拆开实现，再让它们在同一个场景里共同工作。</p>
<div class="facts"><span>ApplicationWindow + Render</span><span>1536² 真实太阳影图</span><span>原树冠法线算法</span><span>原三维云噪声</span><span>可移动人物与坡道</span></div>
{fig('overview-day','新版默认白天，直接读取程序的 1280×720 GPU 帧缓冲。树、教堂、墓碑与桥使用本机游戏提取的纹理和网格；地图由本模块重新布置。')}
{compare('overview-day','overview-day','第一轮','第二轮','同一总览相机的前后版本。第二轮改变了受光、投影、接触暗部与色调，保留原资源和场景布局。',True)}
<p class="sub">本报告的本地效果图均来自程序真实渲染。水流、风动、落叶、地形和行走的基础调查保留于 <a href="foundation.html">第一轮完整报告</a>。这里重点解释本轮修正的光影。</p></div>

<section id="reference"><div class="num">01 / LOOK AT THE GAME</div><h2>先看实机截图：亮部温暖，暗部仍有颜色</h2>
<p>本轮实际查看了 <a href="https://www.gog.com/en/game/graveyard_keeper_2">GOG 官方商店</a>的街道、夜间科技树背景、白天工坊和占星师室内截图。<a href="https://www.graveyardkeeper2.com/">游戏官网</a>直接链接到该商店；也核对了 <a href="https://store.steampowered.com/app/4358690/Graveyard_Keeper_2/">二代 Steam 页面</a>，避免把一代画面混进来。</p>
<div class="trio ref"><figure><a href="{day}"><img loading="lazy" src="{day}" alt="GOG 官方白天工坊实机截图，联网加载"></a><figcaption>白天工坊：绿叶与土石保留自身颜色，明暗以叶簇和几何面为单位变化；画面不靠统一黑色阴影覆盖。</figcaption></figure><figure><a href="{night}"><img loading="lazy" src="{night}" alt="GOG 官方夜间科技树截图，联网加载"></a><figcaption>夜间背景：蓝色环境仍能分辨屋顶、道路和植被，局部暖光形成视觉落点。界面遮住了部分场景，只据可见区域比较。</figcaption></figure><figure><a href="{indoor}"><img loading="lazy" src="{indoor}" alt="GOG 官方占星师室内截图，联网加载"></a><figcaption>室内灯光：烛火周围有较集中的暖色受光，远处明显压暗。此图用于观察冷暖与衰减，本模块重建的是户外场景。</figcaption></figure></div>
<p class="sub">上方三张远程图片属于官方商店，需要联网；点击可看来源。其余本地效果图离线可用。此次没有下载远程媒体，也没有观看并逐帧分析预告片。</p>
<div class="callout"><p>截图用于判断“看起来怎样”；具体“怎么算”由本地程序集、材质、灯光预设和 DXBC 指令支持。不同地图和曝光不能用一张截图的像素误差来证明完全复现。</p></div></section>

<section id="model"><div class="num">02 / LIGHT HAS A DIRECTION</div><h2>贴图提供细节，法线决定哪一面接到光</h2>
<p>初版对一些植物采用几乎固定的受光量，太阳换方向也难以改变它们。复核原 <code>Custom/3D Object</code> 后发现，主材质保存的 PBR 开关为 0，核心是 Lambert 受光。它需要真实网格法线，也需要一盏从太阳反方向补进来的灯。</p>
<div class="grid"><div><pre>朝向光的程度 = max(法线 · 指向太阳的方向, 0)
表面颜色 ≈ 贴图 × [环境光
                   + 太阳色 × 朝向 × 阴影可见度
                   + 背光色 × 反向朝向]
最后再加入局部灯光和后期。</pre><p>把表面想成一张小纸片：纸面朝着灯就亮，侧过去就暗。环境光保住暗面的细节，反向补光进一步拉开背面的颜色。</p></div><div class="card"><h3>不是给所有物体临时造法线贴图</h3><p>选中的树、灌木、教堂、墓碑和桥没有绑定法线图；它们使用原网格法线。树冠还有一条特殊分支：用原贴图的红通道分成三档，混合两个切线空间法线，让叶簇朝向略有不同。</p><p><code>k = ½ × [step(.76,1−R) + step(.58,1−R)]</code></p><p>草的原指令输出的是变换后的局部向上法线。风会弯草，但不能据此声称原作也弯曲了草的光照法线。</p></div></div>
{compare('lighting-off','lighting-on','关闭照明','恢复方向照明','同一黄昏参数下关闭和开启照明。原贴图里的像素细节保留，运行时受光再提供统一的方向与冷暖关系。')}
<p><span class="tag">原件确证</span>本地版本使用 Gamma 颜色空间；迁移没有额外插入 sRGB→Linear 的现代 PBR 流程。原指令含 <code>2 × glstateAmbient</code>，但 Unity native 上传的常量与磁盘环境 RGB 的精确缩放尚未抓帧证明，因此程序保留环境光校准滑杆，不声称其数值一比一等同原运行时。</p>
<div class="source">逐指令与材质依据：<a href="../Evidence/lighting-v2/findings.md">照明模型复核</a> · <a href="../Evidence/lighting-v2/selected-normal-audit.json">法线绑定审计</a> · <a href="../Assets/shaders/scene.frag">迁移片元 Shader</a>。</div></section>

<section id="shadow"><div class="num">03 / SHADOWS COME FROM GEOMETRY</div><h2>先从太阳看一次场景，才知道光被谁挡住</h2>
<p>第一轮把物体轮廓压在地面上，影子无法正确落到岩壁或另一件物体上。现在增加了一张真正的太阳深度图：从太阳方向记录最近表面，再让每个可见片元比较自己是否躲在那个表面后面。</p>
<div class="diagram"><svg viewBox="0 0 1050 240" role="img" aria-label="太阳影图与主相机的两个绘制通道"><defs><marker id="arrow" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8" fill="#d8bb78"/></marker></defs><g fill="#253c30" stroke="#66876e"><rect x="22" y="65" width="230" height="105" rx="8"/><rect x="317" y="65" width="230" height="105" rx="8"/><rect x="612" y="65" width="200" height="105" rx="8"/><rect x="875" y="65" width="150" height="105" rx="8"/></g><g fill="#e9ede2" text-anchor="middle" font-size="18"><text x="137" y="102">太阳视角绘制</text><text x="137" y="136" font-size="14">同一网格、风动和透明裁切</text><text x="432" y="102">1536² 最近深度</text><text x="432" y="136" font-size="14">RGBA8 打包 + Depth32F</text><text x="712" y="102">观察相机绘制</text><text x="712" y="136" font-size="14">把世界点投到影图中比较</text><text x="950" y="102">软阴影</text><text x="950" y="136" font-size="14">3×3 PCF</text></g><g stroke="#d8bb78" stroke-width="2" marker-end="url(#arrow)"><path d="M255 117H308"/><path d="M550 117H603"/><path d="M815 117H866"/></g><text x="525" y="216" text-anchor="middle" fill="#aebeaf" font-size="15">主角、树、建筑、地表和岩壁共享同一个真实空间；阴影不再只贴在地面上。</text></svg></div>
{compare('sun-shadow-off','sun-shadow-on','关闭太阳阴影','真实太阳阴影','固定近景、固定时间，仅切换太阳阴影。观察树冠投影、墓碑与教堂周围，以及高地边缘。')}
<div class="grid">{fig('shadow-map-depth','真实影图深度读回。灰度表示太阳视角的距离，不是后画的黑色剪影。','smallfig')}{fig('shadow-visibility','接收端的太阳可见度诊断：白色可直视太阳，黑色被挡住，灰色来自边缘过滤。')}</div>
<h3>边缘要柔和，表面也不能自己“长格子”</h3><p>PCF 比较周围 9 个深度样本，平均出边缘覆盖率。相邻样本对应的接收平面深度并不相同；这里用屏幕导数估计接收平面斜率，为每个采样点修正比较深度，连最近纹素中心的偏移也计入，修复首轮调试时出现的斜向自阴影格纹。</p>
{compare('pcf-off','pcf-on','单点硬阴影','3×3 PCF','同一影图的边缘过滤对照。过滤控制边缘过渡，不改变投影物体。可在程序放大镜中观察细节。')}
<div class="grid">{fig('sun-direction-changed','改变太阳方位后的同一近景：投影方向和受光面一起变化。')}{fig('sun-low','降低太阳仰角到 30°：影子沿地形拉长。这是额外诊断状态，默认白天仍使用恢复的原太阳方向。')}</div>
<p class="sub">影图和主视图复用相同的顶点风动代码与 .5 Alpha 裁切；透明叶孔会透光。交互时动画影图最多刷新 12 次/秒，固定截图每个状态强制刷新。原游戏有 Unity 阴影与质量档系统，这里实现单张正交影图，未声称完整迁移其级联阴影、NGSS/PCSS 或运行时质量设置。</p>
<div class="source"><a href="../Assets/shaders/shadow.vert">太阳顶点通道</a> · <a href="../Assets/shaders/shadow.frag">裁切与深度打包</a> · <a href="../Captures/lighting-v2/capture-manifest.json">逐张截图参数</a>。</div></section>

<section id="contact"><div class="num">04 / CONTACT MAKES OBJECTS SIT DOWN</div><h2>脚边、墙根和物体交界，再补一层短距离遮挡</h2><p>太阳影图回答“太阳被挡了吗”，接触 AO 回答“这个位置是不是挤在几何夹缝里”。程序从 Depth32F 重建可见表面的世界位置，检查周围 12 个深度样本，让很近、凸到表面上方的邻居带来轻微暗化。</p>
{compare('contact-ao-off','contact-ao-on','关闭接触 AO','开启接触 AO','小尺度接触阴影对照。效果刻意控制在边缘和交界处，避免把整张图蒙黑。')}
{fig('contact-ao-only','只显示 AO 遮挡。它只知道屏幕里可见的深度，画面外或被遮住的几何无法参与。')}
<p><span class="tag demo">本模块补充</span>此 AO 是迁移场景为强化接触感实现的近似，未确证为原作这一画面的同款算法。它不能代替太阳影图，也可能在轮廓附近出现屏幕空间局限。</p></section>

<section id="cloud"><div class="num">05 / CLOUD SHADOWS MOVE TOGETHER</div><h2>云影使用原始三维噪声，不再是一层随意的明暗波纹</h2>
<p>复核 <code>Custom/Clouds Shadow Procedural</code> 得到了两次体噪声采样和一条“细节侵蚀大云团”的公式。原噪声是 32³ RGB 体纹理；这里把每一层排进有边界填充的二维图集，再插值相邻 Z 层，通过 Render 画成固定世界坐标的 256² 云影图。</p>
<div class="grid"><div><pre>p = 世界XZ / 30 + 风向 × 累计风偏移
大云团 = saturate(噪声(p×.819, t×.01)
                  + 密度 − .792)
细节 = 噪声(p×1.42, t×.017)
侵蚀 = 大云团 × saturate((细节 − (1−大云团))
                         / max(大云团,.001))
云Alpha = .515 × smoothstep(0,.096,侵蚀)
受光 *= 1 − 云Alpha × .35 × 预览强度</pre></div><div class="card"><h3>第三维是“变化”，不只是位移</h3><p>风推动 XY 坐标，Z 坐标随时间变化，因此云团边缘还会慢慢生长与消散。所有地面与物体用同一世界坐标采样这张遮罩；移动相机时，云影不会粘在屏幕上。</p><p>默认密度 .643 来自原材质，适合展示；原天气系统能够覆盖密度，当前运行时天气值没有抓取，不能把这个默认值称为原作每一时刻的实际天气。</p></div></div>
{compare('clouds-off','clouds-on','关闭云影','原噪声云影','固定相机与时间，只切换云影强度。局部灯光仍在云下提供暖色，不会一起被整屏压暗。')}
<div class="grid">{fig('cloudmap-only','t=12 秒的 GPU 云影 Alpha；亮处表示云遮挡更强。','smallfig')}{fig('cloudmap-later','t=32 秒的 GPU 云影 Alpha；位置和形状都随时间变化。','smallfig')}</div>
{compare('clouds-on','clouds-later','t=12 秒','t=32 秒','两次确定性时间采样。除云影外，水流和风动也跟随共同时间变化，因此这组图不作为“仅云影”的消融。')}
<p class="sub">采样边界：原 Texture3D 保存 Point/Clamp，但 Shader 使用独立 inline sampler，其 packed native 状态尚未解码；原 DXBC 坐标没有 frac。迁移明确选择 Linear/Repeat 平滑循环体噪声，这部分不宣称逐像素复原。图集数据已逐字节重建核验；原体纹理 32³，图集 272×136、8×4 层、每层 1px 循环边界。</p>
<div class="source"><a href="../Evidence/lighting-v2/cloud-procedural.md">云影指令、参数与边界</a> · <a href="../Assets/cloud-noise-manifest.json">原噪声来源和哈希</a> · <a href="../Assets/shaders/cloud.frag">云图生成 GLSL</a>。</div></section>

<section id="night"><div class="num">06 / TIME CHANGES THE WHOLE LIGHTING SYSTEM</div><h2>昼夜切换，换的是一组灯光关系</h2>
<div class="trio">{fig('overview-day','中午：暖白太阳、保留颜色的环境底光和灰色反向补光。')}{fig('overview-dusk','黄昏：橙色主光搭配紫蓝背光，冷暖分离。')}{fig('overview-night','夜间：蓝色月光和环境底色仍存在；暖色灯池提供局部焦点。')}</div>
<h3>选取的原预设节点（t 为原归一化时间）</h3><table><thead><tr><th>原时段</th><th>恢复的太阳 RGB × 强度</th><th>环境 RGB</th><th>背光</th></tr></thead><tbody><tr><td>中午 t=.50</td><td>(1,.892,.675) × .6</td><td>(.415,.344,.323)</td><td>灰色，强度 1</td></tr><tr><td>黄昏 t=.82</td><td>(.887,.414,.213) × .8</td><td>(.194,.169,.319)</td><td>紫蓝，强度 1</td></tr><tr><td>夜间 t=.88</td><td>(.306,.392,1) × .3</td><td>(.110,.110,.358)</td><td>蓝色，强度 .2</td></tr></tbody></table>
<p class="sub">预览滑杆按“夜间→黄昏→中午”重组这三个节点，光色插值，太阳方向插值后归一化；滑杆值不等于表中的原 t。此次没有完整迁移原作 14 节点的全天曲线与各项插值规则。</p>
<p>真实 Back Light 是 Sun 的子物体，局部 Y 旋转 180°，所以方向取太阳的反向。它与全局名为 <code>_BacklightColor</code> 的参数不是同一件事；后者在这些户外节点为黑色。</p>
<h3>灯池按三维位置衰减</h3><p>每盏灯有世界空间位置。片元到灯的距离决定衰减，法线朝向决定受光；地面、台阶和墙面因此不会得到同样的平面颜色罩。少量独立的光晕与轻微闪动补充火焰氛围。灯的位置、半径和衰减属于新场景的适配，尚未复原原场景全部光源和遮挡。</p>
<h3>后期只收尾，不伪造光向</h3><p>本轮移除了初版屏幕空间的斜条“阳光”。新的后期仅补充克制的 Bloom、轻微暗角和颜色微调。已发现 PPv2 的饱和 +7、亮度 +6、对比 −10 作为近似依据；存在的 LUT 纹理未被证明处于开启状态，因此没有假称套用了原作 LUT。</p>
{compare('post-off','post-on','关闭颜色后期','轻微 Bloom 与调色','后期开关对照，保持同一照明状态。这里是轻量近似，没有完整重建 Unity PPv2。')}
<div class="source"><a href="../Evidence/lighting-v2/outdoor-light-keyframes.json">14 个户外原始节点</a> · <a href="../Evidence/lighting-v2/light-transforms.json">太阳与背光父子链</a> · <a href="../Assets/shaders/post.frag">AO 与后期 GLSL</a>。</div></section>

<section id="run"><div class="num">07 / RUN THE SCENE</div><h2>运行、移动，再亲手切换每一层</h2>
<p>双击模块目录的 <a href="../Run.cmd">Run.cmd</a>。<kbd>W</kbd><kbd>A</kbd><kbd>S</kbd><kbd>D</kbd> 或方向键移动，沿中央坡道可走上高地。窗口里可暂停时间、调日夜、风、水和云密度；展开光影面板可独立开关影图、接触 AO、背光、PCF 与旧剪影，或手动改变太阳方向。</p>
{fig('ui','新版程序及工具面板的真实截图。可在暂停状态做同参数比较，也可用放大镜检查像素与阴影边缘。')}
<pre>cmake --build build --target graveyard_scene_lab --parallel 4
.\\Module\\GraveyardSceneLab\\Run.cmd --smoke-test
.\\bin\\graveyard_scene_lab.exe --capture-lighting Module\\GraveyardSceneLab\\Captures\\lighting-v2</pre>
<p>窗口由 <code>ApplicationWindow::Window</code> 管理；场景、太阳影图、云图与后期均由 <code>Render::OpenglBackend + RHIResourcePool</code> 创建资源并提交。ImGui 负责调试面板，原生 GL 只用于诊断与帧缓冲读回。</p>
<div class="grid"><div class="card"><div class="metric">{fps:.1f} FPS</div><p>本机本次隐藏窗口捕获平均值，平均帧时 {mean:.1f} ms；60 帧预热后记录 120 帧。编辑器、场景与呈现均计入。</p></div><div class="card"><h3>当前性能边界</h3><p>Intel RaptorLake-S Mobile 核显，1280×720，Debug 目标局部 O2。真实阴影和多次采样增加了成本，目前适合技术预览，尚未达到流畅游戏的帧率；该值不能作为其他设备性能保证。</p></div></div>
<p class="sub">云图和动画影图交互刷新上限均为 12Hz。静态 VBO 缓存与草牌按纹理合批继续保留；旧版的 22 张逐像素一致证明属于当时的缓存优化检查，不是本轮与原游戏画面一致的证明。</p></section>

<section id="evidence"><div class="num">08 / WHAT WAS VERIFIED</div><h2>从原件到效果图，每一步有可检查的出处</h2>
<table><thead><tr><th>层级</th><th>本轮依据</th><th>保留的边界</th></tr></thead><tbody><tr><td>官方画面</td><td>实际查看的 GOG 二代官方截图，核对官网和商店身份。</td><td>静态截图只能比较可见外观，不代表已运行或逐帧观看原游戏。</td></tr><tr><td>原件调查</td><td>REA 托管分析、原材质/灯光序列化、选定 DXBC、纹理与网格。</td><td>只读安装目录；没有原运行时 GPU 常量、最终质量档和天气快照。</td></tr><tr><td>迁移</td><td>原法线、树冠分档、背光方向、昼夜颜色、云噪声与侵蚀算式。</td><td>新地图布局、单张影图、接触 AO、灯具参数、平滑循环云采样和 PPv2 近似。</td></tr><tr><td>验证</td><td>正常构建；24 组 GPU 状态；GL error=0；影图深度覆盖；云Alpha空间变化；效果开关像素差；坡道行走和深度遮挡。</td><td>验证程序工作与效果开关有效，不能等同于原游戏像素级匹配。</td></tr></tbody></table>
<p>本轮太阳阴影开关改变 <strong>{changed.get('sun_shadow',0):,}</strong> 像素，接触 AO 改变 <strong>{changed.get('contact_ao',0):,}</strong> 像素，云影改变 <strong>{changed.get('cloud_mask',0):,}</strong> 像素；这些数值来自固定参数的 GPU 帧比较。</p>
<div class="grid"><div class="card"><h3>原始技术证据</h3><p><a href="../Evidence/lighting-v2/findings.md">完整照明复核</a> · <a href="../Evidence/lighting-v2/presets-findings.md">材质与预设调查</a> · <a href="../Evidence/lighting-v2/lighting-model.json">结构化数学模型</a> · <a href="../Evidence/lighting-v2/selected-programs.json">DXBC 来源与变体</a> · <a href="../Evidence/lighting-v2/cloud-procedural.md">云影恢复</a> · <a href="../Evidence/lighting-v2/visual-reference.md">官方视觉参考记录</a></p></div><div class="card"><h3>本机实现与测量</h3><p><a href="../Private/main.cpp">场景与 RHI 通道</a> · <a href="../Evidence/gpu-validation.json">GPU 验证</a> · <a href="../Evidence/performance-validation.json">性能测量</a> · <a href="../Evidence/validation.json">本轮交付检查</a> · <a href="../Captures/lighting-v2/capture-manifest.json">截图参数</a> · <a href="../README.md">使用说明</a></p></div></div>
<p class="sub">本地版本：Unity 6000.3.9f1；Assembly-CSharp SHA256 185a742ab88b13e0ca9ab26ee820ddefcba05fafd76a6027939f889de65ccd50。PlayerSettings 解析有 4 字节 schema 尾部差异，已在原始调查记录；没有隐去该限制。完整 REA 回包保留在 E:\\REA\\graveyard_keeper2\\lighting-v2；模块收录可独立检查的精简证据。</p></section>
<div class="footer">第二轮光影研究 · 原始游戏文件保持只读 · 所有效果图与参数一起保存 · <a href="foundation.html">查看第一轮水流、风动、落叶和高低差报告</a></div></main>
<script>document.querySelectorAll('.compare input').forEach(input=>input.addEventListener('input',()=>input.parentElement.style.setProperty('--pos',input.value+'%')));</script></body></html>'''
(REPORT/'index.html').write_text(doc,encoding='utf-8')
print(f'Packaged {len(list(OUT.glob("*.webp")))} real GPU figures; report {len(doc):,} characters.')
